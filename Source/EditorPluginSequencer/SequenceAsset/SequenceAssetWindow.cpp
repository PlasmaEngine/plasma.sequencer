#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorFramework/Assets/AssetStatusIndicator.moc.h>
#include <EditorFramework/DocumentWindow/GameObjectViewWidget.moc.h>
#include <EditorFramework/DocumentWindow/QuadViewWidget.moc.h>
#include <EditorFramework/InputContexts/EditorInputContext.h>
#include <EditorFramework/Panels/GameObjectPanel/GameObjectModel.moc.h>
#include <EditorFramework/Panels/GameObjectPanel/GameObjectPanel.moc.h>
#include <EditorFramework/Preferences/EditorPreferences.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAssetWindow.moc.h>
#include <EditorPluginSequencer/SequenceAsset/SequencerPanel.moc.h>
#include <GuiFoundation/ActionViews/MenuBarActionMapView.moc.h>
#include <GuiFoundation/ActionViews/ToolBarActionMapView.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>
#include <GuiFoundation/Widgets/Curve1DEditorWidget.moc.h>
#include <GuiFoundation/Widgets/EventTrackEditorWidget.moc.h>
#include <ToolsFoundation/Command/TreeCommands.h>

#include <QLabel>
#include <QVBoxLayout>

plQtSequenceAssetDocumentWindow::plQtSequenceAssetDocumentWindow(plSequenceAssetDocument* pDocument)
  : plQtGameObjectDocumentWindow(pDocument)
{
  auto ViewFactory = [](plQtEngineDocumentWindow* pWindow, plEngineViewConfig* pConfig) -> plQtEngineViewWidget*
  {
    plQtGameObjectViewWidget* pWidget = new plQtGameObjectViewWidget(nullptr, static_cast<plQtSequenceAssetDocumentWindow*>(pWindow), pConfig);
    pWindow->AddViewWidget(pWidget);
    return pWidget;
  };
  m_pQuadViewWidget = new plQtQuadViewWidget(pDocument, this, ViewFactory, "SequenceAssetViewToolBar");

  pDocument->SetEditToolConfigDelegate(
    [this](plGameObjectEditTool* pTool) { pTool->ConfigureTool(static_cast<plGameObjectDocument*>(GetDocument()), this, this); });

  // 3D viewport of the attached context scene
  {
    plQtDocumentPanel* pViewPanel = new plQtDocumentPanel(this, pDocument);
    pViewPanel->setObjectName("plQtDocumentPanel");
    pViewPanel->setWindowTitle("3D View");
    pViewPanel->setWidget(m_pQuadViewWidget);
    m_pDockManager->setCentralWidget(pViewPanel);
  }

  plEditorPreferencesUser* pPreferences = plPreferences::QueryPreferences<plEditorPreferencesUser>();
  SetTargetFramerate(pPreferences->GetMaxFramerate());

  // Menu Bar
  {
    plQtMenuBarActionMapView* pMenuBar = static_cast<plQtMenuBarActionMapView*>(menuBar());
    plActionContext context;
    context.m_sMapping = "SequenceAssetMenuBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pMenuBar->SetActionContext(context);
  }

  // Tool Bar
  {
    plQtToolBarActionMapView* pToolBar = new plQtToolBarActionMapView("Toolbar", this);
    plActionContext context;
    context.m_sMapping = "SequenceAssetToolBar";
    context.m_pDocument = pDocument;
    context.m_pWindow = this;
    pToolBar->SetActionContext(context);
    pToolBar->setObjectName("SequenceAssetWindowToolBar");
    addToolBar(pToolBar);
  }

  // Scenegraph of the context document
  {
    std::unique_ptr<plQtDocumentTreeModel> pModel(new plQtGameObjectModel(pDocument->GetObjectManager()));
    pModel->AddAdapter(new plQtDummyAdapter(pDocument->GetObjectManager(), plGetStaticRTTI<plDocumentRoot>(), "TempObjects"));
    pModel->AddAdapter(new plQtGameObjectAdapter(pDocument->GetObjectManager()));

    plQtDocumentPanel* pGameObjectPanel = new plQtGameObjectPanel(this, pDocument, "SequenceAsset_ScenegraphContextMenu", std::move(pModel));
    m_pDockManager->addDockWidget(ads::LeftDockWidgetArea, pGameObjectPanel);
  }

  // Property Grid
  {
    plQtDocumentPanel* pPanel = new plQtDocumentPanel(this, pDocument);
    pPanel->setObjectName("SequenceAssetPropertiesDockWidget");
    pPanel->setWindowTitle("Properties");
    pPanel->show();

    plQtPropertyGridWidget* pPropertyGrid = new plQtPropertyGridWidget(pPanel, pDocument);
    QWidget* pWidget = new QWidget();
    pWidget->setObjectName("Group");
    pWidget->setLayout(new QVBoxLayout());
    pWidget->setContentsMargins(0, 0, 0, 0);
    pWidget->layout()->setContentsMargins(0, 0, 0, 0);
    pWidget->layout()->addWidget(new plQtAssetStatusIndicator(GetDocument()));
    pWidget->layout()->addWidget(pPropertyGrid);

    pPanel->setWidget(pWidget, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidget(ads::RightDockWidgetArea, pPanel);
  }

  // Sequencer panel: transport + binding outliner + timeline canvas
  {
    plQtDocumentPanel* pPanel = new plQtDocumentPanel(this, pDocument);
    pPanel->setObjectName("SequenceAssetSequencerDockWidget");
    pPanel->setWindowTitle("Sequencer");
    pPanel->show();

    m_pSequencerPanel = new plQtSequencerPanel(pDocument, pPanel);
    pPanel->setWidget(m_pSequencerPanel, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidget(ads::BottomDockWidgetArea, pPanel);
  }

  // Curve editor dock: values and tangents of the selected track (the canvas edits timing only)
  {
    plQtDocumentPanel* pPanel = new plQtDocumentPanel(this, pDocument);
    pPanel->setObjectName("SequenceAssetCurveDockWidget");
    pPanel->setWindowTitle("Curve Editor");
    pPanel->show();

    m_pCurveEditor = new plQtCurve1DEditorWidget(pPanel);
    pPanel->setWidget(m_pCurveEditor, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidget(ads::BottomDockWidgetArea, pPanel);

    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::InsertCpEvent, this, &plQtSequenceAssetDocumentWindow::onCurveInsertCpAt);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::CpMovedEvent, this, &plQtSequenceAssetDocumentWindow::onCurveCpMoved);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::CpDeletedEvent, this, &plQtSequenceAssetDocumentWindow::onCurveCpDeleted);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::TangentMovedEvent, this, &plQtSequenceAssetDocumentWindow::onCurveTangentMoved);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::TangentLinkEvent, this, &plQtSequenceAssetDocumentWindow::onLinkCurveTangents);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::CpTangentModeEvent, this, &plQtSequenceAssetDocumentWindow::onCurveTangentModeChanged);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::BeginOperationEvent, this, &plQtSequenceAssetDocumentWindow::onCurveBeginOperation);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::EndOperationEvent, this, &plQtSequenceAssetDocumentWindow::onCurveEndOperation);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::BeginCpChangesEvent, this, &plQtSequenceAssetDocumentWindow::onCurveBeginCpChanges);
    connect(m_pCurveEditor, &plQtCurve1DEditorWidget::EndCpChangesEvent, this, &plQtSequenceAssetDocumentWindow::onCurveEndCpChanges);
  }

  // Event track dock: sequence-level, so unlike the curve dock it needs no selection to be useful
  {
    plQtDocumentPanel* pPanel = new plQtDocumentPanel(this, pDocument);
    pPanel->setObjectName("SequenceAssetEventDockWidget");
    pPanel->setWindowTitle("Events");
    pPanel->show();

    m_pEventTrackEditor = new plQtEventTrackEditorWidget(pPanel);
    pPanel->setWidget(m_pEventTrackEditor, ads::CDockWidget::ForceNoScrollArea);

    m_pDockManager->addDockWidgetTab(ads::BottomDockWidgetArea, pPanel);

    connect(m_pEventTrackEditor, &plQtEventTrackEditorWidget::InsertCpEvent, this, &plQtSequenceAssetDocumentWindow::onEventTrackInsertCpAt);
    connect(m_pEventTrackEditor, &plQtEventTrackEditorWidget::CpMovedEvent, this, &plQtSequenceAssetDocumentWindow::onEventTrackCpMoved);
    connect(m_pEventTrackEditor, &plQtEventTrackEditorWidget::CpDeletedEvent, this, &plQtSequenceAssetDocumentWindow::onEventTrackCpDeleted);
    connect(m_pEventTrackEditor, &plQtEventTrackEditorWidget::BeginCpChangesEvent, this, &plQtSequenceAssetDocumentWindow::onEventTrackBeginCpChanges);
    connect(m_pEventTrackEditor, &plQtEventTrackEditorWidget::EndCpChangesEvent, this, &plQtSequenceAssetDocumentWindow::onEventTrackEndCpChanges);
    connect(m_pEventTrackEditor, &plQtEventTrackEditorWidget::BeginOperationEvent, this, &plQtSequenceAssetDocumentWindow::onEventTrackBeginOperation);
    connect(m_pEventTrackEditor, &plQtEventTrackEditorWidget::EndOperationEvent, this, &plQtSequenceAssetDocumentWindow::onEventTrackEndOperation);
  }

  connect(m_pSequencerPanel, &plQtSequencerPanel::TrackSelectionChanged, this, &plQtSequenceAssetDocumentWindow::onTrackSelectionChanged);
  connect(m_pSequencerPanel, &plQtSequencerPanel::KeyPicked, this, &plQtSequenceAssetDocumentWindow::onKeyPicked);

  pDocument->m_SequenceEvents.AddEventHandler(plMakeDelegate(&plQtSequenceAssetDocumentWindow::SequenceEventHandler, this));
  pDocument->GetCommandHistory()->m_Events.AddEventHandler(plMakeDelegate(&plQtSequenceAssetDocumentWindow::CommandHistoryEventHandler, this));

  m_CurvesToDisplay.m_bOwnsData = false;
  UpdateCurveEditor();
  UpdateEventTrackEditor();

  FinishWindowCreation();
}

plQtSequenceAssetDocumentWindow::~plQtSequenceAssetDocumentWindow()
{
  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());
  pDoc->m_SequenceEvents.RemoveEventHandler(plMakeDelegate(&plQtSequenceAssetDocumentWindow::SequenceEventHandler, this));
  pDoc->GetCommandHistory()->m_Events.RemoveEventHandler(plMakeDelegate(&plQtSequenceAssetDocumentWindow::CommandHistoryEventHandler, this));
}

plObjectAccessorBase* plQtSequenceAssetDocumentWindow::GetObjectAccessor()
{
  return GetDocument()->GetObjectAccessor();
}

// ---- curve editor dock -----------------------------------------------------

void plQtSequenceAssetDocumentWindow::SequenceEventHandler(const plSequenceAssetEvent& e)
{
  switch (e.m_Type)
  {
    case plSequenceAssetEvent::Type::ScrubberPositionChanged:
    {
      const plUInt64 uiTick = static_cast<const plSequenceAssetDocument*>(e.m_pDocument)->GetScrubberPosition();
      m_pCurveEditor->SetScrubberPosition(uiTick);
      m_pEventTrackEditor->SetScrubberPosition(uiTick);
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

void plQtSequenceAssetDocumentWindow::CommandHistoryEventHandler(const plCommandHistoryEvent& e)
{
  if (e.m_Type == plCommandHistoryEvent::Type::TransactionEnded || e.m_Type == plCommandHistoryEvent::Type::UndoEnded ||
      e.m_Type == plCommandHistoryEvent::Type::RedoEnded)
  {
    UpdateCurveEditor();
    UpdateEventTrackEditor();
  }
}

void plQtSequenceAssetDocumentWindow::onTrackSelectionChanged()
{
  const plUuid selected = m_pSequencerPanel->GetSelectedTrack();
  if (m_SelectedTrackGuid == selected)
    return;

  m_SelectedTrackGuid = selected;
  UpdateCurveEditor();
}

void plQtSequenceAssetDocumentWindow::onKeyPicked(plUuid trackGuid, plUuid cpGuid)
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
      m_pCurveEditor->CurveEdit->SetSelection(cp);
      return;
    }
  }
}

void plQtSequenceAssetDocumentWindow::UpdateCurveEditor()
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

  m_pCurveEditor->SetCurveExtents(0, pDoc->GetDuration().GetSeconds(), true, true);
  m_pCurveEditor->SetCurves(m_CurvesToDisplay);
  m_pCurveEditor->SetScrubberPosition(pDoc->GetScrubberPosition());
}

plUuid plQtSequenceAssetDocumentWindow::GetCurveTrackGuid(plUInt32 uiCurveIdx) const
{
  // With a vector group selected the dock shows several curves, so which track an edit belongs to
  // comes from the curve index the widget reports, not from "the" selected track.
  return uiCurveIdx < m_CurveTrackGuids.GetCount() ? m_CurveTrackGuids[uiCurveIdx] : plUuid();
}

plUuid plQtSequenceAssetDocumentWindow::GetCurveCpGuid(plUInt32 uiCurveIdx, plUInt32 uiCpIdx) const
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

void plQtSequenceAssetDocumentWindow::onCurveInsertCpAt(plUInt32 uiCurveIdx, plInt64 iTickX, double fClickPosY)
{
  const plUuid trackGuid = GetCurveTrackGuid(uiCurveIdx);
  if (!trackGuid.IsValid())
    return;

  static_cast<plSequenceAssetDocument*>(GetDocument())->InsertCurveCpAt(trackGuid, iTickX, fClickPosY);
}

void plQtSequenceAssetDocumentWindow::onCurveCpMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, plInt64 iTickX, double fNewPosY)
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

void plQtSequenceAssetDocumentWindow::onCurveCpDeleted(plUInt32 uiCurveIdx, plUInt32 uiCpIdx)
{


  const plUuid cpGuid = GetCurveCpGuid(uiCurveIdx, uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  plRemoveObjectCommand cmdRemove;
  cmdRemove.m_Object = cpGuid;
  GetDocument()->GetCommandHistory()->AddCommand(cmdRemove).AssertSuccess();
}

void plQtSequenceAssetDocumentWindow::onCurveTangentMoved(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, float fNewPosX, float fNewPosY, bool bRightTangent)
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

void plQtSequenceAssetDocumentWindow::onLinkCurveTangents(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bLink)
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
        onCurveTangentMoved(uiCurveIdx, uiCpIdx, -leftTangent.x, -leftTangent.y, true);
      }
    }
  }
}

void plQtSequenceAssetDocumentWindow::onCurveTangentModeChanged(plUInt32 uiCurveIdx, plUInt32 uiCpIdx, bool bRightTangent, int iMode)
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

void plQtSequenceAssetDocumentWindow::onCurveBeginOperation(QString sName)
{
  GetDocument()->GetCommandHistory()->BeginTemporaryCommands(sName.toUtf8().data());
}

void plQtSequenceAssetDocumentWindow::onCurveEndOperation(bool bCommit)
{
  if (bCommit)
    GetDocument()->GetCommandHistory()->FinishTemporaryCommands();
  else
    GetDocument()->GetCommandHistory()->CancelTemporaryCommands();

  UpdateCurveEditor();
}

// ---- event track dock ------------------------------------------------------

void plQtSequenceAssetDocumentWindow::UpdateEventTrackEditor()
{
  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());
  m_pEventTrackEditor->SetData(pDoc->GetProperties()->m_EventTrack, pDoc->GetDuration().GetSeconds());
  m_pEventTrackEditor->SetScrubberPosition(pDoc->GetScrubberPosition());
}

plUuid plQtSequenceAssetDocumentWindow::GetEventCpGuid(plUInt32 uiIndex) const
{
  plSequenceAssetDocument* pDoc = static_cast<plSequenceAssetDocument*>(GetDocument());

  const plDocumentObject* pTrackObj = pDoc->GetEventTrackObject();
  if (pTrackObj == nullptr)
    return plUuid();

  const plVariant cpGuid = pTrackObj->GetTypeAccessor().GetValue("ControlPoints", uiIndex);
  return cpGuid.IsA<plUuid>() ? cpGuid.Get<plUuid>() : plUuid();
}

void plQtSequenceAssetDocumentWindow::onEventTrackInsertCpAt(plInt64 iTickX, QString sValue)
{
  static_cast<plSequenceAssetDocument*>(GetDocument())->InsertEventTrackCpAt(plMath::Max<plInt64>(iTickX, 0), sValue.toUtf8().data());
}

void plQtSequenceAssetDocumentWindow::onEventTrackCpMoved(plUInt32 uiCpIdx, plInt64 iTickX)
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

void plQtSequenceAssetDocumentWindow::onEventTrackCpDeleted(plUInt32 uiCpIdx)
{
  const plUuid cpGuid = GetEventCpGuid(uiCpIdx);
  if (!cpGuid.IsValid())
    return;

  plRemoveObjectCommand cmd;
  cmd.m_Object = cpGuid;
  GetDocument()->GetCommandHistory()->AddCommand(cmd).AssertSuccess();
}

void plQtSequenceAssetDocumentWindow::onEventTrackBeginCpChanges(QString sName)
{
  GetDocument()->GetCommandHistory()->StartTransaction(sName.toUtf8().data());
}

void plQtSequenceAssetDocumentWindow::onEventTrackEndCpChanges()
{
  GetDocument()->GetCommandHistory()->FinishTransaction();
  UpdateEventTrackEditor();
}

void plQtSequenceAssetDocumentWindow::onEventTrackBeginOperation(QString sName)
{
  GetDocument()->GetCommandHistory()->BeginTemporaryCommands(sName.toUtf8().data());
}

void plQtSequenceAssetDocumentWindow::onEventTrackEndOperation(bool bCommit)
{
  if (bCommit)
    GetDocument()->GetCommandHistory()->FinishTemporaryCommands();
  else
    GetDocument()->GetCommandHistory()->CancelTemporaryCommands();

  UpdateEventTrackEditor();
}


void plQtSequenceAssetDocumentWindow::onCurveBeginCpChanges(QString sName)
{
  GetDocument()->GetCommandHistory()->StartTransaction(sName.toUtf8().data());
}

void plQtSequenceAssetDocumentWindow::onCurveEndCpChanges()
{
  GetDocument()->GetCommandHistory()->FinishTransaction();

  UpdateCurveEditor();
}

void plQtSequenceAssetDocumentWindow::InternalRedraw()
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
  plQtEngineDocumentWindow::InternalRedraw();
}
