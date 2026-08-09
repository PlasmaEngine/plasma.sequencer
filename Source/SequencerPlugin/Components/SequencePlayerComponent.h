#pragma once

#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <SequencerPlugin/Resources/SequenceResource.h>
#include <SequencerPlugin/SequencerPluginDLL.h>

using plSequencePlayerComponentManager = plComponentManagerSimple<class plSequencePlayerComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Plays a Sequence asset at runtime.
///
/// P0 skeleton: owns the resource handle and the play state machine, but does not yet evaluate
/// tracks - binding resolve and track application land with the P2 milestone
/// (Documentation/Sequencer/Plan.html §7).
class PL_SEQUENCER_DLL plSequencePlayerComponent : public plComponent
{
  PL_DECLARE_COMPONENT_TYPE(plSequencePlayerComponent, plComponent, plSequencePlayerComponentManager);

  //////////////////////////////////////////////////////////////////////////
  // plComponent

public:
  virtual void SerializeComponent(plWorldWriter& inout_stream) const override;
  virtual void DeserializeComponent(plWorldReader& inout_stream) override;

protected:
  virtual void OnSimulationStarted() override;

  //////////////////////////////////////////////////////////////////////////
  // plSequencePlayerComponent

public:
  plSequencePlayerComponent();
  ~plSequencePlayerComponent();

  void SetSequenceFile(const char* szFile); // [ property ]
  const char* GetSequenceFile() const;      // [ property ]

  void SetSequence(const plSequenceResourceHandle& hResource);
  PL_ALWAYS_INLINE const plSequenceResourceHandle& GetSequence() const { return m_hSequence; }

  bool m_bPlayOnStart = false; // [ property ]
  bool m_bLoop = false;        // [ property ]
  float m_fSpeed = 1.0f;       // [ property ]

  void Play();
  void Pause();
  void Stop();

  bool IsPlaying() const { return m_bPlaying; }
  plTime GetPlaybackTime() const { return m_PlaybackTime; }
  void SetPlaybackTime(plTime time);

protected:
  void Update();

  plSequenceResourceHandle m_hSequence;
  plTime m_PlaybackTime;
  bool m_bPlaying = false;
};
