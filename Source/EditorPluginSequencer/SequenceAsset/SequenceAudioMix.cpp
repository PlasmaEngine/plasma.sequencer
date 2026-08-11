#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceAudioMix.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceWaveformCache.h>

#include <Foundation/IO/FileSystem/FileWriter.h>

namespace
{
  /// The 44-byte canonical PCM WAV header. Hand-rolled because it is 44 bytes: nothing in-tree
  /// writes audio files, and pulling in an encoder for this would be absurd.
  plResult WriteWavHeader(plFileWriter& inout_file, plUInt32 uiSampleRate, plUInt32 uiDataBytes)
  {
    const plUInt16 uiChannels = 2;
    const plUInt16 uiBitsPerSample = 16;
    const plUInt32 uiByteRate = uiSampleRate * uiChannels * (uiBitsPerSample / 8);
    const plUInt16 uiBlockAlign = uiChannels * (uiBitsPerSample / 8);

    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes("RIFF", 4));
    const plUInt32 uiRiffSize = 36 + uiDataBytes;
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiRiffSize, 4));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes("WAVEfmt ", 8));
    const plUInt32 uiFmtSize = 16;
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiFmtSize, 4));
    const plUInt16 uiFormat = 1; // PCM
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiFormat, 2));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiChannels, 2));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiSampleRate, 4));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiByteRate, 4));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiBlockAlign, 2));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiBitsPerSample, 2));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes("data", 4));
    PL_SUCCEED_OR_RETURN(inout_file.WriteBytes(&uiDataBytes, 4));
    return PL_SUCCESS;
  }

  struct MixClip
  {
    plString m_sPayload;
    plUInt64 m_uiStartTick = 0;
    plUInt64 m_uiDurationTicks = 0;
  };

  void CollectAudioClips(const plSequenceAssetDocument* pDoc, const plDocumentObject* pOwnerObj, plDynamicArray<MixClip>& inout_clips)
  {
    if (pOwnerObj == nullptr)
      return;

    const plInt32 iCount = pOwnerObj->GetTypeAccessor().GetCount("Clips");
    for (plInt32 c = 0; c < iCount; ++c)
    {
      const plVariant guid = pOwnerObj->GetTypeAccessor().GetValue("Clips", c);
      if (!guid.IsA<plUuid>())
        continue;

      const plSequenceClip* pClip = pDoc->GetClip(guid.Get<plUuid>());
      if (pClip == nullptr || pClip->m_Kind != plSequenceClipKind::AudioTrigger || pClip->m_sPayload.IsEmpty())
        continue;

      MixClip& clip = inout_clips.ExpandAndGetRef();
      clip.m_sPayload = pClip->m_sPayload;
      clip.m_uiStartTick = pClip->m_uiStartTick;
      clip.m_uiDurationTicks = pClip->m_uiDurationTicks;

      if (pClip->m_fSpeed != 1.0f)
        plLog::Warning("Sequence audio mix: clip speed {0} is not applied yet; '{1}' mixes at 1x.", pClip->m_fSpeed, pClip->m_sPayload);
    }
  }
} // namespace

plResult plSequenceAudioMix::WriteMixToWav(
  const plSequenceAssetDocument* pDocument, plUInt64 uiStartTick, plUInt64 uiEndTick, plStringView sWavFile, bool& out_bHadClips)
{
  out_bHadClips = false;

  if (uiEndTick <= uiStartTick)
    return PL_FAILURE;

  plDynamicArray<MixClip> clips;

  const plDocumentObject* pPropObj = const_cast<plSequenceAssetDocument*>(pDocument)->GetPropertyObject();
  CollectAudioClips(pDocument, pPropObj, clips); // sequence-level clips

  const plInt32 iBindingCount = pPropObj->GetTypeAccessor().GetCount("Bindings");
  for (plInt32 b = 0; b < iBindingCount; ++b)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("Bindings", b);
    if (guid.IsA<plUuid>())
      CollectAudioClips(pDocument, pDocument->GetObjectManager()->GetObject(guid.Get<plUuid>()), clips);
  }

  const double fRangeSeconds = (double)(uiEndTick - uiStartTick) / (double)plSequenceTicksPerSecond;
  const plUInt64 uiMixFrames = (plUInt64)(fRangeSeconds * (double)SampleRate);

  // Summed in 32 bits, clamped to 16 on the way out, so overlapping clips saturate instead of
  // wrapping into noise.
  plDynamicArray<plInt32> mix;
  mix.SetCount((plUInt32)(uiMixFrames * 2), 0);

  plDynamicArray<plInt16> pcm;

  for (const MixClip& clip : clips)
  {
    if (plSequenceWaveformCache::DecodePayloadPcm(clip.m_sPayload, SampleRate, pcm).Failed())
    {
      plLog::Warning("Sequence audio mix: '{0}' does not resolve to a decodable sample; skipped.", clip.m_sPayload);
      continue;
    }

    out_bHadClips = true;

    // Where the clip's audio starts relative to the render range - clips beginning before the
    // range start partway through their sample.
    const plInt64 iOffsetTicks = (plInt64)clip.m_uiStartTick - (plInt64)uiStartTick;
    const plInt64 iOffsetFrames = (plInt64)((double)iOffsetTicks / (double)plSequenceTicksPerSecond * (double)SampleRate);

    // The clip's authored length caps its sound, same as playback: a trimmed clip cuts off.
    const plUInt64 uiClipCapFrames = (plUInt64)((double)clip.m_uiDurationTicks / (double)plSequenceTicksPerSecond * (double)SampleRate);
    const plUInt64 uiSampleFrames = plMath::Min<plUInt64>(pcm.GetCount() / 2, uiClipCapFrames);

    for (plUInt64 uiFrame = 0; uiFrame < uiSampleFrames; ++uiFrame)
    {
      const plInt64 iDest = iOffsetFrames + (plInt64)uiFrame;
      if (iDest < 0)
        continue;
      if ((plUInt64)iDest >= uiMixFrames)
        break;

      mix[(plUInt32)(iDest * 2 + 0)] += pcm[(plUInt32)(uiFrame * 2 + 0)];
      mix[(plUInt32)(iDest * 2 + 1)] += pcm[(plUInt32)(uiFrame * 2 + 1)];
    }
  }

  plDynamicArray<plInt16> out;
  out.SetCountUninitialized(mix.GetCount());
  for (plUInt32 i = 0; i < mix.GetCount(); ++i)
  {
    out[i] = (plInt16)plMath::Clamp<plInt32>(mix[i], -32768, 32767);
  }

  plFileWriter file;
  if (file.Open(sWavFile).Failed())
  {
    plLog::Error("Sequence audio mix: could not write '{0}'.", sWavFile);
    return PL_FAILURE;
  }

  const plUInt32 uiDataBytes = out.GetCount() * sizeof(plInt16);
  PL_SUCCEED_OR_RETURN(WriteWavHeader(file, SampleRate, uiDataBytes));
  PL_SUCCEED_OR_RETURN(file.WriteBytes(out.GetData(), uiDataBytes));

  plLog::Info("Sequence audio mix: wrote {0}s of audio from {1} clip(s) to '{2}'.", plArgF(fRangeSeconds, 2), clips.GetCount(), sWavFile);
  return PL_SUCCESS;
}
