#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <Core/World/GameObject.h>
#include <EditorFramework/IPC/IPCObjectMirrorEditor.h>
#include <EditorFramework/Object/ObjectPropertyPath.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceObjectAccessor.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceObjectManager.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Object/ObjectCommandAccessor.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceTrack, 1, plRTTIDefaultAllocator<plSequenceTrack>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("ComponentType", m_sComponentType),
    PL_MEMBER_PROPERTY("Property", m_sPropertyPath),
    PL_ENUM_MEMBER_PROPERTY("Target", plPropertyAnimTarget, m_Target),
    PL_MEMBER_PROPERTY("FloatCurve", m_FloatCurve),
    PL_MEMBER_PROPERTY("Gradient", m_ColorGradient),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceClip, 1, plRTTIDefaultAllocator<plSequenceClip>)
{
  PL_BEGIN_PROPERTIES
  {
    // The kind follows from the concrete type, so it is data the lane filter and the runtime read
    // rather than something to edit.
    PL_ENUM_MEMBER_PROPERTY("Kind", plSequenceClipKind, m_Kind)->AddAttributes(new plHiddenAttribute()),
    // Ordered after the derived types' payload group: what a clip plays matters more than when.
    PL_MEMBER_PROPERTY("StartTick", m_uiStartTick)->AddAttributes(new plGroupAttribute("Timing", 2.0f)),
    PL_MEMBER_PROPERTY("DurationTicks", m_uiDurationTicks)->AddAttributes(new plDefaultValueAttribute(4800)),
    PL_MEMBER_PROPERTY("Speed", m_fSpeed)->AddAttributes(new plDefaultValueAttribute(1.0f)),
    PL_MEMBER_PROPERTY("BlendInTicks", m_uiBlendInTicks),
    PL_MEMBER_PROPERTY("BlendOutTicks", m_uiBlendOutTicks),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceAudioClip, 1, plRTTIDefaultAllocator<plSequenceAudioClip>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("PlayTrigger", GetPayload, SetPayload)
      ->AddAttributes(new plDynamicStringEnumAttribute("AudioSystem.Trigger"), new plGroupAttribute("Audio", 1.0f)),
    PL_ACCESSOR_PROPERTY("StopTrigger", GetStopPayload, SetStopPayload)
      ->AddAttributes(new plDynamicStringEnumAttribute("AudioSystem.Trigger")),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceAnimationClip, 1, plRTTIDefaultAllocator<plSequenceAnimationClip>)
{
  PL_BEGIN_PROPERTIES
  {
    // Same filter plSimpleAnimationComponent uses, since that is what ends up playing this.
    PL_ACCESSOR_PROPERTY("Clip", GetPayload, SetPayload)
      ->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Keyframe_Animation"), new plGroupAttribute("Animation", 1.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceSubSequenceClip, 1, plRTTIDefaultAllocator<plSequenceSubSequenceClip>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Sequence", GetPayload, SetPayload)
      ->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Sequence"), new plGroupAttribute("Sub-Sequence", 1.0f)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

const plRTTI* plSequenceClipTypeForKind(plSequenceClipKind::Enum kind)
{
  switch (kind)
  {
    case plSequenceClipKind::AnimationClip:
      return plGetStaticRTTI<plSequenceAnimationClip>();
    case plSequenceClipKind::SubSequence:
      return plGetStaticRTTI<plSequenceSubSequenceClip>();
    case plSequenceClipKind::AudioTrigger:
    default:
      return plGetStaticRTTI<plSequenceAudioClip>();
  }
}

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceBinding, 1, plRTTIDefaultAllocator<plSequenceBinding>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("ObjectPath", m_sObjectSearchSequence),
    PL_ARRAY_MEMBER_PROPERTY("Tracks", m_Tracks)->AddFlags(plPropertyFlags::PointerOwner),
    PL_ARRAY_MEMBER_PROPERTY("Clips", m_Clips)->AddFlags(plPropertyFlags::PointerOwner),
    PL_ARRAY_MEMBER_PROPERTY("ClipLanes", m_ClipLanes),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceCameraCut, 1, plRTTIDefaultAllocator<plSequenceCameraCut>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("CameraName", m_sCameraName),
    PL_MEMBER_PROPERTY("CameraPath", m_sCameraObjectPath),
    PL_MEMBER_PROPERTY("StartTick", m_uiStartTick),
    PL_MEMBER_PROPERTY("DurationTicks", m_uiDurationTicks)->AddAttributes(new plDefaultValueAttribute(9600)),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceMarker, 1, plRTTIDefaultAllocator<plSequenceMarker>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("Name", m_sName),
    PL_MEMBER_PROPERTY("Tick", m_uiTick),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceData, 1, plRTTIDefaultAllocator<plSequenceData>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("FPS", m_uiFramesPerSecond)->AddAttributes(new plDefaultValueAttribute(30)),
    PL_MEMBER_PROPERTY("Duration", m_uiDurationTicks)->AddAttributes(new plDefaultValueAttribute(24000)), // 5 s at plSequenceTicksPerSecond (4800)
    PL_ARRAY_MEMBER_PROPERTY("Bindings", m_Bindings)->AddFlags(plPropertyFlags::PointerOwner),
    PL_ARRAY_MEMBER_PROPERTY("CameraCuts", m_CameraCuts)->AddFlags(plPropertyFlags::PointerOwner),
    PL_ARRAY_MEMBER_PROPERTY("Clips", m_Clips)->AddFlags(plPropertyFlags::PointerOwner),
    PL_ARRAY_MEMBER_PROPERTY("ClipLanes", m_ClipLanes),
    PL_ARRAY_MEMBER_PROPERTY("Markers", m_Markers)->AddFlags(plPropertyFlags::PointerOwner),
    PL_MEMBER_PROPERTY("EventTrack", m_EventTrack),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plSequenceBinding::~plSequenceBinding()
{
  for (plSequenceTrack* pTrack : m_Tracks)
  {
    PL_DEFAULT_DELETE(pTrack);
  }

  for (plSequenceClip* pClip : m_Clips)
  {
    PL_DEFAULT_DELETE(pClip);
  }
}

plSequenceData::~plSequenceData()
{
  for (plSequenceBinding* pBinding : m_Bindings)
  {
    PL_DEFAULT_DELETE(pBinding);
  }

  for (plSequenceCameraCut* pCut : m_CameraCuts)
  {
    PL_DEFAULT_DELETE(pCut);
  }

  for (plSequenceMarker* pMarker : m_Markers)
  {
    PL_DEFAULT_DELETE(pMarker);
  }

  for (plSequenceClip* pClip : m_Clips)
  {
    PL_DEFAULT_DELETE(pClip);
  }
}

plSequenceAssetDocument::plSequenceAssetDocument(plStringView sDocumentPath)
  : BaseClass(PL_DEFAULT_NEW(plSequenceObjectManager), sDocumentPath, plAssetDocEngineConnection::FullObjectMirroring)
{
  m_GameObjectContextEvents.AddEventHandler(plMakeDelegate(&plSequenceAssetDocument::GameObjectContextEventHandler, this));

  // Auto-key: edits to the context scene - property grid, and above all the transform gizmos -
  // become keys at the playhead instead of meaningless writes to throwaway objects.
  m_pObjectAccessor = PL_DEFAULT_NEW(plSequenceObjectAccessor, this, GetCommandHistory());

  plDocument::s_EventsAny.AddEventHandler(plMakeDelegate(&plSequenceAssetDocument::DocumentEventHandler, this));
}

plSequenceAssetDocument::~plSequenceAssetDocument()
{
  plDocument::s_EventsAny.RemoveEventHandler(plMakeDelegate(&plSequenceAssetDocument::DocumentEventHandler, this));
  m_GameObjectContextEvents.RemoveEventHandler(plMakeDelegate(&plSequenceAssetDocument::GameObjectContextEventHandler, this));

  GetObjectManager()->m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plSequenceAssetDocument::TreeStructureEventHandler, this));
  GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plSequenceAssetDocument::TreePropertyEventHandler, this));
}

void plSequenceAssetDocument::InitializeAfterLoading(bool bFirstTimeCreation)
{
  // The IPC mirror has to exist before we can filter it - the base class only creates one on its
  // own later, during engine connection. PropertyAnim does the same.
  m_pMirror = PL_DEFAULT_NEW(plIPCObjectMirrorEditor);

  // The local mirror feeds the native plSequenceData (what evaluation reads) - it must ignore the
  // temporaries, i.e. the mirrored context scene. The remote IPC mirror only sends the temporaries,
  // i.e. the context, to the engine process - the sequence data itself means nothing there.
  // Same split PropertyAnim sets up; the filters have to be in place before base init sends the doc.
  m_ObjectMirror.SetFilterFunction([this](const plDocumentObject* pObject, plStringView sProperty) -> bool
    { return !static_cast<plSequenceObjectManager*>(GetObjectManager())->IsTemporary(pObject, sProperty); });
  m_pMirror->SetFilterFunction([this](const plDocumentObject* pObject, plStringView sProperty) -> bool
    { return static_cast<plSequenceObjectManager*>(GetObjectManager())->IsTemporary(pObject, sProperty); });

  SUPER::InitializeAfterLoading(bFirstTimeCreation);

  // Subscribe after base init, so the mirrors see changes before the mapping reacts to them.
  GetObjectManager()->m_StructureEvents.AddEventHandler(plMakeDelegate(&plSequenceAssetDocument::TreeStructureEventHandler, this));
  GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plSequenceAssetDocument::TreePropertyEventHandler, this));
}

void plSequenceAssetDocument::GameObjectContextEventHandler(const plGameObjectContextEvent& e)
{
  switch (e.m_Type)
  {
    case plGameObjectContextEvent::Type::ContextAboutToBeChanged:
      ClearMapping();
      static_cast<plSequenceObjectManager*>(GetObjectManager())->SetAllowStructureChangeOnTemporaries(true);
      break;
    case plGameObjectContextEvent::Type::ContextChanged:
    {
      static_cast<plSequenceObjectManager*>(GetObjectManager())->SetAllowStructureChangeOnTemporaries(false);
      RebuildMapping();

      plSequenceAssetEvent evt;
      evt.m_pDocument = this;
      evt.m_Type = plSequenceAssetEvent::Type::ContextChanged;
      m_SequenceEvents.Broadcast(evt);
      break;
    }
  }
}

void plSequenceAssetDocument::HandleEngineMessage(const plEditorEngineDocumentMsg* pMsg)
{
  if (const auto* pThumbnail = plDynamicCast<const plCreateThumbnailMsgToEditor*>(pMsg))
  {
    m_ThumbnailArrived.Broadcast(*pThumbnail);
    return;
  }

  SUPER::HandleEngineMessage(pMsg);
}

void plSequenceAssetDocument::DocumentEventHandler(const plDocumentEvent& e)
{
  // The context scene is a clone taken when the context was attached, so edits made in the scene
  // window are invisible here. Re-clone it when that document is saved - saving is the point at
  // which the cached graph the context reads from is refreshed, and it is a predictable moment
  // for the sequence to change under the user.
  if (e.m_Type != plDocumentEvent::Type::DocumentSaved || e.m_pDocument == nullptr)
    return;

  const plUuid contextGuid = GetContextDocumentGuid();
  if (!contextGuid.IsValid() || e.m_pDocument->GetGuid() != contextGuid)
    return;

  const plUuid contextObject = GetContextObjectGuid();
  const plUInt64 uiScrubberPos = m_uiScrubberTickPos;

  // SetContext broadcasts the context-changed events, which clear and rebuild the track mapping.
  if (SetContext(contextGuid, contextObject).Failed())
    return;

  // Re-apply the sequence at the playhead so the refreshed scene shows the animated pose, not the
  // pose it was saved in.
  m_uiScrubberTickPos = uiScrubberPos;
  ApplyAnimation();

  plSequenceAssetEvent evt;
  evt.m_pDocument = this;
  evt.m_Type = plSequenceAssetEvent::Type::ScrubberPositionChanged;
  m_SequenceEvents.Broadcast(evt);
}

void plSequenceAssetDocument::TreeStructureEventHandler(const plDocumentObjectStructureEvent& e)
{
  auto pManager = static_cast<plSequenceObjectManager*>(GetObjectManager());
  if (e.m_pPreviousParent && pManager->IsTemporary(e.m_pPreviousParent, e.m_sParentProperty))
    return;
  if (e.m_pNewParent && pManager->IsTemporary(e.m_pNewParent, e.m_sParentProperty))
    return;

  const plRTTI* pType = e.m_pObject->GetType();

  if (pType == plGetStaticRTTI<plSequenceTrack>())
  {
    switch (e.m_EventType)
    {
      case plDocumentObjectStructureEvent::Type::AfterObjectAdded:
      case plDocumentObjectStructureEvent::Type::AfterObjectMoved:
        AddTrackMapping(e.m_pObject->GetGuid());
        return;
      case plDocumentObjectStructureEvent::Type::BeforeObjectRemoved:
      case plDocumentObjectStructureEvent::Type::BeforeObjectMoved:
        RemoveTrackMapping(e.m_pObject->GetGuid());
        return;

      default:
        break;
    }
  }
  else if (pType == plGetStaticRTTI<plSequenceBinding>())
  {
    // A binding carries the object path its tracks resolve through, and removing one takes all its
    // tracks along without per-track events. Tear everything down before the change and rebuild
    // afterwards - bindings are few, this stays cheap.
    switch (e.m_EventType)
    {
      case plDocumentObjectStructureEvent::Type::BeforeObjectRemoved:
      case plDocumentObjectStructureEvent::Type::BeforeObjectMoved:
        ClearMapping();
        return;
      case plDocumentObjectStructureEvent::Type::AfterObjectAdded:
      case plDocumentObjectStructureEvent::Type::AfterObjectRemoved:
      case plDocumentObjectStructureEvent::Type::AfterObjectMoved:
        RebuildMapping();
        return;

      default:
        break;
    }
  }
  else
  {
    ApplyAnimation();
  }
}

void plSequenceAssetDocument::TreePropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  auto pManager = static_cast<plSequenceObjectManager*>(GetObjectManager());
  if (pManager->IsTemporary(e.m_pObject))
    return;

  const plRTTI* pType = e.m_pObject->GetType();

  if (pType == plGetStaticRTTI<plSequenceTrack>())
  {
    if (e.m_EventType == plDocumentObjectPropertyEvent::Type::PropertySet)
    {
      RemoveTrackMapping(e.m_pObject->GetGuid());
      AddTrackMapping(e.m_pObject->GetGuid());
      return;
    }
  }
  else if (pType == plGetStaticRTTI<plSequenceBinding>())
  {
    if (e.m_EventType == plDocumentObjectPropertyEvent::Type::PropertySet)
    {
      RebuildMapping();
      return;
    }
  }
  else
  {
    ApplyAnimation();
  }
}

// ---- timeline framing ------------------------------------------------------

plTime plSequenceAssetDocument::GetDuration() const
{
  return plTime::MakeFromSeconds((double)GetProperties()->m_uiDurationTicks / (double)plSequenceTicksPerSecond);
}

plUInt64 plSequenceAssetDocument::GetDurationTicks() const
{
  return GetProperties()->m_uiDurationTicks;
}

void plSequenceAssetDocument::SetDurationTicks(plUInt64 uiNumTicks)
{
  if (GetProperties()->m_uiDurationTicks == uiNumTicks)
    return;

  {
    plCommandHistory* pHistory = GetCommandHistory();
    pHistory->StartTransaction("Set Sequence Duration");

    plSetObjectPropertyCommand cmdSet;
    cmdSet.m_Object = GetPropertyObject()->GetGuid();
    cmdSet.m_sProperty = "Duration";
    cmdSet.m_NewValue = uiNumTicks;
    pHistory->AddCommand(cmdSet).AssertSuccess();

    pHistory->FinishTransaction();
  }

  plSequenceAssetEvent e;
  e.m_pDocument = this;
  e.m_Type = plSequenceAssetEvent::Type::DurationChanged;
  m_SequenceEvents.Broadcast(e);
}

plUInt64 plSequenceAssetDocument::GetTicksPerFrame() const
{
  const plUInt16 uiFps = plMath::Max<plUInt16>(1, GetProperties()->m_uiFramesPerSecond);
  return plMath::Max<plUInt64>(1, plSequenceTicksPerSecond / uiFps);
}

// ---- scrubbing & playback --------------------------------------------------

bool plSequenceAssetDocument::SetScrubberPosition(plUInt64 uiTick)
{
  uiTick = plMath::Min(uiTick, GetDurationTicks());

  if (m_uiScrubberTickPos == uiTick)
    return false;

  m_uiScrubberTickPos = uiTick;

  ApplyAnimation();

  plSequenceAssetEvent e;
  e.m_pDocument = this;
  e.m_Type = plSequenceAssetEvent::Type::ScrubberPositionChanged;
  m_SequenceEvents.Broadcast(e);
  return true;
}

void plSequenceAssetDocument::SetPlayAnimation(bool bPlay)
{
  if (m_bPlayAnimation == bPlay)
    return;

  if (m_uiScrubberTickPos >= GetDurationTicks())
    m_uiScrubberTickPos = 0;

  m_bPlayAnimation = bPlay;
  m_LastFrameTime = plTime::Now();

  plSequenceAssetEvent e;
  e.m_pDocument = this;
  e.m_Type = plSequenceAssetEvent::Type::PlaybackChanged;
  m_SequenceEvents.Broadcast(e);
}

void plSequenceAssetDocument::SetRepeatAnimation(bool bRepeat)
{
  if (m_bRepeatAnimation == bRepeat)
    return;

  m_bRepeatAnimation = bRepeat;

  plSequenceAssetEvent e;
  e.m_pDocument = this;
  e.m_Type = plSequenceAssetEvent::Type::PlaybackChanged;
  m_SequenceEvents.Broadcast(e);
}

void plSequenceAssetDocument::ExecuteAnimationPlaybackStep()
{
  if (!m_bPlayAnimation)
    return;

  const plTime now = plTime::Now();
  const plTime diff = now - m_LastFrameTime;

  const plUInt64 uiAdvance = (plUInt64)(diff.GetSeconds() * (double)plSequenceTicksPerSecond);

  // Accumulate further if we render too fast and the elapsed time rounds to zero ticks.
  if (uiAdvance == 0)
    return;

  m_LastFrameTime = now;
  plUInt64 uiNewPos = m_uiScrubberTickPos + uiAdvance;

  const plUInt64 uiDuration = GetDurationTicks();
  if (uiNewPos >= uiDuration)
  {
    if (m_bRepeatAnimation && uiDuration > 0)
    {
      uiNewPos %= uiDuration;
    }
    else
    {
      uiNewPos = uiDuration;
      SetPlayAnimation(false);
    }
  }

  SetScrubberPosition(uiNewPos);
}

void plSequenceAssetDocument::BeginThumbnailPose(plUInt64 uiTick)
{
  m_uiPoseOverrideTick = uiTick;

  // Push the posed values into the mirrored objects. The IPC mirror forwards them to the engine,
  // so by the time the thumbnail request that follows is processed the world is at this time.
  ApplyAnimation();
}

void plSequenceAssetDocument::EndThumbnailPose()
{
  if (!IsThumbnailPoseActive())
    return;

  m_uiPoseOverrideTick = plMath::MaxValue<plUInt64>();
  ApplyAnimation(); // back to the playhead
}

// ---- preview mapping & evaluation ------------------------------------------

const plSequenceTrack* plSequenceAssetDocument::GetTrack(const plUuid& trackGuid) const
{
  auto obj = m_Context.GetObjectByGUID(trackGuid);
  if (obj.m_pObject == nullptr || obj.m_pType != plGetStaticRTTI<plSequenceTrack>())
    return nullptr;

  return static_cast<const plSequenceTrack*>(obj.m_pObject);
}

const plSequenceClip* plSequenceAssetDocument::GetClip(const plUuid& clipGuid) const
{
  auto obj = m_Context.GetObjectByGUID(clipGuid);

  // derived-from rather than equal: a clip is always one of the per-kind types
  if (obj.m_pObject == nullptr || obj.m_pType == nullptr || !obj.m_pType->IsDerivedFrom<plSequenceClip>())
    return nullptr;

  return static_cast<const plSequenceClip*>(obj.m_pObject);
}

const plDocumentObject* plSequenceAssetDocument::ResolveBindingObject(const plUuid& bindingGuid) const
{
  if (GetContextObject() == nullptr)
    return nullptr;

  const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid);
  if (pBindingObj == nullptr)
    return nullptr;

  const plString sObjectPath = pBindingObj->GetTypeAccessor().GetValue("ObjectPath").ConvertTo<plString>();

  // Resolve through the same path machinery the tracks use: every game object has a Name property,
  // so resolving the path against it yields the object reference.
  plHybridArray<plPropertyReference, 1> keys;
  if (FindTrackKeys(sObjectPath.GetData(), "", "Name", keys).Failed() || keys.IsEmpty())
    return nullptr;

  return GetObjectManager()->GetObject(keys[0].m_Object);
}

void plSequenceAssetDocument::RebuildMapping()
{
  ClearMapping();

  // Walk the document objects: bindings, then their tracks.
  const plDocumentObject* pPropObj = GetPropertyObject();
  const plInt32 iBindingCount = pPropObj->GetTypeAccessor().GetCount("Bindings");
  for (plInt32 b = 0; b < iBindingCount; ++b)
  {
    const plVariant bindingGuid = pPropObj->GetTypeAccessor().GetValue("Bindings", b);
    if (!bindingGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid.Get<plUuid>());
    if (pBindingObj == nullptr)
      continue;

    const plInt32 iTrackCount = pBindingObj->GetTypeAccessor().GetCount("Tracks");
    for (plInt32 t = 0; t < iTrackCount; ++t)
    {
      const plVariant trackGuid = pBindingObj->GetTypeAccessor().GetValue("Tracks", t);
      if (trackGuid.IsA<plUuid>())
        AddTrackMapping(trackGuid.Get<plUuid>());
    }
  }
}

void plSequenceAssetDocument::ClearMapping()
{
  while (!m_TrackTable.IsEmpty())
  {
    RemoveTrackMapping(m_TrackTable.GetIterator().Key());
  }

  PL_ASSERT_DEBUG(m_PropertyTable.IsEmpty() && m_TrackTable.IsEmpty(), "All track mappings should be removed.");
}

void plSequenceAssetDocument::AddTrackMapping(const plUuid& trackGuid)
{
  if (m_TrackTable.Contains(trackGuid))
    return;

  auto& keys = m_TrackTable[trackGuid];

  if (GetContextObject() == nullptr)
    return;

  const plSequenceTrack* pTrack = GetTrack(trackGuid);
  const plDocumentObject* pTrackObj = GetObjectManager()->GetObject(trackGuid);
  if (pTrack == nullptr || pTrackObj == nullptr)
    return;

  // The object search path lives on the parent binding.
  const plDocumentObject* pBindingObj = pTrackObj->GetParent();
  if (pBindingObj == nullptr || pBindingObj->GetType() != plGetStaticRTTI<plSequenceBinding>())
    return;

  const plString sObjectPath = pBindingObj->GetTypeAccessor().GetValue("ObjectPath").ConvertTo<plString>();

  FindTrackKeys(sObjectPath.GetData(), pTrack->m_sComponentType.GetData(), pTrack->m_sPropertyPath.GetData(), keys).IgnoreResult();

  for (const plPropertyReference& key : keys)
  {
    if (!m_PropertyTable.Contains(key))
    {
      PropertyValue value;
      if (GetObjectAccessor()->GetValue(GetObjectManager()->GetObject(key.m_Object), key.m_pProperty, value.m_InitialValue, key.m_Index).Failed())
        continue;

      m_PropertyTable.Insert(key, value);
    }

    PropertyValue& value = *m_PropertyTable.GetValue(key);
    value.m_Tracks.PushBack(trackGuid);
    ApplyAnimation(key, value);
  }
}

void plSequenceAssetDocument::RemoveTrackMapping(const plUuid& trackGuid)
{
  auto* pKeys = m_TrackTable.GetValue(trackGuid);
  if (pKeys == nullptr)
    return;

  for (const plPropertyReference& key : *pKeys)
  {
    PropertyValue* pValue = m_PropertyTable.GetValue(key);
    if (pValue == nullptr)
      continue;

    pValue->m_Tracks.RemoveAndSwap(trackGuid);
    ApplyAnimation(key, *pValue); // with the track gone; the last one out restores the initial value
    if (pValue->m_Tracks.IsEmpty())
      m_PropertyTable.Remove(key);
  }

  m_TrackTable.Remove(trackGuid);
}

plStatus plSequenceAssetDocument::FindTrackKeys(const char* szObjectSearchSequence, const char* szComponentType, const char* szPropertyPath,
  plHybridArray<plPropertyReference, 1>& keys) const
{
  plObjectPropertyPathContext context = {GetContextObject(), GetObjectAccessor(), "TempObjects"};

  keys.Clear();
  return plObjectPropertyPath::ResolvePath(context, keys, szObjectSearchSequence, szComponentType, szPropertyPath);
}

void plSequenceAssetDocument::ApplyAnimation()
{
  for (auto it = m_PropertyTable.GetIterator(); it.IsValid(); ++it)
  {
    ApplyAnimation(it.Key(), it.Value());
  }

  UpdateClipPreview();
  SendPreviewTime();
}

void plSequenceAssetDocument::SendPreviewTime()
{
  // A thumbnail pose - the filmstrip or a render to disk - drives the world itself, at its own
  // fixed step. Two sources of simulation time would fight.
  if (IsThumbnailPoseActive())
    return;

  if (GetEditorEngineConnection() == nullptr)
    return;

  const double fSeconds = (double)m_uiScrubberTickPos / (double)plSequenceTicksPerSecond;

  plStringBuilder sValue;
  sValue.SetFormat("{0}", plArgF(fSeconds, 6));

  plDocumentConfigMsgToEngine msg;
  msg.m_DocumentGuid = GetGuid();
  msg.m_sWhatToDo = "SequencePreviewTime";
  msg.m_sValue = sValue;
  GetEditorEngineConnection()->SendMessage(&msg);
}

void plSequenceAssetDocument::UpdateClipPreview()
{
  // Scrubbing must stay silent: a trigger fired on every playhead step turns a drag into a burst of
  // overlapping one-shots. Only actual playback sounds.
  if (!GetPlayAnimation())
  {
    StopAllPreviewClips();
    return;
  }

  const plInt64 iTick = (plInt64)EffectiveTick();

  plSet<plUuid> wanted;
  plMap<plUuid, plString> payloads;
  plMap<plUuid, plUuid> owners; ///< clip -> binding object guid, invalid for sequence-level clips

  auto collect = [&](const plDocumentObject* pOwnerObj, const plUuid& bindingGuid)
  {
    if (pOwnerObj == nullptr)
      return;

    const plInt32 iCount = pOwnerObj->GetTypeAccessor().GetCount("Clips");
    for (plInt32 c = 0; c < iCount; ++c)
    {
      const plVariant clipGuid = pOwnerObj->GetTypeAccessor().GetValue("Clips", c);
      if (!clipGuid.IsA<plUuid>())
        continue;

      const plSequenceClip* pClip = GetClip(clipGuid.Get<plUuid>());
      if (pClip == nullptr || pClip->m_Kind != plSequenceClipKind::AudioTrigger || pClip->m_sPayload.IsEmpty())
        continue;

      const plInt64 iStart = (plInt64)pClip->m_uiStartTick;
      const plInt64 iEnd = iStart + (plInt64)pClip->m_uiDurationTicks;

      if (iTick >= iStart && iTick < iEnd)
      {
        wanted.Insert(clipGuid.Get<plUuid>());
        payloads[clipGuid.Get<plUuid>()] = pClip->m_sPayload;
        owners[clipGuid.Get<plUuid>()] = bindingGuid;
      }
    }
  };

  const plDocumentObject* pPropObj = GetPropertyObject();
  collect(pPropObj, plUuid()); // sequence-level clips, which have no bound object

  const plInt32 iBindingCount = pPropObj->GetTypeAccessor().GetCount("Bindings");
  for (plInt32 b = 0; b < iBindingCount; ++b)
  {
    const plVariant bindingGuid = pPropObj->GetTypeAccessor().GetValue("Bindings", b);
    if (!bindingGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid.Get<plUuid>());
    const plDocumentObject* pBoundObj = ResolveBindingObject(bindingGuid.Get<plUuid>());
    collect(pBindingObj, pBoundObj != nullptr ? pBoundObj->GetGuid() : plUuid());
  }

  auto send = [this](plStringView sVerb, const plUuid& objectGuid, plStringView sPayload)
  {
    plStringBuilder sObject;
    if (objectGuid.IsValid())
      plConversionUtils::ToString(objectGuid, sObject);

    plStringBuilder sValue;
    sValue.Set(sVerb, "|", sObject, "|", sPayload);

    plDocumentConfigMsgToEngine msg;
    msg.m_DocumentGuid = GetGuid();
    msg.m_sWhatToDo = "SequenceAudioPreview";
    msg.m_sValue = sValue;
    GetEditorEngineConnection()->SendMessage(&msg);
  };

  for (auto it : m_RunningPreviewClips)
  {
    if (!wanted.Contains(it.Key()))
      send("stop", it.Value(), "");
  }

  plMap<plUuid, plUuid> stillRunning;
  for (const plUuid& clipGuid : wanted)
  {
    if (!m_RunningPreviewClips.Contains(clipGuid))
      send("start", owners[clipGuid], payloads[clipGuid]);

    stillRunning[clipGuid] = owners[clipGuid];
  }

  m_RunningPreviewClips.Swap(stillRunning);
}

void plSequenceAssetDocument::StopAllPreviewClips()
{
  if (m_RunningPreviewClips.IsEmpty())
    return;

  m_RunningPreviewClips.Clear();

  plDocumentConfigMsgToEngine msg;
  msg.m_DocumentGuid = GetGuid();
  msg.m_sWhatToDo = "SequenceAudioPreview";
  msg.m_sValue = "stopall||";
  GetEditorEngineConnection()->SendMessage(&msg);
}

void plSequenceAssetDocument::ApplyAnimation(const plPropertyReference& key, const PropertyValue& value)
{
  plVariant animValue = value.m_InitialValue;
  plAngle euler[3];
  bool bIsRotation = false;

  for (const plUuid& trackGuid : value.m_Tracks)
  {
    const plSequenceTrack* pTrack = GetTrack(trackGuid);
    if (pTrack == nullptr)
      continue;

    const plRTTI* pPropRtti = key.m_pProperty->GetSpecificType();

    switch (pTrack->m_Target)
    {
      case plPropertyAnimTarget::Number:
      {
        if (pPropRtti->GetVariantType() >= plVariantType::Bool && pPropRtti->GetVariantType() <= plVariantType::Double)
        {
          plVariant evaluated = pTrack->m_FloatCurve.Evaluate((plInt64)EffectiveTick());
          animValue = evaluated.ConvertTo(animValue.GetType());
        }
      }
      break;

      case plPropertyAnimTarget::VectorX:
      case plPropertyAnimTarget::VectorY:
      case plPropertyAnimTarget::VectorZ:
      case plPropertyAnimTarget::VectorW:
      {
        if (pPropRtti->GetVariantType() >= plVariantType::Vector2 && pPropRtti->GetVariantType() <= plVariantType::Vector4U)
        {
          const double fValue = pTrack->m_FloatCurve.Evaluate((plInt64)EffectiveTick());
          plReflectionUtils::SetComponent(animValue, (plUInt32)pTrack->m_Target - plPropertyAnimTarget::VectorX, fValue);
        }
      }
      break;

      case plPropertyAnimTarget::RotationX:
      case plPropertyAnimTarget::RotationY:
      case plPropertyAnimTarget::RotationZ:
      {
        if (pPropRtti->GetVariantType() == plVariantType::Quaternion)
        {
          bIsRotation = true;
          const double fValue = pTrack->m_FloatCurve.Evaluate((plInt64)EffectiveTick());
          euler[(plUInt32)pTrack->m_Target - plPropertyAnimTarget::RotationX] = plAngle::MakeFromDegree((float)fValue);
        }
      }
      break;

      case plPropertyAnimTarget::Color:
      {
        if (pPropRtti->GetVariantType() == plVariantType::Color || pPropRtti->GetVariantType() == plVariantType::ColorGamma)
        {
          plVariant evaluated = pTrack->m_ColorGradient.Evaluate((plInt64)EffectiveTick());
          animValue = evaluated.ConvertTo(animValue.GetType());
        }
      }
      break;
    }
  }

  if (bIsRotation)
  {
    plQuat qRotation = plQuat::MakeFromEulerAngles(euler[0], euler[1], euler[2]);
    animValue = qRotation;
  }

  plDocumentObject* pObj = GetObjectManager()->GetObject(key.m_Object);
  if (pObj == nullptr)
    return;

  plVariant oldValue;
  if (GetObjectAccessor()->GetValue(pObj, key.m_pProperty, oldValue, key.m_Index).Failed())
    return;

  // Write through the object manager, NOT the command history: preview changes are temporary, must
  // not dirty the document or the undo stack, and reach the engine through the remote mirror.
  if (oldValue != animValue)
    GetObjectManager()->SetValue(pObj, key.m_pProperty->GetPropertyName(), animValue, key.m_Index).AssertSuccess();

  // gizmos listen to commands and selection, neither of which happens here
  {
    plGameObjectEvent e;
    e.m_Type = plGameObjectEvent::Type::GizmoTransformMayBeInvalid;
    m_GameObjectEvents.Broadcast(e);
  }
}

// ---- editing ---------------------------------------------------------------

plStatus plSequenceAssetDocument::DeriveBindingPath(const plDocumentObject* pObject, plStringBuilder& out_sName, plStringBuilder& out_sSearchSequence) const
{
  PL_ASSERT_DEV(pObject != nullptr, "Invalid object");

  plStringBuilder& sName = out_sName;
  sName.Clear();
  {
    plVariant name = pObject->GetTypeAccessor().GetValue("Name");
    if (name.IsA<plString>())
      sName = name.Get<plString>();
    if (sName.IsEmpty())
      sName = "<unnamed>";
  }

  // Walk up to the context root, collecting object names - the same search sequence
  // plObjectPropertyPath produces and the runtime resolver consumes.
  plStringBuilder& sSearchSequence = out_sSearchSequence;
  sSearchSequence.Clear();
  {
    plHybridArray<const plDocumentObject*, 8> chain;
    const plDocumentObject* pCur = pObject;
    const plDocumentObject* pContextRoot = GetContextObject();
    while (pCur != nullptr && pCur != pContextRoot && pCur->GetType() == plGetStaticRTTI<plGameObject>())
    {
      chain.PushBack(pCur);
      pCur = pCur->GetParent();
    }

    for (plUInt32 i = chain.GetCount(); i-- > 0;)
    {
      plVariant name = chain[i]->GetTypeAccessor().GetValue("Name");
      plStringBuilder sPart;
      if (name.IsA<plString>())
        sPart = name.Get<plString>();

      if (!sSearchSequence.IsEmpty())
        sSearchSequence.Append("/");
      sSearchSequence.Append(sPart);
    }
  }

  // Same rule as camera cuts: bindings resolve by name at runtime, so an unnamed object cannot be
  // bound. The context root itself is the exception - it needs no path.
  if (sSearchSequence.IsEmpty() && pObject != GetContextObject())
  {
    return plStatus("This object has no name, so the sequence cannot find it again.\n\n"
                    "Give the object a name in the scene, save the scene, then bind it.");
  }

  return plStatus(PL_SUCCESS);
}

plUuid plSequenceAssetDocument::AddBindingForContextObject(const plDocumentObject* pObject)
{
  plStringBuilder sName;
  plStringBuilder sSearchSequence;

  if (plStatus status = DeriveBindingPath(pObject, sName, sSearchSequence); status.Failed())
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation(plFmt("{}", status.m_sMessage));
    return plUuid();
  }

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Binding");

  plUuid newBindingGuid;
  PL_VERIFY(acc.AddObjectByName(GetPropertyObject(), "Bindings", -1, plGetStaticRTTI<plSequenceBinding>(), newBindingGuid).Succeeded(), "");
  const plDocumentObject* pBindingObj = accessor.GetObject(newBindingGuid);
  PL_VERIFY(acc.SetValueByName(pBindingObj, "Name", sName.GetView()).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pBindingObj, "ObjectPath", sSearchSequence.GetView()).Succeeded(), "");

  acc.FinishTransaction();
  return newBindingGuid;
}

plStatus plSequenceAssetDocument::RebindTo(const plUuid& bindingGuid, const plDocumentObject* pObject)
{
  const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid);
  if (pBindingObj == nullptr || pObject == nullptr)
    return plStatus("Nothing to rebind.");

  if (pObject->GetType() != plGetStaticRTTI<plGameObject>())
    return plStatus("A binding has to point at a game object.");

  plStringBuilder sName;
  plStringBuilder sSearchSequence;
  PL_SUCCEED_OR_RETURN(DeriveBindingPath(pObject, sName, sSearchSequence));

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Rebind");

  // Only the path moves. The tracks and clips stay exactly as they are - repairing a binding must
  // not cost the work that hangs off it, which is the whole point of repairing rather than redoing.
  PL_VERIFY(acc.SetValueByName(pBindingObj, "Name", sName.GetView()).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pBindingObj, "ObjectPath", sSearchSequence.GetView()).Succeeded(), "");

  acc.FinishTransaction();
  return plStatus(PL_SUCCESS);
}

plUuid plSequenceAssetDocument::AddTrack(const plUuid& bindingGuid, const char* szComponentType, const char* szPropertyPath, plPropertyAnimTarget::Enum target)
{
  const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid);
  PL_ASSERT_DEV(pBindingObj != nullptr, "Invalid binding");

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Track");

  plUuid newTrackGuid;
  PL_VERIFY(acc.AddObjectByName(pBindingObj, "Tracks", -1, plGetStaticRTTI<plSequenceTrack>(), newTrackGuid).Succeeded(), "");
  const plDocumentObject* pTrackObj = accessor.GetObject(newTrackGuid);
  PL_VERIFY(acc.SetValueByName(pTrackObj, "ComponentType", szComponentType).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pTrackObj, "Property", szPropertyPath).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pTrackObj, "Target", (plInt64)target).Succeeded(), "");
  ApplyAxisCurveColor(acc, pTrackObj, target);

  acc.FinishTransaction();
  return newTrackGuid;
}

void plSequenceAssetDocument::AddVectorTrack(
  const plUuid& bindingGuid, const char* szComponentType, const char* szPropertyPath, bool bRotation, plUInt32 uiAxisCount)
{
  const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid);
  if (pBindingObj == nullptr)
    return;

  // One transaction for the whole vector: adding Position is a single action to undo, not three.
  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Vector Track");

  const plPropertyAnimTarget::Enum firstAxis = bRotation ? plPropertyAnimTarget::RotationX : plPropertyAnimTarget::VectorX;

  for (plUInt32 a = 0; a < uiAxisCount; ++a)
  {
    const plPropertyAnimTarget::Enum target = (plPropertyAnimTarget::Enum)(firstAxis + a);

    // skip axes already keyed, so adding Position twice does not duplicate rows
    bool bExists = false;
    const plInt32 iTrackCount = pBindingObj->GetTypeAccessor().GetCount("Tracks");
    for (plInt32 t = 0; t < iTrackCount && !bExists; ++t)
    {
      const plVariant trackGuid = pBindingObj->GetTypeAccessor().GetValue("Tracks", t);
      if (!trackGuid.IsA<plUuid>())
        continue;

      const plDocumentObject* pTrackObj = GetObjectManager()->GetObject(trackGuid.Get<plUuid>());
      bExists = pTrackObj != nullptr && pTrackObj->GetTypeAccessor().GetValue("Property").ConvertTo<plString>() == szPropertyPath &&
                pTrackObj->GetTypeAccessor().GetValue("ComponentType").ConvertTo<plString>() == szComponentType &&
                pTrackObj->GetTypeAccessor().GetValue("Target").ConvertTo<plInt64>() == (plInt64)target;
    }

    if (bExists)
      continue;

    plUuid newTrackGuid;
    PL_VERIFY(acc.AddObjectByName(pBindingObj, "Tracks", -1, plGetStaticRTTI<plSequenceTrack>(), newTrackGuid).Succeeded(), "");
    const plDocumentObject* pTrackObj = accessor.GetObject(newTrackGuid);
    PL_VERIFY(acc.SetValueByName(pTrackObj, "ComponentType", szComponentType).Succeeded(), "");
    PL_VERIFY(acc.SetValueByName(pTrackObj, "Property", szPropertyPath).Succeeded(), "");
    PL_VERIFY(acc.SetValueByName(pTrackObj, "Target", (plInt64)target).Succeeded(), "");
    ApplyAxisCurveColor(acc, pTrackObj, target);
  }

  acc.FinishTransaction();
}

plUuid plSequenceAssetDocument::AddMarker(plInt64 iTick, const char* szName)
{
  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Marker");

  plUuid newGuid;
  PL_VERIFY(acc.AddObjectByName(GetPropertyObject(), "Markers", -1, plGetStaticRTTI<plSequenceMarker>(), newGuid).Succeeded(), "");
  const plDocumentObject* pMarkerObj = accessor.GetObject(newGuid);
  PL_VERIFY(acc.SetValueByName(pMarkerObj, "Tick", (plUInt64)plMath::Max<plInt64>(0, iTick)).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pMarkerObj, "Name", szName).Succeeded(), "");

  acc.FinishTransaction();
  return newGuid;
}

void plSequenceAssetDocument::GetMarkerTicks(plDynamicArray<plInt64>& out_ticks) const
{
  out_ticks.Clear();

  const plDocumentObject* pPropObj = const_cast<plSequenceAssetDocument*>(this)->GetPropertyObject();
  const plInt32 iCount = pPropObj->GetTypeAccessor().GetCount("Markers");

  for (plInt32 i = 0; i < iCount; ++i)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("Markers", i);
    if (!guid.IsA<plUuid>())
      continue;

    if (const plDocumentObject* pMarkerObj = GetObjectManager()->GetObject(guid.Get<plUuid>()))
      out_ticks.PushBack(pMarkerObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>());
  }
}

void plSequenceAssetDocument::AddClipLane(const plUuid& bindingGuid, plSequenceClipKind::Enum kind)
{
  const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid);
  if (pBindingObj == nullptr)
    return;

  const plInt32 iCount = pBindingObj->GetTypeAccessor().GetCount("ClipLanes");
  for (plInt32 i = 0; i < iCount; ++i)
  {
    if (pBindingObj->GetTypeAccessor().GetValue("ClipLanes", i).ConvertTo<plInt64>() == (plInt64)kind)
      return; // already shown
  }

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Clip Lane");
  PL_VERIFY(acc.InsertValueByName(pBindingObj, "ClipLanes", (plUInt8)kind, -1).Succeeded(), "");
  acc.FinishTransaction();
}

void plSequenceAssetDocument::RemoveClipLane(const plUuid& bindingGuid, plSequenceClipKind::Enum kind)
{
  const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid);
  if (pBindingObj == nullptr)
    return;

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Remove Clip Lane");

  // The lane's clips go with it - a hidden lane whose clips still play would be a trap.
  for (plInt32 i = pBindingObj->GetTypeAccessor().GetCount("Clips") - 1; i >= 0; --i)
  {
    const plVariant clipGuid = pBindingObj->GetTypeAccessor().GetValue("Clips", i);
    if (!clipGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pClipObj = GetObjectManager()->GetObject(clipGuid.Get<plUuid>());
    if (pClipObj != nullptr && pClipObj->GetTypeAccessor().GetValue("Kind").ConvertTo<plInt64>() == (plInt64)kind)
    {
      PL_VERIFY(acc.RemoveObject(pClipObj).Succeeded(), "");
    }
  }

  for (plInt32 i = pBindingObj->GetTypeAccessor().GetCount("ClipLanes") - 1; i >= 0; --i)
  {
    if (pBindingObj->GetTypeAccessor().GetValue("ClipLanes", i).ConvertTo<plInt64>() == (plInt64)kind)
    {
      PL_VERIFY(acc.RemoveValueByName(pBindingObj, "ClipLanes", i).Succeeded(), "");
    }
  }

  acc.FinishTransaction();
}

void plSequenceAssetDocument::InsertVectorKey(plArrayPtr<const plUuid> trackGuids, plInt64 iTickX)
{
  plCommandHistory* pHistory = GetCommandHistory();
  pHistory->StartTransaction("Add Vector Key");

  for (const plUuid& trackGuid : trackGuids)
  {
    if (FindCurveCp(trackGuid, iTickX).IsValid())
      continue;

    // The axis keeps the value it already has at that tick, so keying a vector never moves it.
    double fValue = 0.0;
    if (const plSequenceTrack* pTrack = GetTrack(trackGuid))
      fValue = pTrack->m_FloatCurve.Evaluate(iTickX);

    InsertCurveCpAt(trackGuid, iTickX, fValue);
  }

  pHistory->FinishTransaction();
}

void plSequenceAssetDocument::MoveVectorKey(plArrayPtr<const plUuid> trackGuids, plInt64 iFromTick, plInt64 iToTick)
{
  if (iFromTick == iToTick)
    return;

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Move Vector Key");

  for (const plUuid& trackGuid : trackGuids)
  {
    const plUuid cpGuid = FindCurveCp(trackGuid, iFromTick);
    if (const plDocumentObject* pCpObj = cpGuid.IsValid() ? accessor.GetObject(cpGuid) : nullptr)
    {
      PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iToTick).Succeeded(), "");
    }
  }

  acc.FinishTransaction();
}

void plSequenceAssetDocument::RemoveVectorKey(plArrayPtr<const plUuid> trackGuids, plInt64 iTickX)
{
  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Delete Vector Key");

  for (const plUuid& trackGuid : trackGuids)
  {
    const plUuid cpGuid = FindCurveCp(trackGuid, iTickX);
    if (const plDocumentObject* pCpObj = cpGuid.IsValid() ? accessor.GetObject(cpGuid) : nullptr)
    {
      PL_VERIFY(acc.RemoveObject(pCpObj).Succeeded(), "");
    }
  }

  acc.FinishTransaction();
}

// ---- auto-key support ------------------------------------------------------

plStatus plSequenceAssetDocument::CanAnimate(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target) const
{
  PL_IGNORE_UNUSED(target);

  if (pObject == nullptr)
    return plStatus("Object is null.");
  if (pProp == nullptr)
    return plStatus("Property is null.");
  if (index.IsValid())
    return plStatus("Animating container elements is not supported.");
  if (GetContextObject() == nullptr)
    return plStatus("No context scene attached to this sequence.");

  // must live below the attached context
  {
    const plDocumentObject* pNode = pObject;
    while (pNode != nullptr && pNode != GetContextObject())
    {
      pNode = pNode->GetParent();
    }
    if (pNode == nullptr)
      return plStatus("Object is not part of the attached context scene.");
  }

  return plStatus(PL_SUCCESS);
}

void plSequenceAssetDocument::GenerateTrackInfo(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index,
  plStringBuilder& out_sObjectSearchSequence, plStringBuilder& out_sComponentType, plStringBuilder& out_sPropertyPath) const
{
  plObjectPropertyPathContext context = {GetContextObject(), GetObjectAccessor(), "TempObjects"};
  plPropertyReference propertyRef = {pObject->GetGuid(), pProp, index};
  plObjectPropertyPath::CreatePath(context, propertyRef, out_sObjectSearchSequence, out_sComponentType, out_sPropertyPath).LogFailure();
}

plUuid plSequenceAssetDocument::FindTrack(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target) const
{
  plStringBuilder sObjectPath, sComponentType, sPropertyPath;
  GenerateTrackInfo(pObject, pProp, index, sObjectPath, sComponentType, sPropertyPath);

  const plDocumentObject* pPropObj = const_cast<plSequenceAssetDocument*>(this)->GetPropertyObject();
  const plInt32 iBindingCount = pPropObj->GetTypeAccessor().GetCount("Bindings");

  for (plInt32 b = 0; b < iBindingCount; ++b)
  {
    const plVariant bindingGuid = pPropObj->GetTypeAccessor().GetValue("Bindings", b);
    if (!bindingGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid.Get<plUuid>());
    if (pBindingObj == nullptr)
      continue;

    if (pBindingObj->GetTypeAccessor().GetValue("ObjectPath").ConvertTo<plString>() != sObjectPath)
      continue;

    const plInt32 iTrackCount = pBindingObj->GetTypeAccessor().GetCount("Tracks");
    for (plInt32 t = 0; t < iTrackCount; ++t)
    {
      const plVariant trackGuid = pBindingObj->GetTypeAccessor().GetValue("Tracks", t);
      if (!trackGuid.IsA<plUuid>())
        continue;

      const plDocumentObject* pTrackObj = GetObjectManager()->GetObject(trackGuid.Get<plUuid>());
      if (pTrackObj == nullptr)
        continue;

      if (pTrackObj->GetTypeAccessor().GetValue("ComponentType").ConvertTo<plString>() == sComponentType &&
          pTrackObj->GetTypeAccessor().GetValue("Property").ConvertTo<plString>() == sPropertyPath &&
          pTrackObj->GetTypeAccessor().GetValue("Target").ConvertTo<plInt64>() == (plInt64)target)
      {
        return pTrackObj->GetGuid();
      }
    }
  }

  return plUuid();
}

bool plSequenceAssetDocument::CurveColorForTarget(plPropertyAnimTarget::Enum target, plColorGammaUB& out_color)
{
  switch (target)
  {
    case plPropertyAnimTarget::VectorX:
    case plPropertyAnimTarget::RotationX:
      out_color = plColorGammaUB(224, 49, 49); // LightUI(Red)
      return true;
    case plPropertyAnimTarget::VectorY:
    case plPropertyAnimTarget::RotationY:
      out_color = plColorGammaUB(55, 178, 77); // LightUI(Green)
      return true;
    case plPropertyAnimTarget::VectorZ:
    case plPropertyAnimTarget::RotationZ:
      out_color = plColorGammaUB(77, 171, 247); // LightUI(Blue)
      return true;
    case plPropertyAnimTarget::VectorW:
      out_color = plColorGammaUB(204, 93, 232); // LightUI(Grape)
      return true;
    default:
      return false; // a lone curve needs no axis colour; the editor's default is fine
  }
}

void plSequenceAssetDocument::ApplyAxisCurveColor(
  plObjectAccessorBase& ref_accessor, const plDocumentObject* pTrackObj, plPropertyAnimTarget::Enum target)
{
  if (pTrackObj == nullptr)
    return;

  plColorGammaUB color;
  if (!CurveColorForTarget(target, color))
    return;

  const plVariant curveGuid = pTrackObj->GetTypeAccessor().GetValue("FloatCurve");
  if (!curveGuid.IsA<plUuid>())
    return;

  if (const plDocumentObject* pCurveObj = ref_accessor.GetObject(curveGuid.Get<plUuid>()))
  {
    ref_accessor.SetValueByName(pCurveObj, "Color", color).IgnoreResult();
  }
}

plUuid plSequenceAssetDocument::CreateTrack(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target)
{
  plStringBuilder sObjectPath, sComponentType, sPropertyPath;
  GenerateTrackInfo(pObject, pProp, index, sObjectPath, sComponentType, sPropertyPath);

  // find the binding that already covers this object path
  plUuid bindingGuid;
  {
    const plDocumentObject* pPropObj = GetPropertyObject();
    const plInt32 iBindingCount = pPropObj->GetTypeAccessor().GetCount("Bindings");

    for (plInt32 b = 0; b < iBindingCount; ++b)
    {
      const plVariant guid = pPropObj->GetTypeAccessor().GetValue("Bindings", b);
      if (!guid.IsA<plUuid>())
        continue;

      const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(guid.Get<plUuid>());
      if (pBindingObj != nullptr && pBindingObj->GetTypeAccessor().GetValue("ObjectPath").ConvertTo<plString>() == sObjectPath)
      {
        bindingGuid = pBindingObj->GetGuid();
        break;
      }
    }
  }

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Track");

  // ... or add it, named after the last path segment
  if (!bindingGuid.IsValid())
  {
    plStringBuilder sName = sObjectPath;
    if (const char* szLastSep = sObjectPath.FindLastSubString("/"))
      sName = szLastSep + 1;
    if (sName.IsEmpty())
      sName = "<context root>";

    PL_VERIFY(acc.AddObjectByName(GetPropertyObject(), "Bindings", -1, plGetStaticRTTI<plSequenceBinding>(), bindingGuid).Succeeded(), "");
    const plDocumentObject* pBindingObj = accessor.GetObject(bindingGuid);
    PL_VERIFY(acc.SetValueByName(pBindingObj, "Name", sName.GetView()).Succeeded(), "");
    PL_VERIFY(acc.SetValueByName(pBindingObj, "ObjectPath", sObjectPath.GetView()).Succeeded(), "");
  }

  plUuid trackGuid;
  const plDocumentObject* pBindingObj = accessor.GetObject(bindingGuid);
  PL_VERIFY(acc.AddObjectByName(pBindingObj, "Tracks", -1, plGetStaticRTTI<plSequenceTrack>(), trackGuid).Succeeded(), "");
  const plDocumentObject* pTrackObj = accessor.GetObject(trackGuid);
  PL_VERIFY(acc.SetValueByName(pTrackObj, "ComponentType", sComponentType.GetView()).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pTrackObj, "Property", sPropertyPath.GetView()).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pTrackObj, "Target", (plInt64)target).Succeeded(), "");
  ApplyAxisCurveColor(acc, pTrackObj, target);

  acc.FinishTransaction();
  return trackGuid;
}

plUuid plSequenceAssetDocument::FindCurveCp(const plUuid& trackGuid, plInt64 iTickX) const
{
  const plDocumentObject* pTrackObj = GetObjectManager()->GetObject(trackGuid);
  if (pTrackObj == nullptr)
    return plUuid();

  const plVariant curveGuid = pTrackObj->GetTypeAccessor().GetValue("FloatCurve");
  const plDocumentObject* pCurveObj = GetObjectManager()->GetObject(curveGuid.Get<plUuid>());
  if (pCurveObj == nullptr)
    return plUuid();

  const plInt32 iCpCount = pCurveObj->GetTypeAccessor().GetCount("ControlPoints");
  for (plInt32 i = 0; i < iCpCount; ++i)
  {
    const plVariant cpGuid = pCurveObj->GetTypeAccessor().GetValue("ControlPoints", i);
    if (!cpGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pCpObj = GetObjectManager()->GetObject(cpGuid.Get<plUuid>());
    if (pCpObj != nullptr && pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>() == iTickX)
      return pCpObj->GetGuid();
  }

  return plUuid();
}

void plSequenceAssetDocument::CollectContextCameras(plDynamicArray<const plDocumentObject*>& out_cameras) const
{
  out_cameras.Clear();

  const plDocumentObject* pContext = GetContextObject();
  if (pContext == nullptr)
    return;

  const plRTTI* pCameraRtti = plRTTI::FindTypeByName("plCameraComponent");
  if (pCameraRtti == nullptr)
    return;

  // depth-first over the mirrored context scene, collecting objects that carry a camera
  plHybridArray<const plDocumentObject*, 32> stack;
  stack.PushBack(pContext);

  while (!stack.IsEmpty())
  {
    const plDocumentObject* pObject = stack.PeekBack();
    stack.PopBack();

    for (const plDocumentObject* pChild : pObject->GetChildren())
    {
      if (pChild->GetType() == plGetStaticRTTI<plGameObject>())
        stack.PushBack(pChild);

      if (pChild->GetType()->IsDerivedFrom(pCameraRtti))
        out_cameras.PushBack(pObject);
    }
  }
}

const plDocumentObject* plSequenceAssetDocument::ResolveCameraCutObject(const plUuid& cutGuid) const
{
  const plDocumentObject* pCutObj = GetObjectManager()->GetObject(cutGuid);
  if (pCutObj == nullptr)
    return nullptr;

  const plString sPath = pCutObj->GetTypeAccessor().GetValue("CameraPath").ConvertTo<plString>();
  if (sPath.IsEmpty() || GetContextObject() == nullptr)
    return nullptr;

  plHybridArray<plPropertyReference, 1> keys;
  if (FindTrackKeys(sPath.GetData(), "", "Name", keys).Failed() || keys.IsEmpty())
    return nullptr;

  return GetObjectManager()->GetObject(keys[0].m_Object);
}

bool plSequenceAssetDocument::CanResolveCameraCut(const plUuid& cutGuid) const
{
  return ResolveCameraCutObject(cutGuid) != nullptr;
}

void plSequenceAssetDocument::SetPilotCamera(bool bEnable)
{
  if (m_bPilotCamera == bEnable)
    return;

  m_bPilotCamera = bEnable;

  // the window drives the viewport from this on its next redraw
  plSequenceAssetEvent e;
  e.m_pDocument = this;
  e.m_Type = plSequenceAssetEvent::Type::ScrubberPositionChanged;
  m_SequenceEvents.Broadcast(e);
}

bool plSequenceAssetDocument::GetCutCameraAtTick(plInt64 iAtTick, plTransform& out_transform, float& out_fFov) const
{
  plStringBuilder sWhyNot;
  PL_SCOPE_EXIT(if (!sWhyNot.IsEmpty() && sWhyNot != m_sLastPilotFailure) {
    m_sLastPilotFailure = sWhyNot;
    plLog::Info("Sequencer pilot: {0}", sWhyNot);
  });

  const plDocumentObject* pPropObj = const_cast<plSequenceAssetDocument*>(this)->GetPropertyObject();
  const plInt32 iCutCount = pPropObj->GetTypeAccessor().GetCount("CameraCuts");
  if (iCutCount == 0)
  {
    sWhyNot = "the sequence has no camera cuts.";
    return false;
  }

  // The last cut that has started is the active one; the one before it is what a blend comes from.
  const plDocumentObject* pActiveCut = nullptr;
  const plDocumentObject* pPreviousCut = nullptr;
  plInt64 iActiveStart = -1;
  plInt64 iPreviousStart = -1;

  for (plInt32 c = 0; c < iCutCount; ++c)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("CameraCuts", c);
    if (!guid.IsA<plUuid>())
      continue;

    const plDocumentObject* pCutObj = GetObjectManager()->GetObject(guid.Get<plUuid>());
    if (pCutObj == nullptr)
      continue;

    // A cut with no usable camera must not shadow a working one: two cuts can sit on the same
    // tick, and picking the broken one would leave pilot with nothing to do even though a valid
    // cut starts at that exact time.
    if (pCutObj->GetTypeAccessor().GetValue("CameraPath").ConvertTo<plString>().IsEmpty())
      continue;

    const plInt64 iStart = pCutObj->GetTypeAccessor().GetValue("StartTick").ConvertTo<plInt64>();
    if (iStart <= iAtTick && iStart >= iActiveStart)
    {
      // >= so that among cuts sharing a tick the most recently added one wins
      iPreviousStart = iActiveStart;
      pPreviousCut = pActiveCut;
      iActiveStart = iStart;
      pActiveCut = pCutObj;
    }
    else if (iStart < iActiveStart && iStart > iPreviousStart)
    {
      // cuts arrive in document order, not time order, so the runner-up has to be tracked too
      iPreviousStart = iStart;
      pPreviousCut = pCutObj;
    }
  }

  if (pActiveCut == nullptr)
  {
    sWhyNot.SetFormat("no cut has started at tick {0} - the first cut starts later.", iAtTick);
    return false;
  }

  if (GetContextObject() == nullptr)
  {
    sWhyNot = "no context scene is attached.";
    return false;
  }

  if (!ResolveCutCamera(pActiveCut, out_transform, out_fFov, sWhyNot))
    return false;

  // Overlapping the previous shot's tail is what authors a blend, so the pilot has to honour it or
  // the viewport hard-cuts while the timeline says otherwise.
  if (pPreviousCut != nullptr)
  {
    const plInt64 iPrevEnd = iPreviousStart + plMath::Max<plInt64>(1, pPreviousCut->GetTypeAccessor().GetValue("DurationTicks").ConvertTo<plInt64>());
    const plInt64 iOverlap = iPrevEnd - iActiveStart;

    if (iOverlap > 0 && iAtTick < iPrevEnd)
    {
      plTransform prevTransform;
      float fPrevFov = 60.0f;
      plStringBuilder sIgnored;

      if (ResolveCutCamera(pPreviousCut, prevTransform, fPrevFov, sIgnored))
      {
        const float fLerp = (float)((double)(iAtTick - iActiveStart) / (double)iOverlap);
        // smoothstep, matching the runtime - a linear camera move starts and stops visibly
        const float fEased = plMath::Clamp(fLerp * fLerp * (3.0f - 2.0f * fLerp), 0.0f, 1.0f);

        plTransform blended;
        blended.m_vPosition = plMath::Lerp(prevTransform.m_vPosition, out_transform.m_vPosition, fEased);
        blended.m_qRotation = plQuat::MakeSlerp(prevTransform.m_qRotation, out_transform.m_qRotation, fEased);
        blended.m_vScale = out_transform.m_vScale;

        out_transform = blended;
        out_fFov = plMath::Lerp(fPrevFov, out_fFov, fEased);
      }
    }
  }

  return true;
}

plUuid plSequenceAssetDocument::FindCameraCutAtTick(plInt64 iTick) const
{
  const plDocumentObject* pPropObj = const_cast<plSequenceAssetDocument*>(this)->GetPropertyObject();
  const plInt32 iCutCount = pPropObj->GetTypeAccessor().GetCount("CameraCuts");

  plUuid result;
  plInt64 iBestStart = -1;

  for (plInt32 c = 0; c < iCutCount; ++c)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("CameraCuts", c);
    if (!guid.IsA<plUuid>())
      continue;

    const plDocumentObject* pCutObj = GetObjectManager()->GetObject(guid.Get<plUuid>());
    if (pCutObj == nullptr)
      continue;

    if (pCutObj->GetTypeAccessor().GetValue("CameraPath").ConvertTo<plString>().IsEmpty())
      continue;

    const plInt64 iStart = pCutObj->GetTypeAccessor().GetValue("StartTick").ConvertTo<plInt64>();
    if (iStart <= iTick && iStart >= iBestStart)
    {
      iBestStart = iStart;
      result = pCutObj->GetGuid();
    }
  }

  return result;
}

bool plSequenceAssetDocument::ResolveCutCamera(const plDocumentObject* pCutObj, plTransform& out_transform, float& out_fFov, plStringBuilder& out_sWhyNot) const
{
  // resolve the camera object the same way the tracks resolve their targets
  const plString sPath = pCutObj->GetTypeAccessor().GetValue("CameraPath").ConvertTo<plString>();
  if (sPath.IsEmpty())
  {
    out_sWhyNot = "the active cut has no camera path (it was added before unnamed objects were rejected).";
    return false;
  }

  plHybridArray<plPropertyReference, 1> keys;
  const plStatus resolve = const_cast<plSequenceAssetDocument*>(this)->FindTrackKeys(sPath.GetData(), "", "Name", keys);
  if (resolve.Failed() || keys.IsEmpty())
  {
    out_sWhyNot.SetFormat("camera path '{0}' does not resolve: {1}", sPath, resolve.Failed() ? resolve.m_sMessage : plString("no match"));
    return false;
  }

  const plDocumentObject* pCameraObject = GetObjectManager()->GetObject(keys[0].m_Object);
  if (pCameraObject == nullptr)
  {
    out_sWhyNot = "the resolved camera object no longer exists.";
    return false;
  }

  // Compose the world transform by walking up to the context root. The mirrored context stores
  // local transforms only, and the sequence may be animating any of them, so this has to be read
  // live rather than cached.
  plTransform world = plTransform::MakeIdentity();
  {
    plHybridArray<const plDocumentObject*, 8> chain;
    for (const plDocumentObject* pNode = pCameraObject; pNode != nullptr && pNode != GetContextObject(); pNode = pNode->GetParent())
    {
      if (pNode->GetType() == plGetStaticRTTI<plGameObject>())
        chain.PushBack(pNode);
    }

    for (plUInt32 i = chain.GetCount(); i-- > 0;)
    {
      plTransform local = plTransform::MakeIdentity();
      local.m_vPosition = chain[i]->GetTypeAccessor().GetValue("LocalPosition").ConvertTo<plVec3>();
      local.m_qRotation = chain[i]->GetTypeAccessor().GetValue("LocalRotation").ConvertTo<plQuat>();

      const plVariant scaling = chain[i]->GetTypeAccessor().GetValue("LocalScaling");
      if (scaling.IsValid())
        local.m_vScale = scaling.ConvertTo<plVec3>();

      world = world * local;
    }
  }

  out_transform = world;
  out_fFov = 60.0f;

  // the camera component's own field of view, if it has one
  for (const plDocumentObject* pChild : pCameraObject->GetChildren())
  {
    if (pChild->GetType()->GetTypeName() == "plCameraComponent")
    {
      const plVariant fov = pChild->GetTypeAccessor().GetValue("FOV");
      if (fov.IsValid())
        out_fFov = fov.ConvertTo<float>();
      break;
    }
  }

  return true;
}

plUuid plSequenceAssetDocument::AddCameraCut(const plDocumentObject* pCameraObject, plUInt64 uiStartTick)
{
  PL_ASSERT_DEV(pCameraObject != nullptr, "Invalid camera object");

  // The cut references the camera by the same named-object path the tracks use, so the runtime
  // resolves it exactly the way it resolves bindings.
  plStringBuilder sName;
  {
    const plVariant name = pCameraObject->GetTypeAccessor().GetValue("Name");
    if (name.IsA<plString>())
      sName = name.Get<plString>();
  }

  plStringBuilder sObjectPath, sComponentType, sPropertyPath;
  {
    const plAbstractProperty* pNameProp = plGetStaticRTTI<plGameObject>()->FindPropertyByName("Name");
    GenerateTrackInfo(pCameraObject, pNameProp, plVariant(), sObjectPath, sComponentType, sPropertyPath);
  }

  // Resolution is by name at runtime, so an unnamed object can never be found again. Refuse rather
  // than storing a cut with an empty path that silently does nothing.
  if (sObjectPath.IsEmpty())
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation(
      plFmt("This camera object has no name, so the sequence cannot find it again.\n\n"
            "Give the object a name in the scene, save the scene, then add the cut."));
    return plUuid();
  }

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Camera Cut");

  plUuid cutGuid;
  PL_VERIFY(acc.AddObjectByName(GetPropertyObject(), "CameraCuts", -1, plGetStaticRTTI<plSequenceCameraCut>(), cutGuid).Succeeded(), "");
  const plDocumentObject* pCutObj = accessor.GetObject(cutGuid);
  PL_VERIFY(acc.SetValueByName(pCutObj, "CameraName", sName.GetView()).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCutObj, "CameraPath", sObjectPath.GetView()).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCutObj, "StartTick", uiStartTick).Succeeded(), "");

  acc.FinishTransaction();
  return cutGuid;
}

plUuid plSequenceAssetDocument::AddTrackForContextProperty(const plUuid& objectGuid, const char* szPropertyName, plPropertyAnimTarget::Enum target)
{
  const plDocumentObject* pObject = GetObjectManager()->GetObject(objectGuid);
  if (pObject == nullptr)
    return plUuid();

  const plAbstractProperty* pProp = pObject->GetType()->FindPropertyByName(szPropertyName);
  if (pProp == nullptr)
    return plUuid();

  if (CanAnimate(pObject, pProp, plVariant(), target).Failed())
    return plUuid();

  // CreateTrack generates the object path itself and finds or creates the matching binding, so a
  // property on an unbound child produces its own binding rather than being attached to the parent.
  return CreateTrack(pObject, pProp, plVariant(), target);
}

plUuid plSequenceAssetDocument::InsertCurveCpAt(const plUuid& trackGuid, plInt64 iTickX, double fNewPosY)
{
  // Not every row owns a curve - clip lanes carry their binding's guid - so refuse rather than
  // reading a FloatCurve that is not there.
  const plDocumentObject* pTrackObj = GetObjectManager()->GetObject(trackGuid);
  if (pTrackObj == nullptr)
    return plUuid();

  const plVariant curveGuid = pTrackObj->GetTypeAccessor().GetValue("FloatCurve");
  if (!curveGuid.IsA<plUuid>())
    return plUuid();

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Insert Key");

  plUuid newCpGuid;
  PL_VERIFY(acc.AddObjectByName(accessor.GetObject(curveGuid.Get<plUuid>()), "ControlPoints", -1, plGetStaticRTTI<plCurveControlPointData>(), newCpGuid).Succeeded(), "");
  const plDocumentObject* pCpObj = accessor.GetObject(newCpGuid);
  PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iTickX).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "Value", fNewPosY).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "LeftTangent", plVec2(-0.1f, 0.0f)).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "RightTangent", plVec2(+0.1f, 0.0f)).Succeeded(), "");

  acc.FinishTransaction();
  return newCpGuid;
}

plUuid plSequenceAssetDocument::AddClip(const plUuid& bindingGuid, plSequenceClipKind::Enum kind, plUInt64 uiStartTick, const char* szPayload)
{
  const plDocumentObject* pBindingObj = GetObjectManager()->GetObject(bindingGuid);
  if (pBindingObj == nullptr)
    return plUuid();

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Clip");

  // The concrete type carries the payload property, named and attributed for this kind.
  const plRTTI* pClipType = plSequenceClipTypeForKind(kind);
  const char* szPayloadProperty = (kind == plSequenceClipKind::AnimationClip) ? "Clip"
                                  : (kind == plSequenceClipKind::SubSequence) ? "Sequence"
                                                                             : "PlayTrigger";

  plUuid clipGuid;
  PL_VERIFY(acc.AddObjectByName(pBindingObj, "Clips", -1, pClipType, clipGuid).Succeeded(), "");
  const plDocumentObject* pClipObj = accessor.GetObject(clipGuid);
  PL_VERIFY(acc.SetValueByName(pClipObj, "Kind", (plInt64)kind).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pClipObj, "StartTick", uiStartTick).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pClipObj, szPayloadProperty, szPayload).Succeeded(), "");

  acc.FinishTransaction();
  return clipGuid;
}

// ---- event track -----------------------------------------------------------

const plDocumentObject* plSequenceAssetDocument::GetEventTrackObject() const
{
  const plDocumentObject* pPropObj = const_cast<plSequenceAssetDocument*>(this)->GetPropertyObject();
  const plVariant guid = pPropObj->GetTypeAccessor().GetValue("EventTrack");
  if (!guid.IsA<plUuid>())
    return nullptr;

  return GetObjectManager()->GetObject(guid.Get<plUuid>());
}

plUuid plSequenceAssetDocument::InsertEventTrackCpAt(plInt64 iTickX, const char* szValue)
{
  const plDocumentObject* pTrackObj = GetEventTrackObject();
  if (pTrackObj == nullptr)
    return plUuid();

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Insert Event");

  plUuid newCpGuid;
  PL_VERIFY(acc.AddObjectByName(pTrackObj, "ControlPoints", -1, plGetStaticRTTI<plEventTrackControlPointData>(), newCpGuid).Succeeded(), "");
  const plDocumentObject* pCpObj = accessor.GetObject(newCpGuid);
  PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iTickX).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "Event", szValue).Succeeded(), "");

  acc.FinishTransaction();
  return newCpGuid;
}

// ---- colour gradient control points ----------------------------------------

namespace
{
  /// The gradient object of a colour track, which owns the three control point arrays.
  const plDocumentObject* GetGradientObject(const plDocumentObjectManager* pManager, const plDocumentObject* pTrackObj)
  {
    if (pTrackObj == nullptr)
      return nullptr;

    const plVariant guid = pTrackObj->GetTypeAccessor().GetValue("Gradient");
    if (!guid.IsA<plUuid>())
      return nullptr;

    return pManager->GetObject(guid.Get<plUuid>());
  }

  plUuid FindCpOnTick(const plDocumentObjectManager* pManager, const plDocumentObject* pGradientObj, const char* szArray, plInt64 iTickX)
  {
    if (pGradientObj == nullptr)
      return plUuid();

    const plInt32 iCount = pGradientObj->GetTypeAccessor().GetCount(szArray);
    for (plInt32 i = 0; i < iCount; ++i)
    {
      const plVariant cpGuid = pGradientObj->GetTypeAccessor().GetValue(szArray, i);
      if (!cpGuid.IsA<plUuid>())
        continue;

      const plDocumentObject* pCpObj = pManager->GetObject(cpGuid.Get<plUuid>());
      if (pCpObj != nullptr && pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>() == iTickX)
        return pCpObj->GetGuid();
    }

    return plUuid();
  }
} // namespace

plUuid plSequenceAssetDocument::FindGradientColorCp(const plUuid& trackGuid, plInt64 iTickX) const
{
  return FindCpOnTick(GetObjectManager(), GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid)), "ColorCPs", iTickX);
}

plUuid plSequenceAssetDocument::FindGradientAlphaCp(const plUuid& trackGuid, plInt64 iTickX) const
{
  return FindCpOnTick(GetObjectManager(), GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid)), "AlphaCPs", iTickX);
}

plUuid plSequenceAssetDocument::FindGradientIntensityCp(const plUuid& trackGuid, plInt64 iTickX) const
{
  return FindCpOnTick(GetObjectManager(), GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid)), "IntensityCPs", iTickX);
}

plUuid plSequenceAssetDocument::InsertGradientColorCpAt(const plUuid& trackGuid, plInt64 iTickX, const plColorGammaUB& color)
{
  const plDocumentObject* pGradientObj = GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid));
  if (pGradientObj == nullptr)
    return plUuid();

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Colour Key");

  plUuid newCpGuid;
  PL_VERIFY(acc.AddObjectByName(pGradientObj, "ColorCPs", -1, plGetStaticRTTI<plColorControlPoint>(), newCpGuid).Succeeded(), "");
  const plDocumentObject* pCpObj = accessor.GetObject(newCpGuid);
  PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iTickX).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "Red", color.r).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "Green", color.g).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "Blue", color.b).Succeeded(), "");

  acc.FinishTransaction();
  return newCpGuid;
}

plUuid plSequenceAssetDocument::InsertGradientAlphaCpAt(const plUuid& trackGuid, plInt64 iTickX, plUInt8 uiAlpha)
{
  const plDocumentObject* pGradientObj = GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid));
  if (pGradientObj == nullptr)
    return plUuid();

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Alpha Key");

  plUuid newCpGuid;
  PL_VERIFY(acc.AddObjectByName(pGradientObj, "AlphaCPs", -1, plGetStaticRTTI<plAlphaControlPoint>(), newCpGuid).Succeeded(), "");
  const plDocumentObject* pCpObj = accessor.GetObject(newCpGuid);
  PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iTickX).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "Alpha", uiAlpha).Succeeded(), "");

  acc.FinishTransaction();
  return newCpGuid;
}

plUuid plSequenceAssetDocument::InsertGradientIntensityCpAt(const plUuid& trackGuid, plInt64 iTickX, float fIntensity)
{
  const plDocumentObject* pGradientObj = GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid));
  if (pGradientObj == nullptr)
    return plUuid();

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Add Intensity Key");

  plUuid newCpGuid;
  PL_VERIFY(acc.AddObjectByName(pGradientObj, "IntensityCPs", -1, plGetStaticRTTI<plIntensityControlPoint>(), newCpGuid).Succeeded(), "");
  const plDocumentObject* pCpObj = accessor.GetObject(newCpGuid);
  PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iTickX).Succeeded(), "");
  PL_VERIFY(acc.SetValueByName(pCpObj, "Intensity", fIntensity).Succeeded(), "");

  acc.FinishTransaction();
  return newCpGuid;
}

void plSequenceAssetDocument::GetColorKeys(const plUuid& trackGuid, plDynamicArray<ColorKey>& out_keys) const
{
  out_keys.Clear();

  const plSequenceTrack* pTrack = GetTrack(trackGuid);
  const plDocumentObject* pGradientObj = GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid));
  if (pTrack == nullptr || pGradientObj == nullptr)
    return;

  const plInt32 iCount = pGradientObj->GetTypeAccessor().GetCount("ColorCPs");
  out_keys.Reserve((plUInt32)plMath::Max(0, iCount));

  for (plInt32 i = 0; i < iCount; ++i)
  {
    const plVariant cpGuid = pGradientObj->GetTypeAccessor().GetValue("ColorCPs", i);
    if (!cpGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pCpObj = GetObjectManager()->GetObject(cpGuid.Get<plUuid>());
    if (pCpObj == nullptr)
      continue;

    ColorKey& key = out_keys.ExpandAndGetRef();
    key.m_iTick = pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();
    key.m_ColorCp = pCpObj->GetGuid();
    key.m_AlphaCp = FindGradientAlphaCp(trackGuid, key.m_iTick);
    key.m_IntensityCp = FindGradientIntensityCp(trackGuid, key.m_iTick);
    // taken from the gradient rather than the stop, so it matches exactly what the track evaluates
    key.m_Value = pTrack->m_ColorGradient.Evaluate(key.m_iTick);
  }

  out_keys.Sort([](const ColorKey& lhs, const ColorKey& rhs) -> bool { return lhs.m_iTick < rhs.m_iTick; });
}

void plSequenceAssetDocument::SetColorKey(const plUuid& trackGuid, plInt64 iTickX, const plColor& value)
{
  const plDocumentObject* pGradientObj = GetGradientObject(GetObjectManager(), GetObjectManager()->GetObject(trackGuid));
  if (pGradientObj == nullptr)
    return;

  // The gradient stores an 8-bit colour and a separate multiplier, so an HDR pick has to be split
  // back apart: the brightest channel becomes the intensity, and the colour is normalised by it.
  const float fIntensity = plMath::Max(1.0f, plMath::Max(value.r, value.g, value.b));
  plColor normalized = value;
  normalized.r /= fIntensity;
  normalized.g /= fIntensity;
  normalized.b /= fIntensity;

  const plColorGammaUB gamma = normalized;
  const plUInt8 uiAlpha = (plUInt8)plMath::Clamp((int)(value.a * 255.0f + 0.5f), 0, 255);

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Set Colour Key");

  auto setOrAdd = [&](const char* szArray, const plUuid& existing, const plRTTI* pType, plDelegate<void(const plDocumentObject*)> apply)
  {
    const plDocumentObject* pCpObj = existing.IsValid() ? accessor.GetObject(existing) : nullptr;

    if (pCpObj == nullptr)
    {
      plUuid newGuid;
      PL_VERIFY(acc.AddObjectByName(pGradientObj, szArray, -1, pType, newGuid).Succeeded(), "");
      pCpObj = accessor.GetObject(newGuid);
      PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iTickX).Succeeded(), "");
    }

    apply(pCpObj);
  };

  setOrAdd("ColorCPs", FindGradientColorCp(trackGuid, iTickX), plGetStaticRTTI<plColorControlPoint>(),
    [&](const plDocumentObject* pCpObj)
    {
      PL_VERIFY(acc.SetValueByName(pCpObj, "Red", gamma.r).Succeeded(), "");
      PL_VERIFY(acc.SetValueByName(pCpObj, "Green", gamma.g).Succeeded(), "");
      PL_VERIFY(acc.SetValueByName(pCpObj, "Blue", gamma.b).Succeeded(), "");
    });

  setOrAdd("AlphaCPs", FindGradientAlphaCp(trackGuid, iTickX), plGetStaticRTTI<plAlphaControlPoint>(),
    [&](const plDocumentObject* pCpObj) { PL_VERIFY(acc.SetValueByName(pCpObj, "Alpha", uiAlpha).Succeeded(), ""); });

  setOrAdd("IntensityCPs", FindGradientIntensityCp(trackGuid, iTickX), plGetStaticRTTI<plIntensityControlPoint>(),
    [&](const plDocumentObject* pCpObj) { PL_VERIFY(acc.SetValueByName(pCpObj, "Intensity", fIntensity).Succeeded(), ""); });

  acc.FinishTransaction();
}

void plSequenceAssetDocument::MoveColorKey(const plUuid& trackGuid, plInt64 iFromTick, plInt64 iToTick)
{
  if (iFromTick == iToTick)
    return;

  const plUuid colorCp = FindGradientColorCp(trackGuid, iFromTick);
  if (!colorCp.IsValid())
    return;

  const plUuid alphaCp = FindGradientAlphaCp(trackGuid, iFromTick);
  const plUuid intensityCp = FindGradientIntensityCp(trackGuid, iFromTick);

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Move Colour Key");

  // all three channels move together, or the key would tear into separate stops
  for (const plUuid& cp : {colorCp, alphaCp, intensityCp})
  {
    if (const plDocumentObject* pCpObj = cp.IsValid() ? accessor.GetObject(cp) : nullptr)
    {
      PL_VERIFY(acc.SetValueByName(pCpObj, "Tick", iToTick).Succeeded(), "");
    }
  }

  acc.FinishTransaction();
}

void plSequenceAssetDocument::RemoveColorKey(const plUuid& trackGuid, plInt64 iTickX)
{
  const plUuid colorCp = FindGradientColorCp(trackGuid, iTickX);
  if (!colorCp.IsValid())
    return;

  const plUuid alphaCp = FindGradientAlphaCp(trackGuid, iTickX);
  const plUuid intensityCp = FindGradientIntensityCp(trackGuid, iTickX);

  plObjectCommandAccessor accessor(GetCommandHistory());
  plObjectAccessorBase& acc = accessor;
  acc.StartTransaction("Remove Colour Key");

  for (const plUuid& cp : {colorCp, alphaCp, intensityCp})
  {
    if (const plDocumentObject* pCpObj = cp.IsValid() ? accessor.GetObject(cp) : nullptr)
    {
      PL_VERIFY(acc.RemoveObject(pCpObj).Succeeded(), "");
    }
  }

  acc.FinishTransaction();
}

// ---- transform -------------------------------------------------------------

plTransformStatus plSequenceAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag,
  const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  PL_IGNORE_UNUSED(sOutputTag);
  PL_IGNORE_UNUSED(pAssetProfile);
  PL_IGNORE_UNUSED(AssetHeader);
  PL_IGNORE_UNUSED(transformFlags);

  const plSequenceData* pData = GetProperties();

  plSequenceResourceDescriptor descriptor;
  descriptor.m_uiFramesPerSecond = pData->m_uiFramesPerSecond;
  descriptor.m_uiDurationTicks = pData->m_uiDurationTicks;

  for (const plSequenceBinding* pBinding : pData->m_Bindings)
  {
    plSequenceBindingDesc& bindingDesc = descriptor.m_Bindings.ExpandAndGetRef();
    bindingDesc.m_sName = pBinding->m_sName;
    bindingDesc.m_sObjectSearchSequence = pBinding->m_sObjectSearchSequence;

    for (const plSequenceTrack* pTrack : pBinding->m_Tracks)
    {
      plSequenceTrackDesc& trackDesc = bindingDesc.m_Tracks.ExpandAndGetRef();
      trackDesc.m_sComponentType = pTrack->m_sComponentType;
      trackDesc.m_sPropertyPath = pTrack->m_sPropertyPath;
      trackDesc.m_Target = pTrack->m_Target;

      if (pTrack->m_Target == plPropertyAnimTarget::Color)
      {
        pTrack->m_ColorGradient.FillGradientData(trackDesc.m_ColorGradient);
        trackDesc.m_ColorGradient.SortControlPoints();
      }
      else
      {
        pTrack->m_FloatCurve.ConvertToRuntimeData(trackDesc.m_FloatCurve);
        trackDesc.m_FloatCurve.SortControlPoints();
        trackDesc.m_FloatCurve.ApplyTangentModes();
        trackDesc.m_FloatCurve.ClampTangents();
      }
    }

    for (const plSequenceClip* pClip : pBinding->m_Clips)
    {
      plSequenceClipDesc& clipDesc = bindingDesc.m_Clips.ExpandAndGetRef();
      clipDesc.m_Kind = pClip->m_Kind;
      clipDesc.m_sPayload = pClip->m_sPayload;
      clipDesc.m_sStopPayload = pClip->m_sStopPayload;
      clipDesc.m_uiStartTick = pClip->m_uiStartTick;
      clipDesc.m_uiDurationTicks = pClip->m_uiDurationTicks;
      clipDesc.m_fSpeed = pClip->m_fSpeed;
      clipDesc.m_uiBlendInTicks = pClip->m_uiBlendInTicks;
      clipDesc.m_uiBlendOutTicks = pClip->m_uiBlendOutTicks;
    }

    // the runtime walks these in order to decide what should be playing
    bindingDesc.m_Clips.Sort([](const plSequenceClipDesc& lhs, const plSequenceClipDesc& rhs) -> bool
      { return lhs.m_uiStartTick < rhs.m_uiStartTick; });
  }

  {
    // Sorted first, because the blend of each cut is how far the previous one reaches past its
    // start - which only means anything in time order.
    plHybridArray<const plSequenceCameraCut*, 8> sortedCuts;
    for (const plSequenceCameraCut* pCut : pData->m_CameraCuts)
      sortedCuts.PushBack(pCut);

    sortedCuts.Sort([](const plSequenceCameraCut* lhs, const plSequenceCameraCut* rhs) -> bool
      { return lhs->m_uiStartTick < rhs->m_uiStartTick; });

    for (plUInt32 c = 0; c < sortedCuts.GetCount(); ++c)
    {
      plSequenceCameraCutDesc& cutDesc = descriptor.m_CameraCuts.ExpandAndGetRef();
      cutDesc.m_sCameraObjectPath = sortedCuts[c]->m_sCameraObjectPath;
      cutDesc.m_uiStartTick = sortedCuts[c]->m_uiStartTick;

      // Overlap is the blend: the previous shot running past this one's start is exactly the window
      // the two cameras are cross-faded over.
      cutDesc.m_uiBlendInTicks = 0;
      if (c > 0)
      {
        const plUInt64 uiPrevEnd = sortedCuts[c - 1]->m_uiStartTick + sortedCuts[c - 1]->m_uiDurationTicks;
        if (uiPrevEnd > cutDesc.m_uiStartTick)
          cutDesc.m_uiBlendInTicks = uiPrevEnd - cutDesc.m_uiStartTick;
      }
    }
  }

  for (const plSequenceClip* pClip : pData->m_Clips)
  {
    plSequenceClipDesc& clipDesc = descriptor.m_Clips.ExpandAndGetRef();
    clipDesc.m_Kind = pClip->m_Kind;
    clipDesc.m_sPayload = pClip->m_sPayload;
    clipDesc.m_sStopPayload = pClip->m_sStopPayload;
    clipDesc.m_uiStartTick = pClip->m_uiStartTick;
    clipDesc.m_uiDurationTicks = pClip->m_uiDurationTicks;
    clipDesc.m_fSpeed = pClip->m_fSpeed;
    clipDesc.m_uiBlendInTicks = pClip->m_uiBlendInTicks;
    clipDesc.m_uiBlendOutTicks = pClip->m_uiBlendOutTicks;
  }

  descriptor.m_Clips.Sort([](const plSequenceClipDesc& lhs, const plSequenceClipDesc& rhs) -> bool
    { return lhs.m_uiStartTick < rhs.m_uiStartTick; });

  // the runtime scans these in order to find the active cut
  descriptor.m_CameraCuts.Sort([](const plSequenceCameraCutDesc& lhs, const plSequenceCameraCutDesc& rhs) -> bool
    { return lhs.m_uiStartTick < rhs.m_uiStartTick; });

  pData->m_EventTrack.ConvertToRuntimeData(descriptor.m_EventTrack);

  descriptor.Save(stream);
  return plTransformStatus(PL_SUCCESS);
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceAsset);
