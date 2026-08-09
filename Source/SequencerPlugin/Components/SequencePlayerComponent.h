#pragma once

#include <Core/Messages/CommonMessages.h>
#include <Core/Messages/EventMessage.h>
#include <Core/World/Component.h>
#include <Core/World/World.h>
#include <Foundation/Types/UniquePtr.h>
#include <SequencerPlugin/Resources/SequenceResource.h>
#include <SequencerPlugin/SequencerPluginDLL.h>

/// \brief Sent by plSequencePlayerComponent when playback reaches the end of the sequence
/// (not sent when looping). The script hook for "resume gameplay after the cutscene".
struct PL_SEQUENCER_DLL plMsgSequenceFinished : public plEventMessage
{
  PL_DECLARE_MESSAGE_TYPE(plMsgSequenceFinished, plEventMessage);
};

using plSequencePlayerComponentManager = plComponentManagerSimple<class plSequencePlayerComponent, plComponentUpdateType::WhenSimulating>;

/// \brief Plays a Sequence asset at runtime: resolves the sequence's bindings to live scene
/// objects and drives their properties from the compiled tracks every frame.
///
/// Binding object paths are resolved when playback starts: below the explicit binding root if one
/// was set (from code), else below the owner object, else against the whole world's top-level
/// objects - which matches paths authored against the scene root in the editor.
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

  /// \brief Optional explicit object below which binding paths resolve. Code-only for now;
  /// unset means "owner, then world top-level".
  void SetBindingRoot(const plGameObjectHandle& hObject) { m_hBindingRoot = hObject; }

  bool m_bPlayOnStart = false; // [ property ]
  bool m_bLoop = false;        // [ property ]
  float m_fSpeed = 1.0f;       // [ property ]

  void Play();  // [ scriptable ]
  void Pause(); // [ scriptable ]
  void Stop();  // [ scriptable ]

  bool IsPlaying() const { return m_bPlaying; }
  plTime GetPlaybackTime() const { return m_PlaybackTime; }
  void SetPlaybackTime(plTime time);

protected:
  void Update();

  // ---- cached property bindings (PropertyAnim pattern) ---------------------
  // Pointers reference m_pPlayingDesc, a copy taken when playback starts, so a resource reload
  // mid-playback cannot invalidate them.
  struct Binding
  {
    const plAbstractMemberProperty* m_pMemberProperty = nullptr;
    mutable void* m_pObject = nullptr; // refreshed each frame, objects/components relocate
  };

  struct FloatBinding : public Binding
  {
    const plSequenceTrackDesc* m_pTracks[4] = {nullptr, nullptr, nullptr, nullptr};
  };

  struct ComponentFloatBinding : public FloatBinding
  {
    plComponentHandle m_hComponent;
  };

  struct GameObjectBinding : public FloatBinding
  {
    plGameObjectHandle m_hObject;
  };

  struct ColorBinding : public Binding
  {
    plComponentHandle m_hComponent;
    const plSequenceTrackDesc* m_pTrack = nullptr;
  };

  void CreateBindings();
  void ResolveBindingTargets(plStringView sObjectPath, const plRTTI* pComponentRtti, plHybridArray<plGameObject*, 8>& out_targets);
  void CreateGameObjectBinding(const plSequenceTrackDesc* pTrack, const plRTTI* pRtti, void* pObject, const plGameObjectHandle& hGameObject);
  void CreateComponentFloatBinding(const plSequenceTrackDesc* pTrack, const plRTTI* pRtti, void* pObject, const plComponentHandle& hComponent);
  void CreateColorBinding(const plSequenceTrackDesc* pTrack, const plRTTI* pRtti, void* pObject, const plComponentHandle& hComponent);
  void ApplyAnimations(plTime lookupTime);

  /// Camera cuts are applied by promoting the cut camera's usage hint to MainView, which is how
  /// game states pick their camera - so nothing else has to know a cutscene is running. The
  /// previous hint of every camera we touch is restored when playback stops.
  void ResolveCameraCuts();
  void ApplyCameraCut(plTime lookupTime);
  void RestoreCameras();

  /// Clips are edge-triggered: entering one starts it, leaving one stops it. Seeking and looping
  /// both move the playhead discontinuously, so this compares the set of clips active now against
  /// the set active last frame rather than assuming the playhead advanced smoothly.
  void UpdateClips(plTime lookupTime);
  void StartClip(const plSequenceClipDesc& clip, const plGameObjectHandle& hObject);
  void StopClip(const plSequenceClipDesc& clip, const plGameObjectHandle& hObject);
  void StopAllRunningClips();
  void ApplyFloatAnimation(const FloatBinding& binding, plTime lookupTime);
  void ApplySingleFloatAnimation(const FloatBinding& binding, plTime lookupTime);
  void ApplyColorAnimation(const ColorBinding& binding, plTime lookupTime);
  void EvaluateEventTrack(plTime startTime, plTime endTime);

  plEventMessageSender<plMsgSequenceFinished> m_SequenceFinishedMsgSender; // [ event ]
  plEventMessageSender<plMsgGenericEvent> m_EventTrackMsgSender;          // [ event ]

  plSequenceResourceHandle m_hSequence;
  plGameObjectHandle m_hBindingRoot;
  plTime m_PlaybackTime;
  bool m_bPlaying = false;
  bool m_bBindingsCreated = false;

  struct CameraCutBinding
  {
    plComponentHandle m_hCamera;
    plUInt64 m_uiStartTick = 0;
    plUInt64 m_uiBlendInTicks = 0;
  };

  plUniquePtr<plSequenceResourceDescriptor> m_pPlayingDesc;
  plHybridArray<GameObjectBinding, 4> m_GoFloatBindings;
  plHybridArray<ComponentFloatBinding, 4> m_ComponentFloatBindings;
  plHybridArray<ColorBinding, 4> m_ColorBindings;

  struct ClipBinding
  {
    const plSequenceClipDesc* m_pClip = nullptr;
    plGameObjectHandle m_hObject; ///< the binding's object, which carries the trigger component
  };

  plHybridArray<ClipBinding, 4> m_Clips;
  plHybridArray<plUInt32, 4> m_RunningClips; ///< indices into m_Clips that are currently playing

  plHybridArray<CameraCutBinding, 4> m_CameraCuts;
  plHashTable<plComponentHandle, plInt32> m_RestoreCameraHints; ///< original usage hints, by camera
  plInt32 m_iActiveCameraCut = -1;

  /// Drives the incoming camera from the outgoing one's pose during a cut's blend-in window.
  void UpdateCameraBlend(plUInt64 uiTick);
  void EndCameraBlend();

  /// The authored global transform of the camera currently being blended into, cached when the
  /// blend starts and put back when it ends - the blend drives the object, so without this the
  /// camera would keep whatever pose the last blended frame left it in.
  plGameObjectHandle m_hBlendingCamera;
  plTransform m_BlendCameraRestore;
  plTransform m_BlendFromTransform; ///< where the outgoing camera was when the cut began
  bool m_bBlendActive = false;
};
