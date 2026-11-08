#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceModel.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceTrackView.h>
#include <EditorUi/Content.h>
#include <EditorUi/Menu.h>
#include <EditorUi/SearchMenu.h>
#include <Foundation/Types/UniquePtr.h>

#include <functional>

class plEditorUiControlStrip;

/// \brief The Sequencer panel: the transport strip, the track view, whose head column is the binding outliner, and the
/// status row below it. All engine-drawn; menus are engine menus.
class plSequencerPanel : public plEditorUiContent
{
public:
  explicit plSequencerPanel(plSequenceAssetDocument* pDocument);
  ~plSequencerPanel();

  /// \brief The track document object of the selected row, or an invalid guid (nothing, or a binding or lane row).
  plUuid GetSelectedTrack() const;

  /// \brief The per-axis tracks of the selected row when it is a vector group, else empty. Lets the
  /// curve dock show Position as three curves rather than only the axis the group answers as.
  void GetSelectedAxisTracks(plHybridArray<plUuid, 4>& out_tracks) const;

  plSequenceTrackView* GetTrackView() const { return m_pTrackView.Borrow(); }
  plEditorUiControlStrip* GetTransport() const { return m_pTransport; }
  plEditorUiControlStrip* GetStatus() const { return m_pStatus; }

  /// \brief Called when the selected row changes. Query GetSelectedTrack().
  std::function<void()> m_OnTrackSelectionChanged;

  /// \brief A key was picked in the timeline, after its track's row was selected - so the curve editor already shows
  /// the right curve when this arrives.
  std::function<void(const plUuid& trackGuid, const plUuid& cpGuid)> m_OnKeyPicked;

protected:
  void OnUiUpdate() override;

private:
  void SequenceEventHandler(const plSequenceAssetEvent& e);
  void OnModelEvent(const plSequenceModelEvent& e);
  void CreateTransport();
  void ConnectTrackView();
  void OnTransport(plUInt32 uiControl, plInt32 iValue);
  void UpdateTransportState();
  void ReportTrackSelection();

  /// The status row under the lanes: what is selected, and the keys the track view answers to.
  void CreateStatusRow();
  void OnTrackViewEvent(const plEngineTrackViewEvent& e);
  void UpdateSelectionStatus();

  void OnAddBinding();
  void OnDeleteSelected();
  void OnStepFrame(int iDirection);

  /// The menu of a row's head: cut lane, clip lane, binding or track.
  void ShowRowMenu(const plVec2& vPosition);

  /// The add-track menu of the target binding, at a position in the track view.
  void ShowAddTrackMenu(const plVec2& vPosition);

  /// What can be added: vector tracks, properties and clip lanes of the target binding, or sequence lanes.
  void BuildAddTrackMenu(plEditorUiMenu& ref_menu, const plUuid& bindingGuid);
  void OnAddTrackPicked(plUInt32 uiId, const plUuid& bindingGuid, const plVec2& vPickerPosition);

  /// The binding "Add Track" targets: the selected binding row, or the parent of the selected track row. Invalid if
  /// nothing suitable is selected.
  plUuid GetTargetBinding() const;

  /// The searchable property picker: every animatable property of the bound object and its descendants, grouped by
  /// object and component, found by reflection.
  void ShowPropertyPicker(const plUuid& bindingGuid, const plVec2& vPosition);
  void CollectAnimatableProperties(const plDocumentObject* pObject, const plStringBuilder& sPathPrefix);

  /// Lists the context scene's cameras under ref_item, adding a cut at the playhead for whichever is chosen.
  void BuildCameraCutMenu(plEditorUiMenu& ref_menu);

  /// Points a binding at whatever is selected in the scenegraph, keeping its tracks and clips.
  void RebindToSelection(const plUuid& bindingGuid);

  plSequenceAssetDocument* m_pDocument = nullptr;
  plUniquePtr<plSequenceModel> m_pModel;
  plUniquePtr<plSequenceTrackView> m_pTrackView;
  plEditorUiTrackView* m_pTimeline = nullptr;    ///< a child content, which m_pTrackView drives
  plEditorUiControlStrip* m_pTransport = nullptr; ///< a child content
  plEditorUiControlStrip* m_pStatus = nullptr;    ///< a child content
  plUInt32 m_uiSelection = 0;

  // transport controls
  plUInt32 m_uiAddBinding = 0;
  plUInt32 m_uiAddTrack = 0;
  plUInt32 m_uiToStart = 0;
  plUInt32 m_uiPrevFrame = 0;
  plUInt32 m_uiPlay = 0;
  plUInt32 m_uiNextFrame = 0;
  plUInt32 m_uiToEnd = 0;
  plUInt32 m_uiLoop = 0;
  plUInt32 m_uiAutoKey = 0;
  plUInt32 m_uiPilot = 0;
  plUInt32 m_uiRender = 0;
  plUInt32 m_uiTime = 0;
  plUInt32 m_uiFrame = 0;
  plUInt32 m_uiTotal = 0;
  plUInt32 m_uiFps = 0;
  plUInt32 m_uiSnap = 0;
  plUInt32 m_uiDuration = 0;

  // what the open menus and the property picker stand for, by item id
  struct PickedProperty
  {
    plUuid m_OwnerGuid;
    plString m_sProperty;
    plPropertyAnimTarget::Enum m_Target;
  };

  plDynamicArray<plEditorUiSearchMenuItem> m_PickerItems;
  plDynamicArray<PickedProperty> m_PickerProperties;
  plDynamicArray<const plDocumentObject*> m_MenuCameras;
  plDynamicArray<plString> m_MenuTriggers;
};
