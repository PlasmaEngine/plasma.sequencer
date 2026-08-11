#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessDocumentContext.h>
#include <EnginePluginSequencer/EnginePluginSequencerDLL.h>

/// \brief Engine-process document context for Sequence assets.
///
/// Derives directly from plEngineProcessDocumentContext - NOT from plSceneContext. The context
/// factory resolves document types via FindPropertyByName("DocumentType"), which walks the type
/// hierarchy, so a derived type redefining that constant trips the RTTI correctness assert
/// (plLayerContext and water's profile context derive directly for the same reason). The base
/// class already owns the world and the IPC object mirror, so the attached context scene's
/// objects arrive here without scene-context help.
class PL_ENGINEPLUGINSEQUENCER_DLL plSequenceContext : public plEngineProcessDocumentContext
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceContext, plEngineProcessDocumentContext);

public:
  plSequenceContext();

  virtual void HandleMessage(const plEditorEngineDocumentMsg* pMsg) override;

protected:
  /// Freezes the world. plWorld simulates by default, so without this the preview viewport runs
  /// particles, physics and animation on wall-clock time - they drift on with the playhead standing
  /// still, and show a state belonging to no frame of the sequence. The playhead is the only thing
  /// that should move time here; see AdvancePreviewTo.
  virtual void OnInitialize() override;

  virtual plEngineProcessViewContext* CreateViewContext() override;
  virtual void DestroyViewContext(plEngineProcessViewContext* pContext) override;

  /// Places the thumbnail camera on the object the editor nominated, which is how a camera cut's
  /// preview image gets rendered from that cut's camera. Returning false holds the convergence
  /// counter back, so a thumbnail is never captured before there is something to capture.
  virtual bool UpdateThumbnailViewContext(plEngineProcessViewContext* pThumbnailViewContext) override;

  /// Steps the world exactly once per captured frame while rendering to disk. See SetRenderMode.
  virtual void UpdateDocumentContext() override;

  virtual void OnThumbnailViewContextRequested() override;
  virtual void OnDestroyThumbnailViewContext() override;

private:
  /// \brief Turns the world's simulation on for the duration of a render to disk.
  ///
  /// The thumbnail path this render rides on deliberately freezes the world - a thumbnail of a
  /// moving scene would never be reproducible - which is exactly why particles, physics and
  /// animation were missing from exported video.
  ///
  /// Simply re-enabling simulation is not enough, because one captured frame takes several engine
  /// frames to converge, and the world would advance on every one of them. Instead simulation is
  /// enabled for exactly one engine frame per captured frame, with the world clock on a fixed
  /// timestep of 1/fps. plWorld::Update pauses its clock whenever simulation is off, so the
  /// remaining convergence frames advance time by precisely zero: the result is one deterministic
  /// simulation step per output frame, independent of how fast the machine renders.
  void SetRenderMode(bool bEnabled, float fFramesPerSecond);

  /// \brief Simulates the preview world forward to \a fTargetSeconds of sequence time.
  ///
  /// Locks what the viewport shows to the frame being edited: without it the world is frozen, so a
  /// particle effect sits at whatever state it was left in no matter where the playhead is.
  ///
  /// Scrubbing backwards restarts everything that plays and replays forwards into the new time,
  /// since a simulation cannot run in reverse. Only the last MaxReplaySeconds are replayed, so an
  /// effect older than that window is approximate rather than exact.
  void AdvancePreviewTo(double fTargetSeconds);

  /// \brief Puts everything that plays back to its time-zero state.
  ///
  /// Uses the plain Core plMsgInterruptPlaying / plMsgSetPlaying pair broadcast to every object, so
  /// any component that plays something is covered and the sequencer needs no dependency on the
  /// particle package.
  void RestartEffects();

  // Audio preview. The editor knows where the playhead is and sends only clip edges; this process
  // owns the world and the audio system, so it is where triggers actually fire.
  plGameObject* ResolvePreviewAudioObject(plStringView sObjectGuid);
  void StartPreviewAudio(plStringView sObjectGuid, plStringView sTrigger);
  void StopPreviewAudio(plStringView sObjectGuid);
  void StopAllPreviewAudio();

  /// Host for sequence-level clips, which have no bound object. Created on first use.
  plGameObjectHandle m_hPreviewAudioObject;

  /// Where the next thumbnail is rendered from, set by a config message just before the request.
  /// A transform rather than an object: inside a camera-cut blend the view is a mix of two cameras
  /// and sits at neither of them, so the editor resolves it and sends the result.
  plTransform m_ThumbnailCameraTransform = plTransform::MakeIdentity();
  float m_fThumbnailCameraFov = 60.0f;
  bool m_bThumbnailCameraValid = false;

  /// Render-to-disk state. m_bSteppedThisCapture is what limits the world to a single simulation
  /// step per captured frame no matter how many engine frames the capture takes.
  bool m_bRenderMode = false;
  bool m_bCaptureInProgress = false;
  bool m_bSteppedThisCapture = false;

  /// Sequence time the preview world has been simulated to. See AdvancePreviewTo.
  double m_fPreviewTimeSeconds = 0.0;

  /// How much history one playhead move will replay. Replaying a whole sequence a step at a time
  /// would stall the editor for as long as the sequence, so a jump only replays its tail.
  static constexpr double MaxReplaySeconds = 4.0;

  /// Ceiling on world updates per playhead move, so a stall cannot cascade. Derived from the replay
  /// window rather than picked, so the two cannot drift apart.
  static constexpr plUInt32 MaxStepsPerMove = (plUInt32)(MaxReplaySeconds * 30.0) + 1;

  /// Largest single step handed to the simulation. A stable physics setup becomes an explosion when
  /// given one enormous step after a slow editor frame.
  static constexpr double MaxStepSeconds = 1.0 / 30.0;
};
