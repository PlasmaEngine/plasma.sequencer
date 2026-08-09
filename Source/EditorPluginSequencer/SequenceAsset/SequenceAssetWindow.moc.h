#pragma once

#include <EditorFramework/DocumentWindow/GameObjectDocumentWindow.moc.h>
#include <EditorFramework/EditTools/EditTool.h>
#include <EditorPluginSequencer/EditorPluginSequencerDLL.h>
#include <GuiFoundation/Widgets/CurveEditData.h>

class plSequenceAssetDocument;
class plQtQuadViewWidget;
class plQtSequencerPanel;
class plQtCurve1DEditorWidget;
class plQtEventTrackEditorWidget;
struct plSequenceAssetEvent;
struct plCommandHistoryEvent;

/// \brief Document window for Sequence assets.
///
/// P0 skeleton: viewport of the attached context scene, scenegraph, property grid, and an empty
/// "Sequencer" panel where the binding outliner + plQtSequenceTrackView timeline land in P1
/// (Documentation/Sequencer/Mockup_MainWindow.html shows the target layout).
class plQtSequenceAssetDocumentWindow : public plQtGameObjectDocumentWindow, public plGameObjectGizmoInterface
{
  Q_OBJECT

public:
  plQtSequenceAssetDocumentWindow(plSequenceAssetDocument* pDocument);
  ~plQtSequenceAssetDocumentWindow();

public:
  /// \name plGameObjectGizmoInterface implementation
  ///@{
  virtual plObjectAccessorBase* GetObjectAccessor() override;
  virtual bool CanDuplicateSelection() const override { return false; }
  virtual void DuplicateSelection() override {}
  ///@}

protected:
  virtual void InternalRedraw() override;

private Q_SLOTS:
  // Curve editor dock - single-curve version of PropertyAnim's wiring: curve index is always 0,
  // the track is whatever the sequencer panel's outliner has selected.
  void onCurveInsertCpAt(plUInt32 uiCurveIdx, plInt64 iTickX, double fClickPosY);
  void onCurveCpMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, plInt64 iTickX, double fNewPosY);
  void onCurveCpDeleted(plUInt32 uiCurveIdx, plUInt32 uiCpIdx);
  void onCurveTangentMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, float fNewPosX, float fNewPosY, bool bRightTangent);
  void onLinkCurveTangents(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bLink);
  void onCurveTangentModeChanged(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bRightTangent, int iMode);
  void onCurveBeginOperation(QString sName);
  void onCurveEndOperation(bool bCommit);
  void onCurveBeginCpChanges(QString sName);
  void onCurveEndCpChanges();
  void onTrackSelectionChanged();
  void onKeyPicked(plUuid trackGuid, plUuid cpGuid);

  // Event track dock - sequence-level, so it needs no selection
  void onEventTrackInsertCpAt(plInt64 iTickX, QString sValue);
  void onEventTrackCpMoved(plUInt32 uiCpIdx, plInt64 iTickX);
  void onEventTrackCpDeleted(plUInt32 uiCpIdx);
  void onEventTrackBeginCpChanges(QString sName);
  void onEventTrackEndCpChanges();
  void onEventTrackBeginOperation(QString sName);
  void onEventTrackEndOperation(bool bCommit);

private:
  void SequenceEventHandler(const plSequenceAssetEvent& e);
  void CommandHistoryEventHandler(const plCommandHistoryEvent& e);
  void UpdateCurveEditor();
  void UpdateEventTrackEditor();

  /// The event track control-point document object at an index.
  plUuid GetEventCpGuid(plUInt32 uiIndex) const;

  /// The track behind a displayed curve. A vector group shows several at once, so the curve index
  /// the widget reports is what identifies the track, not the outliner selection.
  plUuid GetCurveTrackGuid(plUInt32 uiCurveIdx) const;

  /// Resolves that curve's ControlPoints[cpIdx] document object guid, for the curve slots.
  plUuid GetCurveCpGuid(plUInt32 uiCurveIdx, plUInt32 uiCpIdx) const;

  plQtQuadViewWidget* m_pQuadViewWidget = nullptr;
  plQtSequencerPanel* m_pSequencerPanel = nullptr;
  plQtCurve1DEditorWidget* m_pCurveEditor = nullptr;
  plQtEventTrackEditorWidget* m_pEventTrackEditor = nullptr;
  plCurveGroupData m_CurvesToDisplay;
  /// Parallel to m_CurvesToDisplay.m_Curves: which track each displayed curve came from.
  plHybridArray<plUuid, 4> m_CurveTrackGuids;
  plUuid m_SelectedTrackGuid;
  plInt8 m_iPilotResolved = -1; ///< -1 = not evaluated yet, so the first result is always reported
};
