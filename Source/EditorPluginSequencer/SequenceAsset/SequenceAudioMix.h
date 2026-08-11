#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>

/// \brief Renders the sequence's audio clips into a WAV file, for the render-to-disk flow.
///
/// The preview render path evaluates the document, which never fires clips - so a video rendered
/// from it is silent unless the audio is produced separately. This mixes every audio clip's sample
/// (resolved and decoded exactly the way the timeline waveform is) at its clip position into one
/// stereo track. It is a straight sum: per-clip volume, pitch and speed are not applied yet, and
/// the mixer's buses are not consulted - this is the clips as placed, not the full audio pipeline.
class plSequenceAudioMix
{
public:
  /// \brief Mixes all audio clips overlapping [startTick, endTick) and writes a 48kHz stereo
  /// 16-bit WAV. Succeeds with a silent file when there are no audio clips - a video with silent
  /// audio is expected output, not an error.
  static plResult WriteMixToWav(
    const plSequenceAssetDocument* pDocument, plUInt64 uiStartTick, plUInt64 uiEndTick, plStringView sWavFile, bool& out_bHadClips);

  static constexpr plUInt32 SampleRate = 48000;
};
