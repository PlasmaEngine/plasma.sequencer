#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceRenderDialog.moc.h>
#include <GuiFoundation/Dialogs/FileDialog.moc.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAudioMix.h>
#include <SequencerPlugin/Capture/VideoEncoderFfmpeg.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/System/Process.h>

#include <Texture/Image/Image.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

plQtSequenceRenderDialog::plQtSequenceRenderDialog(plSequenceAssetDocument* pDocument, QWidget* pParent)
  : QDialog(pParent)
  , m_pDocument(pDocument)
{
  setWindowTitle("Render to Disk");
  setMinimumWidth(430);

  QVBoxLayout* pLayout = new QVBoxLayout(this);
  QFormLayout* pForm = new QFormLayout();
  pLayout->addLayout(pForm);

  {
    QWidget* pRow = new QWidget(this);
    QHBoxLayout* pRowLayout = new QHBoxLayout(pRow);
    pRowLayout->setContentsMargins(0, 0, 0, 0);

    m_pFolder = new QLineEdit(pRow);

    // The project root itself, with nothing appended. Any subfolder - "_Render" next to the asset,
    // or "Renders" here - is a path that does not exist yet, and the picker has nothing real to
    // open at. The capture harness creates whatever folder is finally chosen.
    plStringBuilder sDefault = plToolsProject::GetSingleton()->GetProjectDirectory();
    sDefault.MakeCleanPath();
    m_pFolder->setText(QString::fromUtf8(sDefault.GetData()));

    QPushButton* pBrowse = new QPushButton("…", pRow);
    pBrowse->setFixedWidth(30);
    connect(pBrowse, &QPushButton::clicked, this, &plQtSequenceRenderDialog::onBrowse);

    pRowLayout->addWidget(m_pFolder, 1);
    pRowLayout->addWidget(pBrowse);
    pForm->addRow("Output folder", pRow);
  }

  m_pResolution = new QComboBox(this);
  m_pResolution->addItem("1280 × 720", QSize(1280, 720));
  m_pResolution->addItem("1920 × 1080", QSize(1920, 1080));
  m_pResolution->addItem("2560 × 1440", QSize(2560, 1440));
  m_pResolution->addItem("3840 × 2160", QSize(3840, 2160));
  m_pResolution->setCurrentIndex(1);
  pForm->addRow("Resolution", m_pResolution);

  m_pFps = new QComboBox(this);
  m_pFps->addItem("24 fps", 24);
  m_pFps->addItem("30 fps", 30);
  m_pFps->addItem("60 fps", 60);
  m_pFps->setCurrentIndex(2);
  pForm->addRow("Frame rate", m_pFps);

  m_pEncoder = new QComboBox(this);
  m_pEncoder->addItem("PNG sequence only", -1);

  // Only offer what this machine can actually run. ffmpeg appears when the user has one; nothing
  // here ships or installs it.
  if (plVideoEncoderFactory::IsAvailable(plVideoEncoderKind::MediaFoundation))
    m_pEncoder->addItem("mp4 — Media Foundation (H.264)", (int)plVideoEncoderKind::MediaFoundation);

  if (plVideoEncoderFactory::IsAvailable(plVideoEncoderKind::FfmpegCli))
    m_pEncoder->addItem("ffmpeg", (int)plVideoEncoderKind::FfmpegCli);

  pForm->addRow("Video", m_pEncoder);

  // Codec is a separate choice from the backend, because which codecs exist depends entirely on
  // which ffmpeg the user happens to have. onEncoderChanged fills this in.
  m_pCodec = new QComboBox(this);
  pForm->addRow("Codec", m_pCodec);

  m_pQuality = new QComboBox(this);
  m_pQuality->addItem("Draft — fast, visibly lossy", (int)plVideoQuality::Draft);
  m_pQuality->addItem("Review — small enough to send", (int)plVideoQuality::Review);
  m_pQuality->addItem("Delivery — what a trailer ships at", (int)plVideoQuality::Delivery);
  m_pQuality->addItem("Master — near-transparent, large", (int)plVideoQuality::Master);
  m_pQuality->setCurrentIndex((int)plVideoQuality::Delivery);
  m_pQuality->setToolTip("Quality rather than a bitrate: the number that means \"good\" differs per codec.");
  pForm->addRow("Quality", m_pQuality);

  connect(m_pEncoder, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
    &plQtSequenceRenderDialog::onEncoderChanged);
  onEncoderChanged();

  m_pWriteFrames = new QCheckBox("Also keep the PNG frames", this);
  m_pWriteFrames->setChecked(true);
  m_pWriteFrames->setToolTip("Frames are the master: a render that fails part-way still leaves everything up to that point.");
  pForm->addRow("", m_pWriteFrames);

  {
    QLabel* pNote = new QLabel(
      "<span style='color:#b8b8b8'>The world is simulated one fixed step per frame while rendering, so particles, "
      "physics and animation are captured. Render from the start of the sequence: a sub-range begins with no "
      "simulation history behind it. Animation and sub-sequence clips are not dispatched yet.</span>",
      this);
    pNote->setWordWrap(true);
    pLayout->addWidget(pNote);
  }

  m_pProgress = new QProgressBar(this);
  m_pProgress->setValue(0);
  pLayout->addWidget(m_pProgress);

  m_pStatus = new QLabel(" ", this);
  m_pStatus->setStyleSheet("QLabel { color: #8e8e8e; }");
  pLayout->addWidget(m_pStatus);

  {
    QDialogButtonBox* pButtons = new QDialogButtonBox(this);
    m_pStart = pButtons->addButton("Render", QDialogButtonBox::AcceptRole);
    m_pClose = pButtons->addButton("Close", QDialogButtonBox::RejectRole);
    connect(m_pStart, &QPushButton::clicked, this, &plQtSequenceRenderDialog::onStartOrCancel);
    connect(m_pClose, &QPushButton::clicked, this, &QDialog::reject);
    pLayout->addWidget(pButtons);
  }

  m_pDocument->m_ThumbnailArrived.AddEventHandler(plMakeDelegate(&plQtSequenceRenderDialog::ThumbnailArrivedHandler, this));
}

plQtSequenceRenderDialog::~plQtSequenceRenderDialog()
{
  // A dialog closed mid-render must not leave the scene posed at some frame's tick.
  if (m_bRendering)
    StopRender(true);

  m_pDocument->m_ThumbnailArrived.RemoveEventHandler(plMakeDelegate(&plQtSequenceRenderDialog::ThumbnailArrivedHandler, this));
}

void plQtSequenceRenderDialog::onEncoderChanged()
{
  const int iEncoder = m_pEncoder->currentData().toInt();
  const bool bEncoding = iEncoder >= 0;

  m_pCodec->clear();

  if (bEncoding)
  {
    const plVideoCodec::Enum codecs[] = {plVideoCodec::H264, plVideoCodec::H265, plVideoCodec::AV1, plVideoCodec::ProRes};

    for (plVideoCodec::Enum codec : codecs)
    {
      if (!plVideoEncoderFactory::IsCodecAvailable((plVideoEncoderKind::Enum)iEncoder, codec))
        continue;

      plStringBuilder sLabel = plVideoEncoderFactory::GetCodecDisplayName(codec);
      sLabel.Append(" (.", plVideoEncoderFactory::GetContainerExtension(codec), ")");

      // libaom is an order of magnitude slower than SVT-AV1, which is worth knowing before starting
      // a render rather than an hour into one.
      if (codec == plVideoCodec::AV1 && iEncoder == (int)plVideoEncoderKind::FfmpegCli &&
          plVideoEncoderFfmpeg::ResolveEncoderName(codec) == plStringView("libaom-av1"))
      {
        sLabel.Append(" — libaom, very slow");
      }

      m_pCodec->addItem(QString::fromUtf8(sLabel.GetData()), (int)codec);
    }
  }

  m_pCodec->setEnabled(bEncoding && m_pCodec->count() > 1);
  m_pQuality->setEnabled(bEncoding);

  // Say why the list is short rather than leaving someone to wonder where AV1 went.
  if (bEncoding && iEncoder == (int)plVideoEncoderKind::FfmpegCli && m_pCodec->count() < 4)
    m_pCodec->setToolTip("Only the codecs this ffmpeg build was compiled with are listed.");
  else
    m_pCodec->setToolTip(QString());
}

plVideoCodec::Enum plQtSequenceRenderDialog::SelectedCodec() const
{
  if (m_pCodec == nullptr || m_pCodec->currentIndex() < 0)
    return plVideoCodec::H264;

  return (plVideoCodec::Enum)m_pCodec->currentData().toInt();
}

void plQtSequenceRenderDialog::onBrowse()
{
  // The default output path is a folder that has not been created yet ("<sequence>_Render"), which
  // no shell picker copes with. plQtFileDialog walks up to the nearest existing ancestor itself, so
  // the raw text can go straight in, and its "New folder" button covers the rest.
  const QString sDir = plQtFileDialog::getExistingDirectory(this, QLatin1String("Render output folder"), m_pFolder->text());

  if (!sDir.isEmpty())
    m_pFolder->setText(sDir);
}

void plQtSequenceRenderDialog::SetInputsEnabled(bool bEnabled)
{
  m_pFolder->setEnabled(bEnabled);
  m_pResolution->setEnabled(bEnabled);
  m_pFps->setEnabled(bEnabled);
  m_pEncoder->setEnabled(bEnabled);
  m_pWriteFrames->setEnabled(bEnabled);
  m_pClose->setEnabled(bEnabled);
}

void plQtSequenceRenderDialog::onStartOrCancel()
{
  if (m_bRendering)
  {
    StopRender(true);
    return;
  }

  const QSize size = m_pResolution->currentData().toSize();
  const int iEncoder = m_pEncoder->currentData().toInt();

  plSequenceCaptureDesc desc;
  desc.m_sOutputFolder = m_pFolder->text().toUtf8().constData();
  desc.m_uiWidth = (plUInt32)size.width();
  desc.m_uiHeight = (plUInt32)size.height();
  desc.m_uiFramesPerSecond = (plUInt32)m_pFps->currentData().toInt();
  desc.m_bWriteFrames = m_pWriteFrames->isChecked() || iEncoder < 0;
  desc.m_bEncodeVideo = iEncoder >= 0;

  if (iEncoder >= 0)
  {
    desc.m_EncoderKind = (plVideoEncoderKind::Enum)iEncoder;
    desc.m_Codec = SelectedCodec();
    desc.m_Quality = (plVideoQuality::Enum)m_pQuality->currentData().toInt();
  }

  if (m_Capture.Begin(desc, m_pDocument->GetDurationTicks()).Failed())
  {
    plQtUiServices::GetSingleton()->MessageBoxWarning(plFmt("The render could not be started. See the log for details."));
    return;
  }

  m_bRendering = true;
  m_pStart->setText("Cancel");
  SetInputsEnabled(false);
  m_pProgress->setRange(0, (int)m_Capture.GetTotalFrames());

  // The filmstrip renders through the same one-at-a-time engine channel, so it has to stand down
  // for the duration or the two would interleave requests and each would get the other's frames.
  m_pDocument->SetThumbnailRenderingSuspended(true);

  // Simulate while rendering, so particles, physics and animation are in the output rather than
  // frozen. The engine steps the world once per captured frame at exactly this rate.
  SendRenderMode(true, desc.m_uiFramesPerSecond);

  UpdateProgress();
  RequestNextFrame();
}

void plQtSequenceRenderDialog::SendRenderMode(bool bEnabled, plUInt32 uiFramesPerSecond)
{
  plStringBuilder sValue;
  sValue.SetFormat("{0}|{1}", bEnabled ? 1 : 0, uiFramesPerSecond);

  plDocumentConfigMsgToEngine msg;
  msg.m_DocumentGuid = m_pDocument->GetGuid();
  msg.m_sWhatToDo = "SequenceRenderMode";
  msg.m_sValue = sValue;
  m_pDocument->GetEditorEngineConnection()->SendMessage(&msg);
}

void plQtSequenceRenderDialog::RequestNextFrame()
{
  if (!m_bRendering)
    return;

  if (!m_Capture.IsCapturing())
  {
    StopRender(false);
    return;
  }

  const plUInt64 uiTick = m_Capture.GetCurrentTick();

  // Pose first, then read the camera: the cameras may themselves be animated by this sequence, so
  // reading before posing would sample the wrong moment.
  m_pDocument->BeginThumbnailPose(uiTick);
  m_bPosed = true;

  plTransform cameraTransform;
  float fFov = 60.0f;

  if (!m_pDocument->GetCutCameraAtTick((plInt64)uiTick, cameraTransform, fFov))
  {
    // No cut covers this tick, so there is no defined viewpoint. Better to stop and say so than to
    // silently emit frames from an arbitrary camera.
    m_pDocument->EndThumbnailPose();
    m_bPosed = false;

    plQtUiServices::GetSingleton()->MessageBoxWarning(
      plFmt("No camera cut is active at {0}s, so there is nothing to render from.\n\nAdd a camera cut covering the range.",
        (double)uiTick / (double)plSequenceTicksPerSecond));

    StopRender(true);
    return;
  }

  plStringBuilder sValue;
  sValue.SetFormat("{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}", plArgF(cameraTransform.m_vPosition.x, 4), plArgF(cameraTransform.m_vPosition.y, 4),
    plArgF(cameraTransform.m_vPosition.z, 4), plArgF(cameraTransform.m_qRotation.x, 6), plArgF(cameraTransform.m_qRotation.y, 6),
    plArgF(cameraTransform.m_qRotation.z, 6), plArgF(cameraTransform.m_qRotation.w, 6), plArgF(fFov, 3));

  plDocumentConfigMsgToEngine camMsg;
  camMsg.m_DocumentGuid = m_pDocument->GetGuid();
  camMsg.m_sWhatToDo = "SequenceThumbnailCamera";
  camMsg.m_sValue = sValue;
  m_pDocument->GetEditorEngineConnection()->SendMessage(&camMsg);

  const QSize size = m_pResolution->currentData().toSize();

  plCreateThumbnailMsgToEngine thumbMsg;
  thumbMsg.m_DocumentGuid = m_pDocument->GetGuid();
  thumbMsg.m_uiWidth = (plUInt16)size.width();
  thumbMsg.m_uiHeight = (plUInt16)size.height();
  m_pDocument->GetEditorEngineConnection()->SendMessage(&thumbMsg);
}

void plQtSequenceRenderDialog::ThumbnailArrivedHandler(const plCreateThumbnailMsgToEditor& msg)
{
  if (!m_bRendering)
    return;

  if (m_bPosed)
  {
    m_pDocument->EndThumbnailPose();
    m_bPosed = false;
  }

  const QSize size = m_pResolution->currentData().toSize();
  const plUInt32 uiExpected = (plUInt32)size.width() * (plUInt32)size.height() * 4;

  if (msg.m_ThumbnailData.GetCount() != uiExpected)
  {
    plLog::Error("Sequence render: frame came back as {0} bytes, expected {1}.", msg.m_ThumbnailData.GetCount(), uiExpected);
    StopRender(true);
    return;
  }

  plImageHeader header;
  header.SetWidth((plUInt32)size.width());
  header.SetHeight((plUInt32)size.height());
  header.SetImageFormat(plImageFormat::R8G8B8A8_UNORM_SRGB);

  plImage frame;
  frame.ResetAndAlloc(header);
  plMemoryUtils::RawByteCopy(frame.GetByteBlobPtr().GetPtr(), msg.m_ThumbnailData.GetData(), uiExpected);

  if (m_Capture.SubmitFrame(frame).Failed())
  {
    StopRender(true);
    return;
  }

  UpdateProgress();

  // Queued rather than recursed: this runs inside the message handler, and the next request has to
  // go out after it has unwound.
  QMetaObject::invokeMethod(this, [this]() { RequestNextFrame(); }, Qt::QueuedConnection);
}

void plQtSequenceRenderDialog::UpdateProgress()
{
  const plUInt32 uiDone = m_Capture.GetFramesWritten();
  const plUInt32 uiTotal = m_Capture.GetTotalFrames();

  m_pProgress->setValue((int)uiDone);
  m_pStatus->setText(QString("frame %1 / %2").arg(uiDone).arg(uiTotal));
}

void plQtSequenceRenderDialog::StopRender(bool bCancelled)
{
  if (!m_bRendering)
    return;

  m_bRendering = false;

  if (m_bPosed)
  {
    m_pDocument->EndThumbnailPose();
    m_bPosed = false;
  }

  QString sAudioNote;

  if (bCancelled)
  {
    m_Capture.Abort();
  }
  else
  {
    m_Capture.Finish().IgnoreResult();
    sAudioNote = WriteAndAttachAudio();
  }

  SendRenderMode(false, 0);

  // Simulating moved things - physics settled bodies, particles ran, animations advanced - and none
  // of that came from the document, so the preview world no longer matches it. Re-cloning the
  // context scene is what puts it back; without this a render silently leaves the editor showing a
  // world nobody authored.
  const plUuid contextDoc = m_pDocument->GetContextDocumentGuid();
  if (contextDoc.IsValid())
    m_pDocument->SetContext(contextDoc, m_pDocument->GetContextObjectGuid()).LogFailure();

  m_pDocument->SetThumbnailRenderingSuspended(false);

  m_pStart->setText("Render");
  SetInputsEnabled(true);

  m_pStatus->setText(bCancelled ? QString("Cancelled — %1 frames kept.").arg(m_Capture.GetFramesWritten())
                                : QString("Done — %1 frames.%2").arg(m_Capture.GetFramesWritten()).arg(sAudioNote));
}

QString plQtSequenceRenderDialog::WriteAndAttachAudio()
{
  // The preview render evaluates the document, which never fires clips - so the video comes out
  // silent unless the audio is produced separately. This mixes the sequence's audio clips offline
  // into a WAV next to the frames, and muxes it into the mp4 when ffmpeg is around to do it.
  plStringBuilder sWav = m_pFolder->text().toUtf8().constData();
  sWav.AppendPath("render.wav");
  sWav.MakeCleanPath();

  bool bHadClips = false;
  if (plSequenceAudioMix::WriteMixToWav(m_pDocument, 0, m_pDocument->GetDurationTicks(), sWav, bHadClips).Failed())
    return QString(" Audio mix failed — see log.");

  if (!bHadClips)
  {
    // No audio clips resolved to anything: a silent wav next to the frames would only mislead.
    plOSFile::DeleteFile(sWav).IgnoreResult();
    return QString();
  }

  const int iEncoder = m_pEncoder->currentData().toInt();

  // Only ffmpeg can mux; Media Foundation's writer was closed by Finish and cannot take a late
  // audio stream. In that case the wav sits beside the mp4 for the NLE, and the status says so.
  if (iEncoder != (int)plVideoEncoderKind::FfmpegCli || plVideoEncoderFactory::GetFfmpegPath().IsEmpty())
    return QString(" Audio written to render.wav.");

  // Whatever container the codec chose - .mov for ProRes, .mp4 otherwise.
  const plStringView sExtension = plVideoEncoderFactory::GetContainerExtension(SelectedCodec());

  plStringBuilder sVideo = m_pFolder->text().toUtf8().constData();
  sVideo.AppendPath("render.");
  sVideo.Append(sExtension);
  sVideo.MakeCleanPath();

  plStringBuilder sMuxed = m_pFolder->text().toUtf8().constData();
  sMuxed.AppendPath("render_mux.");
  sMuxed.Append(sExtension);
  sMuxed.MakeCleanPath();

  plProcessOptions opt;
  opt.m_sProcess = plVideoEncoderFactory::GetFfmpegPath();
  opt.m_bHideConsoleWindow = true;

  opt.AddArgument("-y");
  opt.AddArgument("-i");
  opt.AddArgument(sVideo);
  opt.AddArgument("-i");
  opt.AddArgument(sWav);
  opt.AddArgument("-c:v");
  opt.AddArgument("copy"); // the video is already encoded; only the audio needs work
  opt.AddArgument("-c:a");

  // A ProRes .mov is an intermediate headed for an NLE, so its audio stays uncompressed; a delivery
  // mp4 gets AAC.
  opt.AddArgument(SelectedCodec() == plVideoCodec::ProRes ? "pcm_s16le" : "aac");

  opt.AddArgument("-shortest");
  opt.AddArgument(sMuxed);

  plInt32 iExitCode = 0;
  if (plProcess::Execute(opt, &iExitCode).Failed() || iExitCode != 0)
  {
    plLog::Error("Sequence render: audio mux failed (exit {0}); the wav is kept beside the video.", iExitCode);
    return QString(" Audio written to render.wav (mux failed).");
  }

  // Swap the muxed file into place so the deliverable stays "render.<ext>, with audio".
  plOSFile::DeleteFile(sVideo).IgnoreResult();
  if (plOSFile::MoveFileOrDirectory(sMuxed, sVideo).Failed())
    return QString(" Audio muxed to render_mux.%1.").arg(QString::fromUtf8(plStringBuilder(sExtension).GetData()));

  return QString(" Audio mixed in.");
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceRenderDialog);
