#include <SequencerPlugin/SequencerPluginPCH.h>

#include <SequencerPlugin/Capture/VideoEncoderFfmpeg.h>

#include <Foundation/IO/OSFile.h>
#include <Foundation/System/EnvironmentVariableUtils.h>
#include <Texture/Image/Image.h>

plVideoEncoderFfmpeg::~plVideoEncoderFfmpeg()
{
  if (m_bOpen)
    Abort();
}

plResult plVideoEncoderFfmpeg::Begin(const plVideoEncoderDesc& desc)
{
  if (plVideoEncoderFactory::GetFfmpegPath().IsEmpty())
  {
    plLog::Error("Sequence render: ffmpeg was not found, so no video can be encoded.");
    return PL_FAILURE;
  }

  m_Desc = desc;
  m_uiFrameIndex = 0;

  // Said up front rather than discovered by watching a progress bar crawl: the gap between the two
  // AV1 encoders is roughly an order of magnitude, not a few percent.
  if (desc.m_Codec == plVideoCodec::AV1 && ResolveEncoderName(plVideoCodec::AV1) == plStringView("libaom-av1"))
  {
    plLog::Warning("Sequence render: this ffmpeg has no SVT-AV1, so AV1 encodes with libaom. That is correct but many times "
                   "slower; an ffmpeg build with libsvtav1 will finish far sooner.");
  }

  // Frames land next to the output as a numbered PNG sequence, and ffmpeg is run over them once the
  // render finishes. plProcess has no stdin, so streaming is not an option - but writing frames is
  // what the design wanted anyway: a render that dies at frame 9,000 leaves 8,999 usable ones,
  // whereas a truncated video leaves nothing.
  plStringBuilder sDir = desc.m_sOutputFile;
  sDir.RemoveFileExtension();
  sDir.Append("_frames");
  sDir.MakeCleanPath();

  if (plOSFile::CreateDirectoryStructure(sDir).Failed())
  {
    plLog::Error("Sequence render: could not create the frame folder '{0}'.", sDir);
    return PL_FAILURE;
  }

  m_sFrameDir = sDir;
  m_bOpen = true;
  return PL_SUCCESS;
}

plResult plVideoEncoderFfmpeg::AppendFrame(const plImage& frame)
{
  if (!m_bOpen)
    return PL_FAILURE;

  if (frame.GetWidth() != m_Desc.m_uiWidth || frame.GetHeight() != m_Desc.m_uiHeight)
  {
    plLog::Error("Sequence render: frame is {0}x{1} but the encoder was opened for {2}x{3}.", frame.GetWidth(), frame.GetHeight(),
      m_Desc.m_uiWidth, m_Desc.m_uiHeight);
    return PL_FAILURE;
  }

  plStringBuilder sFile;
  sFile.SetFormat("{0}/frame_{1}.png", m_sFrameDir, plArgU(m_uiFrameIndex, 6, true));

  if (frame.SaveTo(sFile).Failed())
  {
    plLog::Error("Sequence render: could not write '{0}'.", sFile);
    return PL_FAILURE;
  }

  ++m_uiFrameIndex;
  return PL_SUCCESS;
}

plResult plVideoEncoderFfmpeg::Finish()
{
  if (!m_bOpen)
    return PL_FAILURE;

  m_bOpen = false;

  if (m_uiFrameIndex == 0)
  {
    plLog::Error("Sequence render: no frames were captured, so there is nothing to encode.");
    return PL_FAILURE;
  }

  plStringBuilder sPattern;
  sPattern.SetFormat("{0}/frame_%06d.png", m_sFrameDir);

  plStringBuilder sFps;
  sFps.SetFormat("{0}", m_Desc.m_uiFramesPerSecond);

  plProcessOptions opt;
  opt.m_sProcess = plVideoEncoderFactory::GetFfmpegPath();
  opt.m_bHideConsoleWindow = true;
  opt.m_onStdError = [](plStringView sLine) { plLog::Dev("ffmpeg: {0}", sLine); };

  opt.AddArgument("-y");         // the dialog already confirmed the overwrite
  opt.AddArgument("-framerate"); //
  opt.AddArgument(sFps);         // input rate, before -i, or ffmpeg assumes 25
  opt.AddArgument("-i");         //
  opt.AddArgument(sPattern);     //
  opt.AddArgument("-an");        // audio is muxed in afterwards, from the offline mix

  plVideoEncoderFfmpeg::AppendCodecArguments(opt, m_Desc);

  opt.AddArgument(m_Desc.m_sOutputFile);

  plInt32 iExitCode = 0;
  if (plProcess::Execute(opt, &iExitCode).Failed() || iExitCode != 0)
  {
    plLog::Error("Sequence render: ffmpeg failed (exit code {0}). The frames are kept at '{1}'.", iExitCode, m_sFrameDir);
    return PL_FAILURE;
  }

  // The frames are deliberately left behind: they are the master, and re-encoding at different
  // settings should not need a re-render. The dialog offers to clear them.
  plLog::Info("Sequence render: encoded {0} frames to '{1}'.", m_uiFrameIndex, m_Desc.m_sOutputFile);
  return PL_SUCCESS;
}

void plVideoEncoderFfmpeg::Abort()
{
  m_bOpen = false;

  // Only the video is removed. The frames captured so far are exactly what makes a cancelled or
  // crashed render still worth something, so they stay.
  if (!m_Desc.m_sOutputFile.IsEmpty())
  {
    plOSFile::DeleteFile(m_Desc.m_sOutputFile).IgnoreResult();
  }
}

// ---- factory ---------------------------------------------------------------

namespace
{
  plString s_sFfmpegOverride;
  plString s_sFfmpegResolved;
  bool s_bFfmpegSearched = false;

  plString s_sEncoderList;
  bool s_bEncodersProbed = false;

  /// The output of "ffmpeg -encoders", fetched once. An ffmpeg build is not one thing: plenty are
  /// compiled without libx265, and AV1 support ranges from none to three different encoders.
  plStringView GetEncoderList()
  {
    if (s_bEncodersProbed)
      return s_sEncoderList;

    s_bEncodersProbed = true;
    s_sEncoderList.Clear();

    const plString sFfmpeg = plVideoEncoderFactory::GetFfmpegPath();
    if (sFfmpeg.IsEmpty())
      return s_sEncoderList;

    plStringBuilder sOutput;

    plProcessOptions opt;
    opt.m_sProcess = sFfmpeg;
    opt.m_bHideConsoleWindow = true;
    opt.m_onStdOut = [&sOutput](plStringView sLine)
    { sOutput.Append(sLine, "\n"); };

    opt.AddArgument("-hide_banner");
    opt.AddArgument("-encoders");

    plInt32 iExitCode = 0;
    if (plProcess::Execute(opt, &iExitCode).Succeeded() && iExitCode == 0)
      s_sEncoderList = sOutput;
    else
      plLog::Warning("Sequence render: could not list ffmpeg's encoders; only H.264 will be offered.");

    return s_sEncoderList;
  }

  bool HasEncoder(plStringView sName)
  {
    const plStringView sList = GetEncoderList();

    // Fall back to assuming only libx264, which every build in practice has. Better than offering
    // codecs that will fail at the end of a long render.
    if (sList.IsEmpty())
      return sName == plStringView("libx264");

    return sList.FindSubString(sName) != nullptr;
  }

  /// CRF per codec and quality. The scales genuinely differ - 23 is ordinary for x264, poor for
  /// SVT-AV1 - so this cannot be one table of numbers.
  plUInt32 GetCrf(plVideoCodec::Enum codec, plVideoQuality::Enum quality)
  {
    switch (codec)
    {
      case plVideoCodec::H264:
        return quality == plVideoQuality::Draft ? 28 : quality == plVideoQuality::Review ? 23
                                                   : quality == plVideoQuality::Delivery ? 20
                                                                                         : 17;
      case plVideoCodec::H265:
        // x265 needs a few points more than x264 for the same perceived quality.
        return quality == plVideoQuality::Draft ? 32 : quality == plVideoQuality::Review ? 27
                                                   : quality == plVideoQuality::Delivery ? 23
                                                                                         : 19;
      case plVideoCodec::AV1:
        // SVT-AV1's CRF runs 0-63, so the useful numbers sit much higher.
        return quality == plVideoQuality::Draft ? 48 : quality == plVideoQuality::Review ? 38
                                                   : quality == plVideoQuality::Delivery ? 30
                                                                                         : 22;
      default:
        return 20;
    }
  }

  /// The speed knob, keyed on the encoder rather than the codec: x264/x265 and SVT-AV1 both spell it
  /// "-preset" but take different values, and libaom spells it "-cpu-used" entirely.
  plStringView GetSpeedSetting(plStringView sEncoder, plVideoQuality::Enum quality)
  {
    if (sEncoder == plStringView("libsvtav1"))
    {
      // SVT-AV1's scale is 0-13, and its slow end is unusable over thousands of frames.
      return quality == plVideoQuality::Draft ? "9" : quality == plVideoQuality::Master ? "4" : "6";
    }

    if (sEncoder == plStringView("libaom-av1"))
    {
      // libaom's default cpu-used is near-zero, which is measured in days for a real render.
      return quality == plVideoQuality::Draft ? "8" : quality == plVideoQuality::Master ? "4" : "6";
    }

    return quality == plVideoQuality::Draft ? "veryfast" : quality == plVideoQuality::Master ? "slow" : "medium";
  }
} // namespace

plStringView plVideoEncoderFfmpeg::ResolveEncoderName(plVideoCodec::Enum codec)
{
  switch (codec)
  {
    case plVideoCodec::H264:
      return HasEncoder("libx264") ? "libx264" : "";

    case plVideoCodec::H265:
      return HasEncoder("libx265") ? "libx265" : "";

    case plVideoCodec::AV1:
      // SVT-AV1 first: libaom is reference-quality and roughly an order of magnitude slower, which
      // over a few thousand frames is the difference between minutes and overnight. Both are
      // offered, because a slow AV1 encode still beats no AV1 encode.
      if (HasEncoder("libsvtav1"))
        return "libsvtav1";
      if (HasEncoder("libaom-av1"))
        return "libaom-av1";
      return "";

    case plVideoCodec::ProRes:
      if (HasEncoder("prores_ks"))
        return "prores_ks";
      if (HasEncoder("prores"))
        return "prores";
      return "";
  }

  return "";
}

void plVideoEncoderFfmpeg::AppendCodecArguments(plProcessOptions& ref_options, const plVideoEncoderDesc& desc)
{
  const plStringView sEncoder = ResolveEncoderName(desc.m_Codec);

  ref_options.AddArgument("-c:v");
  ref_options.AddArgument(sEncoder.IsEmpty() ? plStringView("libx264") : sEncoder);

  if (desc.m_Codec == plVideoCodec::ProRes)
  {
    // ProRes has no CRF; the quality dial is the profile. 10-bit 4:2:2 throughout, which is the
    // point of using it at all.
    plStringBuilder sProfile;
    sProfile.SetFormat("{0}", desc.m_Quality == plVideoQuality::Draft ? 0 : desc.m_Quality == plVideoQuality::Review ? 1
                                                                        : desc.m_Quality == plVideoQuality::Delivery ? 2
                                                                                                                     : 3);
    ref_options.AddArgument("-profile:v");
    ref_options.AddArgument(sProfile);
    ref_options.AddArgument("-pix_fmt");
    ref_options.AddArgument("yuv422p10le");
    return;
  }

  plStringBuilder sCrf;
  sCrf.SetFormat("{0}", GetCrf(desc.m_Codec, desc.m_Quality));
  ref_options.AddArgument("-crf");
  ref_options.AddArgument(sCrf);

  // The speed control is per-encoder, not per-codec. Getting this wrong is not a quality
  // difference: ffmpeg rejects an option its encoder does not have and the render produces nothing.
  const plStringView sSpeed = GetSpeedSetting(sEncoder, desc.m_Quality);

  if (sEncoder == plStringView("libaom-av1"))
  {
    // libaom only treats -crf as constant-quality when the bitrate is explicitly zero; without this
    // it silently switches to constrained quality and ignores most of the setting. -row-mt is what
    // makes it use more than a couple of cores.
    ref_options.AddArgument("-b:v");
    ref_options.AddArgument("0");
    ref_options.AddArgument("-cpu-used");
    ref_options.AddArgument(sSpeed);
    ref_options.AddArgument("-row-mt");
    ref_options.AddArgument("1");
  }
  else
  {
    ref_options.AddArgument("-preset");
    ref_options.AddArgument(sSpeed);
  }

  ref_options.AddArgument("-pix_fmt");
  ref_options.AddArgument("yuv420p"); // what every player can actually decode

  if (desc.m_Codec == plVideoCodec::H265)
  {
    // Without hvc1, QuickTime and Safari refuse the file even though it is valid HEVC.
    ref_options.AddArgument("-tag:v");
    ref_options.AddArgument("hvc1");
  }
}

void plVideoEncoderFactory::SetFfmpegPath(plStringView sAbsolutePath)
{
  s_sFfmpegOverride = sAbsolutePath;
  s_bFfmpegSearched = false;  // re-resolve on the next ask
  s_bEncodersProbed = false;  // a different build has a different encoder set
}

plStringView plVideoEncoderFactory::GetFfmpegPath()
{
  if (s_bFfmpegSearched)
    return s_sFfmpegResolved;

  s_bFfmpegSearched = true;
  s_sFfmpegResolved.Clear();

  // An explicit path wins: it is how a studio points at its own vetted build, and how the editor
  // hands over one the user chose to install.
  if (!s_sFfmpegOverride.IsEmpty() && plOSFile::ExistsFile(s_sFfmpegOverride))
  {
    s_sFfmpegResolved = s_sFfmpegOverride;
    return s_sFfmpegResolved;
  }

  // Otherwise walk PATH. ffmpeg is never shipped with Plasma - it is only used if the machine
  // already has it (Documentation/Sequencer/Plan.html §14 explains why that distinction matters).
#if PL_ENABLED(PL_PLATFORM_WINDOWS)
  const char* szExeName = "ffmpeg.exe";
  const char* szSeparator = ";";
#else
  const char* szExeName = "ffmpeg";
  const char* szSeparator = ":";
#endif

  const plString sPath = plEnvironmentVariableUtils::GetValueString("PATH");

  plHybridArray<plStringView, 64> dirs;
  plStringView(sPath).Split(false, dirs, szSeparator);

  for (plStringView sDir : dirs)
  {
    if (sDir.IsEmpty())
      continue;

    plStringBuilder sCandidate = sDir;
    sCandidate.AppendPath(szExeName);
    sCandidate.MakeCleanPath();

    if (plOSFile::ExistsFile(sCandidate))
    {
      s_sFfmpegResolved = sCandidate;
      break;
    }
  }

  return s_sFfmpegResolved;
}

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Capture_Implementation_VideoEncoderFfmpeg);
