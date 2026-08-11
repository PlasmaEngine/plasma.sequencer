#pragma once

#include <SequencerPlugin/Capture/VideoEncoder.h>
#include <SequencerPlugin/Resources/SequenceResource.h>

#include <Foundation/Types/UniquePtr.h>

class plImage;

/// \brief What to render, at what size and rate, and where to put it.
struct PL_SEQUENCER_DLL plSequenceCaptureDesc
{
  plString m_sOutputFolder;

  plUInt32 m_uiWidth = 1920;
  plUInt32 m_uiHeight = 1080;
  plUInt32 m_uiFramesPerSecond = 60;

  /// Tick range to render. End of zero means "to the end of the sequence".
  plUInt64 m_uiStartTick = 0;
  plUInt64 m_uiEndTick = 0;

  /// Numbered PNGs. Kept on by default - they are the master, and the only thing that survives a
  /// render dying part-way through.
  bool m_bWriteFrames = true;

  /// Encode alongside the frames. Ignored when the chosen encoder is unavailable.
  bool m_bEncodeVideo = false;
  plEnum<plVideoEncoderKind> m_EncoderKind;
  plEnum<plVideoCodec> m_Codec;
  plEnum<plVideoQuality> m_Quality;
  plUInt32 m_uiBitRate = 40 * 1000 * 1000;

  /// Frames rendered and discarded after each camera cut, to let temporal effects settle. Without
  /// this, the first frames of a shot carry the previous shot's TAA history smeared across them.
  plUInt8 m_uiWarmUpFramesPerCut = 2;
};

/// \brief Drives an offline, fixed-timestep render of a sequence to disk.
///
/// The harness owns the clock, not the host: each step advances the sequence by exactly 1/fps of
/// simulated time regardless of how long the frame took to produce. That is the whole point - a
/// machine that needs 200ms per 4K frame still yields a frame-perfect 60fps result, just slower
/// than realtime. Wall-clock pacing anywhere in here would silently cap output at the interactive
/// framerate, which is the problem this exists to solve.
///
/// It does not render. The host calls BeginFrame() to learn what time to pose, renders however it
/// renders, then hands the pixels to SubmitFrame(). That keeps the harness usable from both the
/// editor's engine process and plPlayer, which produce frames very differently.
class PL_SEQUENCER_DLL plSequenceCapture
{
  PL_DISALLOW_COPY_AND_ASSIGN(plSequenceCapture);

public:
  plSequenceCapture();
  ~plSequenceCapture();

  /// \brief Prepares output folders and the encoder. Fails before anything is rendered rather than
  /// discovering a bad path 9,000 frames in.
  plResult Begin(const plSequenceCaptureDesc& desc, plUInt64 uiSequenceDurationTicks);

  /// \brief True while frames remain.
  bool IsCapturing() const { return m_bCapturing; }

  /// \brief The tick the next frame should be posed at. Only meaningful while IsCapturing().
  plUInt64 GetCurrentTick() const { return m_uiCurrentTick; }

  /// \brief Whether the next frame is a warm-up that will be rendered and thrown away.
  bool IsWarmUpFrame() const { return m_uiWarmUpRemaining > 0; }

  /// \brief Hands over a rendered frame. Warm-up frames are counted and dropped; real frames are
  /// written and/or encoded, then the clock advances.
  plResult SubmitFrame(const plImage& frame);

  /// \brief Call when a camera cut starts at the current tick, to schedule warm-up frames.
  void NotifyCameraCut();

  /// \brief Closes the encoder and finalises the video. Safe to call twice.
  plResult Finish();

  /// \brief Stops and removes the video, keeping whatever frames were written.
  void Abort();

  plUInt32 GetFramesWritten() const { return m_uiFramesWritten; }
  plUInt32 GetTotalFrames() const { return m_uiTotalFrames; }

private:
  plSequenceCaptureDesc m_Desc;
  plUniquePtr<plVideoEncoder> m_pEncoder;

  bool m_bCapturing = false;
  plUInt64 m_uiCurrentTick = 0;
  plUInt64 m_uiEndTick = 0;
  plUInt64 m_uiTicksPerFrame = 0;
  plUInt32 m_uiFramesWritten = 0;
  plUInt32 m_uiTotalFrames = 0;
  plUInt8 m_uiWarmUpRemaining = 0;
};
