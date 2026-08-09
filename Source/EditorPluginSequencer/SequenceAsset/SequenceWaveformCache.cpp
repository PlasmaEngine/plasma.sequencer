#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceWaveformCache.h>

#include <EditorFramework/Assets/AssetCurator.h>
#include <Foundation/IO/FileSystem/FileSystem.h>
#include <Foundation/Utilities/ConversionUtils.h>

// The decoder is compiled in here rather than taken as a module dependency: an out-of-tree package
// cannot reference an SDK third-party module, and vendoring a copy of miniaudio for a waveform is
// not worth it. Everything but file decoding is switched off, which is most of the library.
#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_CUSTOM
#define MA_NO_FLAC
#define MA_NO_WINMM
#define MINIAUDIO_IMPLEMENTATION
#include <MiniAudio/miniaudio.h>

namespace
{
  bool IsDecodableAudioFile(plStringView sPath)
  {
    plStringBuilder sExt = plPathUtils::GetFileExtension(sPath);
    sExt.ToLower();
    // what miniaudio decodes in this build - FLAC is compiled out (see miniaudio.module.cs)
    return sExt.IsEqual("wav") || sExt.IsEqual("mp3") || sExt.IsEqual("ogg");
  }
} // namespace

plString plSequenceWaveformCache::PayloadDisplayName(plStringView sPayload)
{
  plStringBuilder s = sPayload;

  if (s.IsEmpty())
    return plString();

  // A GUID-valued trigger is an asset; show its path without the extension.
  if (s.StartsWith("{") && plAssetCurator::GetSingleton() != nullptr)
  {
    const auto subAsset = plAssetCurator::GetSingleton()->FindSubAsset(s);
    if (subAsset.isValid())
    {
      plStringBuilder sIdentifier;
      subAsset->GetSubAssetIdentifier(sIdentifier);

      if (!sIdentifier.IsEmpty())
      {
        sIdentifier.RemoveFileExtension();
        return sIdentifier;
      }
    }

    return s; // unresolvable: better a GUID than nothing, it says the trigger is dangling
  }

  // "<asset-path>|<event>" keeps only the event, and an FMOD-style prefix is noise.
  if (const char* szSeparator = s.FindSubString("|"))
    s = szSeparator + 1;

  if (s.StartsWith_NoCase("event:/"))
    s.Shrink(7, 0);
  else if (s.StartsWith("/"))
    s.Shrink(1, 0);

  return s;
}

plResult plSequenceWaveformCache::ResolveSampleFile(plStringView sPayload, plStringBuilder& out_sAbsolutePath, plUInt32 uiDepth)
{
  // A trigger payload names a sound asset directly, or an audio-event asset that references one, so
  // one level of indirection is enough. The guard is against cycles, not depth.
  if (uiDepth > 4 || sPayload.IsEmpty())
    return PL_FAILURE;

  // The payload may already be a plain file path rather than an asset reference.
  if (IsDecodableAudioFile(sPayload) && plFileSystem::ResolvePath(sPayload, &out_sAbsolutePath, nullptr).Succeeded())
    return PL_SUCCESS;

  plAssetCurator* pCurator = plAssetCurator::GetSingleton();
  if (pCurator == nullptr)
    return PL_FAILURE;

  // Collected under the curator lock, then walked outside it: the recursion locks again.
  plHybridArray<plString, 8> dependencies;
  {
    const auto subAsset = pCurator->FindSubAsset(sPayload, true);
    if (!subAsset.isValid() || subAsset->m_pAssetInfo == nullptr || subAsset->m_pAssetInfo->m_Info == nullptr)
      return PL_FAILURE;

    for (const plString& sDep : subAsset->m_pAssetInfo->m_Info->m_TransformDependencies)
    {
      dependencies.PushBack(sDep);
    }
  }

  // Direct sample files first, so a sound asset resolves without descending into unrelated assets.
  for (const plString& sDep : dependencies)
  {
    if (IsDecodableAudioFile(sDep) && plFileSystem::ResolvePath(sDep, &out_sAbsolutePath, nullptr).Succeeded())
      return PL_SUCCESS;
  }

  for (const plString& sDep : dependencies)
  {
    if (IsDecodableAudioFile(sDep))
      continue;

    if (ResolveSampleFile(sDep, out_sAbsolutePath, uiDepth + 1).Succeeded())
      return PL_SUCCESS;
  }

  return PL_FAILURE;
}

plResult plSequenceWaveformCache::Decode(plStringView sAbsolutePath, Peaks& out_peaks)
{
  plStringBuilder sPath = sAbsolutePath;

  // Decoded to mono so the envelope is one shape; the sample rate is left native because only the
  // frame count relative to the total matters for bucketing.
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 0);

  ma_decoder decoder;
  if (ma_decoder_init_file(sPath.GetData(), &config, &decoder) != MA_SUCCESS)
    return PL_FAILURE;

  ma_uint64 uiTotalFrames = 0;
  if (ma_decoder_get_length_in_pcm_frames(&decoder, &uiTotalFrames) != MA_SUCCESS || uiTotalFrames == 0)
  {
    ma_decoder_uninit(&decoder);
    return PL_FAILURE;
  }

  out_peaks.m_Min.SetCount(BucketCount, 0.0f);
  out_peaks.m_Max.SetCount(BucketCount, 0.0f);
  out_peaks.m_fDurationSeconds = (double)uiTotalFrames / (double)plMath::Max<plUInt32>(1, decoder.outputSampleRate);

  plDynamicArray<float> chunk;
  chunk.SetCountUninitialized(4096);

  ma_uint64 uiFramesDone = 0;
  bool bAnySamples = false;

  while (uiFramesDone < uiTotalFrames)
  {
    ma_uint64 uiRead = 0;
    if (ma_decoder_read_pcm_frames(&decoder, chunk.GetData(), chunk.GetCount(), &uiRead) != MA_SUCCESS || uiRead == 0)
      break;

    for (ma_uint64 i = 0; i < uiRead; ++i)
    {
      const plUInt32 uiBucket = (plUInt32)plMath::Min<ma_uint64>(BucketCount - 1, ((uiFramesDone + i) * BucketCount) / uiTotalFrames);
      const float fSample = plMath::Clamp(chunk[(plUInt32)i], -1.0f, 1.0f);

      out_peaks.m_Min[uiBucket] = plMath::Min(out_peaks.m_Min[uiBucket], fSample);
      out_peaks.m_Max[uiBucket] = plMath::Max(out_peaks.m_Max[uiBucket], fSample);
    }

    uiFramesDone += uiRead;
    bAnySamples = true;
  }

  ma_decoder_uninit(&decoder);

  if (!bAnySamples)
  {
    out_peaks.m_Min.Clear();
    out_peaks.m_Max.Clear();
    return PL_FAILURE;
  }

  return PL_SUCCESS;
}

const plSequenceWaveformCache::Peaks* plSequenceWaveformCache::GetPeaks(plStringView sPayload)
{
  if (sPayload.IsEmpty())
    return nullptr;

  const plString sKey = sPayload;

  if (const auto it = m_Cache.Find(sKey); it.IsValid())
  {
    return it.Value().m_Min.IsEmpty() ? nullptr : &it.Value();
  }

  // Decoded synchronously and kept forever. Sequence clips point at short samples, so the one-off
  // cost is not worth a background task and the thread-safety it would need around the curator.
  Peaks& peaks = m_Cache[sKey];

  plStringBuilder sFile;
  if (ResolveSampleFile(sPayload, sFile).Failed() || Decode(sFile, peaks).Failed())
  {
    peaks.m_Min.Clear();
    peaks.m_Max.Clear();
    return nullptr;
  }

  return &peaks;
}
