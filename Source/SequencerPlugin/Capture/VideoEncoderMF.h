#pragma once

#include <SequencerPlugin/Capture/VideoEncoder.h>

#if PL_ENABLED(PL_PLATFORM_WINDOWS_DESKTOP)

struct IMFSinkWriter;
using DWORD = unsigned long;

/// \brief Encodes H.264 into an mp4 using Windows Media Foundation.
///
/// The default backend, because it ships nothing at all: the encoding is done by the machine's own
/// licensed encoder, so there is neither a copyleft obligation nor a patent-pool question. Frames
/// are converted to NV12 by plImageConversion, which is the format the sink writer wants.
class PL_SEQUENCER_DLL plVideoEncoderMF : public plVideoEncoder
{
public:
  plVideoEncoderMF() = default;
  ~plVideoEncoderMF();

  virtual plResult Begin(const plVideoEncoderDesc& desc) override;
  virtual plResult AppendFrame(const plImage& frame) override;
  virtual plResult Finish() override;
  virtual void Abort() override;

  virtual plStringView GetDisplayName() const override { return "Media Foundation (H.264)"; }

private:
  void Shutdown();

  plVideoEncoderDesc m_Desc;
  IMFSinkWriter* m_pSinkWriter = nullptr;
  DWORD m_uiStreamIndex = 0;
  plInt64 m_iNextFrameTime = 0; ///< in 100ns units, which is what MF counts in
  plInt64 m_iFrameDuration = 0;
  bool m_bMediaFoundationStarted = false;
};

#endif
