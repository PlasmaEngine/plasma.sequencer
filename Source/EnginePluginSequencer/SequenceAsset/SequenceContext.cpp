#include <EnginePluginSequencer/EnginePluginSequencerPCH.h>

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EnginePluginSequencer/SequenceAsset/SequenceContext.h>
#include <EnginePluginSequencer/SequenceAsset/SequenceView.h>
#include <Foundation/Utilities/ConversionUtils.h>
#include <AudioSystem/Components/AudioTriggerComponent.h>
#include <RendererCore/Components/CameraComponent.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceContext, 1, plRTTIDefaultAllocator<plSequenceContext>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_CONSTANT_PROPERTY("DocumentType", (const char*) "Sequence"),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plSequenceContext::plSequenceContext()
  : plEngineProcessDocumentContext(plEngineProcessDocumentContextFlags::CreateWorld)
{
}

void plSequenceContext::HandleMessage(const plEditorEngineDocumentMsg* pMsg)
{
  if (const auto* pConfig = plDynamicCast<const plDocumentConfigMsgToEngine*>(pMsg))
  {
    // The editor nominates the camera by document-object guid rather than sending a transform:
    // the same objects are mirrored into this process, so reading the transform here is both
    // simpler and guaranteed to match what the editor is showing.
    // "px|py|pz|rx|ry|rz|rw|fov". A transform rather than an object to look through, because during
    // a camera-cut blend the view sits between two cameras and is at no object's transform at all.
    if (pConfig->m_sWhatToDo == "SequenceThumbnailCamera")
    {
      plHybridArray<plStringView, 10> parts;
      plStringView(pConfig->m_sValue).Split(false, parts, "|");

      if (parts.GetCount() != 8)
      {
        plLog::Warning("Sequence thumbnail: malformed camera transform '{}'.", pConfig->m_sValue);
        m_bThumbnailCameraValid = false;
        return;
      }

      double v[8] = {};
      for (plUInt32 i = 0; i < 8; ++i)
      {
        plStringBuilder sPart = parts[i];
        if (plConversionUtils::StringToFloat(sPart, v[i]).Failed())
        {
          m_bThumbnailCameraValid = false;
          return;
        }
      }

      m_ThumbnailCameraTransform.SetIdentity();
      m_ThumbnailCameraTransform.m_vPosition.Set((float)v[0], (float)v[1], (float)v[2]);
      m_ThumbnailCameraTransform.m_qRotation = plQuat((float)v[3], (float)v[4], (float)v[5], (float)v[6]);
      m_fThumbnailCameraFov = (float)v[7];
      m_bThumbnailCameraValid = true;
      return;
    }

    // "<verb>|<object guid>|<trigger>": the editor sends only the edges, because it is the one that
    // knows where the playhead is, and this process is the one that owns the audio system.
    if (pConfig->m_sWhatToDo == "SequenceAudioPreview")
    {
      plHybridArray<plStringView, 4> parts;
      plStringView(pConfig->m_sValue).Split(true, parts, "|");

      const plStringView sVerb = parts.GetCount() > 0 ? parts[0] : plStringView();
      const plStringView sObject = parts.GetCount() > 1 ? parts[1] : plStringView();
      const plStringView sTrigger = parts.GetCount() > 2 ? parts[2] : plStringView();

      if (sVerb == "stopall")
        StopAllPreviewAudio();
      else if (sVerb == "start")
        StartPreviewAudio(sObject, sTrigger);
      else if (sVerb == "stop")
        StopPreviewAudio(sObject);

      return;
    }
  }

  plEngineProcessDocumentContext::HandleMessage(pMsg);
}

plGameObject* plSequenceContext::ResolvePreviewAudioObject(plStringView sObjectGuid)
{
  plWorld* pWorld = GetWorld();
  if (pWorld == nullptr)
    return nullptr;

  // A sequence-level clip has no bound object, so it plays on a single object of our own - it is
  // still positional audio, just parked at the origin where the listener can always hear it.
  if (sObjectGuid.IsEmpty())
  {
    plGameObject* pObject = nullptr;
    if (!m_hPreviewAudioObject.IsInvalidated() && pWorld->TryGetObject(m_hPreviewAudioObject, pObject))
      return pObject;

    plGameObjectDesc desc;
    desc.m_sName.Assign("SequenceAudioPreview");
    m_hPreviewAudioObject = pWorld->CreateObject(desc, pObject);
    return pObject;
  }

  const plUuid guid = plConversionUtils::ConvertStringToUuid(sObjectGuid);
  const plGameObjectHandle hObject = GetContext().m_GameObjectMap.GetHandle(guid);

  plGameObject* pObject = nullptr;
  return pWorld->TryGetObject(hObject, pObject) ? pObject : nullptr;
}

void plSequenceContext::StartPreviewAudio(plStringView sObjectGuid, plStringView sTrigger)
{
  plWorld* pWorld = GetWorld();
  if (pWorld == nullptr || sTrigger.IsEmpty())
    return;

  PL_LOCK(pWorld->GetWriteMarker());

  plGameObject* pObject = ResolvePreviewAudioObject(sObjectGuid);
  if (pObject == nullptr)
  {
    plLog::Warning("Sequence audio preview: object '{}' is not in the engine process.", sObjectGuid);
    return;
  }

  plAudioTriggerComponent* pTrigger = nullptr;
  if (!pObject->TryGetComponentOfBaseType(pTrigger))
  {
    plAudioTriggerComponentManager* pManager = pWorld->GetOrCreateComponentManager<plAudioTriggerComponentManager>();
    pManager->CreateComponent(pObject, pTrigger);
  }

  if (pTrigger == nullptr)
    return;

  pTrigger->SetPlayTrigger(sTrigger);
  pTrigger->Play();
}

void plSequenceContext::StopPreviewAudio(plStringView sObjectGuid)
{
  plWorld* pWorld = GetWorld();
  if (pWorld == nullptr)
    return;

  PL_LOCK(pWorld->GetWriteMarker());

  plGameObject* pObject = ResolvePreviewAudioObject(sObjectGuid);
  if (pObject == nullptr)
    return;

  plAudioTriggerComponent* pTrigger = nullptr;
  if (pObject->TryGetComponentOfBaseType(pTrigger))
    pTrigger->Stop();
}

void plSequenceContext::StopAllPreviewAudio()
{
  plWorld* pWorld = GetWorld();
  if (pWorld == nullptr)
    return;

  PL_LOCK(pWorld->GetWriteMarker());

  // Everything the preview could have started carries a trigger component, so stopping them all is
  // the reliable way back to silence - the editor does not tell us which ones were sounding.
  if (plAudioTriggerComponentManager* pManager = pWorld->GetOrCreateComponentManager<plAudioTriggerComponentManager>())
  {
    for (auto it = pManager->GetComponents(); it.IsValid(); ++it)
    {
      it->Stop();
    }
  }
}

plEngineProcessViewContext* plSequenceContext::CreateViewContext()
{
  return PL_DEFAULT_NEW(plSequenceViewContext, this);
}

void plSequenceContext::DestroyViewContext(plEngineProcessViewContext* pContext)
{
  PL_DEFAULT_DELETE(pContext);
}

bool plSequenceContext::UpdateThumbnailViewContext(plEngineProcessViewContext* pThumbnailViewContext)
{
  if (!m_bThumbnailCameraValid)
  {
    plLog::Warning("Sequence thumbnail: no camera transform was sent before the request.");
    return false;
  }

  const plVec3 vPosition = m_ThumbnailCameraTransform.m_vPosition;
  const plQuat qRotation = m_ThumbnailCameraTransform.m_qRotation;

  auto* pViewContext = static_cast<plSequenceViewContext*>(pThumbnailViewContext);

  plCamera& camera = pViewContext->GetCamera();
  camera.SetCameraMode(plCameraMode::PerspectiveFixedFovY, m_fThumbnailCameraFov, 0.1f, 1000.0f);
  // Plasma game objects look down +X with +Z up
  camera.LookAt(vPosition, vPosition + (qRotation * plVec3(1, 0, 0)), qRotation * plVec3(0, 0, 1));

  return true;
}

PL_STATICLINK_FILE(EnginePluginSequencer, EnginePluginSequencer_SequenceAsset_SequenceContext);
