#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <Foundation/Containers/HybridArray.h>
#include <Foundation/Types/UniquePtr.h>
#include <GuiFoundation/EngineUi/EngineDialog.h>
#include <SequencerPlugin/Capture/SequenceCapture.h>

class plEditorUiProcessView;

/// \brief "Render to Disk" drawn by the engine: the output folder ("..." picks it), the resolution, the frame rate, the
/// video encoder with its codec and quality, whether the PNG frames are kept, and the process block with the progress
/// ("frame n / N") and what the render logs.
///
/// Render starts the render and turns into Cancel. Frames come from the offscreen render and readback the camera-cut
/// filmstrip uses, at the chosen resolution, while the world simulates one fixed step per frame. While it renders the
/// settings, Close and Escape do nothing. The block ends with "Done - n frames." and the audio note, "Cancelled - n
/// frames kept.", or Failed with the error in its log. The output folder is remembered per sequence and user.
///
/// The frames share the engine's one offscreen render slot with the filmstrip: a frame request the engine dropped is
/// sent again, and after a cancel the filmstrip and Render wait for the frame that was still being rendered.
class plSequenceRenderEngineDlg : public plEngineDialog
{
public:
  explicit plSequenceRenderEngineDlg(plSequenceAssetDocument* pDocument);
  ~plSequenceRenderEngineDlg();

  /// Opens the dialog, modal over the window that owns dialogs now.
  static void Open(plSequenceAssetDocument* pDocument);

  /// \name State for tests
  ///@{

  bool IsRendering() const { return m_bRendering; }
  plEditorUiProcessView& GetProcess() { return *m_pProcess; }

  ///@}

protected:
  void BuildBody(plUiElement* pBody, const plVec2& vSize) override;
  void OnButton(plStringView sId) override;
  void OnUiEvent(const plMsgUiEvent& event) override;
  void OnUiUpdate() override;
  plVec2 GetPreferredSize() const override;

private:
  enum class StopReason : plUInt8
  {
    Finished,
    Cancelled,
    Failed,
  };

  /// The dropdowns, for their menus.
  enum class Menu : plUInt8
  {
    Resolution,
    FrameRate,
    Video,
    Codec,
    Quality,
  };

  /// An entry of the Video dropdown: an encoder this machine can run, or the PNG sequence alone (-1).
  struct Encoder
  {
    plString m_sLabel;
    plInt32 m_iKind = -1;
  };

  /// An entry of the Codec dropdown.
  struct Codec
  {
    plString m_sLabel;
    plVideoCodec::Enum m_Codec = plVideoCodec::H264;
  };

  // settings
  void GatherEncoders();
  void GatherCodecs();
  bool IsEncoding() const;
  plInt32 GetEncoderKind() const;
  plVideoCodec::Enum GetSelectedCodec() const;
  plStringView GetCodecToolTip() const;
  plString ReadFolder() const;
  void WriteFolder(plStringView sFolder);
  void RememberFolder();
  void Browse();

  // the body
  void BuildFolderRow(plUiElement* pBody, const plRectFloat& rect);
  void BuildSettings(plUiElement* pBody, plEditorUiForm& ref_form);
  void ShowMenu(Menu menu, const plRectFloat& field, plArrayPtr<const plString> items, plUInt32 uiChecked);
  void OnMenuPicked(Menu menu, plUInt32 uiPicked);
  void UpdateButtons();

  // the render
  void StartRender();
  void RequestNextFrame();
  void ThumbnailArrivedHandler(const plCreateThumbnailMsgToEditor& msg);
  void OnFrameTimeout(plUInt32 uiRequest);
  void RetryFrame();
  void ReleaseFrameSlot();
  void SendRenderMode(bool bEnabled, plUInt32 uiFramesPerSecond);
  void UpdateProgress();
  void FailNoCameraCut(plUInt64 uiTick);
  void StopRender(StopReason reason);
  void FinishProcess(StopReason reason, plStringView sAudioNote);

  /// Mixes the sequence's audio clips to a wav and muxes it into the video when ffmpeg can. Returns the note the done
  /// line ends with.
  plString WriteAndAttachAudio();

  plSequenceAssetDocument* m_pDocument = nullptr;
  plSequenceCapture m_Capture;
  plUniquePtr<plEditorUiProcessView> m_pProcess;
  plString m_sRenderFolder; ///< the folder of the running or last render
  bool m_bRendering = false;
  bool m_bPosed = false; ///< a thumbnail pose is held, so it must be released
  bool m_bFrameInFlight = false; ///< a frame request the engine has not answered, also one left by a cancelled render
  plUInt32 m_uiFrameRequest = 0; ///< counts the requests, so a timeout knows whether its request is still the open one

  plHybridArray<Encoder, 3> m_Encoders;
  plHybridArray<Codec, 4> m_Codecs;
  plUInt32 m_uiResolution = 1;
  plUInt32 m_uiFrameRate = 2;
  plUInt32 m_uiEncoder = 0;
  plUInt32 m_uiCodec = 0;
  plUInt32 m_uiQuality = 2;
  bool m_bKeepFrames = true;

  // the dropdown fields as last built, where their menus open
  plRectFloat m_ResolutionRect = plRectFloat(0, 0, 0, 0);
  plRectFloat m_FrameRateRect = plRectFloat(0, 0, 0, 0);
  plRectFloat m_VideoRect = plRectFloat(0, 0, 0, 0);
  plRectFloat m_CodecRect = plRectFloat(0, 0, 0, 0);
  plRectFloat m_QualityRect = plRectFloat(0, 0, 0, 0);

  plEditorUiTimerGroup m_Timers; ///< the deferred render steps and the frame timeouts; stopped when the dialog goes
};
