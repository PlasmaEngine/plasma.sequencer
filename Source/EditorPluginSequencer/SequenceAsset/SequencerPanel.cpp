#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <Core/World/Component.h>
#include <Core/World/GameObject.h>
#include <EditorPluginSequencer/SequenceAsset/SequencerPanel.moc.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceRenderDialog.moc.h>
#include <GuiFoundation/UIServices/DynamicStringEnum.h>
#include <GuiFoundation/Widgets/SearchableMenu.moc.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace
{
  /// Paints outliner rows in the concept's visual language (Mockup_MainWindow.html): binding rows
  /// as bold headers on AlternateBase, track rows with a 3px type strip and dimmed sub-text.
  /// Colors follow the mockup's token table - chrome from QPalette, selection from Plasma.qss.
  class plQtSequenceOutlinerDelegate : public QStyledItemDelegate
  {
  public:
    explicit plQtSequenceOutlinerDelegate(QObject* pParent)
      : QStyledItemDelegate(pParent)
    {
    }

    virtual void paint(QPainter* pPainter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
      const bool bIsBinding = index.data(plQtSequenceModel::IsBindingRole).toBool();
      const bool bSelected = option.state.testFlag(QStyle::State_Selected);
      const bool bHovered = option.state.testFlag(QStyle::State_MouseOver);
      const QColor typeColor = index.data(plQtSequenceModel::TrackColorRole).value<QColor>();
      const QString sText = index.data(Qt::DisplayRole).toString();
      const QString sSub = index.data(plQtSequenceModel::SubTextRole).toString();

      pPainter->save();

      // row background
      QColor bg;
      if (bSelected)
        bg = QColor(0x08, 0x52, 0x42); // Plasma.qss accent (QTreeView::item:selected)
      else if (bHovered)
        bg = QColor(0x3a, 0x3a, 0x3a); // Midlight
      else if (bIsBinding)
        bg = QColor(0x2d, 0x2d, 0x2d); // AlternateBase - binding header rows
      else
        bg = Qt::transparent;

      if (bg.alpha() > 0)
        pPainter->fillRect(option.rect, bg);

      QRect content = option.rect.adjusted(4, 0, -4, 0);

      // type strip
      {
        QRect strip(content.left(), option.rect.top() + 4, 3, option.rect.height() - 8);
        pPainter->setPen(Qt::NoPen);
        pPainter->setBrush(typeColor);
        pPainter->drawRect(strip);
        content.setLeft(strip.right() + 6);
      }

      // component icon - what the bound object actually is
      {
        const QIcon icon = index.data(plQtSequenceModel::IconRole).value<QIcon>();
        if (!icon.isNull())
        {
          const int iSize = 14;
          QRect iconRect(content.left(), option.rect.center().y() - iSize / 2, iSize, iSize);
          icon.paint(pPainter, iconRect);
          content.setLeft(iconRect.right() + 6);
        }
      }

      // primary text
      QFont font = option.font;
      font.setBold(bIsBinding);
      pPainter->setFont(font);
      pPainter->setPen(bSelected ? QColor(0xff, 0xff, 0xff) : (bIsBinding ? QColor(0xdd, 0xdd, 0xdd) : QColor(0xcc, 0xcc, 0xcc)));

      const QFontMetrics fm(font);
      const int iTextWidth = fm.horizontalAdvance(sText);
      pPainter->drawText(content, Qt::AlignVCenter | Qt::AlignLeft, sText);

      // dimmed sub-text after the name
      if (!sSub.isEmpty())
      {
        QRect subRect = content;
        subRect.setLeft(content.left() + iTextWidth + 8);

        if (subRect.width() > 20)
        {
          QFont subFont = option.font;
          subFont.setBold(false);
          pPainter->setFont(subFont);
          pPainter->setPen(bSelected ? QColor(0xbf, 0xe8, 0xdc) : QColor(0x8e, 0x8e, 0x8e)); // PlaceholderText
          pPainter->drawText(subRect, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(subFont).elidedText(sSub, Qt::ElideRight, subRect.width()));
        }
      }

      pPainter->restore();
    }

    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
      QSize size = QStyledItemDelegate::sizeHint(option, index);

      // must match the canvas lane heights exactly, or labels shear against their lanes
      if (index.data(plQtSequenceModel::IsCameraCutLaneRole).toBool())
        size.setHeight(plQtSequenceTrackView::CameraLaneHeight);
      else if (index.data(plQtSequenceModel::IsClipLaneRole).toBool())
        size.setHeight(plQtSequenceTrackView::ClipLaneHeight);
      else
        size.setHeight(plQtSequenceTrackView::RowHeight);

      return size;
    }
  };
} // namespace

plQtSequencerPanel::plQtSequencerPanel(plSequenceAssetDocument* pDocument, QWidget* pParent)
  : QWidget(pParent)
  , m_pDocument(pDocument)
{
  m_pModel = new plQtSequenceModel(pDocument, this);

  QVBoxLayout* pLayout = new QVBoxLayout(this);
  pLayout->setContentsMargins(0, 0, 0, 0);
  pLayout->setSpacing(0);

  // transport. Deliberately NOT plQtTimeScrubberToolbar: that widget carries its own full-width
  // time ruler, which duplicated - and misaligned against - the canvas ruler that actually lines
  // up with the lanes. The canvas ruler is the single source of time here; this bar is controls.
  pLayout->addWidget(CreateTransportBar());

  // outliner | canvas
  QSplitter* pSplitter = new QSplitter(Qt::Horizontal, this);
  pLayout->addWidget(pSplitter, 1);

  // left: header buttons + tree
  {
    QWidget* pLeft = new QWidget(pSplitter);
    QVBoxLayout* pLeftLayout = new QVBoxLayout(pLeft);
    pLeftLayout->setContentsMargins(0, 0, 0, 0);
    pLeftLayout->setSpacing(0);

    // Column header, the outliner's counterpart to the canvas ruler. It must be EXACTLY as tall as
    // that ruler: tree row N and canvas lane N share their y offset from the top of the splitter,
    // so any difference here shears the labels against their lanes.
    QLabel* pHeader = new QLabel("BINDINGS / TRACKS", pLeft);
    pHeader->setFixedHeight(plQtSequenceTrackView::RulerHeight);
    pHeader->setIndent(9);
    pHeader->setStyleSheet("QLabel { background: #505050; color: #d0d0d0; font-size: 10px; letter-spacing: 1px; }");
    pLeftLayout->addWidget(pHeader);

    m_pOutliner = new QTreeView(pLeft);
    m_pOutliner->setModel(m_pModel);
    m_pOutliner->setHeaderHidden(true);
    m_pOutliner->setExpandsOnDoubleClick(false);
    m_pOutliner->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_pOutliner->setMouseTracking(true); // hover states in the delegate
    m_pOutliner->setItemDelegate(new plQtSequenceOutlinerDelegate(m_pOutliner));
    m_pOutliner->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pOutliner, &QTreeView::customContextMenuRequested, this, &plQtSequencerPanel::onOutlinerContextMenu);

    // Del removes the selected track or binding. Scoped to the tree so it does not fight the
    // canvas, where Del removes the selected key.
    QShortcut* pDelete = new QShortcut(QKeySequence(Qt::Key_Delete), m_pOutliner);
    pDelete->setContext(Qt::WidgetShortcut);
    connect(pDelete, &QShortcut::activated, this, &plQtSequencerPanel::onDeleteSelected);
    // Row heights come from the delegate's sizeHint so they can vary per row; the engine qss gives
    // QTreeView::item 3px padding, which would inflate them past the lanes, so zero it here.
    m_pOutliner->setUniformRowHeights(false);
    m_pOutliner->setStyleSheet("QTreeView::item { padding: 0px; margin: 0px; }");
    pLeftLayout->addWidget(m_pOutliner, 1);

    pSplitter->addWidget(pLeft);
  }

  // right: timeline canvas
  {
    m_pTrackView = new plQtSequenceTrackView(pSplitter);
    m_pTrackView->SetModel(m_pModel);
    pSplitter->addWidget(m_pTrackView);
  }

  pSplitter->setStretchFactor(0, 0);
  pSplitter->setStretchFactor(1, 1);
  pSplitter->setSizes({280, 800});

  // expand all bindings whenever the model rebuilds, so tree rows match the canvas's flat rows,
  // and restore the selection the reset just cleared
  connect(m_pModel, &plQtSequenceModel::ModelRebuilt, this, [this]() {
    m_pOutliner->expandAll();

    if (m_LastSelectedGuid.IsValid())
    {
      const QModelIndex restored = FindIndexForGuid(m_LastSelectedGuid);
      if (restored.isValid())
        m_pOutliner->setCurrentIndex(restored);
    }

    Q_EMIT TrackSelectionChanged(); });
  m_pOutliner->expandAll();

  // scroll sync tree -> canvas
  connect(m_pOutliner->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int iValue) { m_pTrackView->SetVerticalOffset(iValue); });

  // Wheeling the canvas scrolls the outliner's scrollbar rather than the canvas offset directly:
  // that bar is the single source of the scroll position, and driving it keeps the labels and the
  // lanes from drifting apart.
  connect(m_pTrackView, &plQtSequenceTrackView::ScrollByRequested, this, [this](int iDelta) {
    QScrollBar* pBar = m_pOutliner->verticalScrollBar();
    pBar->setValue(plMath::Clamp(pBar->value() + iDelta, pBar->minimum(), pBar->maximum())); });

  // selection -> curve dock (via the window)
  connect(m_pOutliner->selectionModel(), &QItemSelectionModel::currentChanged, this,
    [this](const QModelIndex& current, const QModelIndex&) {
      if (const plUuid guid = GuidFromIndex(current); guid.IsValid())
        m_LastSelectedGuid = guid;
      Q_EMIT TrackSelectionChanged(); });

  // selection -> highlighted lane in the canvas
  connect(this, &plQtSequencerPanel::TrackSelectionChanged, this, [this]() { m_pTrackView->SetHighlightedTrack(GetSelectedTrack()); });

  // key click in the canvas selects its track row, then reports the exact key
  connect(m_pTrackView, &plQtSequenceTrackView::KeySelected, this, [this](plUuid trackGuid, plUuid cpGuid) {
    const QModelIndex trackIndex = FindIndexForGuid(trackGuid);
    if (trackIndex.isValid() && m_pOutliner->currentIndex() != trackIndex)
      m_pOutliner->setCurrentIndex(trackIndex);
    Q_EMIT KeyPicked(trackGuid, cpGuid); });

  // canvas -> document
  connect(m_pTrackView, &plQtSequenceTrackView::ScrubRequested, this, [this](plUInt64 uiTick) { m_pDocument->SetScrubberPosition(uiTick); });

  // document -> panel
  m_pDocument->m_SequenceEvents.AddEventHandler(plMakeDelegate(&plQtSequencerPanel::SequenceEventHandler, this));

  UpdateTransportState();
}

QWidget* plQtSequencerPanel::CreateTransportBar()
{
  // Height accounts for the engine stylesheet's input metrics: Plasma.qss gives QComboBox and
  // QAbstractSpinBox min-height 20px plus 4px padding and a 1px border, i.e. ~30px natural height.
  // Squeezing them below that clips their text and arrows.
  constexpr int iControlHeight = 26;
  constexpr int iBarHeight = iControlHeight + 10;

  QWidget* pBar = new QWidget(this);
  pBar->setFixedHeight(iBarHeight);
  pBar->setStyleSheet("QWidget { background: #1e1e1e; }"); // QPalette::Window

  QHBoxLayout* pLayout = new QHBoxLayout(pBar);
  pLayout->setContentsMargins(6, 5, 6, 5);
  pLayout->setSpacing(2);

  auto addButton = [&](const char* szIcon, const char* szTooltip, bool bCheckable) -> QToolButton*
  {
    QToolButton* pButton = new QToolButton(pBar);
    pButton->setIcon(QIcon(QString::fromUtf8(szIcon)));
    pButton->setToolTip(QString::fromUtf8(szTooltip));
    pButton->setCheckable(bCheckable);
    pButton->setAutoRaise(true);
    pButton->setFixedSize(iControlHeight, iControlHeight);
    pLayout->addWidget(pButton);
    return pButton;
  };

  auto addSeparator = [&]()
  {
    QFrame* pLine = new QFrame(pBar);
    pLine->setFrameShape(QFrame::VLine);
    pLine->setFixedWidth(1);
    pLine->setStyleSheet("QFrame { background: #3c3c3c; border: none; margin: 5px 4px; }"); // QPalette::Mid
    pLayout->addWidget(pLine);
  };

  // Authoring actions first, directly above the outliner column they add rows to.
  m_pAddBindingButton = new QToolButton(pBar);
  m_pAddBindingButton->setText("+ Binding");
  m_pAddBindingButton->setProperty("class", "property-action");
  m_pAddBindingButton->setFixedHeight(iControlHeight);
  m_pAddBindingButton->setToolTip("Add a binding for the object selected in the Scenegraph panel.");
  connect(m_pAddBindingButton, &QToolButton::clicked, this, &plQtSequencerPanel::onAddBinding);
  pLayout->addWidget(m_pAddBindingButton);

  m_pAddTrackButton = new QToolButton(pBar);
  m_pAddTrackButton->setText("+ Track");
  m_pAddTrackButton->setProperty("class", "property-action");
  m_pAddTrackButton->setFixedHeight(iControlHeight);
  m_pAddTrackButton->setToolTip("Add a track to the selected binding.");
  connect(m_pAddTrackButton, &QToolButton::clicked, this, &plQtSequencerPanel::onAddTrack);
  pLayout->addWidget(m_pAddTrackButton);

  addSeparator();

  QToolButton* pToStart = addButton(":/GuiFoundation/Icons/ControlSkipBackwards.svg", "Jump to start", false);
  connect(pToStart, &QToolButton::clicked, this, &plQtSequencerPanel::onGoToStart);

  QToolButton* pPrevFrame = addButton(":/GuiFoundation/Icons/ControlSkipBackwards.svg", "Previous frame", false);
  connect(pPrevFrame, &QToolButton::clicked, this, [this]() { onStepFrame(-1); });

  m_pPlayButton = addButton(":/GuiFoundation/Icons/ControlPlay.svg", "Play / Pause", false);
  connect(m_pPlayButton, &QToolButton::clicked, this, &plQtSequencerPanel::onPlayPause);

  QToolButton* pNextFrame = addButton(":/GuiFoundation/Icons/ControlSkipForwards.svg", "Next frame", false);
  connect(pNextFrame, &QToolButton::clicked, this, [this]() { onStepFrame(+1); });

  QToolButton* pToEnd = addButton(":/GuiFoundation/Icons/ControlSkipForwards.svg", "Jump to end", false);
  connect(pToEnd, &QToolButton::clicked, this, &plQtSequencerPanel::onGoToEnd);

  m_pLoopButton = addButton(":/GuiFoundation/Icons/ControlRepeat.svg", "Loop playback", true);
  connect(m_pLoopButton, &QToolButton::clicked, this, &plQtSequencerPanel::onRepeat);

  addSeparator();

  // Auto-key: with it armed, moving an object with the gizmos (or editing it in the property grid)
  // records a key at the playhead instead of being discarded.
  m_pAutoKeyButton = new QToolButton(pBar);
  m_pAutoKeyButton->setText("● AUTO-KEY");
  m_pAutoKeyButton->setCheckable(true);
  m_pAutoKeyButton->setChecked(m_pDocument->GetAutoKey());
  m_pAutoKeyButton->setFixedHeight(iControlHeight);
  m_pAutoKeyButton->setToolTip("Record changes to the context scene as keys at the playhead.\nWith this off, the scene cannot be edited from here.");
  m_pAutoKeyButton->setStyleSheet("QToolButton { background: #38383c; border-radius: 3px; padding: 2px 8px; color: #b0b0b4; }"
                                  "QToolButton:checked { background: #5a2020; color: #ff8f8f; }");
  connect(m_pAutoKeyButton, &QToolButton::clicked, this, [this](bool bChecked) { m_pDocument->SetAutoKey(bChecked); });
  pLayout->addWidget(m_pAutoKeyButton);

  // Pilot: view through the camera the active cut selects while scrubbing.
  m_pPilotButton = new QToolButton(pBar);
  m_pPilotButton->setIcon(QIcon(":/TypeIcons/plCameraComponent.svg"));
  m_pPilotButton->setText("Pilot");
  m_pPilotButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_pPilotButton->setCheckable(true);
  m_pPilotButton->setChecked(m_pDocument->GetPilotCamera());
  m_pPilotButton->setFixedHeight(iControlHeight);
  m_pPilotButton->setToolTip("View through the camera of the active cut while scrubbing.");
  m_pPilotButton->setStyleSheet("QToolButton { background: #38383c; border-radius: 3px; padding: 2px 8px; color: #b0b0b4; }"
                                "QToolButton:checked { background: #085242; color: #ffffff; }");
  connect(m_pPilotButton, &QToolButton::clicked, this, [this](bool bChecked) { m_pDocument->SetPilotCamera(bChecked); });
  pLayout->addWidget(m_pPilotButton);

  // Render to disk. Sits next to Pilot because it renders through the same camera cuts Pilot shows.
  {
    QToolButton* pRender = new QToolButton(pBar);
    pRender->setText("Render…");
    pRender->setFixedHeight(iControlHeight);
    pRender->setToolTip("Render this sequence to an image sequence, and optionally a video.");
    pRender->setStyleSheet("QToolButton { background: #38383c; border-radius: 3px; padding: 2px 8px; color: #b0b0b4; }");
    connect(pRender, &QToolButton::clicked, this, [this]() {
      // Parentless and modal, which is how every dialog in the editor is opened - plQtCppClassDlg,
      // plQtExtractGeometryDlg and plQtExportProjectDlg all do exactly this. Modeless-and-parentless
      // is a combination nothing else uses, and it is what hid the Browse picker: a parentless
      // modeless window owns nothing, so the application-modal file dialog it spawns can stack
      // behind the main window - input blocked everywhere, and nothing visible to dismiss.
      //
      // exec() costs the render nothing: it runs a full nested event loop, so the engine replies
      // that drive frame capture still arrive.
      plQtSequenceRenderDialog dlg(m_pDocument, nullptr);
      dlg.exec(); });
    pLayout->addWidget(pRender);
  }

  addSeparator();

  // timecode readout - the mockup's monospace field on the qss input surface
  m_pTimecodeLabel = new QLabel(pBar);
  m_pTimecodeLabel->setTextFormat(Qt::RichText);
  m_pTimecodeLabel->setStyleSheet("QLabel { background: #38383c; border-radius: 4px; padding: 2px 9px;"
                                  " font-family: Consolas, monospace; color: #ffffff; }");
  pLayout->addWidget(m_pTimecodeLabel);

  m_pFpsLabel = new QLabel(pBar);
  m_pFpsLabel->setStyleSheet("QLabel { color: #8e8e8e; }"); // PlaceholderText
  pLayout->addWidget(m_pFpsLabel);

  addSeparator();

  {
    QLabel* pSnapLabel = new QLabel("Snap", pBar);
    pSnapLabel->setStyleSheet("QLabel { color: #8e8e8e; }");
    pLayout->addWidget(pSnapLabel);

    m_pSnapCombo = new QComboBox(pBar);
    m_pSnapCombo->addItem("Frame");
    m_pSnapCombo->addItem("Half Frame");
    m_pSnapCombo->addItem("Free");
    m_pSnapCombo->setFixedHeight(iControlHeight);
    // trim the stylesheet's property-grid padding so the text fits a transport-height control
    m_pSnapCombo->setStyleSheet("QComboBox { padding: 1px 6px; padding-right: 22px; min-height: 0px; }");
    m_pSnapCombo->setToolTip("Snapping applied to scrubbing and key dragging.");
    connect(m_pSnapCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &plQtSequencerPanel::onSnapModeChanged);
    pLayout->addWidget(m_pSnapCombo);
  }

  {
    QLabel* pDurationLabel = new QLabel("Duration", pBar);
    pDurationLabel->setStyleSheet("QLabel { color: #8e8e8e; }");
    pLayout->addWidget(pDurationLabel);

    m_pDurationSpin = new QDoubleSpinBox(pBar);
    m_pDurationSpin->setRange(0.1, 3600.0);
    m_pDurationSpin->setDecimals(2);
    m_pDurationSpin->setSingleStep(0.5);
    m_pDurationSpin->setSuffix(" s");
    m_pDurationSpin->setFixedHeight(iControlHeight);
    m_pDurationSpin->setFixedWidth(96);
    m_pDurationSpin->setStyleSheet("QAbstractSpinBox { padding: 1px 6px; min-height: 0px; }");
    m_pDurationSpin->setKeyboardTracking(false);
    connect(m_pDurationSpin, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
      &plQtSequencerPanel::onDurationChanged);
    pLayout->addWidget(m_pDurationSpin);
  }

  pLayout->addStretch();

  return pBar;
}

void plQtSequencerPanel::UpdateTransportState()
{
  const plUInt64 uiTick = m_pDocument->GetScrubberPosition();
  const plUInt64 uiDuration = m_pDocument->GetDurationTicks();
  const plUInt16 uiFps = plMath::Max<plUInt16>(1, m_pDocument->GetProperties()->m_uiFramesPerSecond);

  auto formatTimecode = [](plUInt64 uiTicks) -> QString
  {
    const double fSeconds = (double)uiTicks / (double)plSequenceTicksPerSecond;
    const int iMinutes = (int)(fSeconds / 60.0);
    const double fRest = fSeconds - iMinutes * 60.0;
    return QString("%1:%2").arg(iMinutes, 2, 10, QChar('0')).arg(fRest, 6, 'f', 3, QChar('0'));
  };

  const plUInt64 uiFrame = (uiTick * uiFps) / plSequenceTicksPerSecond;

  // frame number in the plasma accent, total dimmed - the mockup's readout
  m_pTimecodeLabel->setText(QString("%1 <span style='color:#06fac9'>f %2</span> <span style='color:#8e8e8e'>/ %3</span>")
                              .arg(formatTimecode(uiTick))
                              .arg(uiFrame)
                              .arg(formatTimecode(uiDuration)));

  m_pFpsLabel->setText(QString("%1 fps").arg(uiFps));

  m_pPlayButton->setIcon(QIcon(m_pDocument->GetPlayAnimation() ? ":/GuiFoundation/Icons/ControlPause.svg" : ":/GuiFoundation/Icons/ControlPlay.svg"));
  m_pLoopButton->setChecked(m_pDocument->GetRepeatAnimation());

  const double fDurationSeconds = (double)uiDuration / (double)plSequenceTicksPerSecond;
  if (plMath::Abs(m_pDurationSpin->value() - fDurationSeconds) > 0.001)
  {
    plQtScopedBlockSignals block(m_pDurationSpin);
    m_pDurationSpin->setValue(fDurationSeconds);
  }
}

plQtSequencerPanel::~plQtSequencerPanel()
{
  m_pDocument->m_SequenceEvents.RemoveEventHandler(plMakeDelegate(&plQtSequencerPanel::SequenceEventHandler, this));
}

void plQtSequencerPanel::SequenceEventHandler(const plSequenceAssetEvent& e)
{
  switch (e.m_Type)
  {
    case plSequenceAssetEvent::Type::DurationChanged:
    case plSequenceAssetEvent::Type::ScrubberPositionChanged:
      UpdateTransportState();
      m_pTrackView->update();
      break;

    case plSequenceAssetEvent::Type::PlaybackChanged:
      UpdateTransportState();
      break;
  }
}

void plQtSequencerPanel::onGoToStart()
{
  m_pDocument->SetScrubberPosition(0);
}

void plQtSequencerPanel::onGoToEnd()
{
  m_pDocument->SetScrubberPosition(m_pDocument->GetDurationTicks());
}

void plQtSequencerPanel::onStepFrame(int iDirection)
{
  const plInt64 iStep = (plInt64)m_pDocument->GetTicksPerFrame() * iDirection;
  const plInt64 iNew = (plInt64)m_pDocument->GetScrubberPosition() + iStep;

  m_pDocument->SetScrubberPosition((plUInt64)plMath::Clamp<plInt64>(iNew, 0, (plInt64)m_pDocument->GetDurationTicks()));
}

void plQtSequencerPanel::CollectAnimatableProperties(
  plQtSearchableMenu* pMenu, const plDocumentObject* pObject, const plStringBuilder& sPathPrefix)
{
  if (pObject == nullptr)
    return;

  // Every entry carries the document object that owns the property plus the property name and the
  // target channel; the document turns that into the right binding and track.
  auto addProperty = [&](const plDocumentObject* pOwner, const plAbstractProperty* pProp, const plStringBuilder& sGroup, const char* szSuffix,
                       plPropertyAnimTarget::Enum target)
  {
    plStringBuilder sPath = sGroup;
    sPath.AppendPath(pProp->GetPropertyName());
    sPath.Append(szSuffix);

    plStringBuilder sGuid;
    plConversionUtils::ToString(pOwner->GetGuid(), sGuid);

    QStringList payload;
    payload << QString::fromUtf8(sGuid.GetData()) << QString::fromUtf8(pProp->GetPropertyName()) << QString::number((int)target);

    plStringBuilder sDisplay = pProp->GetPropertyName();
    sDisplay.Append(szSuffix);

    pMenu->AddItem(sDisplay.GetView(), sPath.GetData(), QVariant(payload));
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
          {
            addProperty(pOwner, pProp, sGroup, axis[a], (plPropertyAnimTarget::Enum)(plPropertyAnimTarget::VectorX + a));
          }
        }
        else if (varType == plVariantType::Quaternion)
        {
          const char* axis[3] = {".X", ".Y", ".Z"};
          for (plUInt32 a = 0; a < 3; ++a)
          {
            addProperty(pOwner, pProp, sGroup, axis[a], (plPropertyAnimTarget::Enum)(plPropertyAnimTarget::RotationX + a));
          }
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

      CollectAnimatableProperties(pMenu, pChild, sChildPrefix);
    }
  }
}

void plQtSequencerPanel::ShowPropertyPicker(const plUuid& bindingGuid, const QPoint& globalPos)
{
  const plDocumentObject* pBoundObj = m_pDocument->ResolveBindingObject(bindingGuid);
  if (pBoundObj == nullptr)
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation(
      plFmt("This binding does not resolve to an object in the attached scene, so its properties cannot be listed."));
    return;
  }

  QMenu menu(this);
  plQtSearchableMenu* pSearchable = new plQtSearchableMenu(&menu);

  plStringBuilder sEmptyPrefix;
  CollectAnimatableProperties(pSearchable, pBoundObj, sEmptyPrefix);

  connect(pSearchable, &plQtSearchableMenu::MenuItemTriggered, this, [this, &menu](const QString& sName, const QVariant& variant) {
    PL_IGNORE_UNUSED(sName);

    const QStringList payload = variant.toStringList();
    if (payload.size() != 3)
      return;

    plUuid ownerGuid;
    if (!plConversionUtils::TryConvertStringToUuid(payload[0].toUtf8().constData(), ownerGuid))
      return;

    m_pDocument->AddTrackForContextProperty(
      ownerGuid, payload[1].toUtf8().constData(), (plPropertyAnimTarget::Enum)payload[2].toInt());

    menu.close(); });

  // plQtSearchableMenu is itself a QWidgetAction, so it goes straight into the menu
  menu.addAction(pSearchable);

  pSearchable->Finalize("");
  menu.exec(globalPos);
}

void plQtSequencerPanel::BuildCameraCutMenu(QMenu& ref_menu)
{
  plDynamicArray<const plDocumentObject*> cameras;
  m_pDocument->CollectContextCameras(cameras);

  if (cameras.IsEmpty())
  {
    QAction* pNone = ref_menu.addAction("No cameras in the attached scene");
    pNone->setEnabled(false);
    return;
  }

  const plUInt64 uiTick = m_pDocument->GetScrubberPosition();

  QMenu* pAddMenu = ref_menu.addMenu(QString("Cut to camera at %1s").arg((double)uiTick / (double)plSequenceTicksPerSecond, 0, 'f', 2));

  for (const plDocumentObject* pCamera : cameras)
  {
    plStringBuilder sName = pCamera->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
    if (sName.IsEmpty())
      sName = "<unnamed>";

    pAddMenu->addAction(QString::fromUtf8(sName.GetData()), this, [this, pCamera, uiTick]() { m_pDocument->AddCameraCut(pCamera, uiTick); });
  }
}

void plQtSequencerPanel::onDeleteSelected()
{
  const QModelIndex current = m_pOutliner->currentIndex();
  const plUuid guid = GuidFromIndex(current);
  if (!guid.IsValid())
    return;

  const bool bIsBinding = !current.parent().isValid();

  // Removing the objects fires the structure events the document listens to, so the preview
  // mapping drops those tracks and restores the properties they were driving.
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

void plQtSequencerPanel::onOutlinerContextMenu(const QPoint& pos)
{
  const QModelIndex index = m_pOutliner->indexAt(pos);
  if (!index.isValid())
    return;

  m_pOutliner->setCurrentIndex(index);

  const bool bIsBinding = !index.parent().isValid();

  QMenu menu(this);

  const bool bIsCutLane = index.data(plQtSequenceModel::IsCameraCutLaneRole).toBool();
  const bool bIsClipLane = index.data(plQtSequenceModel::IsClipLaneRole).toBool();

  if (bIsCutLane)
  {
    BuildCameraCutMenu(menu);
  }
  else if (bIsClipLane)
  {
    const plUuid ownerGuid = GuidFromIndex(index);
    const plUInt64 uiTick = m_pDocument->GetScrubberPosition();
    const plSequenceClipKind::Enum kind = (plSequenceClipKind::Enum)index.data(plQtSequenceModel::ClipKindRole).toInt();
    const QString sAt = QString(" at %1s").arg((double)uiTick / (double)plSequenceTicksPerSecond, 0, 'f', 2);

    if (kind == plSequenceClipKind::AudioTrigger)
    {
      // Triggers are a dynamic string enum, the same list plAudioTriggerComponent offers - typing
      // one by hand is how you get a clip that silently plays nothing.
      const auto& triggers = plDynamicStringEnum::GetDynamicEnum("AudioSystem.Trigger").GetAllValidValues();

      if (triggers.IsEmpty())
      {
        QAction* pNone = menu.addAction("No audio triggers registered in this project");
        pNone->setEnabled(false);
      }
      else
      {
        QMenu* pTriggers = menu.addMenu(QString("Add Trigger%1").arg(sAt));
        for (const plString& sTrigger : triggers)
        {
          // the stored value is an asset GUID, so the menu shows the resolved name
          const plString sCopy = sTrigger;
          const plString sLabel = plSequenceWaveformCache::PayloadDisplayName(sTrigger);
          pTriggers->addAction(QString::fromUtf8(sLabel.GetData()), this,
            [this, ownerGuid, uiTick, sCopy]() { m_pDocument->AddClip(ownerGuid, plSequenceClipKind::AudioTrigger, uiTick, sCopy); });
        }
      }
    }
    else
    {
      menu.addAction(QString("Add Clip%1").arg(sAt), this,
        [this, ownerGuid, uiTick, kind]() { m_pDocument->AddClip(ownerGuid, kind, uiTick, ""); });
    }

    menu.addSeparator();
    menu.addAction("Remove Lane and its Clips", this, [this, ownerGuid, kind]() { m_pDocument->RemoveClipLane(ownerGuid, kind); });
  }
  else if (bIsBinding)
  {
    const bool bUnresolved = index.data(plQtSequenceModel::UnresolvedRole).toBool();
    const plUuid bindingGuid = GuidFromIndex(index);

    if (bUnresolved)
    {
      // A binding that resolves to nothing cannot gain tracks - CanAnimate has no object to check
      // against - so the repair comes first and the rest is disabled with a reason.
      QAction* pWhy = menu.addAction("This binding's object is not in the attached scene");
      pWhy->setEnabled(false);

      menu.addAction("Rebind to Selected Object...", this, [this, bindingGuid]() { RebindToSelection(bindingGuid); });
      menu.addSeparator();

      QAction* pAdd = menu.addAction("Add Track...");
      pAdd->setEnabled(false);
      pAdd->setToolTip("Rebind this to an object first.");
    }
    else
    {
      menu.addAction("Add Track...", this, &plQtSequencerPanel::onAddTrack);
      menu.addSeparator();
      menu.addAction("Rebind to Selected Object...", this, [this, bindingGuid]() { RebindToSelection(bindingGuid); });
    }

    menu.addSeparator();
    menu.addAction("Remove Binding and its Tracks", this, &plQtSequencerPanel::onDeleteSelected);
  }
  else
  {
    menu.addAction("Remove Track", this, &plQtSequencerPanel::onDeleteSelected);
  }

  menu.exec(m_pOutliner->viewport()->mapToGlobal(pos));
}

void plQtSequencerPanel::onSnapModeChanged(int iIndex)
{
  switch (iIndex)
  {
    case 1:
      m_pTrackView->SetSnapMode(plQtSequenceTrackView::SnapMode::HalfFrame);
      break;
    case 2:
      m_pTrackView->SetSnapMode(plQtSequenceTrackView::SnapMode::Free);
      break;
    default:
      m_pTrackView->SetSnapMode(plQtSequenceTrackView::SnapMode::Frame);
      break;
  }
}

plUuid plQtSequencerPanel::GuidFromIndex(const QModelIndex& index) const
{
  if (!index.isValid())
    return plUuid();

  const QString sGuid = m_pModel->data(index, plQtSequenceModel::ObjectGuidRole).toString();

  plUuid guid;
  if (!plConversionUtils::TryConvertStringToUuid(sGuid.toUtf8().constData(), guid))
    return plUuid();
  return guid;
}

QModelIndex plQtSequencerPanel::FindIndexForGuid(const plUuid& guid) const
{
  const int iBindingCount = m_pModel->rowCount();
  for (int b = 0; b < iBindingCount; ++b)
  {
    const QModelIndex bindingIndex = m_pModel->index(b, 0);
    if (GuidFromIndex(bindingIndex) == guid)
      return bindingIndex;

    const int iTrackCount = m_pModel->rowCount(bindingIndex);
    for (int t = 0; t < iTrackCount; ++t)
    {
      const QModelIndex trackIndex = m_pModel->index(t, 0, bindingIndex);
      if (GuidFromIndex(trackIndex) == guid)
        return trackIndex;
    }
  }

  return QModelIndex();
}

plUuid plQtSequencerPanel::GetSelectedTrack() const
{
  const QModelIndex current = m_pOutliner->currentIndex();
  if (!current.isValid() || !current.parent().isValid())
    return plUuid(); // nothing, or a binding row

  return GuidFromIndex(current);
}

void plQtSequencerPanel::GetSelectedAxisTracks(plHybridArray<plUuid, 4>& out_tracks) const
{
  out_tracks.Clear();

  const QModelIndex current = m_pOutliner->currentIndex();
  if (!current.isValid())
    return;

  for (const QString& sGuid : current.data(plQtSequenceModel::AxisTracksRole).toStringList())
  {
    plUuid guid;
    if (plConversionUtils::TryConvertStringToUuid(sGuid.toUtf8().constData(), guid))
      out_tracks.PushBack(guid);
  }
}

plUuid plQtSequencerPanel::GetTargetBinding() const
{
  const QModelIndex current = m_pOutliner->currentIndex();
  if (!current.isValid())
  {
    // fall back to the first binding, so "add track" works right after "add binding"
    const auto& rows = m_pModel->GetFlatRows();
    for (const auto& row : rows)
    {
      if (row.IsBinding())
        return row.m_ObjectGuid;
    }
    return plUuid();
  }

  const QModelIndex bindingIndex = current.parent().isValid() ? current.parent() : current;
  return GuidFromIndex(bindingIndex);
}

void plQtSequencerPanel::onAddBinding()
{
  // The user points at scene objects through the scenegraph panel's selection.
  const plDocumentObject* pSelected = m_pDocument->GetSelectionManager()->GetCurrentObject();
  if (pSelected == nullptr)
  {
    plQtUiServices::GetSingleton()->ShowAllDocumentsTemporaryStatusBarMessage(
      plFmt("Select an object of the context scene in the Scenegraph panel first."), plTime::MakeFromSeconds(4));
    return;
  }

  m_pDocument->AddBindingForContextObject(pSelected);
}

void plQtSequencerPanel::RebindToSelection(const plUuid& bindingGuid)
{
  const plDocumentObject* pSelected = m_pDocument->GetSelectionManager()->GetCurrentObject();

  if (pSelected == nullptr)
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation(
      plFmt("Select the object to bind to in the scenegraph first, then rebind."));
    return;
  }

  if (plStatus status = m_pDocument->RebindTo(bindingGuid, pSelected); status.Failed())
  {
    plQtUiServices::GetSingleton()->MessageBoxInformation(plFmt("{}", status.m_sMessage));
  }
}

void plQtSequencerPanel::onAddTrack()
{
  const plUuid bindingGuid = GetTargetBinding();

  // With nothing selected, the only thing that can be added is sequence-level: clips that play
  // without being attached to anything in the scene - music, VO, stingers.
  if (!bindingGuid.IsValid())
  {
    const plUuid sequenceGuid = m_pDocument->GetPropertyObject()->GetGuid();

    QMenu seqMenu(this);
    QMenu* pLanes = seqMenu.addMenu("Sequence Clip Lane");
    auto addSeqLane = [&](const char* szLabel, plSequenceClipKind::Enum kind) {
      pLanes->addAction(QString::fromUtf8(szLabel), [this, sequenceGuid, kind]() { m_pDocument->AddClipLane(sequenceGuid, kind); }); };

    addSeqLane("Audio", plSequenceClipKind::AudioTrigger);
    addSeqLane("Animation", plSequenceClipKind::AnimationClip);
    addSeqLane("Sub-Sequence", plSequenceClipKind::SubSequence);

    seqMenu.addSeparator();
    QAction* pHint = seqMenu.addAction("Select a binding to add tracks to it");
    pHint->setEnabled(false);

    seqMenu.exec(m_pAddTrackButton->mapToGlobal(QPoint(0, m_pAddTrackButton->height())));
    return;
  }

  QMenu menu(this);

  // Transform channels are authored as whole values: one Position row over its X/Y/Z tracks, not
  // three unrelated rows that have to be keyed in lockstep by hand.
  auto addVector = [&](const char* szLabel, const char* szProperty, bool bRotation) {
    menu.addAction(QString::fromUtf8(szLabel), [this, bindingGuid, szProperty, bRotation]() {
      m_pDocument->AddVectorTrack(bindingGuid, "", szProperty, bRotation); }); };

  addVector("Position", "LocalPosition", false);
  addVector("Rotation", "LocalRotation", true);
  addVector("Scale", "LocalScaling", false);

  menu.addSeparator();

  // The reflection-driven picker the mockup specifies: searchable, grouped by object and
  // component, and covering descendants - a bound group's components usually sit on its children.
  menu.addAction("Property...", this,
    [this, bindingGuid]() { ShowPropertyPicker(bindingGuid, m_pAddTrackButton->mapToGlobal(QPoint(0, m_pAddTrackButton->height()))); });

  menu.addSeparator();

  // Clip lanes are their own kind of row, one per clip type, so audio never shares a lane with
  // animation. Adding a lane is what makes an empty one appear - clips come after.
  QMenu* pLanes = menu.addMenu("Clip Lane");
  auto addLane = [&](const char* szLabel, plSequenceClipKind::Enum kind) {
    pLanes->addAction(QString::fromUtf8(szLabel), [this, bindingGuid, kind]() { m_pDocument->AddClipLane(bindingGuid, kind); }); };

  addLane("Audio", plSequenceClipKind::AudioTrigger);
  addLane("Animation", plSequenceClipKind::AnimationClip);
  addLane("Sub-Sequence", plSequenceClipKind::SubSequence);

  menu.exec(m_pAddTrackButton->mapToGlobal(QPoint(0, m_pAddTrackButton->height())));
}

void plQtSequencerPanel::onPlayPause()
{
  m_pDocument->SetPlayAnimation(!m_pDocument->GetPlayAnimation());
}

void plQtSequencerPanel::onRepeat()
{
  m_pDocument->SetRepeatAnimation(!m_pDocument->GetRepeatAnimation());
}

void plQtSequencerPanel::onDurationChanged(double fDuration)
{
  m_pDocument->SetDurationTicks((plUInt64)(fDuration * (double)plSequenceTicksPerSecond));
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequencerPanel);
