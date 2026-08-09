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
#include <GuiFoundation/ActionViews/MenuBarActionMapView.moc.h>
#include <GuiFoundation/ActionViews/ToolBarActionMapView.moc.h>
#include <GuiFoundation/DockPanels/DocumentPanel.moc.h>
#include <GuiFoundation/PropertyGrid/PropertyGridWidget.moc.h>

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

  // Sequencer panel - placeholder until the P1 outliner + track view land
  {
    plQtDocumentPanel* pPanel = new plQtDocumentPanel(this, pDocument);
    pPanel->setObjectName("SequenceAssetSequencerDockWidget");
    pPanel->setWindowTitle("Sequencer");
    pPanel->show();

    QLabel* pPlaceholder = new QLabel(
      "The sequencer timeline lands with the next milestone:\nbinding outliner, track lanes, transport and curve editing.\n\n"
      "See Documentation/Sequencer/ in the engine repository for the plan and mockups.");
    pPlaceholder->setAlignment(Qt::AlignCenter);
    pPlaceholder->setEnabled(false);
    pPanel->setWidget(pPlaceholder);

    m_pDockManager->addDockWidget(ads::BottomDockWidgetArea, pPanel);
  }

  FinishWindowCreation();
}

plQtSequenceAssetDocumentWindow::~plQtSequenceAssetDocumentWindow() = default;

plObjectAccessorBase* plQtSequenceAssetDocumentWindow::GetObjectAccessor()
{
  return GetDocument()->GetObjectAccessor();
}

void plQtSequenceAssetDocumentWindow::InternalRedraw()
{
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
