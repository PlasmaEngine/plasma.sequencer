#include <EnginePluginSequencer/EnginePluginSequencerPCH.h>

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EnginePluginSequencer/SequenceAsset/SequenceContext.h>
#include <EnginePluginSequencer/SequenceAsset/SequenceView.h>
#include <Core/Messages/CommonMessages.h>
#include <Foundation/Time/Clock.h>
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

    // "<0|1>|<fps>". Sent once when a render to disk starts and once when it ends.
    if (pConfig->m_sWhatToDo == "SequenceRenderMode")
    {
      plHybridArray<plStringView, 4> parts;
      plStringView(pConfig->m_sValue).Split(false, parts, "|");

      const bool bEnable = parts.GetCount() > 0 && parts[0] == plStringView("1");

      double fFps = 60.0;
      if (parts.GetCount() > 1)
      {
        plStringBuilder sFps = parts[1];
        plConversionUtils::StringToFloat(sFps, fFps).IgnoreResult();
      }

      SetRenderMode(bEnable, (float)fFps);
      return;
    }

    // "<seconds>": the sequence time the preview viewport should be showing.
    if (pConfig->m_sWhatToDo == "SequencePreviewTime")
    {
      double fSeconds = 0.0;
      plStringBuilder sValue = pConfig->m_sValue;
      if (plConversionUtils::StringToFloat(sValue, fSeconds).Succeeded())
        AdvancePreviewTo(fSeconds);

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

void plSequenceContext::SetRenderMode(bool bEnabled, float fFramesPerSecond)
{
  plWorld* pWorld = GetWorld();
  if (pWorld == nullptr)
    return;

  m_bRenderMode = bEnabled;
  m_bCaptureInProgress = false;
  m_bSteppedThisCapture = false;

  PL_LOCK(pWorld->GetWriteMarker());

  if (bEnabled)
  {
    const double fStep = 1.0 / plMath::Max(1.0f, fFramesPerSecond);

    // Fixed rather than wall-clock: the output must not depend on how long a frame took to render,
    // and a frame here can take a second.
    pWorld->GetClock().SetFixedTimeStep(plTime::MakeFromSeconds(fStep));
    pWorld->GetClock().SetPaused(false);

    plLog::Info("Sequence render: simulating the world at a fixed {0} s step, so effects, physics and animation are captured.",
      plArgF(fStep, 4));
  }
  else
  {
    // Back to wall-clock time and frozen, which is what the editor preview expects.
    pWorld->GetClock().SetFixedTimeStep(plTime::MakeZero());
    pWorld->SetWorldSimulationEnabled(false);
  }
}

void plSequenceContext::OnInitialize()
{
  plEngineProcessDocumentContext::OnInitialize();

  if (plWorld* pWorld = GetWorld())
  {
    // plWorld starts simulating. For a sequence that is wrong from the first frame: effects would
    // run on wall-clock time regardless of the playhead, so the viewport would show a moment that
    // exists nowhere in the sequence and would differ every time it was looked at.
    PL_LOCK(pWorld->GetWriteMarker());
    pWorld->SetWorldSimulationEnabled(false);
  }
}

void plSequenceContext::RestartEffects()
{
  plWorld* pWorld = GetWorld();
  if (pWorld == nullptr)
    return;

  PL_LOCK(pWorld->GetWriteMarker());

  // Interrupt first so nothing is left mid-flight, then start, which builds a fresh effect instance
  // - that pair is what "back to time zero" means for anything playing.
  //
  // Sent to every object rather than to a known list: these are plain Core messages, so any
  // component that plays something answers them, and the sequencer needs no knowledge of the
  // particle package or of what else might be running.
  plMsgInterruptPlaying interrupt;
  plMsgSetPlaying play;
  play.m_bPlay = true;

  for (auto it = pWorld->GetObjects(); it.IsValid(); ++it)
  {
    it->SendMessage(interrupt);
    it->SendMessage(play);
  }
}

void plSequenceContext::AdvancePreviewTo(double fTargetSeconds)
{
  plWorld* pWorld = GetWorld();

  // A render owns the clock while it runs; its own one-step-per-frame scheme is what keeps the
  // output deterministic, and preview stepping on top of it would corrupt that.
  if (pWorld == nullptr || m_bRenderMode)
    return;

  double fDelta = fTargetSeconds - m_fPreviewTimeSeconds;

  // Going backwards. A simulation cannot run in reverse, so the only way to show the earlier moment
  // is to restart the effects and replay forwards into it.
  if (fDelta < 0.0)
  {
    RestartEffects();
    m_fPreviewTimeSeconds = 0.0;
    fDelta = fTargetSeconds;
  }

  if (fDelta <= 0.0)
  {
    m_fPreviewTimeSeconds = fTargetSeconds;
    return;
  }

  // Replaying a long span one step at a time would stall the editor for as long as the span, so
  // beyond this only the tail is replayed. The result is right for anything with a finite lifetime
  // - which is most of what a cinematic uses - and approximate for an effect older than the window.
  if (fDelta > MaxReplaySeconds)
  {
    m_fPreviewTimeSeconds = fTargetSeconds - MaxReplaySeconds;
    fDelta = MaxReplaySeconds;
  }

  // Subdivided so a slow editor frame does not hand physics one enormous step, which is how a
  // stable simulation turns into an explosion.
  plUInt32 uiSteps = (plUInt32)plMath::Ceil(fDelta / MaxStepSeconds);
  uiSteps = plMath::Clamp(uiSteps, 1u, MaxStepsPerMove);
  const double fStep = fDelta / (double)uiSteps;

  {
    PL_LOCK(pWorld->GetWriteMarker());

    const plTime previousStep = pWorld->GetClock().GetFixedTimeStep();
    pWorld->GetClock().SetFixedTimeStep(plTime::MakeFromSeconds(fStep));
    pWorld->GetClock().SetPaused(false);
    pWorld->SetWorldSimulationEnabled(true);

    for (plUInt32 i = 0; i < uiSteps; ++i)
    {
      pWorld->Update();
    }

    // Back to frozen, so the world advances only when the playhead does. plWorld pauses its clock
    // whenever simulation is off, so the main loop's own updates now cost zero time.
    pWorld->SetWorldSimulationEnabled(false);
    pWorld->GetClock().SetFixedTimeStep(previousStep);
  }

  m_fPreviewTimeSeconds = fTargetSeconds;
}

void plSequenceContext::OnThumbnailViewContextRequested()
{
  // A new captured frame begins. The base has just switched simulation off for the capture; the
  // world gets its single step from UpdateDocumentContext below.
  m_bCaptureInProgress = m_bRenderMode;
  m_bSteppedThisCapture = false;
}

void plSequenceContext::OnDestroyThumbnailViewContext()
{
  m_bCaptureInProgress = false;

  plEngineProcessDocumentContext::OnDestroyThumbnailViewContext();
}

void plSequenceContext::UpdateDocumentContext()
{
  if (m_bCaptureInProgress)
  {
    plWorld* pWorld = GetWorld();
    if (pWorld != nullptr)
    {
      // Exactly one simulating frame per capture. The convergence frames that follow still render
      // and still let resources stream in, but with the clock paused they advance nothing, so the
      // simulation stays locked to the output frame rate.
      const bool bStepNow = !m_bSteppedThisCapture;
      m_bSteppedThisCapture = true;

      PL_LOCK(pWorld->GetWriteMarker());
      pWorld->SetWorldSimulationEnabled(bStepNow);
    }
  }

  plEngineProcessDocumentContext::UpdateDocumentContext();
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
