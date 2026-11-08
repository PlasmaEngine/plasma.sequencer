#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <Core/World/Component.h>
#include <Core/World/GameObject.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceRenderEngineDlg.h>
#include <EditorPluginSequencer/SequenceAsset/SequencerPanel.h>
#include <EditorUi/Surface.h>
#include <GuiFoundation/EngineUi/EngineUiControlStrip.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  // menu ids: the add-track menu, then the rows' menus
  enum MenuId : plUInt32
  {
    AddPosition = 1,
    AddRotation,
    AddScale,
    AddProperty,
    AddLane = 10,         ///< + plSequenceClipKind: a clip lane on the binding
    AddSequenceLane = 20, ///< + plSequenceClipKind: a sequence-level clip lane
    CutToCamera = 100,    ///< + camera index
    AddTrigger = 1000,    ///< + trigger index
    AddClipHere = 2000,
    RemoveLane,
    AddTrackMenu,
    Rebind,
    RemoveRow,
  };

  constexpr const char* s_Snaps[] = {"Frame", "Half Frame", "Free"};

  /// "mm:ss.mmm"
  plString FormatTimecode(plUInt64 uiTicks)
  {
    const plUInt64 uiMilliseconds = (uiTicks * 1000 + plSequenceTicksPerSecond / 2) / plSequenceTicksPerSecond;
    const plUInt32 uiMinutes = static_cast<plUInt32>(uiMilliseconds / 60000);
    const plUInt32 uiSeconds = static_cast<plUInt32>((uiMilliseconds / 1000) % 60);
    const plUInt32 uiRest = static_cast<plUInt32>(uiMilliseconds % 1000);

    plStringBuilder s;
    s.SetFormat("{}:{}.{}", plArgU(uiMinutes, 2, true), plArgU(uiSeconds, 2, true), plArgU(uiRest, 3, true));
    return s;
  }
} // namespace

plSequencerPanel::plSequencerPanel(plSequenceAssetDocument* pDocument)
  : plEditorUiContent("SequencerPanel")
  , m_pDocument(pDocument)
{
  m_pModel = PL_DEFAULT_NEW(plSequenceModel, pDocument);

  // The transport is controls only; the track view's ruler is the single source of time, lined up with the lanes.
  CreateTransport();

  // the track view, whose head column is the binding outliner
  m_pTimeline = static_cast<plEditorUiTrackView*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiTrackView, "SequencerTrackView"))));
  m_pTrackView = PL_DEFAULT_NEW(plSequenceTrackView, *m_pTimeline);
  m_pTrackView->SetModel(m_pModel.Borrow());
  ConnectTrackView();

  CreateStatusRow();

  m_pModel->m_Events.AddEventHandler(plMakeDelegate(&plSequencerPanel::OnModelEvent, this));
  m_pDocument->m_SequenceEvents.AddEventHandler(plMakeDelegate(&plSequencerPanel::SequenceEventHandler, this));

  UpdateTransportState();
}

plSequencerPanel::~plSequencerPanel()
{
  m_pTimeline->m_Events.RemoveEventHandler(plMakeDelegate(&plSequencerPanel::OnTrackViewEvent, this));
  m_pModel->m_Events.RemoveEventHandler(plMakeDelegate(&plSequencerPanel::OnModelEvent, this));
  m_pDocument->m_SequenceEvents.RemoveEventHandler(plMakeDelegate(&plSequencerPanel::SequenceEventHandler, this));
}

void plSequencerPanel::OnUiUpdate()
{
  if (!IsReady())
    return;

  // the transport and the status row are as high as their rows, the timeline takes the rest
  const plVec2 vSize = GetSurface()->GetLogicalSize();
  const float fTransport = m_pTransport->GetHeight();
  const float fStatus = m_pStatus->GetHeight();
  const float fTimeline = plMath::Max(vSize.y - fTransport - fStatus, 0.0f);

  SetChildRect(m_pTransport, plRectFloat(0, 0, vSize.x, fTransport));
  SetChildRect(m_pTimeline, plRectFloat(0, fTransport, vSize.x, fTimeline));
  SetChildRect(m_pStatus, plRectFloat(0, fTransport + fTimeline, vSize.x, fStatus));
}

void plSequencerPanel::ConnectTrackView()
{
  m_pTrackView->m_OnRowSelectionChanged = [this]()
  { ReportTrackSelection(); };
  m_pTrackView->m_OnKeySelected = [this](const plUuid& trackGuid, const plUuid& cpGuid)
  {
    if (m_OnKeyPicked)
      m_OnKeyPicked(trackGuid, cpGuid);
  };
  m_pTrackView->m_OnScrubRequested = [this](plUInt64 uiTick)
  { m_pDocument->SetScrubberPosition(uiTick); };
  m_pTrackView->m_OnRowContextMenuRequested = [this](const plVec2& vPosition)
  { ShowRowMenu(vPosition); };
  m_pTrackView->m_OnRowAddRequested = [this](const plVec2& vPosition)
  { ShowAddTrackMenu(vPosition); };
  m_pTrackView->m_OnRowDeleteRequested = [this]()
  { OnDeleteSelected(); };
}

void plSequencerPanel::ReportTrackSelection()
{
  if (m_OnTrackSelectionChanged)
    m_OnTrackSelectionChanged();
}

void plSequencerPanel::OnModelEvent(const plSequenceModelEvent& e)
{
  if (e.m_Type == plSequenceModelEvent::Type::ModelRebuilt)
    ReportTrackSelection();
}

void plSequencerPanel::ShowAddTrackMenu(const plVec2& vPosition)
{
  plEditorUiMenu menu;
  const plUuid bindingGuid = GetTargetBinding();
  BuildAddTrackMenu(menu, bindingGuid);
  m_pTimeline->ShowContextMenu(vPosition, std::move(menu), [this, bindingGuid, vPosition](plUInt32 uiId)
    { OnAddTrackPicked(uiId, bindingGuid, vPosition); });
}

//////////////////////////////////////////////////////////////////////////
// Transport

void plSequencerPanel::CreateTransport()
{
  using plEditorUi::Icon;

  m_pTransport = static_cast<plEditorUiControlStrip*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiControlStrip))));

  // authoring first, then playback, recording and rendering, then time, snapping and length
  m_uiAddBinding = m_pTransport->AddIconTextButton(Icon::Add, "Binding", "Add a binding for the object selected in the Scenegraph panel.");
  m_uiAddTrack = m_pTransport->AddIconTextButton(Icon::Add, "Track", "Add a track to the selected binding.");
  m_pTransport->AddSeparator();

  m_uiToStart = m_pTransport->AddIconButton(Icon::ToStart, "Jump to start");
  m_uiPrevFrame = m_pTransport->AddIconButton(Icon::StepBack, "Previous frame");
  m_uiPlay = m_pTransport->AddIconButton(Icon::Play, "Play / Pause");
  m_uiNextFrame = m_pTransport->AddIconButton(Icon::StepForward, "Next frame");
  m_uiToEnd = m_pTransport->AddIconButton(Icon::ToEnd, "Jump to end");
  m_uiLoop = m_pTransport->AddIconButton(Icon::Repeat, "Loop playback", true);
  m_pTransport->AddSeparator();

  // With auto-key armed, moving an object with the gizmos (or editing it in the property grid) records a key at the
  // playhead instead of being discarded.
  m_uiAutoKey = m_pTransport->AddIconTextButton(Icon::Record, "AUTO-KEY", "Auto-key: record changes to the context scene as keys at the playhead. With this off, the scene cannot be edited from here.", true, plEditorUi::Danger());
  m_uiPilot = m_pTransport->AddIconButton(Icon::Camera, "Pilot: view through the camera of the active cut while scrubbing.", true);
  m_uiRender = m_pTransport->AddIconTextButton(Icon::Render, "Render...", "Render this sequence to an image sequence, and optionally a video.");
  m_pTransport->AddSeparator();

  // The readout reads as one line, "00:03.200 f 96 / 00:12.000": each part is as wide as its longest text, so the
  // labels update without the strip being rebuilt while playing.
  auto codeWidth = [](plStringView sLongest)
  { return plMath::Ceil(plEditorUi::MeasureCodeText(sLongest, plEditorUi::FontSize - 1)) + 2.0f; };
  m_uiTime = m_pTransport->AddCodeLabel("00:00.000", codeWidth("00:00.000"));
  m_uiFrame = m_pTransport->AddCodeLabel("f 0", codeWidth("f 0000"));
  m_pTransport->SetColor(m_uiFrame, plEditorUi::Accent());
  m_uiTotal = m_pTransport->AddCodeLabel("/ 00:00.000", codeWidth("/ 00:00.000"));
  m_pTransport->SetColor(m_uiTotal, plEditorUi::Dim());
  m_uiFps = m_pTransport->AddCodeLabel("30 fps", codeWidth("120 fps"));
  m_pTransport->SetColor(m_uiFps, plEditorUi::Dim());
  m_pTransport->AddSeparator();

  plHybridArray<plString, 3> snaps;
  for (const char* szSnap : s_Snaps)
    snaps.PushBack(szSnap);
  m_uiSnap = m_pTransport->AddDropdown(snaps, 0, "Snapping applied to scrubbing and key dragging.", 140.0f);
  m_pTransport->SetLead(m_uiSnap, "Snap");
  m_uiDuration = m_pTransport->AddNumberField("DUR", "s", "0.00", "Duration", 96.0f);
  m_pTransport->AddSpacer();

  m_pTransport->SetChecked(m_uiAutoKey, m_pDocument->GetAutoKey());
  m_pTransport->SetChecked(m_uiPilot, m_pDocument->GetPilotCamera());

  m_pTransport->m_OnChanged = [this](plUInt32 uiControl, plInt32 iValue)
  { OnTransport(uiControl, iValue); };
}

void plSequencerPanel::OnTransport(plUInt32 uiControl, plInt32 iValue)
{
  if (uiControl == m_uiAddBinding)
  {
    OnAddBinding();
  }
  else if (uiControl == m_uiAddTrack)
  {
    plEditorUiMenu menu;
    const plUuid bindingGuid = GetTargetBinding();
    BuildAddTrackMenu(menu, bindingGuid);

    const plRectFloat rect = m_pTransport->GetControlRect(m_uiAddTrack);
    m_pTransport->ShowMenu(m_uiAddTrack, menu, [this, bindingGuid](plUInt32 uiId)
      { OnAddTrackPicked(uiId, bindingGuid, plVec2(plSequenceTrackView::HeadWidth * 0.5f, 0.0f)); });
    PL_IGNORE_UNUSED(rect);
  }
  else if (uiControl == m_uiToStart)
    m_pDocument->SetScrubberPosition(0);
  else if (uiControl == m_uiPrevFrame)
    OnStepFrame(-1);
  else if (uiControl == m_uiPlay)
    m_pDocument->SetPlayAnimation(!m_pDocument->GetPlayAnimation());
  else if (uiControl == m_uiNextFrame)
    OnStepFrame(+1);
  else if (uiControl == m_uiToEnd)
    m_pDocument->SetScrubberPosition(m_pDocument->GetDurationTicks());
  else if (uiControl == m_uiLoop)
    m_pDocument->SetRepeatAnimation(iValue != 0);
  else if (uiControl == m_uiAutoKey)
    m_pDocument->SetAutoKey(iValue != 0);
  else if (uiControl == m_uiPilot)
    m_pDocument->SetPilotCamera(iValue != 0);
  else if (uiControl == m_uiRender)
  {
    plSequenceRenderEngineDlg::Open(m_pDocument);
  }
  else if (uiControl == m_uiSnap)
  {
    m_pTrackView->SetSnapMode(iValue == 1 ? plSequenceTrackView::SnapMode::HalfFrame : iValue == 2 ? plSequenceTrackView::SnapMode::Free
                                                                                                    : plSequenceTrackView::SnapMode::Frame);
  }
  else if (uiControl == m_uiDuration)
  {
    double fSeconds = 0.0;
    if (plConversionUtils::StringToFloat(m_pTransport->GetText(m_uiDuration), fSeconds).Succeeded())
      m_pDocument->SetDurationTicks(static_cast<plUInt64>(plMath::Clamp(fSeconds, 0.1, 3600.0) * static_cast<double>(plSequenceTicksPerSecond)));

    UpdateTransportState();
  }
}

void plSequencerPanel::UpdateTransportState()
{
  const plUInt64 uiTick = m_pDocument->GetScrubberPosition();
  const plUInt64 uiDuration = m_pDocument->GetDurationTicks();
  const plUInt16 uiFps = plMath::Max<plUInt16>(1, m_pDocument->GetProperties()->m_uiFramesPerSecond);
  const plUInt64 uiFrame = (uiTick * uiFps) / plSequenceTicksPerSecond;

  plStringBuilder s;
  m_pTransport->SetText(m_uiTime, FormatTimecode(uiTick));
  s.SetFormat("f {}", uiFrame);
  m_pTransport->SetText(m_uiFrame, s);
  s.Set("/ ", FormatTimecode(uiDuration));
  m_pTransport->SetText(m_uiTotal, s);
  s.SetFormat("{} fps", uiFps);
  m_pTransport->SetText(m_uiFps, s);

  m_pTransport->SetIcon(m_uiPlay, m_pDocument->GetPlayAnimation() ? plEditorUi::Icon::Pause : plEditorUi::Icon::Play);
  m_pTransport->SetChecked(m_uiLoop, m_pDocument->GetRepeatAnimation());

  s.SetFormat("{}", plArgF(static_cast<double>(uiDuration) / static_cast<double>(plSequenceTicksPerSecond), 2));
  m_pTransport->SetText(m_uiDuration, s);
}

//////////////////////////////////////////////////////////////////////////
// Status row

void plSequencerPanel::CreateStatusRow()
{
  m_pStatus = static_cast<plEditorUiControlStrip*>(AddChild(plUniquePtr<plEditorUiContent>(PL_DEFAULT_NEW(plEditorUiControlStrip))));

  m_uiSelection = m_pStatus->AddLabel("0 keys selected", 160.0f);
  m_pStatus->AddHint("Ruler: drag scrubs, wheel zooms, right-drag pans \xC2\xB7 lanes: [Ctrl]+wheel zooms \xC2\xB7 double-click adds \xC2\xB7 [Del] \xC2\xB7 [Ctrl]+C / V \xC2\xB7 [Left]/[Right] nudge");

  m_pTimeline->m_Events.AddEventHandler(plMakeDelegate(&plSequencerPanel::OnTrackViewEvent, this));
}

void plSequencerPanel::OnTrackViewEvent(const plEngineTrackViewEvent& e)
{
  if (e.m_Type == plEngineTrackViewEvent::Type::SelectionChanged)
    UpdateSelectionStatus();
}

void plSequencerPanel::UpdateSelectionStatus()
{
  const plEditorUiTrackView* pView = m_pTimeline;
  const plUInt32 uiKeys = pView->GetSelectedKeys().GetCount();

  plStringBuilder s;
  if (pView->GetSelectedClip() != 0)
    s = "1 clip selected";
  else if (pView->GetSelectedMarker() != 0)
    s = "1 marker selected";
  else
    s.SetFormat("{} {} selected", uiKeys, uiKeys == 1 ? "key" : "keys");

  m_pStatus->SetText(m_uiSelection, s);
}

void plSequencerPanel::SequenceEventHandler(const plSequenceAssetEvent& e)
{
  switch (e.m_Type)
  {
    case plSequenceAssetEvent::Type::DurationChanged:
    case plSequenceAssetEvent::Type::ScrubberPositionChanged:
      UpdateTransportState();
      m_pTrackView->Refresh();
      break;

    case plSequenceAssetEvent::Type::PlaybackChanged:
      UpdateTransportState();
      break;
  }
}

void plSequencerPanel::OnStepFrame(int iDirection)
{
  const plInt64 iStep = static_cast<plInt64>(m_pDocument->GetTicksPerFrame()) * iDirection;
  const plInt64 iNew = static_cast<plInt64>(m_pDocument->GetScrubberPosition()) + iStep;

  m_pDocument->SetScrubberPosition(static_cast<plUInt64>(plMath::Clamp<plInt64>(iNew, 0, static_cast<plInt64>(m_pDocument->GetDurationTicks()))));
}

//////////////////////////////////////////////////////////////////////////
// Rows

plUuid plSequencerPanel::GetSelectedTrack() const
{
  const plSequenceModel::FlatRow* pRow = m_pTrackView->GetSelectedRow();
  if (pRow == nullptr || pRow->IsBinding() || pRow->IsClipLane())
    return plUuid(); // nothing, or a binding or lane row

  return pRow->m_ObjectGuid;
}

void plSequencerPanel::GetSelectedAxisTracks(plHybridArray<plUuid, 4>& out_tracks) const
{
  out_tracks.Clear();

  if (const plSequenceModel::FlatRow* pRow = m_pTrackView->GetSelectedRow())
    out_tracks = pRow->m_AxisTracks;
}

plUuid plSequencerPanel::GetTargetBinding() const
{
  const plSequenceModel::FlatRow* pRow = m_pTrackView->GetSelectedRow();
  const auto& rows = m_pModel->GetFlatRows();

  if (pRow == nullptr)
  {
    // fall back to the first binding, so "add track" works right after "add binding"
    for (const auto& row : rows)
    {
      if (row.m_Kind == plSequenceModel::RowKind::Binding)
        return row.m_ObjectGuid;
    }
    return plUuid();
  }

  // the row's binding: itself, or the top-level node above it
  plInt32 iNode = pRow->m_iNode;
  while (m_pModel->GetNode(iNode).m_iParent >= 0)
    iNode = m_pModel->GetNode(iNode).m_iParent;

  const plSequenceModel::Node& top = m_pModel->GetNode(iNode);
  return top.m_Kind == plSequenceModel::RowKind::Binding ? top.m_ObjectGuid : plUuid();
}

void plSequencerPanel::OnDeleteSelected()
{
  const plSequenceModel::FlatRow* pRow = m_pTrackView->GetSelectedRow();
  if (pRow == nullptr || pRow->IsCameraCutLane() || pRow->IsEventLane() || pRow->IsClipLane())
    return;

  // Removing the objects fires the structure events the document listens to, so the preview
  // mapping drops those tracks and restores the properties they were driving.
  const bool bIsBinding = pRow->m_Kind == plSequenceModel::RowKind::Binding;
  const plUuid guid = pRow->m_ObjectGuid;

  plCommandHistory* pHistory = m_pDocument->GetCommandHistory();
  pHistory->StartTransaction(bIsBinding ? "Remove Binding" : "Remove Track");

  plRemoveObjectCommand cmd;
  cmd.m_Object = guid;

  if (pHistory->AddCommand(cmd).Failed())
  {
    pHistory->CancelTransaction();
    return;
  }

  pHistory->FinishTransaction();
}

void plSequencerPanel::ShowRowMenu(const plVec2& vPosition)
{
  using plEditorUi::Icon;
  using plEditorUi::IconPath;

  const plSequenceModel::FlatRow* pRow = m_pTrackView->GetSelectedRow();
  if (pRow == nullptr)
    return;

  const plSequenceModel::FlatRow row = *pRow;
  const plSequenceModel::Node& node = m_pModel->GetNode(row.m_iNode);
  const plUInt64 uiTick = m_pDocument->GetScrubberPosition();

  plStringBuilder sAt;
  sAt.SetFormat(" at {}s", plArgF(static_cast<double>(uiTick) / static_cast<double>(plSequenceTicksPerSecond), 2));

  plEditorUiMenu menu;
  m_MenuCameras.Clear();
  m_MenuTriggers.Clear();

  auto addDanger = [&](plStringView sLabel, plUInt32 uiId)
  {
    auto& item = menu.AddAction(sLabel, uiId);
    item.m_sIcon = IconPath(Icon::Clear);
    item.m_bDanger = true;
  };

  if (row.IsCameraCutLane())
  {
    BuildCameraCutMenu(menu);
  }
  else if (row.IsClipLane())
  {
    if (row.m_ClipKind == plSequenceClipKind::AudioTrigger)
    {
      // Triggers are a dynamic string enum, the same list plAudioTriggerComponent offers - typing one by hand is how
      // you get a clip that silently plays nothing.
      for (const plString& sTrigger : plDynamicStringEnum::GetDynamicEnum("AudioSystem.Trigger").GetAllValidValues())
        m_MenuTriggers.PushBack(sTrigger);

      if (m_MenuTriggers.IsEmpty())
      {
        menu.AddAction("No audio triggers registered in this project", 0).m_bEnabled = false;
      }
      else
      {
        auto& triggers = menu.AddSubmenu(plStringBuilder("Add Trigger", sAt));
        for (plUInt32 i = 0; i < m_MenuTriggers.GetCount(); ++i)
        {
          // the stored value is an asset GUID, so the menu shows the resolved name
          auto& item = triggers.m_Children.ExpandAndGetRef();
          item.m_sLabel = plSequenceWaveformCache::PayloadDisplayName(m_MenuTriggers[i]);
          item.m_uiId = MenuId::AddTrigger + i;
        }
      }
    }
    else
    {
      menu.AddAction(plStringBuilder("Add Clip", sAt), MenuId::AddClipHere).m_sIcon = IconPath(Icon::Add);
    }

    menu.AddSeparator();
    addDanger("Remove Lane and its Clips", MenuId::RemoveLane);
  }
  else if (row.m_Kind == plSequenceModel::RowKind::Binding)
  {
    if (node.m_bUnresolved)
    {
      // A binding that resolves to nothing cannot gain tracks, so the repair comes first and the rest is disabled
      // with a reason.
      menu.AddAction("This binding's object is not in the attached scene", 0).m_bEnabled = false;
      menu.AddAction("Rebind to Selected Object...", MenuId::Rebind);
      menu.AddSeparator();
      auto& add = menu.AddAction("Add Track...", MenuId::AddTrackMenu);
      add.m_bEnabled = false;
      add.m_sTooltip = "Rebind this to an object first.";
    }
    else
    {
      menu.AddAction("Add Track...", MenuId::AddTrackMenu).m_sIcon = IconPath(Icon::Add);
      menu.AddSeparator();
      menu.AddAction("Rebind to Selected Object...", MenuId::Rebind);
    }

    menu.AddSeparator();
    addDanger("Remove Binding and its Tracks", MenuId::RemoveRow);
  }
  else if (!row.IsEventLane())
  {
    addDanger("Remove Track", MenuId::RemoveRow);
  }

  if (menu.m_Items.IsEmpty())
    return;

  m_pTimeline->ShowContextMenu(vPosition, std::move(menu), [this, row, uiTick, vPosition](plUInt32 uiId)
    {
    if (uiId >= MenuId::CutToCamera && uiId < MenuId::AddTrigger)
    {
      const plUInt32 uiCamera = uiId - MenuId::CutToCamera;
      if (uiCamera < m_MenuCameras.GetCount())
        m_pDocument->AddCameraCut(m_MenuCameras[uiCamera], uiTick);
    }
    else if (uiId >= MenuId::AddTrigger && uiId < MenuId::AddClipHere)
    {
      const plUInt32 uiTrigger = uiId - MenuId::AddTrigger;
      if (uiTrigger < m_MenuTriggers.GetCount())
        m_pDocument->AddClip(row.m_ObjectGuid, plSequenceClipKind::AudioTrigger, uiTick, m_MenuTriggers[uiTrigger]);
    }
    else if (uiId == MenuId::AddClipHere)
      m_pDocument->AddClip(row.m_ObjectGuid, row.m_ClipKind, uiTick, "");
    else if (uiId == MenuId::RemoveLane)
      m_pDocument->RemoveClipLane(row.m_ObjectGuid, row.m_ClipKind);
    else if (uiId == MenuId::Rebind)
      RebindToSelection(row.m_ObjectGuid);
    else if (uiId == MenuId::RemoveRow)
      OnDeleteSelected();
    else if (uiId == MenuId::AddTrackMenu)
    {
      plEditorUiMenu addMenu;
      BuildAddTrackMenu(addMenu, row.m_ObjectGuid);
      const plUuid bindingGuid = row.m_ObjectGuid;
      m_pTimeline->ShowContextMenu(vPosition, std::move(addMenu), [this, bindingGuid, vPosition](plUInt32 uiPicked)
        { OnAddTrackPicked(uiPicked, bindingGuid, vPosition); });
    } });
}

void plSequencerPanel::BuildCameraCutMenu(plEditorUiMenu& ref_menu)
{
  m_MenuCameras.Clear();
  plDynamicArray<const plDocumentObject*> cameras;
  m_pDocument->CollectContextCameras(cameras);

  if (cameras.IsEmpty())
  {
    ref_menu.AddAction("No cameras in the attached scene", 0).m_bEnabled = false;
    return;
  }

  const plUInt64 uiTick = m_pDocument->GetScrubberPosition();

  plStringBuilder sLabel;
  sLabel.SetFormat("Cut to camera at {}s", plArgF(static_cast<double>(uiTick) / static_cast<double>(plSequenceTicksPerSecond), 2));
  auto& cuts = ref_menu.AddSubmenu(sLabel);
  cuts.m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Camera);

  for (const plDocumentObject* pCamera : cameras)
  {
    plStringBuilder sName = pCamera->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
    if (sName.IsEmpty())
      sName = "<unnamed>";

    auto& item = cuts.m_Children.ExpandAndGetRef();
    item.m_sLabel = sName;
    item.m_uiId = MenuId::CutToCamera + m_MenuCameras.GetCount();
    m_MenuCameras.PushBack(pCamera);
  }
}

void plSequencerPanel::BuildAddTrackMenu(plEditorUiMenu& ref_menu, const plUuid& bindingGuid)
{
  auto addLanes = [&](plStringView sLabel, plUInt32 uiBase)
  {
    // Clip lanes are their own kind of row, one per clip type, so audio never shares a lane with animation.
    auto& lanes = ref_menu.AddSubmenu(sLabel);
    const char* szNames[] = {"Audio", "Animation", "Sub-Sequence"};
    const plSequenceClipKind::Enum kinds[] = {plSequenceClipKind::AudioTrigger, plSequenceClipKind::AnimationClip, plSequenceClipKind::SubSequence};
    for (plUInt32 i = 0; i < 3; ++i)
    {
      auto& item = lanes.m_Children.ExpandAndGetRef();
      item.m_sLabel = szNames[i];
      item.m_uiId = uiBase + static_cast<plUInt32>(kinds[i]);
    }
  };

  // With nothing selected, the only thing that can be added is sequence-level: clips that play without being attached
  // to anything in the scene - music, VO, stingers.
  if (!bindingGuid.IsValid())
  {
    addLanes("Sequence Clip Lane", MenuId::AddSequenceLane);
    ref_menu.AddSeparator();
    ref_menu.AddAction("Select a binding to add tracks to it", 0).m_bEnabled = false;
    return;
  }

  // Transform channels are authored as whole values: one Position row over its X/Y/Z tracks.
  ref_menu.AddAction("Position", MenuId::AddPosition).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Move);
  ref_menu.AddAction("Rotation", MenuId::AddRotation);
  ref_menu.AddAction("Scale", MenuId::AddScale);
  ref_menu.AddSeparator();

  // the reflection-driven picker: searchable, grouped by object and component, covering descendants
  ref_menu.AddAction("Property...", MenuId::AddProperty).m_sIcon = plEditorUi::IconPath(plEditorUi::Icon::Search);
  ref_menu.AddSeparator();
  addLanes("Clip Lane", MenuId::AddLane);
}

void plSequencerPanel::OnAddTrackPicked(plUInt32 uiId, const plUuid& bindingGuid, const plVec2& vPickerPosition)
{
  switch (uiId)
  {
    case MenuId::AddPosition:
      m_pDocument->AddVectorTrack(bindingGuid, "", "LocalPosition", false);
      return;
    case MenuId::AddRotation:
      m_pDocument->AddVectorTrack(bindingGuid, "", "LocalRotation", true);
      return;
    case MenuId::AddScale:
      m_pDocument->AddVectorTrack(bindingGuid, "", "LocalScaling", false);
      return;
    case MenuId::AddProperty:
      ShowPropertyPicker(bindingGuid, vPickerPosition);
      return;
    default:
      break;
  }

  if (uiId >= MenuId::AddSequenceLane && uiId < MenuId::AddSequenceLane + 3)
    m_pDocument->AddClipLane(m_pDocument->GetPropertyObject()->GetGuid(), static_cast<plSequenceClipKind::Enum>(uiId - MenuId::AddSequenceLane));
  else if (uiId >= MenuId::AddLane && uiId < MenuId::AddLane + 3)
    m_pDocument->AddClipLane(bindingGuid, static_cast<plSequenceClipKind::Enum>(uiId - MenuId::AddLane));
}

void plSequencerPanel::CollectAnimatableProperties(const plDocumentObject* pObject, const plStringBuilder& sPathPrefix)
{
  if (pObject == nullptr)
    return;

  // Every entry carries the document object that owns the property plus the property name and the
  // target channel; the document turns that into the right binding and track.
  auto addProperty = [&](const plDocumentObject* pOwner, const plAbstractProperty* pProp, const plStringBuilder& sGroup, const char* szSuffix, plPropertyAnimTarget::Enum target)
  {
    plStringBuilder sDisplay = pProp->GetPropertyName();
    sDisplay.Append(szSuffix);

    plEditorUiSearchMenuItem& item = m_PickerItems.ExpandAndGetRef();
    item.m_sLabel = sDisplay;
    item.m_sCategory = sGroup;
    item.m_uiId = m_PickerItems.GetCount();

    m_PickerProperties.PushBack({pOwner->GetGuid(), pProp->GetPropertyName(), target});
  };

  auto addPropertiesOf = [&](const plDocumentObject* pOwner, const plStringBuilder& sGroup)
  {
    for (const plRTTI* pType = pOwner->GetType(); pType != nullptr; pType = pType->GetParentType())
    {
      for (const plAbstractProperty* pProp : pType->GetProperties())
      {
        if (pProp->GetCategory() != plPropertyCategory::Member)
          continue;
        if (pProp->GetFlags().IsAnySet(plPropertyFlags::ReadOnly | plPropertyFlags::Hidden))
          continue;

        const plVariantType::Enum varType = pProp->GetSpecificType()->GetVariantType();

        if (varType >= plVariantType::Int8 && varType <= plVariantType::Double)
        {
          addProperty(pOwner, pProp, sGroup, "", plPropertyAnimTarget::Number);
        }
        else if (varType >= plVariantType::Vector2 && varType <= plVariantType::Vector4)
        {
          const plUInt32 uiComponents = (varType == plVariantType::Vector2) ? 2 : (varType == plVariantType::Vector3 ? 3 : 4);
          const char* axis[4] = {".X", ".Y", ".Z", ".W"};

          for (plUInt32 a = 0; a < uiComponents; ++a)
            addProperty(pOwner, pProp, sGroup, axis[a], (plPropertyAnimTarget::Enum)(plPropertyAnimTarget::VectorX + a));
        }
        else if (varType == plVariantType::Quaternion)
        {
          const char* axis[3] = {".X", ".Y", ".Z"};
          for (plUInt32 a = 0; a < 3; ++a)
            addProperty(pOwner, pProp, sGroup, axis[a], (plPropertyAnimTarget::Enum)(plPropertyAnimTarget::RotationX + a));
        }
        else if (varType == plVariantType::Color || varType == plVariantType::ColorGamma)
        {
          addProperty(pOwner, pProp, sGroup, "", plPropertyAnimTarget::Color);
        }
      }
    }
  };

  // the object's own transform properties
  {
    plStringBuilder sGroup = sPathPrefix;
    sGroup.AppendPath("Transform");
    addPropertiesOf(pObject, sGroup);
  }

  for (const plDocumentObject* pChild : pObject->GetChildren())
  {
    if (pChild->GetType()->IsDerivedFrom<plComponent>())
    {
      plStringBuilder sName = pChild->GetType()->GetTypeName();
      sName.TrimWordStart("pl");
      sName.TrimWordEnd("Component");

      plStringBuilder sGroup = sPathPrefix;
      sGroup.AppendPath(sName);
      addPropertiesOf(pChild, sGroup);
    }
    else if (pChild->GetType() == plGetStaticRTTI<plGameObject>())
    {
      // Descendants matter: binding a group is natural, but its components usually sit on the
      // children, so those have to be reachable or the picker looks empty.
      plStringBuilder sChildName = pChild->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
      if (sChildName.IsEmpty())
        sChildName = "<unnamed>";

      plStringBuilder sChildPrefix = sPathPrefix;
      sChildPrefix.AppendPath(sChildName);

      CollectAnimatableProperties(pChild, sChildPrefix);
    }
  }
}

void plSequencerPanel::ShowPropertyPicker(const plUuid& bindingGuid, const plVec2& vPosition)
{
  const plDocumentObject* pBoundObj = m_pDocument->ResolveBindingObject(bindingGuid);
  if (pBoundObj == nullptr)
  {
    plEditorUiServices::MessageBoxInformation(
      plFmt("This binding does not resolve to an object in the attached scene, so its properties cannot be listed."));
    return;
  }

  m_PickerItems.Clear();
  m_PickerProperties.Clear();
  CollectAnimatableProperties(pBoundObj, plStringBuilder());

  plEditorUiSearchMenuView::Show(*m_pTimeline->GetSurface(), vPosition, m_PickerItems, "Search properties", [this](plUInt32 uiId)
    {
    if (uiId == 0 || uiId > m_PickerProperties.GetCount())
      return;

    const PickedProperty& picked = m_PickerProperties[uiId - 1];
    m_pDocument->AddTrackForContextProperty(picked.m_OwnerGuid, picked.m_sProperty, picked.m_Target); });
}

void plSequencerPanel::OnAddBinding()
{
  // The user points at scene objects through the scenegraph panel's selection.
  const plDocumentObject* pSelected = m_pDocument->GetSelectionManager()->GetCurrentObject();
  if (pSelected == nullptr)
  {
    plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(
      plFmt("Select an object of the context scene in the Scenegraph panel first."), plTime::MakeFromSeconds(4));
    return;
  }

  m_pDocument->AddBindingForContextObject(pSelected);
}

void plSequencerPanel::RebindToSelection(const plUuid& bindingGuid)
{
  const plDocumentObject* pSelected = m_pDocument->GetSelectionManager()->GetCurrentObject();

  if (pSelected == nullptr)
  {
    plEditorUiServices::MessageBoxInformation(plFmt("Select the object to bind to in the scenegraph first, then rebind."));
    return;
  }

  if (plStatus status = m_pDocument->RebindTo(bindingGuid, pSelected); status.Failed())
  {
    plEditorUiServices::MessageBoxInformation(plFmt("{}", status.m_sMessage));
  }
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequencerPanel);
