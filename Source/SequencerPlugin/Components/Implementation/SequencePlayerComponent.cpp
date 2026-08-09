#include <SequencerPlugin/SequencerPluginPCH.h>

#include <Core/WorldSerializer/WorldReader.h>
#include <Core/WorldSerializer/WorldWriter.h>
#include <SequencerPlugin/Components/SequencePlayerComponent.h>

// clang-format off
PL_BEGIN_COMPONENT_TYPE(plSequencePlayerComponent, 1, plComponentMode::Static)
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
  // const plUInt32 uiVersion = inout_stream.GetComponentTypeVersion(GetStaticRTTI());
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
  m_hSequence = hResource;
}

void plSequencePlayerComponent::Play()
{
  if (!m_hSequence.IsValid())
    return;

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
}

void plSequencePlayerComponent::SetPlaybackTime(plTime time)
{
  m_PlaybackTime = plMath::Max(time, plTime::MakeZero());
}

void plSequencePlayerComponent::Update()
{
  if (!m_bPlaying || !m_hSequence.IsValid())
    return;

  plResourceLock<plSequenceResource> pSequence(m_hSequence, plResourceAcquireMode::AllowLoadingFallback);
  if (pSequence.GetAcquireResult() != plResourceAcquireResult::Final)
    return;

  const plTime duration = pSequence->GetDescriptor().GetDuration();

  m_PlaybackTime += GetWorld()->GetClock().GetTimeDiff() * (double)m_fSpeed;

  // TODO P2: resolve bindings and evaluate tracks at m_PlaybackTime.

  if (m_PlaybackTime >= duration)
  {
    if (m_bLoop && duration.IsPositive())
    {
      while (m_PlaybackTime >= duration)
      {
        m_PlaybackTime -= duration;
      }
    }
    else
    {
      Stop();
      // TODO P2: fire plMsgSequenceFinished.
    }
  }
}

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Components_Implementation_SequencePlayerComponent);
