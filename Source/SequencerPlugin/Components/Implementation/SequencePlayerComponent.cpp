#include <SequencerPlugin/SequencerPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <Foundation/Reflection/ReflectionUtils.h>
#include <AudioSystem/Components/AudioTriggerComponent.h>
#include <GameEngine/Animation/Skeletal/SimpleAnimationComponent.h>
#include <RendererCore/Components/CameraComponent.h>
#include <SequencerPlugin/Components/SequencePlayerComponent.h>

// clang-format off
PL_IMPLEMENT_MESSAGE_TYPE(plMsgSequenceFinished);
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plMsgSequenceFinished, 1, plRTTIDefaultAllocator<plMsgSequenceFinished>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_COMPONENT_TYPE(plSequencePlayerComponent, 2, plComponentMode::Static)
{
  PL_BEGIN_PROPERTIES
  {
    PL_ACCESSOR_PROPERTY("Sequence", GetSequenceFile, SetSequenceFile)->AddAttributes(new plAssetBrowserAttribute("CompatibleAsset_Sequence")),
    PL_MEMBER_PROPERTY("PlayOnStart", m_bPlayOnStart),
    PL_MEMBER_PROPERTY("Loop", m_bLoop),
    PL_MEMBER_PROPERTY("Speed", m_fSpeed)->AddAttributes(new plDefaultValueAttribute(1.0f)),
  }
  PL_END_PROPERTIES;
  PL_BEGIN_ATTRIBUTES
  {
    new plCategoryAttribute("Animation"),
  }
  PL_END_ATTRIBUTES;
  PL_BEGIN_MESSAGESENDERS
  {
    PL_MESSAGE_SENDER(m_SequenceFinishedMsgSender),
    PL_MESSAGE_SENDER(m_EventTrackMsgSender),
  }
  PL_END_MESSAGESENDERS;
  PL_BEGIN_FUNCTIONS
  {
    PL_SCRIPT_FUNCTION_PROPERTY(Play),
    PL_SCRIPT_FUNCTION_PROPERTY(Pause),
    PL_SCRIPT_FUNCTION_PROPERTY(Stop),
  }
  PL_END_FUNCTIONS;
}
PL_END_COMPONENT_TYPE;
// clang-format on

plSequencePlayerComponent::plSequencePlayerComponent() = default;
plSequencePlayerComponent::~plSequencePlayerComponent() = default;

void plSequencePlayerComponent::SerializeComponent(plWorldWriter& inout_stream) const
{
  SUPER::SerializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s << m_hSequence;
  s << m_bPlayOnStart;
  s << m_bLoop;
  s << m_fSpeed;
}

void plSequencePlayerComponent::DeserializeComponent(plWorldReader& inout_stream)
{
  SUPER::DeserializeComponent(inout_stream);
  auto& s = inout_stream.GetStream();

  s >> m_hSequence;
  s >> m_bPlayOnStart;
  s >> m_bLoop;
  s >> m_fSpeed;
}

void plSequencePlayerComponent::OnSimulationStarted()
{
  SUPER::OnSimulationStarted();

  if (m_bPlayOnStart)
  {
    Play();
  }
}

void plSequencePlayerComponent::SetSequenceFile(const char* szFile)
{
  plSequenceResourceHandle hResource;

  if (!plStringUtils::IsNullOrEmpty(szFile))
  {
    hResource = plResourceManager::LoadResource<plSequenceResource>(szFile);
  }

  SetSequence(hResource);
}

const char* plSequencePlayerComponent::GetSequenceFile() const
{
  if (!m_hSequence.IsValid())
    return "";

  return m_hSequence.GetResourceID();
}

void plSequencePlayerComponent::SetSequence(const plSequenceResourceHandle& hResource)
{
  if (m_hSequence == hResource)
    return;

  m_hSequence = hResource;

  // next Play resolves against the new sequence
  m_bBindingsCreated = false;
  m_pPlayingDesc.Clear();
}

void plSequencePlayerComponent::Play()
{
  if (!m_hSequence.IsValid())
    return;

  if (!m_bBindingsCreated)
  {
    CreateBindings();
  }

  m_bPlaying = true;
}

void plSequencePlayerComponent::Pause()
{
  m_bPlaying = false;
}

void plSequencePlayerComponent::Stop()
{
  m_bPlaying = false;
  m_PlaybackTime = plTime::MakeZero();

  // stopping mid-cutscene must not leave a sound or animation running
  StopAllRunningClips();
  RestoreCameras();
}

void plSequencePlayerComponent::SetPlaybackTime(plTime time)
{
  m_PlaybackTime = plMath::Max(time, plTime::MakeZero());
}

// ---- binding resolve -------------------------------------------------------

void plSequencePlayerComponent::CreateBindings()
{
  m_GoFloatBindings.Clear();
  m_ComponentFloatBindings.Clear();
  m_ColorBindings.Clear();
  m_pPlayingDesc.Clear();
  m_bBindingsCreated = true;

  if (!m_hSequence.IsValid())
    return;

  plResourceLock<plSequenceResource> pSequence(m_hSequence, plResourceAcquireMode::BlockTillLoaded);
  if (!pSequence || pSequence.GetAcquireResult() == plResourceAcquireResult::MissingFallback)
    return;

  // Copy the descriptor: bindings store pointers into it, and a resource reload mid-playback must
  // not invalidate them (same rationale as PropertyAnim's shared descriptor).
  m_pPlayingDesc = PL_DEFAULT_NEW(plSequenceResourceDescriptor);
  *m_pPlayingDesc = pSequence->GetDescriptor();

  plUInt32 uiResolved = 0;

  for (const plSequenceBindingDesc& bindingDesc : m_pPlayingDesc->m_Bindings)
  {
    for (const plSequenceTrackDesc& trackDesc : bindingDesc.m_Tracks)
    {
      const plRTTI* pComponentRtti = nullptr;
      if (!trackDesc.m_sComponentType.IsEmpty())
      {
        pComponentRtti = plRTTI::FindTypeByName(trackDesc.m_sComponentType);
        if (pComponentRtti == nullptr)
        {
          plLog::Warning("Sequence track: component type '{0}' is unknown.", trackDesc.m_sComponentType);
          continue;
        }
      }

      plHybridArray<plGameObject*, 8> targets;
      ResolveBindingTargets(bindingDesc.m_sObjectSearchSequence, pComponentRtti, targets);

      for (plGameObject* pTargetObject : targets)
      {
        if (pComponentRtti == nullptr)
        {
          CreateGameObjectBinding(&trackDesc, plGetStaticRTTI<plGameObject>(), pTargetObject, pTargetObject->GetHandle());
          ++uiResolved;
        }
        else
        {
          plComponent* pComp;
          if (pTargetObject->TryGetComponentOfBaseType(pComponentRtti, pComp))
          {
            if (trackDesc.m_Target == plPropertyAnimTarget::Color)
              CreateColorBinding(&trackDesc, pComp->GetDynamicRTTI(), pComp, pComp->GetHandle());
            else
              CreateComponentFloatBinding(&trackDesc, pComp->GetDynamicRTTI(), pComp, pComp->GetHandle());
            ++uiResolved;
          }
        }
      }

      if (targets.IsEmpty())
      {
        plLog::Warning("Sequence binding '{0}': object path '{1}' resolved to nothing.", bindingDesc.m_sName, bindingDesc.m_sObjectSearchSequence);
      }
    }
  }

  // clips resolve to their binding's object; the component that acts on them is looked up when the
  // clip actually fires, so an object gaining a trigger component later still works
  m_Clips.Clear();
  m_RunningClips.Clear();

  for (const plSequenceBindingDesc& bindingDesc : m_pPlayingDesc->m_Bindings)
  {
    if (bindingDesc.m_Clips.IsEmpty())
      continue;

    plHybridArray<plGameObject*, 8> targets;
    ResolveBindingTargets(bindingDesc.m_sObjectSearchSequence, nullptr, targets);

    if (targets.IsEmpty())
      continue;

    for (const plSequenceClipDesc& clipDesc : bindingDesc.m_Clips)
    {
      ClipBinding& binding = m_Clips.ExpandAndGetRef();
      binding.m_pClip = &clipDesc;
      binding.m_hObject = targets[0]->GetHandle();
    }
  }

  // sequence-level clips are not attached to anything in the scene, so they play on the player's
  // own object - which is what lets a cutscene carry its own music and VO
  for (const plSequenceClipDesc& clipDesc : m_pPlayingDesc->m_Clips)
  {
    ClipBinding& binding = m_Clips.ExpandAndGetRef();
    binding.m_pClip = &clipDesc;
    binding.m_hObject = GetOwner()->GetHandle();
  }

  ResolveCameraCuts();

  plLog::Debug("Sequence player: {0} track bindings, {1} camera cuts resolved.", uiResolved, m_CameraCuts.GetCount());
}

void plSequencePlayerComponent::ResolveCameraCuts()
{
  m_CameraCuts.Clear();
  m_iActiveCameraCut = -1;

  if (m_pPlayingDesc == nullptr)
    return;

  for (const plSequenceCameraCutDesc& cutDesc : m_pPlayingDesc->m_CameraCuts)
  {
    plHybridArray<plGameObject*, 8> targets;
    ResolveBindingTargets(cutDesc.m_sCameraObjectPath, plGetStaticRTTI<plCameraComponent>(), targets);

    bool bFound = false;
    for (plGameObject* pTarget : targets)
    {
      plCameraComponent* pCamera = nullptr;
      if (pTarget->TryGetComponentOfBaseType(pCamera))
      {
        CameraCutBinding& binding = m_CameraCuts.ExpandAndGetRef();
        binding.m_hCamera = pCamera->GetHandle();
        binding.m_uiStartTick = cutDesc.m_uiStartTick;
        binding.m_uiBlendInTicks = cutDesc.m_uiBlendInTicks;
        bFound = true;
        break;
      }
    }

    if (!bFound)
    {
      plLog::Warning("Sequence camera cut: '{0}' did not resolve to an object with a camera component.", cutDesc.m_sCameraObjectPath);
    }
  }

  m_CameraCuts.Sort([](const CameraCutBinding& lhs, const CameraCutBinding& rhs) -> bool { return lhs.m_uiStartTick < rhs.m_uiStartTick; });
}

void plSequencePlayerComponent::UpdateClips(plTime lookupTime)
{
  if (m_Clips.IsEmpty())
    return;

  const plUInt64 uiTick = (plUInt64)(lookupTime.GetSeconds() * (double)plSequenceTicksPerSecond);

  for (plUInt32 i = 0; i < m_Clips.GetCount(); ++i)
  {
    const plSequenceClipDesc& clip = *m_Clips[i].m_pClip;

    const bool bShouldRun = uiTick >= clip.m_uiStartTick && uiTick < clip.GetEndTick();
    const bool bIsRunning = m_RunningClips.Contains(i);

    if (bShouldRun == bIsRunning)
      continue;

    if (bShouldRun)
    {
      StartClip(clip, m_Clips[i].m_hObject);
      m_RunningClips.PushBack(i);
    }
    else
    {
      StopClip(clip, m_Clips[i].m_hObject);
      m_RunningClips.RemoveAndCopy(i);
    }
  }
}

void plSequencePlayerComponent::StartClip(const plSequenceClipDesc& clip, const plGameObjectHandle& hObject)
{
  plGameObject* pObject = nullptr;
  if (!GetWorld()->TryGetObject(hObject, pObject))
    return;

  switch (clip.m_Kind)
  {
    case plSequenceClipKind::AudioTrigger:
    {
      plAudioTriggerComponent* pTrigger = nullptr;
      if (!pObject->TryGetComponentOfBaseType(pTrigger))
      {
        // A cutscene should be able to play music, VO or a stinger without the level having been
        // prepared for it, so the trigger component is created on demand rather than required to
        // exist. Its audio proxy is created for it by plAudioSystemProxyDependentComponent.
        GetWorld()->GetOrCreateComponentManager<plAudioTriggerComponentManager>()->CreateComponent(pObject, pTrigger);

        if (pTrigger == nullptr)
        {
          plLog::Warning("Sequence clip: could not create an audio trigger to play '{0}'.", clip.m_sPayload);
          return;
        }
      }

      // The clip names the trigger, so one component can serve every audio clip on this object
      // rather than needing one component per sound.
      if (!clip.m_sPayload.IsEmpty())
        pTrigger->SetPlayTrigger(clip.m_sPayload);

      if (!clip.m_sStopPayload.IsEmpty())
        pTrigger->SetStopTrigger(clip.m_sStopPayload);

      pTrigger->Play();
      break;
    }

    case plSequenceClipKind::AnimationClip:
    {
      if (clip.m_sPayload.IsEmpty())
        break;

      plSimpleAnimationComponent* pAnim = nullptr;
      if (!pObject->TryGetComponentOfBaseType(pAnim))
      {
        // Created on demand for the same reason the audio trigger is: a cutscene should not
        // require the level to have been prepared for it.
        GetWorld()->GetOrCreateComponentManager<plSimpleAnimationComponentManager>()->CreateComponent(pObject, pAnim);

        if (pAnim == nullptr)
        {
          plLog::Warning("Sequence clip: could not create an animation player for '{0}'.", clip.m_sPayload);
          break;
        }
      }

      pAnim->SetAnimationClipFile(clip.m_sPayload);
      pAnim->m_fSpeed = clip.m_fSpeed;
      // The clip's own length on the timeline is what decides when it stops, so the animation runs
      // once from the start rather than looping under it.
      pAnim->m_AnimationMode = plPropertyAnimMode::Once;
      pAnim->SetNormalizedPlaybackPosition(0.0f);
      pAnim->SetActiveFlag(true);
      break;
    }

    case plSequenceClipKind::SubSequence:
    {
      if (clip.m_sPayload.IsEmpty())
        break;

      plSequencePlayerComponent* pPlayer = nullptr;
      if (!pObject->TryGetComponentOfBaseType(pPlayer))
      {
        GetWorld()->GetOrCreateComponentManager<plSequencePlayerComponentManager>()->CreateComponent(pObject, pPlayer);

        if (pPlayer == nullptr)
        {
          plLog::Warning("Sequence clip: could not create a player for sub-sequence '{0}'.", clip.m_sPayload);
          break;
        }
      }

      // Guard against a sequence that contains itself, which would recurse until the world runs out
      // of components.
      if (pPlayer == this)
      {
        plLog::Error("Sequence clip: '{0}' would play itself as a sub-sequence.", clip.m_sPayload);
        break;
      }

      pPlayer->SetSequenceFile(clip.m_sPayload);
      pPlayer->m_bLoop = false;
      pPlayer->SetActiveFlag(true);
      pPlayer->Play();
      break;
    }
  }
}

void plSequencePlayerComponent::StopClip(const plSequenceClipDesc& clip, const plGameObjectHandle& hObject)
{
  plGameObject* pObject = nullptr;
  if (!GetWorld()->TryGetObject(hObject, pObject))
    return;

  switch (clip.m_Kind)
  {
    case plSequenceClipKind::AudioTrigger:
    {
      plAudioTriggerComponent* pTrigger = nullptr;
      if (pObject->TryGetComponentOfBaseType(pTrigger))
      {
        pTrigger->Stop();
      }
      break;
    }

    case plSequenceClipKind::AnimationClip:
    {
      // Deactivated rather than left at its last pose: the clip's length on the timeline is what
      // owns the animation's lifetime.
      plSimpleAnimationComponent* pAnim = nullptr;
      if (pObject->TryGetComponentOfBaseType(pAnim))
      {
        pAnim->SetActiveFlag(false);
      }
      break;
    }

    case plSequenceClipKind::SubSequence:
    {
      plSequencePlayerComponent* pPlayer = nullptr;
      if (pObject->TryGetComponentOfBaseType(pPlayer) && pPlayer != this)
      {
        pPlayer->Stop();
      }
      break;
    }
  }
}

void plSequencePlayerComponent::StopAllRunningClips()
{
  for (plUInt32 uiIndex : m_RunningClips)
  {
    if (uiIndex < m_Clips.GetCount())
      StopClip(*m_Clips[uiIndex].m_pClip, m_Clips[uiIndex].m_hObject);
  }

  m_RunningClips.Clear();
}

void plSequencePlayerComponent::ApplyCameraCut(plTime lookupTime)
{
  if (m_CameraCuts.IsEmpty())
    return;

  const plUInt64 uiTick = (plUInt64)(lookupTime.GetSeconds() * (double)plSequenceTicksPerSecond);

  // the last cut that has started is the active one
  plInt32 iActive = -1;
  for (plUInt32 i = 0; i < m_CameraCuts.GetCount(); ++i)
  {
    if (m_CameraCuts[i].m_uiStartTick <= uiTick)
      iActive = (plInt32)i;
    else
      break;
  }

  if (iActive == m_iActiveCameraCut)
  {
    // The cut has not changed, but a blend in progress advances every frame.
    UpdateCameraBlend(uiTick);
    return;
  }

  const plInt32 iPrevious = m_iActiveCameraCut;

  // demote the outgoing camera before promoting the incoming one, or a frame could see two
  if (m_iActiveCameraCut >= 0 && m_iActiveCameraCut < (plInt32)m_CameraCuts.GetCount())
  {
    plCameraComponent* pPrev = nullptr;
    if (GetWorld()->TryGetComponent(m_CameraCuts[m_iActiveCameraCut].m_hCamera, pPrev))
    {
      plInt32* pOriginal = m_RestoreCameraHints.GetValue(m_CameraCuts[m_iActiveCameraCut].m_hCamera);
      if (pOriginal != nullptr)
        pPrev->SetUsageHint(static_cast<plCameraUsageHint::Enum>(*pOriginal));
    }
  }

  m_iActiveCameraCut = iActive;

  if (iActive < 0)
    return;

  plCameraComponent* pCamera = nullptr;
  if (!GetWorld()->TryGetComponent(m_CameraCuts[iActive].m_hCamera, pCamera))
    return;

  if (!m_RestoreCameraHints.Contains(m_CameraCuts[iActive].m_hCamera))
    m_RestoreCameraHints.Insert(m_CameraCuts[iActive].m_hCamera, (plInt32)pCamera->GetUsageHint());

  pCamera->SetUsageHint(plCameraUsageHint::MainView);

  // A blend needs the pose the shot is coming FROM, which only exists at the moment of the switch -
  // the outgoing camera may itself be animated, so it has to be sampled now, not looked up later.
  EndCameraBlend();

  if (m_CameraCuts[iActive].m_uiBlendInTicks > 0 && iPrevious >= 0)
  {
    plCameraComponent* pPrevCamera = nullptr;
    plGameObject* pIncoming = pCamera->GetOwner();

    if (GetWorld()->TryGetComponent(m_CameraCuts[iPrevious].m_hCamera, pPrevCamera) && pIncoming != nullptr)
    {
      m_hBlendingCamera = pIncoming->GetHandle();
      m_BlendCameraRestore = pIncoming->GetGlobalTransform();
      m_BlendFromTransform = pPrevCamera->GetOwner()->GetGlobalTransform();
      m_bBlendActive = true;
    }
  }
}

void plSequencePlayerComponent::UpdateCameraBlend(plUInt64 uiTick)
{
  if (!m_bBlendActive || m_iActiveCameraCut < 0)
    return;

  const CameraCutBinding& cut = m_CameraCuts[m_iActiveCameraCut];

  plGameObject* pCameraObject = nullptr;
  if (!GetWorld()->TryGetObject(m_hBlendingCamera, pCameraObject))
  {
    m_bBlendActive = false;
    return;
  }

  const plUInt64 uiElapsed = uiTick > cut.m_uiStartTick ? uiTick - cut.m_uiStartTick : 0;

  if (uiElapsed >= cut.m_uiBlendInTicks)
  {
    EndCameraBlend();
    return;
  }

  const float fLerp = (float)((double)uiElapsed / (double)cut.m_uiBlendInTicks);
  // smoothstep: a linear camera move starts and stops with a visible jerk
  const float fEased = fLerp * fLerp * (3.0f - 2.0f * fLerp);

  plTransform blended;
  blended.m_vPosition = plMath::Lerp(m_BlendFromTransform.m_vPosition, m_BlendCameraRestore.m_vPosition, fEased);
  blended.m_qRotation = plQuat::MakeSlerp(m_BlendFromTransform.m_qRotation, m_BlendCameraRestore.m_qRotation, fEased);
  blended.m_vScale = m_BlendCameraRestore.m_vScale;

  pCameraObject->SetGlobalTransform(blended);
}

void plSequencePlayerComponent::EndCameraBlend()
{
  if (!m_bBlendActive)
    return;

  m_bBlendActive = false;

  // Put the camera back where it was authored: the blend drove the object, and leaving it at the
  // last blended pose would silently move a level camera.
  plGameObject* pCameraObject = nullptr;
  if (GetWorld()->TryGetObject(m_hBlendingCamera, pCameraObject))
  {
    pCameraObject->SetGlobalTransform(m_BlendCameraRestore);
  }

  m_hBlendingCamera.Invalidate();
}

void plSequencePlayerComponent::RestoreCameras()
{
  // before the hints, so a camera stopped mid-blend goes back to its authored pose
  EndCameraBlend();

  for (auto it = m_RestoreCameraHints.GetIterator(); it.IsValid(); ++it)
  {
    plCameraComponent* pCamera = nullptr;
    if (GetWorld()->TryGetComponent(it.Key(), pCamera))
    {
      pCamera->SetUsageHint(static_cast<plCameraUsageHint::Enum>(it.Value()));
    }
  }

  m_RestoreCameraHints.Clear();
  m_iActiveCameraCut = -1;
}

void plSequencePlayerComponent::ResolveBindingTargets(plStringView sObjectPath, const plRTTI* pComponentRtti, plHybridArray<plGameObject*, 8>& out_targets)
{
  out_targets.Clear();

  // explicit root wins
  plGameObject* pRoot = nullptr;
  if (!m_hBindingRoot.IsInvalidated() && GetWorld()->TryGetObject(m_hBindingRoot, pRoot))
  {
    pRoot->SearchForChildrenByNameSequence(sObjectPath, pComponentRtti, out_targets);
    return;
  }

  // then below the owner
  GetOwner()->SearchForChildrenByNameSequence(sObjectPath, pComponentRtti, out_targets);
  if (!out_targets.IsEmpty())
    return;

  // else the world's top-level objects: paths were authored against the scene root, so the first
  // segment names a top-level object and the rest resolves below it
  plStringView sFirst = sObjectPath;
  plStringView sRest;
  if (const char* szSep = sObjectPath.FindSubString("/"))
  {
    sFirst = plStringView(sObjectPath.GetStartPointer(), szSep);
    sRest = plStringView(szSep + 1, sObjectPath.GetEndPointer());
  }

  for (auto it = GetWorld()->GetObjects(); it.IsValid(); ++it)
  {
    plGameObject* pObject = &(*it);
    if (pObject->GetParent() != nullptr)
      continue;
    if (pObject->GetName() != sFirst)
      continue;

    if (sRest.IsEmpty())
    {
      if (pComponentRtti != nullptr)
      {
        plComponent* pComp;
        if (!pObject->TryGetComponentOfBaseType(pComponentRtti, pComp))
          continue;
      }
      out_targets.PushBack(pObject);
    }
    else
    {
      pObject->SearchForChildrenByNameSequence(sRest, pComponentRtti, out_targets);
    }
  }
}

void plSequencePlayerComponent::CreateGameObjectBinding(const plSequenceTrackDesc* pTrack, const plRTTI* pRtti, void* pObject, const plGameObjectHandle& hGameObject)
{
  if (pTrack->m_Target < plPropertyAnimTarget::Number || pTrack->m_Target > plPropertyAnimTarget::RotationZ)
    return;

  // Every rejection below drops a track the author can see in the timeline, so each one says why.
  // Silently binding nothing is how "the camera does not move" turns into a hunt with no evidence.
  const plAbstractProperty* pAbstract = pRtti->FindPropertyByName(pTrack->m_sPropertyPath);
  if (pAbstract == nullptr || pAbstract->GetCategory() != plPropertyCategory::Member)
  {
    plLog::Warning("Sequence player: '{0}' has no member property '{1}', so that track does nothing.", pRtti->GetTypeName(),
      pTrack->m_sPropertyPath);
    return;
  }

  auto pMember = static_cast<const plAbstractMemberProperty*>(pAbstract);
  const plRTTI* pPropRtti = pMember->GetSpecificType();

  if (pTrack->m_Target == plPropertyAnimTarget::Number)
  {
    if (pPropRtti != plGetStaticRTTI<float>() && pPropRtti != plGetStaticRTTI<bool>())
    {
      plLog::Warning("Sequence player: '{0}' is a {1}, which a number track cannot drive.", pTrack->m_sPropertyPath,
        pPropRtti->GetTypeName());
      return;
    }
  }
  else if (pTrack->m_Target >= plPropertyAnimTarget::RotationX && pTrack->m_Target <= plPropertyAnimTarget::RotationZ)
  {
    if (pPropRtti != plGetStaticRTTI<plQuat>())
    {
      plLog::Warning("Sequence player: rotation track targets '{0}', which is a {1} rather than a quaternion.",
        pTrack->m_sPropertyPath, pPropRtti->GetTypeName());
      return;
    }
  }
  else
  {
    if (pPropRtti != plGetStaticRTTI<plVec2>() && pPropRtti != plGetStaticRTTI<plVec3>() && pPropRtti != plGetStaticRTTI<plVec4>())
    {
      plLog::Warning("Sequence player: vector track targets '{0}', which is a {1} rather than a vector.", pTrack->m_sPropertyPath,
        pPropRtti->GetTypeName());
      return;
    }
  }

  GameObjectBinding* pBinding = nullptr;
  for (auto& b : m_GoFloatBindings)
  {
    if (b.m_hObject == hGameObject && b.m_pMemberProperty == pMember && b.m_pObject == pObject)
    {
      pBinding = &b;
      break;
    }
  }

  if (pBinding == nullptr)
  {
    pBinding = &m_GoFloatBindings.ExpandAndGetRef();
  }

  pBinding->m_hObject = hGameObject;
  pBinding->m_pObject = pObject;
  pBinding->m_pMemberProperty = pMember;

  if (pTrack->m_Target >= plPropertyAnimTarget::VectorX && pTrack->m_Target <= plPropertyAnimTarget::VectorW)
    pBinding->m_pTracks[(int)pTrack->m_Target - (int)plPropertyAnimTarget::VectorX] = pTrack;
  else if (pTrack->m_Target >= plPropertyAnimTarget::RotationX && pTrack->m_Target <= plPropertyAnimTarget::RotationZ)
    pBinding->m_pTracks[(int)pTrack->m_Target - (int)plPropertyAnimTarget::RotationX] = pTrack;
  else
    pBinding->m_pTracks[0] = pTrack;
}

void plSequencePlayerComponent::CreateComponentFloatBinding(const plSequenceTrackDesc* pTrack, const plRTTI* pRtti, void* pObject, const plComponentHandle& hComponent)
{
  if (pTrack->m_Target < plPropertyAnimTarget::Number || pTrack->m_Target > plPropertyAnimTarget::VectorW)
    return;

  const plAbstractProperty* pAbstract = pRtti->FindPropertyByName(pTrack->m_sPropertyPath);
  if (pAbstract == nullptr || pAbstract->GetCategory() != plPropertyCategory::Member)
    return;

  auto pMember = static_cast<const plAbstractMemberProperty*>(pAbstract);
  const plRTTI* pPropRtti = pMember->GetSpecificType();

  if (pTrack->m_Target == plPropertyAnimTarget::Number)
  {
    if (pPropRtti != plGetStaticRTTI<float>() && pPropRtti != plGetStaticRTTI<double>() && pPropRtti != plGetStaticRTTI<bool>() &&
        pPropRtti != plGetStaticRTTI<plInt64>() && pPropRtti != plGetStaticRTTI<plInt32>() && pPropRtti != plGetStaticRTTI<plInt16>() &&
        pPropRtti != plGetStaticRTTI<plInt8>() && pPropRtti != plGetStaticRTTI<plUInt64>() && pPropRtti != plGetStaticRTTI<plUInt32>() &&
        pPropRtti != plGetStaticRTTI<plUInt16>() && pPropRtti != plGetStaticRTTI<plUInt8>() && pPropRtti != plGetStaticRTTI<plAngle>() &&
        pPropRtti != plGetStaticRTTI<plTime>())
      return;
  }
  else
  {
    if (pPropRtti != plGetStaticRTTI<plVec2>() && pPropRtti != plGetStaticRTTI<plVec3>() && pPropRtti != plGetStaticRTTI<plVec4>())
      return;
  }

  ComponentFloatBinding* pBinding = nullptr;
  for (auto& b : m_ComponentFloatBindings)
  {
    if (b.m_hComponent == hComponent && b.m_pMemberProperty == pMember && b.m_pObject == pObject)
    {
      pBinding = &b;
      break;
    }
  }

  if (pBinding == nullptr)
  {
    pBinding = &m_ComponentFloatBindings.ExpandAndGetRef();
  }

  pBinding->m_hComponent = hComponent;
  pBinding->m_pObject = pObject;
  pBinding->m_pMemberProperty = pMember;

  if (pTrack->m_Target >= plPropertyAnimTarget::VectorX && pTrack->m_Target <= plPropertyAnimTarget::VectorW)
    pBinding->m_pTracks[(int)pTrack->m_Target - (int)plPropertyAnimTarget::VectorX] = pTrack;
  else
    pBinding->m_pTracks[0] = pTrack;
}

void plSequencePlayerComponent::CreateColorBinding(const plSequenceTrackDesc* pTrack, const plRTTI* pRtti, void* pObject, const plComponentHandle& hComponent)
{
  const plAbstractProperty* pAbstract = pRtti->FindPropertyByName(pTrack->m_sPropertyPath);
  if (pAbstract == nullptr || pAbstract->GetCategory() != plPropertyCategory::Member)
    return;

  auto pMember = static_cast<const plAbstractMemberProperty*>(pAbstract);
  const plRTTI* pPropRtti = pMember->GetSpecificType();

  if (pPropRtti != plGetStaticRTTI<plColor>() && pPropRtti != plGetStaticRTTI<plColorGammaUB>())
    return;

  ColorBinding& binding = m_ColorBindings.ExpandAndGetRef();
  binding.m_hComponent = hComponent;
  binding.m_pObject = pObject;
  binding.m_pMemberProperty = pMember;
  binding.m_pTrack = pTrack;
}

// ---- evaluation ------------------------------------------------------------

void plSequencePlayerComponent::Update()
{
  if (!m_bPlaying || m_pPlayingDesc == nullptr)
    return;

  const plTime tStart = m_PlaybackTime;
  const plTime duration = m_pPlayingDesc->GetDuration();

  m_PlaybackTime += GetWorld()->GetClock().GetTimeDiff() * (double)m_fSpeed;

  bool bFinished = false;
  if (m_PlaybackTime >= duration)
  {
    if (m_bLoop && duration.IsPositive())
    {
      EvaluateEventTrack(tStart, duration);

      while (m_PlaybackTime >= duration)
      {
        m_PlaybackTime -= duration;
      }

      EvaluateEventTrack(plTime::MakeZero(), m_PlaybackTime);
    }
    else
    {
      m_PlaybackTime = duration;
      bFinished = true;
      EvaluateEventTrack(tStart, m_PlaybackTime);
    }
  }
  else
  {
    EvaluateEventTrack(tStart, m_PlaybackTime);
  }

  ApplyAnimations(m_PlaybackTime);
  ApplyCameraCut(m_PlaybackTime);
  UpdateClips(m_PlaybackTime);

  if (bFinished)
  {
    m_bPlaying = false;

    // anything still sounding would outlive the cutscene otherwise
    StopAllRunningClips();

    // hand the view back to whatever owned it before the cutscene
    RestoreCameras();

    plMsgSequenceFinished msg;
    m_SequenceFinishedMsgSender.SendEventMessage(msg, this, GetOwner());
  }
}

void plSequencePlayerComponent::ApplyAnimations(plTime lookupTime)
{
  for (plUInt32 i = 0; i < m_ComponentFloatBindings.GetCount();)
  {
    auto& binding = m_ComponentFloatBindings[i];

    plComponent* pComp;
    if (!GetWorld()->TryGetComponent(binding.m_hComponent, pComp))
    {
      m_ComponentFloatBindings.RemoveAtAndSwap(i);
      continue;
    }

    binding.m_pObject = pComp;
    ApplyFloatAnimation(binding, lookupTime);
    ++i;
  }

  for (plUInt32 i = 0; i < m_ColorBindings.GetCount();)
  {
    auto& binding = m_ColorBindings[i];

    plComponent* pComp;
    if (!GetWorld()->TryGetComponent(binding.m_hComponent, pComp))
    {
      m_ColorBindings.RemoveAtAndSwap(i);
      continue;
    }

    binding.m_pObject = pComp;
    ApplyColorAnimation(binding, lookupTime);
    ++i;
  }

  for (plUInt32 i = 0; i < m_GoFloatBindings.GetCount();)
  {
    auto& binding = m_GoFloatBindings[i];

    plGameObject* pObj;
    if (!GetWorld()->TryGetObject(binding.m_hObject, pObj))
    {
      m_GoFloatBindings.RemoveAtAndSwap(i);
      continue;
    }

    binding.m_pObject = pObj;
    ApplyFloatAnimation(binding, lookupTime);
    ++i;
  }
}

void plSequencePlayerComponent::ApplySingleFloatAnimation(const FloatBinding& binding, plTime lookupTime)
{
  const plRTTI* pRtti = binding.m_pMemberProperty->GetSpecificType();

  double fFinalValue = 0;
  {
    const plCurve1D& curve = binding.m_pTracks[0]->m_FloatCurve;

    if (curve.IsEmpty())
      return;

    fFinalValue = curve.Evaluate(lookupTime.GetSeconds());
  }

  if (pRtti == plGetStaticRTTI<bool>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<bool>*>(binding.m_pMemberProperty);
    pTyped->SetValue(binding.m_pObject, fFinalValue > 0.99);
    return;
  }
  else if (pRtti == plGetStaticRTTI<plAngle>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plAngle>*>(binding.m_pMemberProperty);
    pTyped->SetValue(binding.m_pObject, plAngle::MakeFromDegree((float)fFinalValue));
    return;
  }
  else if (pRtti == plGetStaticRTTI<plTime>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plTime>*>(binding.m_pMemberProperty);
    pTyped->SetValue(binding.m_pObject, plTime::MakeFromSeconds(fFinalValue));
    return;
  }

  // this handles float, double, all int types, etc.
  plVariant value = fFinalValue;
  if (pRtti->GetVariantType() != plVariantType::Invalid && value.CanConvertTo(pRtti->GetVariantType()))
  {
    plReflectionUtils::SetMemberPropertyValue(binding.m_pMemberProperty, binding.m_pObject, value);
  }
}

void plSequencePlayerComponent::ApplyFloatAnimation(const FloatBinding& binding, plTime lookupTime)
{
  if (binding.m_pTracks[0] != nullptr && binding.m_pTracks[0]->m_Target == plPropertyAnimTarget::Number)
  {
    ApplySingleFloatAnimation(binding, lookupTime);
    return;
  }

  const plRTTI* pRtti = binding.m_pMemberProperty->GetSpecificType();

  float fCurValue[4] = {0, 0, 0, 0};

  if (pRtti == plGetStaticRTTI<plVec2>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plVec2>*>(binding.m_pMemberProperty);
    const plVec2 value = pTyped->GetValue(binding.m_pObject);
    fCurValue[0] = value.x;
    fCurValue[1] = value.y;
  }
  else if (pRtti == plGetStaticRTTI<plVec3>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plVec3>*>(binding.m_pMemberProperty);
    const plVec3 value = pTyped->GetValue(binding.m_pObject);
    fCurValue[0] = value.x;
    fCurValue[1] = value.y;
    fCurValue[2] = value.z;
  }
  else if (pRtti == plGetStaticRTTI<plVec4>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plVec4>*>(binding.m_pMemberProperty);
    const plVec4 value = pTyped->GetValue(binding.m_pObject);
    fCurValue[0] = value.x;
    fCurValue[1] = value.y;
    fCurValue[2] = value.z;
    fCurValue[3] = value.w;
  }
  else if (pRtti == plGetStaticRTTI<plQuat>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plQuat>*>(binding.m_pMemberProperty);
    const plQuat value = pTyped->GetValue(binding.m_pObject);

    plAngle euler[3];
    value.GetAsEulerAngles(euler[0], euler[1], euler[2]);
    fCurValue[0] = euler[0].GetDegree();
    fCurValue[1] = euler[1].GetDegree();
    fCurValue[2] = euler[2].GetDegree();
  }

  for (plUInt32 i = 0; i < 4; ++i)
  {
    if (binding.m_pTracks[i] != nullptr)
    {
      const plCurve1D& curve = binding.m_pTracks[i]->m_FloatCurve;

      if (!curve.IsEmpty())
      {
        fCurValue[i] = (float)curve.Evaluate(lookupTime.GetSeconds());
      }
    }
  }

  if (pRtti == plGetStaticRTTI<plVec2>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plVec2>*>(binding.m_pMemberProperty);
    pTyped->SetValue(binding.m_pObject, plVec2(fCurValue[0], fCurValue[1]));
  }
  else if (pRtti == plGetStaticRTTI<plVec3>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plVec3>*>(binding.m_pMemberProperty);
    pTyped->SetValue(binding.m_pObject, plVec3(fCurValue[0], fCurValue[1], fCurValue[2]));
  }
  else if (pRtti == plGetStaticRTTI<plVec4>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plVec4>*>(binding.m_pMemberProperty);
    pTyped->SetValue(binding.m_pObject, plVec4(fCurValue[0], fCurValue[1], fCurValue[2], fCurValue[3]));
  }
  else if (pRtti == plGetStaticRTTI<plQuat>())
  {
    auto pTyped = static_cast<const plTypedMemberProperty<plQuat>*>(binding.m_pMemberProperty);

    plQuat rot = plQuat::MakeFromEulerAngles(
      plAngle::MakeFromDegree(fCurValue[0]), plAngle::MakeFromDegree(fCurValue[1]), plAngle::MakeFromDegree(fCurValue[2]));
    pTyped->SetValue(binding.m_pObject, rot);
  }
}

void plSequencePlayerComponent::ApplyColorAnimation(const ColorBinding& binding, plTime lookupTime)
{
  const plRTTI* pRtti = binding.m_pMemberProperty->GetSpecificType();

  if (pRtti == plGetStaticRTTI<plColorGammaUB>())
  {
    plColorGammaUB gamma;
    float fIntensity;
    binding.m_pTrack->m_ColorGradient.Evaluate(lookupTime.AsFloatInSeconds(), gamma, fIntensity);
    binding.m_pMemberProperty->SetValuePtr(binding.m_pObject, &gamma);
    return;
  }

  if (pRtti == plGetStaticRTTI<plColor>())
  {
    plColorGammaUB gamma;
    float fIntensity;
    binding.m_pTrack->m_ColorGradient.Evaluate(lookupTime.AsFloatInSeconds(), gamma, fIntensity);

    plColor finalColor = gamma;
    finalColor.ScaleRGB(fIntensity);
    binding.m_pMemberProperty->SetValuePtr(binding.m_pObject, &finalColor);
    return;
  }
}

void plSequencePlayerComponent::EvaluateEventTrack(plTime startTime, plTime endTime)
{
  const plEventTrack& track = m_pPlayingDesc->m_EventTrack;

  if (track.IsEmpty())
    return;

  plHybridArray<plHashedString, 8> events;
  track.Sample(startTime, endTime, events);

  for (const plHashedString& sEvent : events)
  {
    plMsgGenericEvent msg;
    msg.m_sMessage = sEvent;
    m_EventTrackMsgSender.SendEventMessage(msg, this, GetOwner());
  }
}

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Components_Implementation_SequencePlayerComponent);
