#pragma once

#include <EditorPluginSequencer/EditorPluginSequencerDLL.h>

#include <Foundation/Containers/DynamicArray.h>
#include <Foundation/Containers/Map.h>
#include <Foundation/Strings/String.h>

/// \brief Peak envelopes for the audio behind a clip payload, so audio clips can be drawn as a real
/// waveform rather than a coloured bar.
///
/// A clip payload is an audio trigger name, which the middleware resolves as a sound or audio-event
/// asset id. This walks that asset's transform dependencies down to the sample file on disk and
/// decodes it. Nothing here knows about a specific audio backend: the dependency walk is generic,
/// and a payload that resolves to no decodable file simply has no waveform.
class plSequenceWaveformCache
{
public:
  struct Peaks
  {
    /// Min/max sample value per horizontal bucket, both in -1..1. Empty if there is no waveform.
    plDynamicArray<float> m_Min;
    plDynamicArray<float> m_Max;
    double m_fDurationSeconds = 0.0;
  };

  /// \brief The peaks for a payload, decoding on the first request. Returns nullptr when the payload
  /// resolves to nothing decodable - callers fall back to a plain bar.
  const Peaks* GetPeaks(plStringView sPayload);

  /// \brief Drops every decoded waveform. Call when assets change under the editor.
  void Clear() { m_Cache.Clear(); }

  /// \brief Number of buckets every waveform is resampled to. Fixed, so zooming rescales the same
  /// envelope instead of forcing a re-decode.
  static constexpr plUInt32 BucketCount = 1024;

  /// \brief What to show for a clip payload.
  ///
  /// Audio triggers, animation clips and sub-sequences all resolve to asset GUIDs, so a raw payload
  /// reads as a GUID wherever it is shown. This is the sequencer's own copy of what the property
  /// grid does for the same values - the engine's version is file-local to its widget.
  static plString PayloadDisplayName(plStringView sPayload);

  /// \brief Resolves a trigger payload to an absolute path of a decodable audio file. Public so the
  /// offline audio mix (render-to-disk) resolves clips exactly the way the waveform display does.
  static plResult ResolveSampleFile(plStringView sPayload, plStringBuilder& out_sAbsolutePath, plUInt32 uiDepth = 0);

  /// \brief Fully decodes a payload's sample to interleaved stereo 16-bit PCM at the given rate.
  /// This is the offline-mix workhorse; the peaks path below stays as cheap as it was.
  static plResult DecodePayloadPcm(plStringView sPayload, plUInt32 uiSampleRate, plDynamicArray<plInt16>& out_samples);

private:

  static plResult Decode(plStringView sAbsolutePath, Peaks& out_peaks);

  /// An entry with empty arrays is a payload already resolved as having no waveform - cached so a
  /// failing lookup is attempted once, not on every repaint.
  plMap<plString, Peaks> m_Cache;
};
