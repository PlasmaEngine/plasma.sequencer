#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceModel.moc.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceThumbnailCache.moc.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceWaveformCache.h>
#include <QWidget>

/// \brief The custom-painted multi-lane timeline canvas - the one genuinely new widget of the
/// Sequencer (Documentation/Sequencer/Mockup_MainWindow.html).
///
/// Paints one lane per flattened outliner row (bindings as header lanes, tracks as key lanes),
/// a time ruler, grid lines, key diamonds from each track's float curve, and the shared playhead.
/// P1 interactions: click to scrub (frame-snapped), click to select a key, drag to move it,
/// double-click to insert one, Del to remove - every edit through the command history.
class plQtSequenceTrackView : public QWidget
{
  Q_OBJECT

public:
  explicit plQtSequenceTrackView(QWidget* pParent);
  ~plQtSequenceTrackView();

  enum class SnapMode
  {
    Frame,     ///< snap to whole display frames (default)
    HalfFrame, ///< snap to half frames, for offsetting against the beat
    Free,      ///< no snapping, tick resolution
  };

  void SetModel(plQtSequenceModel* pModel);
  void SetVerticalOffset(int iOffset);
  void SetSnapMode(SnapMode mode) { m_SnapMode = mode; }

  /// \brief The track whose lane is tinted, mirroring the outliner selection.
  void SetHighlightedTrack(const plUuid& trackGuid);

  static constexpr int RulerHeight = 24;
  static constexpr int RowHeight = 24;
  /// The camera cut lane is taller than a track lane so its preview images are legible.
  static constexpr int CameraLaneHeight = 48;
  /// Clip lanes are taller too: a waveform and a label have to share the bar without colliding.
  static constexpr int ClipLaneHeight = 40;
  /// The band at the top of a clip bar reserved for its name, above the waveform.
  static constexpr int LabelStripHeight = 14;

Q_SIGNALS:
  void ScrubRequested(plUInt64 uiTick);

  /// \brief A key was clicked or created. Carries the track and control-point document guids.
  void KeySelected(plUuid trackGuid, plUuid cpGuid);

  /// \brief The wheel asked to scroll the lanes by this many pixels. The canvas does not own the
  /// scroll position - the outliner's scrollbar does, and it drives both - so this is a request.
  void ScrollByRequested(int iDeltaPixels);

private Q_SLOTS:
  // Old-style SLOT() connections, because that is the interface plQtUiServices::ShowColorDialog offers.
  void onColorPickerChanged(const plColor& color);
  void onColorPickerAccepted();
  void onColorPickerRejected();

protected:
  virtual void paintEvent(QPaintEvent* e) override;
  virtual void mousePressEvent(QMouseEvent* e) override;
  virtual void mouseMoveEvent(QMouseEvent* e) override;
  virtual void mouseReleaseEvent(QMouseEvent* e) override;
  virtual void mouseDoubleClickEvent(QMouseEvent* e) override;
  virtual void keyPressEvent(QKeyEvent* e) override;
  virtual void contextMenuEvent(QContextMenuEvent* e) override;
  virtual void wheelEvent(QWheelEvent* e) override;

private:
  struct KeyRef
  {
    plUuid m_TrackGuid;
    plUuid m_CpGuid;
    plInt64 m_iTick = 0;

    /// Non-empty when the key was picked on a vector group row: every axis it stands for, so moving
    /// and deleting reach all of them and not just the axis that happened to own the identity.
    plHybridArray<plUuid, 4> m_AxisTracks;

    bool IsValid() const { return m_CpGuid.IsValid(); }
    void Invalidate()
    {
      m_CpGuid = plUuid();
      m_AxisTracks.Clear();
    }
  };

  plSequenceAssetDocument* GetDocument() const { return m_pModel != nullptr ? m_pModel->GetDocument() : nullptr; }

  double PixelsPerTick() const;
  plInt64 TickAtPixel(int x) const;
  int PixelAtTick(plInt64 iTick) const;
  plInt64 SnapTick(plInt64 iTick) const;
  int RowAtPixel(int y) const;

  // Rows are not all the same height - the camera cut lane is taller to fit its preview images -
  // so lane geometry goes through these rather than multiplying by a constant.
  int RowHeightAt(plUInt32 uiRow) const;
  int RowTop(plUInt32 uiRow) const;

  /// Iterates the control points of the track in the given flat row. Callback: (cpGuid, tick, value).
  void ForEachKey(const plQtSequenceModel::FlatRow& row, plDelegate<void(const plUuid&, plInt64, double)> callback) const;

  KeyRef PickKey(const QPoint& pos) const;

  /// A colour track is keyed on its gradient, so its keys, drag and delete take a different path.
  bool IsColorTrack(const plUuid& trackGuid) const;

  /// Opens the colour picker on the key at a tick, previewing live through temporary commands.
  void EditColorKey(const plUuid& trackGuid, plInt64 iTick);

  /// Watches for an audio clip's trigger being set, so the clip can take the sample's length.
  void DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e);

  plQtSequenceModel* m_pModel = nullptr;
  int m_iVerticalOffset = 0;
  plUuid m_HighlightedTrack;
  /// A clip under the cursor, and which part of it - the edges trim, the body moves.
  struct ClipRef
  {
    plUuid m_OwnerGuid; ///< the binding the clip belongs to (invalid = sequence level)
    plUuid m_ClipGuid;
    plInt64 m_iStart = 0;
    plInt64 m_iDuration = 0;

    bool IsValid() const { return m_ClipGuid.IsValid(); }
    void Invalidate() { m_ClipGuid = plUuid(); }
  };

  enum class ClipDrag
  {
    None,
    Move,
    TrimStart,
    TrimEnd,
  };

  ClipRef PickClip(const QPoint& pos, ClipDrag& out_mode) const;
  void ForEachClip(const plQtSequenceModel::FlatRow& row, plDelegate<void(const plDocumentObject*)> callback) const;

  /// A camera cut, in time order. Cuts are a sequence-level array, not a per-row one, so painting
  /// and picking both go through the same collected list rather than re-reading the document twice.
  struct CutRef
  {
    plUuid m_CutGuid;
    plInt64 m_iStart = 0;
    plInt64 m_iDuration = 0;
    QString m_sName;
    bool m_bUnresolved = false;

    bool IsValid() const { return m_CutGuid.IsValid(); }
    void Invalidate() { m_CutGuid = plUuid(); }
  };

  void CollectCameraCuts(plHybridArray<CutRef, 8>& out_cuts) const;
  CutRef PickCut(const QPoint& pos, ClipDrag& out_mode) const;

  plUuid m_SelectedCut;
  CutRef m_DraggedCut;
  ClipDrag m_CutDragMode = ClipDrag::None;
  int m_iCutDragStartX = 0;
  plInt64 m_iCutPreviewStart = 0;
  plInt64 m_iCutPreviewDuration = 0;
  bool m_bCutDragActive = false;

  plUuid m_SelectedClip;
  ClipRef m_DraggedClip;
  ClipDrag m_ClipDragMode = ClipDrag::None;
  int m_iClipDragStartX = 0;
  plInt64 m_iClipPreviewStart = 0;
  plInt64 m_iClipPreviewDuration = 0;
  bool m_bClipDragActive = false;
  SnapMode m_SnapMode = SnapMode::Frame;
  plQtSequenceThumbnailCache* m_pThumbnails = nullptr;

  /// Peak envelopes behind audio clips. Owned here because it is purely presentational - nothing in
  /// the document or the runtime depends on it.
  mutable plSequenceWaveformCache m_Waveforms;

  // the colour key the picker is currently open on
  plUuid m_ColorEditTrack;
  plInt64 m_iColorEditTick = 0;
  plColor m_ColorEditValue = plColor::White;

  /// The key the curve editor mirrors and the drag is anchored on. Always also in m_SelectedKeys.
  KeyRef m_SelectedKey;

  /// Every selected key. Moving, deleting and copying act on all of them, so a shot can be retimed
  /// without dragging one control point at a time.
  plHybridArray<KeyRef, 16> m_SelectedKeys;

  bool IsKeySelected(const plUuid& cpGuid) const;
  void SelectKey(const KeyRef& key, bool bToggle);
  void SelectKeysInRect(const QRect& rect);
  void MoveKeyTo(KeyRef& ref_key, plInt64 iTo, plCommandHistory* pHistory);
  void MoveSelectedKeys(plInt64 iDeltaTicks);

  /// Retime: scales every selected key's distance from the pivot, so a whole beat stretches or
  /// compresses without its internal shape changing.
  void ScaleSelectedKeys(plInt64 iPivotTick, double fFactor);

  /// Ease presets - plCurveTangentMode values applied to both sides of every selected curve key.
  void SetSelectedKeyTangents(plInt64 iLeftMode, plInt64 iRightMode);

  /// The tick range the selection spans. False when fewer than two keys are selected, which is
  /// when retiming means nothing.
  bool GetSelectionRange(plInt64& out_iMin, plInt64& out_iMax) const;

  // Retime handles, drawn in a band under the ruler whenever a range is selected.
  enum class RetimeDrag
  {
    None,
    Start,
    End,
  };

  RetimeDrag PickRetimeHandle(const QPoint& pos) const;

  RetimeDrag m_RetimeDrag = RetimeDrag::None;
  plInt64 m_iRetimePivot = 0;
  plInt64 m_iRetimeGrabbed = 0; ///< tick of the handle being dragged, at grab time
  double m_fRetimePreviewFactor = 1.0;

  static constexpr int RetimeBandHeight = 9;
  void DeleteSelectedKeys();
  void CopySelectedKeys();
  void PasteKeysAtPlayhead();

  /// One copied key: which track it came from and how far it sat from the earliest key in the
  /// copy, so a paste lands as a group at the playhead rather than back where it was cut from.
  struct CopiedKey
  {
    plUuid m_TrackGuid;
    plInt64 m_iOffsetTicks = 0;
    double m_fValue = 0.0;
    plString m_sEvent; ///< event keys carry a name instead of a value
    bool m_bIsEvent = false;
  };
  plHybridArray<CopiedKey, 16> m_Clipboard;

  bool m_bDraggingKey = false;
  bool m_bDragActive = false; ///< true once the mouse moved far enough to count as a drag
  int m_iDragStartX = 0;
  plInt64 m_iDragStartTick = 0;
  plInt64 m_iDragPreviewTick = 0;
  plInt64 m_iDragDeltaTicks = 0; ///< applied to every selected key while dragging

  // Box select: drag on empty lane space to gather keys, instead of scrubbing.
  bool m_bBoxSelecting = false;
  QPoint m_BoxSelectStart;
  QPoint m_BoxSelectEnd;

  // Markers live on the ruler, so they are picked and dragged there rather than in the lanes.
  plUuid PickMarker(const QPoint& pos) const;

  plUuid m_SelectedMarker;
  bool m_bMarkerDragActive = false;
  plInt64 m_iMarkerPreviewTick = 0;

  bool m_bScrubbing = false;
};
