#pragma once

#include <EditorFramework/DocumentWindow/GameObjectDocumentWindow.h>
#include <EditorFramework/EditTools/EditTool.h>
#include <EditorPluginSequencer/EditorPluginSequencerDLL.h>
#include <GuiFoundation/EngineUi/EngineCurveEditor.h>
#include <GuiFoundation/Widgets/CurveEditData.h>

class plSequenceAssetDocument;
class plSequencerPanel;
class plEditorUiEventTrackView;
struct plSequenceAssetEvent;
struct plCommandHistoryEvent;

/// \brief Document window for Sequence assets.
///
/// The viewport of the attached context scene (one or four views), its scene tree ("SceneTree"), the Sequencer panel
/// ("Sequencer": transport, the track view whose head column is the binding outliner, status row), the curve editor of
/// the selected track ("Curves") and the sequence's event track ("Events"). The editors' edits become undoable
/// document commands.
class plSequenceAssetDocumentWindow : public plGameObjectDocumentWindow, public plGameObjectGizmoInterface, private plEditorUiCurveEdits
{
public:
  plSequenceAssetDocumentWindow(plSequenceAssetDocument* pDocument);
  ~plSequenceAssetDocumentWindow();

public:
  /// \name plGameObjectGizmoInterface implementation
  ///@{
  virtual plObjectAccessorBase* GetObjectAccessor() override;
  virtual bool CanDuplicateSelection() const override { return false; }
  virtual void DuplicateSelection() override {}
  ///@}

protected:
  virtual void InternalRedraw() override;

private:
  void CreatePanels(plSequenceAssetDocument* pDocument);
  void ConnectEventTrackEditor();

  /// \name plEditorUiCurveEdits: the curve editor's edits. A single-curve version of PropertyAnim's: the curve index
  /// picks the track, which is whatever the sequencer panel's outliner has selected.
  ///@{
  void InsertCp(plUInt32 uiCurveIdx, plInt64 iTickX, double fClickPosY) override;
  void CpMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, plInt64 iTickX, double fNewPosY) override;
  void CpDeleted(plUInt32 uiCurveIdx, plUInt32 uiCpIdx) override;
  void TangentMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, float fNewPosX, float fNewPosY, bool bRightTangent) override;
  void TangentLink(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bLink) override;
  void CpTangentMode(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bRightTangent, int iMode) override;
  void BeginOperation(plStringView sName) override;
  void EndOperation(bool bCommit) override;
  void BeginCpChanges(plStringView sName) override;
  void EndCpChanges() override;
  ///@}

  void OnTrackSelectionChanged();
  void OnKeyPicked(const plUuid& trackGuid, const plUuid& cpGuid);

  /// \name The event track editor's edits; the event track is sequence-level, so it needs no selection
  ///@{
  void OnEventTrackInsertCpAt(plInt64 iTickX, plStringView sValue);
  void OnEventTrackCpMoved(plUInt32 uiCpIdx, plInt64 iTickX);
  void OnEventTrackCpDeleted(plUInt32 uiCpIdx);
  void OnEventTrackBeginCpChanges(plStringView sName);
  void OnEventTrackEndCpChanges();
  void OnEventTrackBeginOperation(plStringView sName);
  void OnEventTrackEndOperation(bool bCommit);
  ///@}

  void SequenceEventHandler(const plSequenceAssetEvent& e);
  void CommandHistoryEventHandler(const plCommandHistoryEvent& e);
  void UpdateCurveEditor();
  void UpdateEventTrackEditor();

  /// The event track control-point document object at an index.
  plUuid GetEventCpGuid(plUInt32 uiIndex) const;

  /// The track behind a displayed curve. A vector group shows several at once, so the curve index
  /// the editor reports is what identifies the track, not the outliner selection.
  plUuid GetCurveTrackGuid(plUInt32 uiCurveIdx) const;

  /// Resolves that curve's ControlPoints[cpIdx] document object guid, for the curve edits.
  plUuid GetCurveCpGuid(plUInt32 uiCurveIdx, plUInt32 uiCpIdx) const;

  plSequencerPanel* m_pSequencerPanel = nullptr;
  plEditorUiCurveView* m_pCurveEditor = nullptr;
  plEditorUiEventTrackView* m_pEventTrackEditor = nullptr;
  plCurveGroupData m_CurvesToDisplay;
  /// Parallel to m_CurvesToDisplay.m_Curves: which track each displayed curve came from.
  plHybridArray<plUuid, 4> m_CurveTrackGuids;
  plUuid m_SelectedTrackGuid;
  plInt8 m_iPilotResolved = -1; ///< -1 = not evaluated yet, so the first result is always reported
};
