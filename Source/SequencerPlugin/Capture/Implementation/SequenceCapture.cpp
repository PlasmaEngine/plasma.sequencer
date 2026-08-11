#include <SequencerPlugin/SequencerPluginPCH.h>

#include <SequencerPlugin/Capture/SequenceCapture.h>

#include <Foundation/IO/OSFile.h>
#include <Texture/Image/Image.h>

plSequenceCapture::plSequenceCapture() = default;

plSequenceCapture::~plSequenceCapture()
{
  if (m_bCapturing)
    Abort();
}

plResult plSequenceCapture::Begin(const plSequenceCaptureDesc& desc, plUInt64 uiSequenceDurationTicks)
{
  PL_ASSERT_DEV(!m_bCapturing, "A capture is already running");

  m_Desc = desc;
  m_uiFramesWritten = 0;
  m_uiWarmUpRemaining = 0;

  if (desc.m_uiFramesPerSecond == 0 || desc.m_uiWidth == 0 || desc.m_uiHeight == 0)
  {
    plLog::Error("Sequence render: {0}x{1} at {2} fps is not renderable.", desc.m_uiWidth, desc.m_uiHeight, desc.m_uiFramesPerSecond);
    return PL_FAILURE;
  }

  m_uiTicksPerFrame = plMath::Max<plUInt64>(1, plSequenceTicksPerSecond / desc.m_uiFramesPerSecond);
  m_uiCurrentTick = desc.m_uiStartTick;
  m_uiEndTick = desc.m_uiEndTick > 0 ? desc.m_uiEndTick : uiSequenceDurationTicks;

  if (m_uiEndTick <= m_uiCurrentTick)
  {
    plLog::Error("Sequence render: the range is empty ({0} to {1} ticks).", m_uiCurrentTick, m_uiEndTick);
    return PL_FAILURE;
  }

  m_uiTotalFrames = (plUInt32)((m_uiEndTick - m_uiCurrentTick + m_uiTicksPerFrame - 1) / m_uiTicksPerFrame);

  if (m_Desc.m_bWriteFrames)
  {
    if (plOSFile::CreateDirectoryStructure(m_Desc.m_sOutputFolder).Failed())
    {
      plLog::Error("Sequence render: could not create '{0}'.", m_Desc.m_sOutputFolder);
      return PL_FAILURE;
    }
  }

  if (m_Desc.m_bEncodeVideo)
  {
    m_pEncoder = plVideoEncoderFactory::Create(m_Desc.m_EncoderKind);

    if (m_pEncoder == nullptr)
    {
      // Refused rather than silently downgraded to frames-only: someone asked for a video, and
      // discovering at the end that they did not get one wastes the whole render.
      plLog::Error("Sequence render: the requested video encoder is not available on this machine.");
      return PL_FAILURE;
    }

    // The container follows the codec, so ProRes lands as .mov rather than in an mp4 no NLE expects.
    plStringBuilder sVideo = m_Desc.m_sOutputFolder;
    sVideo.AppendPath("render.");
    sVideo.Append(plVideoEncoderFactory::GetContainerExtension(m_Desc.m_Codec));
    sVideo.MakeCleanPath();

    plVideoEncoderDesc encDesc;
    encDesc.m_sOutputFile = sVideo;
    encDesc.m_uiWidth = m_Desc.m_uiWidth;
    encDesc.m_uiHeight = m_Desc.m_uiHeight;
    encDesc.m_uiFramesPerSecond = m_Desc.m_uiFramesPerSecond;
    encDesc.m_Codec = m_Desc.m_Codec;
    encDesc.m_Quality = m_Desc.m_Quality;
    encDesc.m_uiBitRate = m_Desc.m_uiBitRate;

    if (m_pEncoder->Begin(encDesc).Failed())
    {
      m_pEncoder.Clear();
      return PL_FAILURE;
    }
  }

  m_bCapturing = true;

  plLog::Info("Sequence render: {0} frames at {1}x{2}, {3} fps -> '{4}'.", m_uiTotalFrames, m_Desc.m_uiWidth, m_Desc.m_uiHeight,
    m_Desc.m_uiFramesPerSecond, m_Desc.m_sOutputFolder);

  return PL_SUCCESS;
}

void plSequenceCapture::NotifyCameraCut()
{
  // Scheduled rather than applied now: the frames are discarded as they arrive, which is the only
  // way to let temporal effects converge without also stalling the clock.
  m_uiWarmUpRemaining = m_Desc.m_uiWarmUpFramesPerCut;
}

plResult plSequenceCapture::SubmitFrame(const plImage& frame)
{
  if (!m_bCapturing)
    return PL_FAILURE;

  if (m_uiWarmUpRemaining > 0)
  {
    // Rendered at the same tick and thrown away, so a shot does not open with the previous shot's
    // temporal history smeared over it. The clock deliberately does not advance.
    --m_uiWarmUpRemaining;
    return PL_SUCCESS;
  }

  if (m_Desc.m_bWriteFrames)
  {
    plStringBuilder sFile;
    sFile.SetFormat("{0}/frame_{1}.png", m_Desc.m_sOutputFolder, plArgU(m_uiFramesWritten, 6, true));
    sFile.MakeCleanPath();

    if (frame.SaveTo(sFile).Failed())
    {
      plLog::Error("Sequence render: could not write '{0}'.", sFile);
      return PL_FAILURE;
    }
  }

  if (m_pEncoder != nullptr && m_pEncoder->AppendFrame(frame).Failed())
    return PL_FAILURE;

  ++m_uiFramesWritten;

  // Fixed step. Never derived from elapsed wall time - that is what keeps the output frame-perfect
  // on a machine that cannot render this scene in realtime.
  m_uiCurrentTick += m_uiTicksPerFrame;

  if (m_uiCurrentTick >= m_uiEndTick)
    m_bCapturing = false;

  return PL_SUCCESS;
}

plResult plSequenceCapture::Finish()
{
  m_bCapturing = false;

  plResult res = PL_SUCCESS;

  if (m_pEncoder != nullptr)
  {
    res = m_pEncoder->Finish();
    m_pEncoder.Clear();
  }

  plLog::Info("Sequence render: finished with {0} frames.", m_uiFramesWritten);
  return res;
}

void plSequenceCapture::Abort()
{
  m_bCapturing = false;

  if (m_pEncoder != nullptr)
  {
    m_pEncoder->Abort();
    m_pEncoder.Clear();
  }

  // Written frames are deliberately kept. A cancelled 4-hour render that leaves 3 hours of usable
  // frames is a partial success; deleting them turns it into a total loss.
  plLog::Info("Sequence render: cancelled after {0} frames, which are kept in '{1}'.", m_uiFramesWritten, m_Desc.m_sOutputFolder);
}

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Capture_Implementation_SequenceCapture);
