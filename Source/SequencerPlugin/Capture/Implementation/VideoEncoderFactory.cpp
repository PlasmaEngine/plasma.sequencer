#include <SequencerPlugin/SequencerPluginPCH.h>

#include <SequencerPlugin/Capture/VideoEncoderFfmpeg.h>

#if PL_ENABLED(PL_PLATFORM_WINDOWS_DESKTOP)
#  include <SequencerPlugin/Capture/VideoEncoderMF.h>
#endif

bool plVideoEncoderFactory::IsAvailable(plVideoEncoderKind::Enum kind)
{
  switch (kind)
  {
    case plVideoEncoderKind::MediaFoundation:
#if PL_ENABLED(PL_PLATFORM_WINDOWS_DESKTOP)
      return true;
#else
      return false;
#endif

    case plVideoEncoderKind::FfmpegCli:
      // Availability is "the user has one", not "we could ship one" - nothing here installs it.
      return !GetFfmpegPath().IsEmpty();
  }

  return false;
}

bool plVideoEncoderFactory::IsCodecAvailable(plVideoEncoderKind::Enum kind, plVideoCodec::Enum codec)
{
  if (!IsAvailable(kind))
    return false;

  switch (kind)
  {
    case plVideoEncoderKind::MediaFoundation:
      // The MF writer is wired for H.264 only. Windows can do HEVC, but only with a codec pack the
      // user may not have, and claiming support we cannot guarantee is how a render fails at the end.
      return codec == plVideoCodec::H264;

    case plVideoEncoderKind::FfmpegCli:
      return !plVideoEncoderFfmpeg::ResolveEncoderName(codec).IsEmpty();
  }

  return false;
}

plStringView plVideoEncoderFactory::GetCodecDisplayName(plVideoCodec::Enum codec)
{
  switch (codec)
  {
    case plVideoCodec::H264:
      return "H.264";
    case plVideoCodec::H265:
      return "H.265 (HEVC)";
    case plVideoCodec::AV1:
      return "AV1";
    case plVideoCodec::ProRes:
      return "ProRes 422";
  }

  return "H.264";
}

plStringView plVideoEncoderFactory::GetContainerExtension(plVideoCodec::Enum codec)
{
  // ProRes in an mp4 is not something an NLE expects to find, and some refuse it outright.
  return codec == plVideoCodec::ProRes ? "mov" : "mp4";
}

plUniquePtr<plVideoEncoder> plVideoEncoderFactory::Create(plVideoEncoderKind::Enum kind)
{
  // Deliberately null rather than a stub that fails later: a render must not start believing it has
  // an encoder and discover otherwise 9,000 frames in.
  if (!IsAvailable(kind))
    return nullptr;

  switch (kind)
  {
    case plVideoEncoderKind::MediaFoundation:
#if PL_ENABLED(PL_PLATFORM_WINDOWS_DESKTOP)
      return PL_DEFAULT_NEW(plVideoEncoderMF);
#else
      return nullptr;
#endif

    case plVideoEncoderKind::FfmpegCli:
      return PL_DEFAULT_NEW(plVideoEncoderFfmpeg);
  }

  return nullptr;
}

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Capture_Implementation_VideoEncoderFactory);
