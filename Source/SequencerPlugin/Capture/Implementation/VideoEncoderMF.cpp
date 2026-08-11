#include <SequencerPlugin/SequencerPluginPCH.h>

#include <SequencerPlugin/Capture/VideoEncoderMF.h>

#if PL_ENABLED(PL_PLATFORM_WINDOWS_DESKTOP)

#  include <Foundation/IO/OSFile.h>
#  include <Texture/Image/Image.h>
#  include <Texture/Image/ImageConversion.h>

// The Media Foundation headers pull in Windows.h, and the engine asserts if that happens by any
// route other than its own wrapper - so the wrapper goes first.
#  include <Foundation/Basics/Platform/Win/IncludeWindows.h>

#  include <codecapi.h>
#  include <mfapi.h>
#  include <mferror.h>
#  include <mfidl.h>
#  include <mfreadwrite.h>

// windows.h arrives via the Media Foundation headers and macro-hijacks these names, which would
// otherwise rewrite plOSFile::DeleteFile into DeleteFileW.
#  undef DeleteFile
#  undef CreateDirectory

namespace
{
  /// MF counts time in 100-nanosecond units.
  constexpr plInt64 g_iHnsPerSecond = 10'000'000;

  void SafeRelease(IUnknown*& ref_pObject)
  {
    if (ref_pObject != nullptr)
    {
      ref_pObject->Release();
      ref_pObject = nullptr;
    }
  }
} // namespace

plVideoEncoderMF::~plVideoEncoderMF()
{
  if (m_pSinkWriter != nullptr)
    Abort();
}

plResult plVideoEncoderMF::Begin(const plVideoEncoderDesc& desc)
{
  m_Desc = desc;

  // H.264 requires even dimensions; an odd render size would otherwise fail deep inside MF with an
  // opaque HRESULT rather than here where it can be explained.
  if ((desc.m_uiWidth % 2) != 0 || (desc.m_uiHeight % 2) != 0)
  {
    plLog::Error("Sequence render: H.264 needs even dimensions, got {0}x{1}.", desc.m_uiWidth, desc.m_uiHeight);
    return PL_FAILURE;
  }

  if (FAILED(MFStartup(MF_VERSION)))
  {
    plLog::Error("Sequence render: MFStartup failed; Media Foundation is unavailable.");
    return PL_FAILURE;
  }

  m_bMediaFoundationStarted = true;
  m_iFrameDuration = g_iHnsPerSecond / (plInt64)plMath::Max(1u, desc.m_uiFramesPerSecond);
  m_iNextFrameTime = 0;

  plStringBuilder sPath = desc.m_sOutputFile;
  sPath.MakeCleanPath();
  const plStringWChar sPathW(sPath.GetView());

  IMFMediaType* pTypeOut = nullptr;
  IMFMediaType* pTypeIn = nullptr;

  PL_SCOPE_EXIT(SafeRelease(reinterpret_cast<IUnknown*&>(pTypeOut)); SafeRelease(reinterpret_cast<IUnknown*&>(pTypeIn)););

  if (FAILED(MFCreateSinkWriterFromURL(reinterpret_cast<LPCWSTR>(sPathW.GetData()), nullptr, nullptr, &m_pSinkWriter)))
  {
    plLog::Error("Sequence render: could not create an mp4 writer at '{0}'.", sPath);
    Shutdown();
    return PL_FAILURE;
  }

  // Output: H.264 at the requested bitrate.
  bool bOk = SUCCEEDED(MFCreateMediaType(&pTypeOut));
  bOk = bOk && SUCCEEDED(pTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
  bOk = bOk && SUCCEEDED(pTypeOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
  bOk = bOk && SUCCEEDED(pTypeOut->SetUINT32(MF_MT_AVG_BITRATE, desc.m_uiBitRate));
  bOk = bOk && SUCCEEDED(pTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
  bOk = bOk && SUCCEEDED(MFSetAttributeSize(pTypeOut, MF_MT_FRAME_SIZE, desc.m_uiWidth, desc.m_uiHeight));
  bOk = bOk && SUCCEEDED(MFSetAttributeRatio(pTypeOut, MF_MT_FRAME_RATE, desc.m_uiFramesPerSecond, 1));
  bOk = bOk && SUCCEEDED(MFSetAttributeRatio(pTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
  bOk = bOk && SUCCEEDED(m_pSinkWriter->AddStream(pTypeOut, &m_uiStreamIndex));

  // Input: NV12, which is what the H.264 encoder consumes natively.
  bOk = bOk && SUCCEEDED(MFCreateMediaType(&pTypeIn));
  bOk = bOk && SUCCEEDED(pTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
  bOk = bOk && SUCCEEDED(pTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
  bOk = bOk && SUCCEEDED(pTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
  bOk = bOk && SUCCEEDED(MFSetAttributeSize(pTypeIn, MF_MT_FRAME_SIZE, desc.m_uiWidth, desc.m_uiHeight));
  bOk = bOk && SUCCEEDED(MFSetAttributeRatio(pTypeIn, MF_MT_FRAME_RATE, desc.m_uiFramesPerSecond, 1));
  bOk = bOk && SUCCEEDED(MFSetAttributeRatio(pTypeIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
  bOk = bOk && SUCCEEDED(m_pSinkWriter->SetInputMediaType(m_uiStreamIndex, pTypeIn, nullptr));

  bOk = bOk && SUCCEEDED(m_pSinkWriter->BeginWriting());

  if (!bOk)
  {
    plLog::Error("Sequence render: Media Foundation rejected the {0}x{1} @ {2}fps configuration.", desc.m_uiWidth, desc.m_uiHeight,
      desc.m_uiFramesPerSecond);
    Shutdown();
    return PL_FAILURE;
  }

  return PL_SUCCESS;
}

plResult plVideoEncoderMF::AppendFrame(const plImage& frame)
{
  if (m_pSinkWriter == nullptr)
    return PL_FAILURE;

  if (frame.GetWidth() != m_Desc.m_uiWidth || frame.GetHeight() != m_Desc.m_uiHeight)
  {
    plLog::Error("Sequence render: frame is {0}x{1} but the encoder was opened for {2}x{3}.", frame.GetWidth(), frame.GetHeight(),
      m_Desc.m_uiWidth, m_Desc.m_uiHeight);
    return PL_FAILURE;
  }

  // The sRGB -> NV12 conversion is already in the engine (PlanarConversions.cpp), so this is a
  // format request rather than pixel work done here.
  plImage nv12;
  if (plImageConversion::Convert(frame, nv12, plImageFormat::NV12).Failed())
  {
    plLog::Error("Sequence render: could not convert the frame to NV12.");
    return PL_FAILURE;
  }

  const plConstByteBlobPtr source = nv12.GetByteBlobPtr();

  IMFMediaBuffer* pBuffer = nullptr;
  IMFSample* pSample = nullptr;
  PL_SCOPE_EXIT(SafeRelease(reinterpret_cast<IUnknown*&>(pBuffer)); SafeRelease(reinterpret_cast<IUnknown*&>(pSample)););

  if (FAILED(MFCreateMemoryBuffer((DWORD)source.GetCount(), &pBuffer)))
    return PL_FAILURE;

  BYTE* pDest = nullptr;
  if (FAILED(pBuffer->Lock(&pDest, nullptr, nullptr)))
    return PL_FAILURE;

  plMemoryUtils::RawByteCopy(pDest, source.GetPtr(), source.GetCount());
  pBuffer->Unlock();
  pBuffer->SetCurrentLength((DWORD)source.GetCount());

  if (FAILED(MFCreateSample(&pSample)) || FAILED(pSample->AddBuffer(pBuffer)))
    return PL_FAILURE;

  // Timestamps are derived from the frame index, never from wall clock: the whole point of an
  // offline render is that how long a frame took to produce does not affect the result.
  if (FAILED(pSample->SetSampleTime(m_iNextFrameTime)) || FAILED(pSample->SetSampleDuration(m_iFrameDuration)))
    return PL_FAILURE;

  if (FAILED(m_pSinkWriter->WriteSample(m_uiStreamIndex, pSample)))
  {
    plLog::Error("Sequence render: Media Foundation rejected a frame.");
    return PL_FAILURE;
  }

  m_iNextFrameTime += m_iFrameDuration;
  return PL_SUCCESS;
}

plResult plVideoEncoderMF::Finish()
{
  if (m_pSinkWriter == nullptr)
    return PL_FAILURE;

  // Finalize writes the index; without it the file exists but is not playable.
  const bool bOk = SUCCEEDED(m_pSinkWriter->Finalize());
  Shutdown();

  if (!bOk)
  {
    plLog::Error("Sequence render: finalising the mp4 failed.");
    return PL_FAILURE;
  }

  return PL_SUCCESS;
}

void plVideoEncoderMF::Abort()
{
  Shutdown();

  // A finalised-but-truncated mp4 plays for a few seconds and looks like a successful render, which
  // is a worse failure than an absent file.
  if (!m_Desc.m_sOutputFile.IsEmpty())
  {
    plOSFile::DeleteFile(m_Desc.m_sOutputFile).IgnoreResult();
  }
}

void plVideoEncoderMF::Shutdown()
{
  SafeRelease(reinterpret_cast<IUnknown*&>(m_pSinkWriter));

  if (m_bMediaFoundationStarted)
  {
    MFShutdown();
    m_bMediaFoundationStarted = false;
  }
}

#endif

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Capture_Implementation_VideoEncoderMF);
