#pragma once

#include <SequencerPlugin/Capture/VideoEncoder.h>

#include <Foundation/System/Process.h>

/// \brief Writes a PNG sequence, then muxes it with an ffmpeg the user already has.
///
/// Frames go to disk first rather than being streamed, which is what makes a cancelled or crashed
/// render still worth something - and plProcess offers no stdin to stream over in any case.
///
/// ffmpeg is invoked as a separate program and is never redistributed with Plasma - that process
/// boundary is what keeps its LGPL/GPL terms from attaching to anything here. Bundling the binary
/// into the package would undo that; see Documentation/Sequencer/Plan.html §14.
class PL_SEQUENCER_DLL plVideoEncoderFfmpeg : public plVideoEncoder
{
public:
  plVideoEncoderFfmpeg() = default;
  ~plVideoEncoderFfmpeg();

  virtual plResult Begin(const plVideoEncoderDesc& desc) override;
  virtual plResult AppendFrame(const plImage& frame) override;
  virtual plResult Finish() override;
  virtual void Abort() override;

  virtual plStringView GetDisplayName() const override { return "ffmpeg"; }

  /// \brief Appends the -c:v and quality arguments for the desc's codec.
  ///
  /// Split out because the quality mapping is the interesting part: every codec has its own CRF
  /// scale, so one number cannot serve them all. Public so the audio mux can ask the same question.
  static void AppendCodecArguments(plProcessOptions& ref_options, const plVideoEncoderDesc& desc);

  /// \brief Which ffmpeg encoder implements this codec on this machine, or empty if none does.
  /// AV1 in particular has several, and the fast one is not always present.
  static plStringView ResolveEncoderName(plVideoCodec::Enum codec);

private:
  plVideoEncoderDesc m_Desc;
  plString m_sFrameDir;
  plUInt32 m_uiFrameIndex = 0;
  bool m_bOpen = false;
};
