#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <SequencerPlugin/Capture/SequenceCapture.h>

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QCheckBox;

/// \brief The "Render to Disk" dialog: collects settings, then drives the render frame by frame.
///
/// Frames come from the same offscreen render + readback path the camera-cut filmstrip uses, just
/// at the requested resolution instead of thumbnail size. The clock belongs to plSequenceCapture,
/// so how long a frame takes to come back never affects the output timing.
///
/// The world <b>simulates</b> while this renders: the engine steps it exactly one fixed 1/fps step
/// per captured frame, so particles, physics and animation are in the output rather than frozen.
/// That is why a render must start at the beginning of the sequence - a sub-range would begin with
/// no simulation history behind it, and effects would pop into existence.
///
/// Simulating leaves the preview world holding state the document never described, so the context
/// scene is re-cloned when the render ends.
///
/// Audio clips are mixed offline and muxed in afterwards. Animation and sub-sequence clips are
/// still not dispatched - those are driven by plSequencePlayerComponent, which does not exist in
/// the preview world; see Documentation/Sequencer/Plan.html §13.
class plQtSequenceRenderDialog : public QDialog
{
  Q_OBJECT

public:
  plQtSequenceRenderDialog(plSequenceAssetDocument* pDocument, QWidget* pParent);
  ~plQtSequenceRenderDialog();

private Q_SLOTS:
  void onBrowse();
  void onStartOrCancel();

  /// Refills the codec list for the selected backend: an ffmpeg build without libx265 must not
  /// offer H.265, or the render fails at the very end.
  void onEncoderChanged();

private:
  void ThumbnailArrivedHandler(const plCreateThumbnailMsgToEditor& msg);

  /// Poses the scene at the harness's current tick and asks the engine for one frame.
  void RequestNextFrame();

  /// Tells the engine process to simulate the world while capturing, at the output frame rate.
  void SendRenderMode(bool bEnabled, plUInt32 uiFramesPerSecond);

  void UpdateProgress();

  /// Mixes the sequence's audio clips to a wav and muxes it into the video when ffmpeg can.
  /// Returns a short status suffix for the user.
  QString WriteAndAttachAudio();

  /// The codec currently chosen, or H.264 when the codec list is empty.
  plVideoCodec::Enum SelectedCodec() const;
  void StopRender(bool bCancelled);
  void SetInputsEnabled(bool bEnabled);

  plSequenceAssetDocument* m_pDocument = nullptr;
  plSequenceCapture m_Capture;
  bool m_bRendering = false;
  bool m_bPosed = false; ///< a thumbnail pose is currently held, so it must be released

  QLineEdit* m_pFolder = nullptr;
  QComboBox* m_pResolution = nullptr;
  QComboBox* m_pFps = nullptr;
  QComboBox* m_pEncoder = nullptr;
  QComboBox* m_pCodec = nullptr;
  QComboBox* m_pQuality = nullptr;
  QCheckBox* m_pWriteFrames = nullptr;
  QProgressBar* m_pProgress = nullptr;
  QLabel* m_pStatus = nullptr;
  QPushButton* m_pStart = nullptr;
  QPushButton* m_pClose = nullptr;
};
