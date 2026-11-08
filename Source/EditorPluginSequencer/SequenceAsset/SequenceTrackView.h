#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceModel.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceThumbnailCache.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceWaveformCache.h>
#include <EditorUi/Runtime.h>
#include <Foundation/Containers/HashTable.h>
#include <Foundation/Containers/Set.h>
#include <Foundation/Types/UniquePtr.h>
#include <GuiFoundation/EngineUi/EngineTrackView.h>

#include <functional>

/// \brief The Sequencer's timeline: drives the editor's shared track view (plEditorUiTrackView) from the sequence model.
///
/// One row per binding, track and lane, with its head on the left (twisty, icon, name, path, add button) and its lane
/// on the right: camera cuts with thumbnails and blends, clips with waveforms, colour tracks as gradient bars, curve
/// keys, events and markers. The view handles picking, dragging, snapping and zoom; every edit it reports lands here
/// as one undo step, through the same document commands as before. Rows, colours, menus and commands are the
/// package's own. The view is a content its owner shows (the Sequencer panel); it must outlive this object.
class plSequenceTrackView
{
public:
  explicit plSequenceTrackView(plEditorUiTrackView& ref_view);
  ~plSequenceTrackView();

  enum class SnapMode
  {
    Frame,     ///< snap to whole display frames (default)
    HalfFrame, ///< snap to half frames, for offsetting against the beat
    Free,      ///< no snapping, tick resolution
  };

  void SetModel(plSequenceModel* pModel);
  void SetSnapMode(SnapMode mode);

  /// The engine view, for the panel's menus and for tests.
  plEditorUiTrackView* GetView() const { return m_pView; }

  /// The selected row - its head is highlighted and the curve dock follows it. nullptr when none is.
  const plSequenceModel::FlatRow* GetSelectedRow() const;

  /// Selects the first row showing an object (a vector group before its first axis, never a clip lane).
  void SelectRowFor(const plUuid& objectGuid);

  /// Rebuilds what the view shows from the document.
  void Refresh();

  static constexpr float RowHeight = 24.0f;
  /// The camera cut lane is taller than a track lane so its preview images are legible.
  static constexpr float CameraLaneHeight = 48.0f;
  /// Clip lanes are taller too: a waveform and a label share the bar.
  static constexpr float ClipLaneHeight = 40.0f;
  static constexpr float HeadWidth = 240.0f;

  /// \name Reports to the owner
  ///@{

  std::function<void(plUInt64 uiTick)> m_OnScrubRequested;

  /// \brief A key was clicked or created. Carries the track and control-point document guids.
  std::function<void(const plUuid& trackGuid, const plUuid& cpGuid)> m_OnKeySelected;

  /// The selected row changed (a head was clicked, or a key selected its track).
  std::function<void()> m_OnRowSelectionChanged;

  /// A row's head was right-clicked (it is selected first), or its add button clicked; vPosition is in view units.
  std::function<void(const plVec2& vPosition)> m_OnRowContextMenuRequested;
  std::function<void(const plVec2& vPosition)> m_OnRowAddRequested;

  /// Del after clicking a head: remove that row's binding or track.
  std::function<void()> m_OnRowDeleteRequested;

  ///@}

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
  };

  struct ClipRef
  {
    plUuid m_Guid;
    bool m_bCut = false; ///< a camera cut, else a clip
    plInt64 m_iStart = 0;
    plInt64 m_iDuration = 0;
  };

  plSequenceAssetDocument* GetDocument() const { return m_pModel != nullptr ? m_pModel->GetDocument() : nullptr; }

  void OnModelEvent(const plSequenceModelEvent& e);
  void OnViewEvent(const plEngineTrackViewEvent& e);
  void OnSelectionChanged();
  void OnAddKey(plInt32 iLane, double fTime);
  void OnKeyActivated(plUInt64 uiKey);
  void OnContextMenu(const plEngineTrackViewEvent& e);
  void OnKeyPress(const plEditorUiKeyEvent& key);
  void OnEditClip(plUInt64 uiClip, double fStart, double fDuration);
  void OnLaneEvent(const plEngineTrackViewEvent& e);

  // Ticks are the document's unit; the view works in seconds.
  static double ToSeconds(plInt64 iTick) { return static_cast<double>(iTick) / static_cast<double>(plSequenceTicksPerSecond); }
  static plInt64 ToTicks(double fSeconds) { return static_cast<plInt64>(plMath::Round(fSeconds * static_cast<double>(plSequenceTicksPerSecond))); }
  static plUInt64 IdOf(const plUuid& guid);
  plInt64 SnapTick(plInt64 iTick) const;
  double GetSnapStep() const;

  void BuildContent(plEngineTrackContent& out_content);
  void AddLaneContent(plUInt32 uiLane, plUInt32 uiRow, plEngineTrackContent& ref_content);
  void AddBindingSummary(plUInt32 uiLane, plUInt32 uiRow, plEngineTrackContent& ref_content);
  void AddCameraCuts(plUInt32 uiLane, plEngineTrackContent& ref_content);
  void AddClips(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content);
  void AddEvents(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content);
  void AddColorKeys(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content);
  void AddCurveKeys(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content);
  plString GetThumbnail(double fTime);

  bool IsRowCollapsed(const plSequenceModel::FlatRow& row) const;
  const plSequenceModel::FlatRow* GetRowOfLane(plInt32 iLane) const;

  /// Rows are told apart by object, kind and clip kind: a clip lane shows its owner's object, a vector group its
  /// first axis track.
  bool IsSelected(const plSequenceModel::FlatRow& row) const;
  void SelectRow(const plSequenceModel::FlatRow& row);

  /// Iterates the control points of the track in the given flat row. Callback: (cpGuid, tick, value).
  void ForEachKey(const plSequenceModel::FlatRow& row, plDelegate<void(const plUuid&, plInt64, double)> callback) const;
  void ForEachClip(const plSequenceModel::FlatRow& row, plDelegate<void(const plDocumentObject*)> callback) const;

  /// A colour track is keyed on its gradient, so its keys, drag and delete take a different path.
  bool IsColorTrack(const plUuid& trackGuid) const;

  /// Opens the colour picker on the key at a tick, previewing live through temporary commands.
  void EditColorKey(const plUuid& trackGuid, plInt64 iTick);

  /// Watches for an audio clip's trigger being set, so the clip can take the sample's length.
  void DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e);

  void GetSelectedKeyRefs(plDynamicArray<KeyRef>& out_keys) const;
  void SelectKeyIds(plArrayPtr<const plUInt64> keys);
  void SelectAllKeys();
  void MoveKeyTo(KeyRef& ref_key, plInt64 iTo, plCommandHistory* pHistory);
  void MoveSelectedKeys(plInt64 iDeltaTicks);

  /// Retime: scales every selected key's distance from the pivot, so a whole beat stretches or
  /// compresses without its internal shape changing.
  void ScaleSelectedKeys(plInt64 iPivotTick, double fFactor);

  /// Ease presets - plCurveTangentMode values applied to both sides of every selected curve key.
  void SetSelectedKeyTangents(plInt64 iLeftMode, plInt64 iRightMode);

  void DeleteSelectedKeys();
  void CopySelectedKeys();
  void PasteKeysAtPlayhead();
  void RemoveObject(const char* szUndoName, const plUuid& guid);
  void AddMarkerAt(plInt64 iTick);
  void RenameMarker(const plUuid& markerGuid);
  void RenameEvent(const KeyRef& key);
  void FitClipToTrigger(const plUuid& clipGuid);

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

  plEditorUiTrackView* m_pView = nullptr;
  plSequenceModel* m_pModel = nullptr;
  plUniquePtr<plSequenceThumbnailCache> m_pThumbnails;
  SnapMode m_SnapMode = SnapMode::Frame;

  /// Peak envelopes behind audio clips. Owned here because it is purely presentational - nothing in
  /// the document or the runtime depends on it.
  mutable plSequenceWaveformCache m_Waveforms;

  // what the view's ids stand for, rebuilt with the content
  plDynamicArray<plUInt32> m_LaneRows; ///< lane -> flat row
  plHashTable<plUInt64, KeyRef> m_Keys;
  plHashTable<plUInt64, ClipRef> m_Clips;
  plHashTable<plUInt64, plUuid> m_Markers;
  plHashTable<plUInt32, plString> m_ThumbnailTextures; ///< thumbnail serial -> texture resource

  plSet<plUuid> m_Collapsed; ///< bindings and vector groups whose rows are folded
  plUuid m_SelectedRow;      ///< the object of the highlighted row
  plSequenceModel::RowKind m_SelectedKind = plSequenceModel::RowKind::Track;
  plUInt8 m_uiSelectedClipKind = 0;
  bool m_bHeadFocus = false; ///< a head was clicked last, so Del removes its row

  plHybridArray<CopiedKey, 16> m_Clipboard;

  plEditorUiTimerGroup m_Timers; ///< work that waits until the current transaction is over
};
