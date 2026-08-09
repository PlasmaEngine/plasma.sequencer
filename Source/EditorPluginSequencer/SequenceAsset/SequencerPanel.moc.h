#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceModel.moc.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceTrackView.moc.h>
#include <QWidget>

class QTreeView;
class QToolButton;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class plQtSearchableMenu;

/// \brief The Sequencer dock content: transport toolbar, binding outliner and the timeline canvas.
class plQtSequencerPanel : public QWidget
{
  Q_OBJECT

public:
  plQtSequencerPanel(plSequenceAssetDocument* pDocument, QWidget* pParent);
  ~plQtSequencerPanel();

  /// \brief The track document object currently selected in the outliner, or an invalid guid.
  plUuid GetSelectedTrack() const;

  /// \brief The per-axis tracks of the selected row when it is a vector group, else empty. Lets the
  /// curve dock show Position as three curves rather than only the axis the group answers as.
  void GetSelectedAxisTracks(plHybridArray<plUuid, 4>& out_tracks) const;

Q_SIGNALS:
  /// \brief Fired when the outliner's current row changes. Query GetSelectedTrack().
  void TrackSelectionChanged();

  /// \brief A key was picked in the timeline canvas, after the outliner selection was moved to its
  /// track - so the curve editor already shows the right curve when this arrives.
  void KeyPicked(plUuid trackGuid, plUuid cpGuid);

private Q_SLOTS:
  void onAddBinding();
  void onAddTrack();
  void onPlayPause();
  void onRepeat();
  void onDurationChanged(double fDuration);
  void onGoToStart();
  void onGoToEnd();
  void onStepFrame(int iDirection);
  void onSnapModeChanged(int iIndex);
  void onDeleteSelected();
  void onOutlinerContextMenu(const QPoint& pos);

private:
  void SequenceEventHandler(const plSequenceAssetEvent& e);
  QWidget* CreateTransportBar();
  void UpdateTransportState();

  /// The binding the "Add Track" button targets: the selected binding row, or the parent of the
  /// selected track row. Invalid if nothing suitable is selected.
  plUuid GetTargetBinding() const;

  plUuid GuidFromIndex(const QModelIndex& index) const;

  /// Finds the model index of a binding or track row by its document object guid.
  QModelIndex FindIndexForGuid(const plUuid& guid) const;

  /// Builds the searchable "Property..." picker: every animatable property of the bound object and
  /// its descendants, grouped by object and component, found by reflection.
  void ShowPropertyPicker(const plUuid& bindingGuid, const QPoint& globalPos);

  /// Collects animatable properties under a context object into a searchable menu.
  void CollectAnimatableProperties(plQtSearchableMenu* pMenu, const plDocumentObject* pObject, const plStringBuilder& sPathPrefix);

  /// Lists the context scene's cameras, adding a cut at the playhead for whichever is chosen.
  void BuildCameraCutMenu(QMenu& ref_menu);

  /// Points a binding at whatever is selected in the scenegraph, keeping its tracks and clips.
  void RebindToSelection(const plUuid& bindingGuid);

  plSequenceAssetDocument* m_pDocument = nullptr;
  plQtSequenceModel* m_pModel = nullptr;
  QTreeView* m_pOutliner = nullptr;
  plQtSequenceTrackView* m_pTrackView = nullptr;

  // transport bar
  QToolButton* m_pPlayButton = nullptr;
  QToolButton* m_pLoopButton = nullptr;
  QToolButton* m_pAutoKeyButton = nullptr;
  QToolButton* m_pPilotButton = nullptr;
  QLabel* m_pTimecodeLabel = nullptr;
  QLabel* m_pFpsLabel = nullptr;
  QComboBox* m_pSnapCombo = nullptr;
  QDoubleSpinBox* m_pDurationSpin = nullptr;
  QToolButton* m_pAddBindingButton = nullptr;
  QToolButton* m_pAddTrackButton = nullptr;
  plUuid m_LastSelectedGuid; ///< restored after model rebuilds, which reset the tree's current index
};
