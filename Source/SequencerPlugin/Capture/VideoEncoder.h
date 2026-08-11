#pragma once

#include <SequencerPlugin/SequencerPluginDLL.h>

#include <Foundation/Strings/String.h>
#include <Foundation/Types/UniquePtr.h>

class plImage;

/// \brief Which video codec to encode with.
///
/// These are the delivery formats worth having, not everything ffmpeg can do. Each is here because
/// it answers a question the others do not, and the dialog only offers the ones the machine can
/// actually run.
struct PL_SEQUENCER_DLL plVideoCodec
{
  using StorageType = plUInt8;

  enum Enum
  {
    H264,   ///< Plays everywhere. The safe answer, and the only one Media Foundation offers.
    H265,   ///< Roughly half the bitrate of H.264 at the same quality; patchier browser support.
    AV1,    ///< Best compression and royalty-free, but far slower to encode.
    ProRes, ///< Intermediate, not delivery: huge files that an NLE can actually scrub and grade.

    Default = H264
  };
};

/// \brief How hard the encoder should try. Presented instead of raw numbers because the number that
/// means "good" is different for every codec - CRF 23 is ordinary for H.264 and poor for AV1.
struct PL_SEQUENCER_DLL plVideoQuality
{
  using StorageType = plUInt8;

  enum Enum
  {
    Draft,    ///< Fast and visibly lossy. For checking timing, not for showing anyone.
    Review,   ///< Small enough to send around.
    Delivery, ///< What a trailer ships at.
    Master,   ///< Near-transparent and large. For archival or another encode downstream.

    Default = Delivery
  };
};

/// \brief What a video file is being asked for. Fixed for the whole file - an encoder cannot change
/// resolution or frame rate mid-stream, so this is taken once at Begin.
struct PL_SEQUENCER_DLL plVideoEncoderDesc
{
  plString m_sOutputFile;
  plUInt32 m_uiWidth = 1920;
  plUInt32 m_uiHeight = 1080;
  plUInt32 m_uiFramesPerSecond = 60;

  plEnum<plVideoCodec> m_Codec;
  plEnum<plVideoQuality> m_Quality;

  /// Target bits per second. Only used by encoders that cannot work to a quality target; the ffmpeg
  /// backend prefers CRF, which holds quality steady instead of holding size steady.
  plUInt32 m_uiBitRate = 40 * 1000 * 1000;
};

/// \brief A sink that turns a stream of frames into a video file.
///
/// The point of the abstraction is that the choice of encoder stays open. The capture harness talks
/// to this, so adding a vendored AV1 encoder later is a new implementation rather than a rewrite of
/// the capture path (see Documentation/Sequencer/Plan.html §14).
class PL_SEQUENCER_DLL plVideoEncoder
{
  PL_DISALLOW_COPY_AND_ASSIGN(plVideoEncoder);

public:
  plVideoEncoder() = default;
  virtual ~plVideoEncoder() = default;

  virtual plResult Begin(const plVideoEncoderDesc& desc) = 0;

  /// \brief Appends one frame. Frames arrive in order, one per fixed timestep. Taken as plImage so
  /// the sRGB -> NV12 conversion already in plImageConversion does the pixel-format work.
  virtual plResult AppendFrame(const plImage& frame) = 0;

  /// \brief Closes the file. Must be called or the result is unplayable - an mp4 without its index
  /// is not a video, it is a byte pile.
  virtual plResult Finish() = 0;

  /// \brief Gives up and removes the partial file. A half-written video is worse than none: it
  /// looks like a deliverable until someone tries to play it.
  virtual void Abort() = 0;

  /// \brief Human-readable name for the dialog ("Media Foundation (H.264)").
  virtual plStringView GetDisplayName() const = 0;
};

/// \brief Which encoders this build knows how to construct.
struct PL_SEQUENCER_DLL plVideoEncoderKind
{
  using StorageType = plUInt8;

  enum Enum
  {
    MediaFoundation, ///< Windows only. Ships nothing - the OS does the encoding.
    FfmpegCli,       ///< Any format, if the user has ffmpeg. Never redistributed; see the plan's §14.

    Default = MediaFoundation
  };
};

/// \brief Creates encoders, and reports which ones this machine can actually run.
class PL_SEQUENCER_DLL plVideoEncoderFactory
{
public:
  /// \brief Whether an encoder of this kind can be created right now. False for Media Foundation
  /// off Windows, and for ffmpeg when no executable has been found.
  static bool IsAvailable(plVideoEncoderKind::Enum kind);

  /// \brief Whether this kind can encode this codec on this machine.
  ///
  /// An ffmpeg build is not one thing: plenty ship without libx265 or any AV1 encoder at all. This
  /// asks the executable what it has (once, cached) so a codec the user picks cannot fail twenty
  /// minutes into a render.
  static bool IsCodecAvailable(plVideoEncoderKind::Enum kind, plVideoCodec::Enum codec);

  /// \brief Name for the dialog, e.g. "H.265 (HEVC)".
  static plStringView GetCodecDisplayName(plVideoCodec::Enum codec);

  /// \brief Container the codec belongs in, without the dot: "mp4", or "mov" for ProRes, which is
  /// not legal in an mp4 and which no NLE expects to find there.
  static plStringView GetContainerExtension(plVideoCodec::Enum codec);

  /// \brief Null when the kind is unavailable, so callers cannot half-start a render.
  static plUniquePtr<plVideoEncoder> Create(plVideoEncoderKind::Enum kind);

  /// \brief Where ffmpeg was found, or empty. Searched on PATH, then at the override below.
  static plStringView GetFfmpegPath();

  /// \brief Points at a user-supplied or editor-installed ffmpeg. The editor sets this from its
  /// preferences; nothing here downloads or ships anything.
  static void SetFfmpegPath(plStringView sAbsolutePath);
};
