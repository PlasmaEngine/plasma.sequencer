#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorFramework/DocumentWindow/EngineQuadView.h>
#include <EditorFramework/DocumentWindow/GameObjectViewWidget.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <EditorFramework/Panels/GameObjectPanel/GameObjectPanel.h>
#include <EditorFramework/Preferences/EditorPreferences.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAssetWindow.h>
#include <EditorPluginSequencer/SequenceAsset/SequencerPanel.h>
#include <GuiFoundation/EngineUi/EngineEventTrackEditor.h>
#include <ToolsFoundation/Command/TreeCommands.h>

plSequenceAssetDocumentWindow::plSequenceAssetDocumentWindow(plSequenceAssetDocument* pDocument)
  : plGameObjectDocumentWindow(pDocument)
{
  // 3D viewport of the attached context scene
  auto ViewFactory = [](plEngineDocumentWindow* pWindow, plEngineViewConfig* pConfig) -> plEngineViewWidget*
  {
    plGameObjectViewWidget* pWidget = new plGameObjectViewWidget(static_cast<plSequenceAssetDocumentWindow*>(pWindow), pConfig);
    pWindow->AddViewWidget(pWidget);
    return pWidget;
  };
  SetQuadView(PL_DEFAULT_NEW(plEngineQuadView, pDocument, this, ViewFactory, "SequenceAssetViewToolBar"));

  pDocument->SetEditToolConfigDelegate(
    [this](plGameObjectEditTool* pTool) { pTool->ConfigureTool(static_cast<plGameObjectDocument*>(GetDocument()), this, this); });

  plEditorPreferencesUser* pPreferences = plPreferences::QueryPreferences<plEditorPreferencesUser>();
  SetTargetFramerate(pPreferences->GetMaxFramerate());

  SetMenuBarActions("SequenceAssetMenuBar");
  SetToolBarActions("SequenceAssetToolBar");

  CreatePanels(pDocument);

  pDocument->m_SequenceEvents.AddEventHandler(plMakeDelegate(&plSequenceAssetDocumentWindow::SequenceEventHandler, this));
  pDocument->GetCommandHistory()->m_Events.AddEventHandler(plMakeDelegate(&plSequenceAssetDocumentWindow::CommandHistoryEventHandler, this));

  m_CurvesToDisplay.m_bOwnsData = false;
  UpdateCurveEditor();
  UpdateEventTrackEditor();
}

plSequenceAssetDocumentWindow::~plSequenceAssetDocumentWindow()
{
  // the panels report to this window
  DestroyPanels();

  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());
  pDoc->m_SequenceEvents.RemoveEventHandler(plMakeDelegate(&plSequenceAssetDocumentWindow::SequenceEventHandler, this));
  pDoc->GetCommandHistory()->m_Events.RemoveEventHandler(plMakeDelegate(&plSequenceAssetDocumentWindow::CommandHistoryEventHandler, this));
}

void plSequenceAssetDocumentWindow::CreatePanels(plSequenceAssetDocument* pDocument)
{
  // Scenegraph of the context document: its objects live under the document root's "TempObjects"
  {
    plEditorUiGameObjectTree* pTree = AddPanel<plEditorUiGameObjectTree>({"SceneTree", "Scene Tree", plEditorUiDockRole::Left}, pDocument, "SequenceAsset_ScenegraphContextMenu", nullptr, nullptr, nullptr, nullptr, "TempObjects");
    pTree->SetContextWindow(this);
  }

  // Sequencer panel: transport and the track view (its head column is the binding outliner). The Sequencer, Curve
  // Editor and Events panels share the area below the view as tabs.
  {
    m_pSequencerPanel = AddPanel<plSequencerPanel>({"Sequencer", "Sequencer", plEditorUiDockRole::Bottom}, pDocument);
    m_pSequencerPanel->m_OnTrackSelectionChanged = [this]()
    { OnTrackSelectionChanged(); };
    m_pSequencerPanel->m_OnKeyPicked = [this](const plUuid& trackGuid, const plUuid& cpGuid)
    { OnKeyPicked(trackGuid, cpGuid); };
  }

  // Curve editor: values and tangents of the selected track (the canvas edits timing only)
  {
    plEditorUiCurveEdits& curveEdits = *this;
    m_pCurveEditor = AddPanel<plEditorUiCurveView>({"Curves", "Curve Editor", plEditorUiDockRole::Bottom}, curveEdits);
  }

  // Event track: sequence-level, so unlike the curve editor it needs no selection to be useful
  m_pEventTrackEditor = AddPanel<plEditorUiEventTrackView>({"Events", "Events", plEditorUiDockRole::Bottom});
  ConnectEventTrackEditor();
}

void plSequenceAssetDocumentWindow::ConnectEventTrackEditor()
{
  m_pEventTrackEditor->m_OnInsertCp = [this](plInt64 iTick, plStringView sValue)
  { OnEventTrackInsertCpAt(iTick, sValue); };
  m_pEventTrackEditor->m_OnCpMoved = [this](plUInt32 uiIndex, plInt64 iTick)
  { OnEventTrackCpMoved(uiIndex, iTick); };
  m_pEventTrackEditor->m_OnCpDeleted = [this](plUInt32 uiIndex)
  { OnEventTrackCpDeleted(uiIndex); };
  m_pEventTrackEditor->m_OnBeginCpChanges = [this](plStringView sName)
  { OnEventTrackBeginCpChanges(sName); };
  m_pEventTrackEditor->m_OnEndCpChanges = [this]()
  { OnEventTrackEndCpChanges(); };
  m_pEventTrackEditor->m_OnBeginOperation = [this](plStringView sName)
  { OnEventTrackBeginOperation(sName); };
  m_pEventTrackEditor->m_OnEndOperation = [this](bool bCommit)
  { OnEventTrackEndOperation(bCommit); };
}

plObjectAccessorBase* plSequenceAssetDocumentWindow::GetObjectAccessor()
{
  return GetDocument()->GetObjectAccessor();
}

// ---- curve editor dock -----------------------------------------------------

void plSequenceAssetDocumentWindow::SequenceEventHandler(const plSequenceAssetEvent& e)
{
  switch (e.m_Type)
  {
    case plSequenceAssetEvent::Type::ScrubberPositionChanged:
    {
      const plUInt64 uiTick = static_cast<const plSequenceAssetDocument*>(e.m_pDocument)->GetScrubberPosition();
      const double fSeconds = static_cast<double>(uiTick) / static_cast<double>(plSequenceTicksPerSecond);
      m_pCurveEditor->SetScrubberPosition(fSeconds);
      m_pEventTrackEditor->SetScrubberPosition(fSeconds);
      break;
    }
    case plSequenceAssetEvent::Type::DurationChanged:
      UpdateCurveEditor();
      UpdateEventTrackEditor();
      break;
    default:
      break;
  }
}

void plSequenceAssetDocumentWindow::CommandHistoryEventHandler(const plCommandHistoryEvent& e)
{
  if (e.m_Type == plCommandHistoryEvent::Type::TransactionEnded || e.m_Type == plCommandHistoryEvent::Type::UndoEnded ||
      e.m_Type == plCommandHistoryEvent::Type::RedoEnded)
  {
    UpdateCurveEditor();
    UpdateEventTrackEditor();
  }
}

void plSequenceAssetDocumentWindow::OnTrackSelectionChanged()
{
  const plUuid selected = m_pSequencerPanel->GetSelectedTrack();
  if (m_SelectedTrackGuid == selected)
    return;

  m_SelectedTrackGuid = selected;
  UpdateCurveEditor();
}

void plSequenceAssetDocumentWindow::OnKeyPicked(const plUuid& trackGuid, const plUuid& cpGuid)
{
  if (m_SelectedTrackGuid != trackGuid)
  {
    m_SelectedTrackGuid = trackGuid;
    UpdateCurveEditor();
  }

  // Mirror the picked key into the curve editor's own selection.
  const plDocumentObject* pTrackObj = GetDocument()->GetObjectManager()->GetObject(trackGuid);
  if (pTrackObj == nullptr)
    return;

  // Not every pickable key belongs to a curve track - the event lane's keys sit on the sequence's
  // event track, which has no FloatCurve to mirror them into.
  const plVariant curveGuid = pTrackObj->GetTypeAccessor().GetValue("FloatCurve");
  if (!curveGuid.IsA<plUuid>())
    return;

  const plDocumentObject* pCurveObj = GetDocument()->GetObjectManager()->GetObject(curveGuid.Get<plUuid>());
  if (pCurveObj == nullptr)
    return;

  const plInt32 iCpCount = pCurveObj->GetTypeAccessor().GetCount("ControlPoints");
  for (plInt32 i = 0; i < iCpCount; ++i)
  {
    const plVariant guid = pCurveObj->GetTypeAccessor().GetValue("ControlPoints", i);
    if (guid.IsA<plUuid>() && guid.Get<plUuid>() == cpGuid)
    {
      plSelectedCurveCP cp;
      cp.m_uiCurve = 0;
      cp.m_uiPoint = (plUInt16)i;
      m_pCurveEditor->SetSelection(plMakeArrayPtr(&cp, 1));
      return;
    }
  }
}

void plSequenceAssetDocumentWindow::UpdateCurveEditor()
{
  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());

  m_CurvesToDisplay.m_bOwnsData = false;
  m_CurvesToDisplay.m_Curves.Clear();
  m_CurvesToDisplay.m_uiFramesPerSecond = pDoc->GetProperties()->m_uiFramesPerSecond;
  m_CurveTrackGuids.Clear();

  // A vector group shows all of its axes at once - editing Position means seeing X, Y and Z
  // together, which is the whole reason they are one row. Each curve carries its own colour.
  plHybridArray<plUuid, 4> axisTracks;
  m_pSequencerPanel->GetSelectedAxisTracks(axisTracks);

  if (axisTracks.IsEmpty() && m_SelectedTrackGuid.IsValid())
    axisTracks.PushBack(m_SelectedTrackGuid);

  for (const plUuid& trackGuid : axisTracks)
  {
    if (const plSequenceTrack* pTrack = pDoc->GetTrack(trackGuid))
    {
      plSingleCurveData* pCurve = const_cast<plSingleCurveData*>(&pTrack->m_FloatCurve);

      // Also set here, not only when the track is created: tracks authored before axis colours
      // existed would otherwise all draw in the default lime and be impossible to tell apart. This
      // writes the engine-side mirror the dock is handed, not the document, so it neither dirties
      // the asset nor needs an undo step - the colour is display data either way.
      plColorGammaUB axisColor;
      if (plSequenceAssetDocument::CurveColorForTarget(pTrack->m_Target, axisColor))
        pCurve->m_CurveColor = axisColor;

      m_CurvesToDisplay.m_Curves.PushBack(pCurve);
      m_CurveTrackGuids.PushBack(trackGuid);
    }
  }

  // the curve list names each curve by its property and axis
  plHybridArray<plString, 4> names;
  for (const plUuid& trackGuid : m_CurveTrackGuids)
  {
    const plSequenceTrack* pTrack = pDoc->GetTrack(trackGuid);
    plStringBuilder sName = pTrack->m_sPropertyPath;
    sName.TrimWordStart("Local");
    switch (pTrack->m_Target.GetValue())
    {
      case plPropertyAnimTarget::VectorX:
      case plPropertyAnimTarget::RotationX:
        sName.Append(".X");
        break;
      case plPropertyAnimTarget::VectorY:
      case plPropertyAnimTarget::RotationY:
        sName.Append(".Y");
        break;
      case plPropertyAnimTarget::VectorZ:
      case plPropertyAnimTarget::RotationZ:
        sName.Append(".Z");
        break;
      case plPropertyAnimTarget::VectorW:
        sName.Append(".W");
        break;
      default:
        break;
    }
    names.PushBack(sName);
  }

  m_pCurveEditor->SetCurveExtents(0, pDoc->GetDuration().GetSeconds(), true, true);
  m_pCurveEditor->SetCurves(m_CurvesToDisplay);
  m_pCurveEditor->SetCurveNames(names, "Selected track");
  m_pCurveEditor->SetScrubberPosition(static_cast<double>(pDoc->GetScrubberPosition()) / static_cast<double>(plSequenceTicksPerSecond));
}

plUuid plSequenceAssetDocumentWindow::GetCurveTrackGuid(plUInt32 uiCurveIdx) const
{
  // With a vector group selected the dock shows several curves, so which track an edit belongs to
  // comes from the curve index the widget reports, not from "the" selected track.
  return uiCurveIdx < m_CurveTrackGuids.GetCount() ? m_CurveTrackGuids[uiCurveIdx] : plUuid();
}

plUuid plSequenceAssetDocumentWindow::GetCurveCpGuid(plUInt32 uiCurveIdx, plUInt32 uiCpIdx) const
{
  const plUuid trackGuid = GetCurveTrackGuid(uiCurveIdx);
  if (!trackGuid.IsValid())
    return plUuid();

  const plDocumentObject* pTrackObj = GetDocument()->GetObjectManager()->GetObject(trackGuid);
  if (pTrackObj == nullptr)
    return plUuid();

  const plVariant curveGuid = pTrackObj->GetTypeAccessor().GetValue("FloatCurve");
  if (!curveGuid.IsA<plUuid>())
    return plUuid(); // the selected row is not a curve track

  const plDocumentObject* pCurveObj = GetDocument()->GetObjectManager()->GetObject(curveGuid.Get<plUuid>());
  if (pCurveObj == nullptr)
    return plUuid();

  const plVariant cpGuid = pCurveObj->GetTypeAccessor().GetValue("ControlPoints", uiCpIdx);
  if (!cpGuid.IsA<plUuid>())
    return plUuid();

  return cpGuid.Get<plUuid>();
}

void plSequenceAssetDocumentWindow::InsertCp(plUInt32 uiCurveIdx, plInt64 iTickX, double fClickPosY)
{
  const plUuid trackGuid = GetCurveTrackGuid(uiCurveIdx);
  if (!trackGuid.IsValid())
    return;

  static_cast<plSequenceAssetDocument*>(GetDocument())->InsertCurveCpAt(trackGuid, iTickX, fClickPosY);
}

void plSequenceAssetDocumentWindow::CpMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, plInt64 iTickX, double fNewPosY)
{


  const plUuid cpGuid = GetCurveCpGuid(uiCurveIdx, uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  iTickX = plMath::Max<plInt64>(iTickX, 0);

  plSetObjectPropertyCommand cmdSet;
  cmdSet.m_Object = cpGuid;

  cmdSet.m_sProperty = "Tick";
  cmdSet.m_NewValue = iTickX;
  GetDocument()->GetCommandHistory()->AddCommand(cmdSet).AssertSuccess();

  cmdSet.m_sProperty = "Value";
  cmdSet.m_NewValue = fNewPosY;
  GetDocument()->GetCommandHistory()->AddCommand(cmdSet).AssertSuccess();
}

void plSequenceAssetDocumentWindow::CpDeleted(plUInt32 uiCurveIdx, plUInt32 uiCpIdx)
{


  const plUuid cpGuid = GetCurveCpGuid(uiCurveIdx, uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  plRemoveObjectCommand cmdRemove;
  cmdRemove.m_Object = cpGuid;
  GetDocument()->GetCommandHistory()->AddCommand(cmdRemove).AssertSuccess();
}

void plSequenceAssetDocumentWindow::TangentMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, float fNewPosX, float fNewPosY, bool bRightTangent)
{


  const plUuid cpGuid = GetCurveCpGuid(uiCurveIdx, uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  // clamp tangents to one side
  if (bRightTangent)
    fNewPosX = plMath::Max(fNewPosX, 0.0f);
  else
    fNewPosX = plMath::Min(fNewPosX, 0.0f);

  plSetObjectPropertyCommand cmdSet;
  cmdSet.m_Object = cpGuid;
  cmdSet.m_sProperty = bRightTangent ? "RightTangent" : "LeftTangent";
  cmdSet.m_NewValue = plVec2(fNewPosX, fNewPosY);
  GetDocument()->GetCommandHistory()->AddCommand(cmdSet).AssertSuccess();
}

void plSequenceAssetDocumentWindow::TangentLink(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bLink)
{
  const plUuid cpGuid = GetCurveCpGuid(uiCurveIdx, uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  plSetObjectPropertyCommand cmdLink;
  cmdLink.m_Object = cpGuid;
  cmdLink.m_sProperty = "Linked";
  cmdLink.m_NewValue = bLink;
  GetDocument()->GetCommandHistory()->AddCommand(cmdLink).AssertSuccess();

  if (bLink)
  {
    plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());
    if (const plSequenceTrack* pTrack = pDoc->GetTrack(GetCurveTrackGuid(uiCurveIdx)))
    {
      if (uiCpIdx < pTrack->m_FloatCurve.m_ControlPoints.GetCount())
      {
        const plVec2 leftTangent = pTrack->m_FloatCurve.m_ControlPoints[uiCpIdx].m_LeftTangent;
        TangentMoved(uiCurveIdx, uiCpIdx, -leftTangent.x, -leftTangent.y, true);
      }
    }
  }
}

void plSequenceAssetDocumentWindow::CpTangentMode(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bRightTangent, int iMode)
{


  const plUuid cpGuid = GetCurveCpGuid(uiCurveIdx, uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  plSetObjectPropertyCommand cmd;
  cmd.m_Object = cpGuid;
  cmd.m_sProperty = bRightTangent ? "RightTangentMode" : "LeftTangentMode";
  cmd.m_NewValue = iMode;
  GetDocument()->GetCommandHistory()->AddCommand(cmd).AssertSuccess();
}

void plSequenceAssetDocumentWindow::BeginOperation(plStringView sName)
{
  GetDocument()->GetCommandHistory()->BeginTemporaryCommands(sName);
}

void plSequenceAssetDocumentWindow::EndOperation(bool bCommit)
{
  if (bCommit)
    GetDocument()->GetCommandHistory()->FinishTemporaryCommands();
  else
    GetDocument()->GetCommandHistory()->CancelTemporaryCommands();

  UpdateCurveEditor();
}

// ---- event track dock ------------------------------------------------------

void plSequenceAssetDocumentWindow::UpdateEventTrackEditor()
{
  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());
  m_pEventTrackEditor->SetData(pDoc->GetProperties()->m_EventTrack, pDoc->GetDuration().GetSeconds());
  m_pEventTrackEditor->SetScrubberPosition(static_cast<double>(pDoc->GetScrubberPosition()) / static_cast<double>(plSequenceTicksPerSecond));
}

plUuid plSequenceAssetDocumentWindow::GetEventCpGuid(plUInt32 uiIndex) const
{
  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());

  const plDocumentObject* pTrackObj = pDoc->GetEventTrackObject();
  if (pTrackObj == nullptr)
    return plUuid();

  const plVariant cpGuid = pTrackObj->GetTypeAccessor().GetValue("ControlPoints", uiIndex);
  return cpGuid.IsA<plUuid>() ? cpGuid.Get<plUuid>() : plUuid();
}

void plSequenceAssetDocumentWindow::OnEventTrackInsertCpAt(plInt64 iTickX, plStringView sValue)
{
  static_cast<plSequenceAssetDocument*>(GetDocument())->InsertEventTrackCpAt(plMath::Max<plInt64>(iTickX, 0), plString(sValue).GetData());
}

void plSequenceAssetDocumentWindow::OnEventTrackCpMoved(plUInt32 uiCpIdx, plInt64 iTickX)
{
  const plUuid cpGuid = GetEventCpGuid(uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  plSetObjectPropertyCommand cmd;
  cmd.m_Object = cpGuid;
  cmd.m_sProperty = "Tick";
  cmd.m_NewValue = plMath::Max<plInt64>(iTickX, 0);
  GetDocument()->GetCommandHistory()->AddCommand(cmd).AssertSuccess();
}

void plSequenceAssetDocumentWindow::OnEventTrackCpDeleted(plUInt32 uiCpIdx)
{
  const plUuid cpGuid = GetEventCpGuid(uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  plRemoveObjectCommand cmd;
  cmd.m_Object = cpGuid;
  GetDocument()->GetCommandHistory()->AddCommand(cmd).AssertSuccess();
}

void plSequenceAssetDocumentWindow::OnEventTrackBeginCpChanges(plStringView sName)
{
  GetDocument()->GetCommandHistory()->StartTransaction(sName);
}

void plSequenceAssetDocumentWindow::OnEventTrackEndCpChanges()
{
  GetDocument()->GetCommandHistory()->FinishTransaction();
  UpdateEventTrackEditor();
}

void plSequenceAssetDocumentWindow::OnEventTrackBeginOperation(plStringView sName)
{
  GetDocument()->GetCommandHistory()->BeginTemporaryCommands(sName);
}

void plSequenceAssetDocumentWindow::OnEventTrackEndOperation(bool bCommit)
{
  if (bCommit)
    GetDocument()->GetCommandHistory()->FinishTemporaryCommands();
  else
    GetDocument()->GetCommandHistory()->CancelTemporaryCommands();

  UpdateEventTrackEditor();
}


void plSequenceAssetDocumentWindow::BeginCpChanges(plStringView sName)
{
  GetDocument()->GetCommandHistory()->StartTransaction(sName);
}

void plSequenceAssetDocumentWindow::EndCpChanges()
{
  GetDocument()->GetCommandHistory()->FinishTransaction();

  UpdateCurveEditor();
}

void plSequenceAssetDocumentWindow::InternalRedraw()
{
  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());
  pDoc->ExecuteAnimationPlaybackStep();

  // Pilot: put the viewport behind the camera the active cut selects, so scrubbing shows the shot
  // rather than the editor's free camera. Applied before SyncToEngine below, so the engine renders
  // this frame from it.
  if (pDoc->GetPilotCamera())
  {
    plTransform cameraTransform;
    float fFov = 60.0f;

    const bool bResolved = pDoc->GetActiveCutCamera(cameraTransform, fFov);

    // Report only on change - this runs every frame, and a silent failure here is exactly what
    // makes pilot look broken rather than unconfigured.
    if ((plInt8)bResolved != m_iPilotResolved)
    {
      m_iPilotResolved = (plInt8)bResolved;

      if (bResolved)
      {
        plLog::Info("Sequencer pilot: camera resolved at {0},{1},{2}, fov {3}, driving {4} view(s)", plArgF(cameraTransform.m_vPosition.x, 2),
          plArgF(cameraTransform.m_vPosition.y, 2), plArgF(cameraTransform.m_vPosition.z, 2), plArgF(fFov, 1), m_ViewWidgets.GetCount());
      }
      else
      {
        plLog::Info("Sequencer pilot: no camera at the playhead - either no cut has started yet, or its camera is not in the attached scene.");
      }
    }

    if (bResolved)
    {
      // Plasma game objects look down +X with +Z up
      const plVec3 vPosition = cameraTransform.m_vPosition;
      const plVec3 vForward = cameraTransform.m_qRotation * plVec3(1, 0, 0);
      const plVec3 vUp = cameraTransform.m_qRotation * plVec3(0, 0, 1);

      for (auto pView : m_ViewWidgets)
      {
        plEngineViewConfig* pConfig = pView->m_pViewConfig;
        pConfig->m_Camera.SetCameraMode(plCameraMode::PerspectiveFixedFovY, fFov, pConfig->m_Camera.GetNearPlane(), pConfig->m_Camera.GetFarPlane());
        pConfig->m_Camera.LookAt(vPosition, vPosition + vForward, vUp);
      }
    }
  }

  plEditorInputContext::UpdateActiveInputContext();
  {
    // do not try to redraw while the process is crashed, it is obviously futile
    if (plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
      return;

    {
      plSimulationSettingsMsgToEngine msg;
      msg.m_bSimulateWorld = false;
      GetEditorEngineConnection()->SendMessage(&msg);
    }
    {
      plGridSettingsMsgToEngine msg = GetGridSettings();
      GetEditorEngineConnection()->SendMessage(&msg);
    }
    {
      plWorldSettingsMsgToEngine msg = GetWorldSettings();
      GetEditorEngineConnection()->SendMessage(&msg);
    }

    GetGameObjectDocument()->SendObjectSelection();

    auto pHoveredView = GetHoveredViewWidget();

    for (auto pView : m_ViewWidgets)
    {
      pView->SetEnablePicking(pView == pHoveredView);
      pView->UpdateCameraInterpolation();
      pView->SyncToEngine();
    }
  }
  plEngineDocumentWindow::InternalRedraw();
}
