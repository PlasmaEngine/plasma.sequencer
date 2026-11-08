#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceAudioMix.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceRenderEngineDlg.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceRenderPreferences.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceThumbnailCache.h>
#include <EditorUi/Menu.h>
#include <EditorUi/ProcessView.h>
#include <EditorUi/Surface.h>
#include <Foundation/IO/OSFile.h>
#include <Foundation/System/Process.h>
#include <GuiFoundation/Dialogs/FileDialog.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <SequencerPlugin/Capture/VideoEncoderFfmpeg.h>
#include <Texture/Image/Image.h>
#include <ToolsFoundation/Project/ToolsProject.h>

namespace
{
  const char* s_szRender = "Render";
  const char* s_szClose = "Close";

  const char* s_szFolder = "Render.Folder";
  const char* s_szFolderText = "Render.Folder.Text";
  const char* s_szBrowse = "Render.Browse";
  const char* s_szResolution = "Render.Resolution";
  const char* s_szFrameRate = "Render.FrameRate";
  const char* s_szVideo = "Render.Video";
  const char* s_szCodec = "Render.Codec";
  const char* s_szQuality = "Render.Quality";
  const char* s_szKeepFrames = "Render.KeepFrames";

  const char* s_szKeepFramesLabel = "Also keep the PNG frames";
  const char* s_szKeepFramesTip = "Frames are the master: a render that fails part-way still leaves everything up to that point.";
  const char* s_szQualityTip = "Quality rather than a bitrate: the number that means \"good\" differs per codec.";
  const char* s_szNote = "The world is simulated one fixed step per frame while rendering, so particles, physics and animation are captured. "
                         "Render from the start of the sequence: a sub-range begins with no simulation history behind it. "
                         "Animation and sub-sequence clips are not dispatched yet.";

  constexpr float s_fWidth = 520.0f;
  constexpr float s_fLabelWidth = 110.0f;
  constexpr float s_fTop = 12.0f;
  constexpr float s_fBottom = 12.0f;
  constexpr float s_fGap = 10.0f; ///< between the form, the note and the process block
  constexpr float s_fBrowseWidth = 26.0f;
  constexpr float s_fNoteLineHeight = 16.0f;
  constexpr plUInt32 s_uiSettingRows = 7; ///< the folder, five dropdowns and the PNG check box

  /// How long a frame request may stay unanswered before it is sent again (the filmstrip's request timeout).
  constexpr int s_iFrameTimeoutMs = 10000;

  /// The size of a filmstrip tile, which comes back on the same channel as the frames.
  constexpr plUInt32 s_uiTileBytes = plSequenceThumbnailCache::ThumbnailWidth * plSequenceThumbnailCache::ThumbnailHeight * 4;

  /// The header, the bar and four log lines inside the block's frame.
  constexpr float s_fProcessHeight = 2.0f + plEditorUiProcessView::TopHeight + 12.0f + 4.0f * plEditorUiProcessView::LineHeight;

  struct Resolution
  {
    const char* m_szLabel;
    plUInt32 m_uiWidth;
    plUInt32 m_uiHeight;
  };

  const Resolution s_Resolutions[] = {
    {"1280 \xC3\x97 720", 1280, 720},
    {"1920 \xC3\x97 1080", 1920, 1080},
    {"2560 \xC3\x97 1440", 2560, 1440},
    {"3840 \xC3\x97 2160", 3840, 2160},
  };

  struct FrameRate
  {
    const char* m_szLabel;
    plUInt32 m_uiFramesPerSecond;
  };

  const FrameRate s_FrameRates[] = {
    {"24 fps", 24},
    {"30 fps", 30},
    {"60 fps", 60},
  };

  struct Quality
  {
    const char* m_szLabel;
    plVideoQuality::Enum m_Quality;
  };

  const Quality s_Qualities[] = {
    {"Draft \xE2\x80\x94 fast, visibly lossy", plVideoQuality::Draft},
    {"Review \xE2\x80\x94 small enough to send", plVideoQuality::Review},
    {"Delivery \xE2\x80\x94 what a trailer ships at", plVideoQuality::Delivery},
    {"Master \xE2\x80\x94 near-transparent, large", plVideoQuality::Master},
  };

  /// While in scope, what the render logs also shows in the process block; the editor's log still gets every message.
  class RenderLog : public plLogInterface
  {
  public:
    explicit RenderLog(plEditorUiProcessView& ref_process)
      : m_Process(ref_process)
      , m_pPrevious(plLog::GetThreadLocalLogSystem())
    {
      plLog::SetThreadLocalLogSystem(this);
    }

    ~RenderLog() { plLog::SetThreadLocalLogSystem(m_pPrevious); }

    void HandleLogMessage(const plLoggingEventData& le) override
    {
      m_pPrevious->HandleLogMessage(le);

      // errors, warnings and info; groups and dev output stay in the editor's log
      if (le.m_EventType >= plLogMsgType::ErrorMsg && le.m_EventType <= plLogMsgType::InfoMsg)
        m_Process.AppendLine(le.m_sText, plEditorUiProcessView::GetLogMessageStyle(le.m_EventType));
    }

  private:
    plEditorUiProcessView& m_Process;
    plLogInterface* m_pPrevious = nullptr;
  };

  template <typename T, plUInt32 N>
  plHybridArray<plString, N> GetLabels(const T (&entries)[N])
  {
    plHybridArray<plString, N> labels;
    for (const T& entry : entries)
      labels.PushBack(entry.m_szLabel);

    return labels;
  }
} // namespace

plSequenceRenderEngineDlg::plSequenceRenderEngineDlg(plSequenceAssetDocument* pDocument)
  : plEngineDialog("SequenceRender", "Render to Disk")
  , m_pDocument(pDocument)
{
  GatherEncoders();
  GatherCodecs();

  m_pDocument->m_ThumbnailArrived.AddEventHandler(plMakeDelegate(&plSequenceRenderEngineDlg::ThumbnailArrivedHandler, this));

  if (!IsReady())
    return;

  plEditorUiDialogButton buttons[2];
  buttons[0].m_sId = s_szRender;
  buttons[0].m_sLabel = "Render";
  buttons[0].m_Role = plEditorUiDialogButton::Role::Accept;
  buttons[0].m_sIconPath = plEditorUi::IconPath(plEditorUi::Icon::Record);
  buttons[1].m_sId = s_szClose;
  buttons[1].m_sLabel = "Close";
  buttons[1].m_Role = plEditorUiDialogButton::Role::Reject;
  GetHost().SetButtons(buttons);

  // no steps: the header shows "frame n / N" and the bar follows the frames
  m_pProcess = PL_DEFAULT_NEW(plEditorUiProcessView, *GetSurface(), "Render.Process");

  // the folder of this sequence's last render, else the project root
  plStringBuilder sFolder = plPreferences::QueryPreferences<plSequenceRenderPreferencesUser>(m_pDocument)->m_sOutputFolder;
  if (sFolder.IsEmpty())
  {
    sFolder = plToolsProject::GetSingleton()->GetProjectDirectory();
    sFolder.MakeCleanPath();
  }

  WriteFolder(sFolder);
}

plSequenceRenderEngineDlg::~plSequenceRenderEngineDlg()
{
  // a dialog closed mid-render must not leave the scene posed at some frame's tick
  if (m_bRendering)
    StopRender(StopReason::Cancelled);

  // the frame a cancelled render left open will not be waited for any more
  if (m_bFrameInFlight)
    m_pDocument->SetThumbnailRenderingSuspended(false);

  m_pDocument->m_ThumbnailArrived.RemoveEventHandler(plMakeDelegate(&plSequenceRenderEngineDlg::ThumbnailArrivedHandler, this));
}

void plSequenceRenderEngineDlg::Open(plSequenceAssetDocument* pDocument)
{
  // Exec runs the modal loop, so the engine replies that drive the frame capture still arrive
  plSequenceRenderEngineDlg dlg(pDocument);
  dlg.Exec();
}

//////////////////////////////////////////////////////////////////////////
// Settings

void plSequenceRenderEngineDlg::GatherEncoders()
{
  m_Encoders.Clear();
  m_Encoders.PushBack({"PNG sequence only", -1});

  // only what this machine can run; ffmpeg appears when the user has one
  if (plVideoEncoderFactory::IsAvailable(plVideoEncoderKind::MediaFoundation))
    m_Encoders.PushBack({"mp4 \xE2\x80\x94 Media Foundation (H.264)", static_cast<plInt32>(plVideoEncoderKind::MediaFoundation)});

  if (plVideoEncoderFactory::IsAvailable(plVideoEncoderKind::FfmpegCli))
    m_Encoders.PushBack({"ffmpeg", static_cast<plInt32>(plVideoEncoderKind::FfmpegCli)});

  m_uiEncoder = 0;
}

void plSequenceRenderEngineDlg::GatherCodecs()
{
  m_Codecs.Clear();
  m_uiCodec = 0;

  const plInt32 iEncoder = GetEncoderKind();
  if (iEncoder < 0)
    return;

  // which codecs exist depends on the backend, and for ffmpeg on the build the user has
  const plVideoCodec::Enum codecs[] = {plVideoCodec::H264, plVideoCodec::H265, plVideoCodec::AV1, plVideoCodec::ProRes};

  for (plVideoCodec::Enum codec : codecs)
  {
    if (!plVideoEncoderFactory::IsCodecAvailable(static_cast<plVideoEncoderKind::Enum>(iEncoder), codec))
      continue;

    plStringBuilder sLabel = plVideoEncoderFactory::GetCodecDisplayName(codec);
    sLabel.Append(" (.", plVideoEncoderFactory::GetContainerExtension(codec), ")");

    // libaom is an order of magnitude slower than SVT-AV1, which is worth knowing before a render starts
    if (codec == plVideoCodec::AV1 && iEncoder == static_cast<plInt32>(plVideoEncoderKind::FfmpegCli) &&
        plVideoEncoderFfmpeg::ResolveEncoderName(codec) == plStringView("libaom-av1"))
    {
      sLabel.Append(" \xE2\x80\x94 libaom, very slow");
    }

    m_Codecs.PushBack({sLabel, codec});
  }
}

bool plSequenceRenderEngineDlg::IsEncoding() const
{
  return GetEncoderKind() >= 0;
}

plInt32 plSequenceRenderEngineDlg::GetEncoderKind() const
{
  return m_Encoders[m_uiEncoder].m_iKind;
}

plVideoCodec::Enum plSequenceRenderEngineDlg::GetSelectedCodec() const
{
  return m_Codecs.IsEmpty() ? plVideoCodec::H264 : m_Codecs[m_uiCodec].m_Codec;
}

plStringView plSequenceRenderEngineDlg::GetCodecToolTip() const
{
  // says why the list is short rather than leaving someone to wonder where AV1 went
  if (GetEncoderKind() == static_cast<plInt32>(plVideoEncoderKind::FfmpegCli) && m_Codecs.GetCount() < 4)
    return "Only the codecs this ffmpeg build was compiled with are listed.";

  return "Only the codecs this encoder offers are listed.";
}

plString plSequenceRenderEngineDlg::ReadFolder() const
{
  const plVariant value = GetSurface()->GetBlackboard().GetEntryValue(plTempHashedString(s_szFolderText));
  return value.IsValid() ? value.ConvertTo<plString>() : plString();
}

void plSequenceRenderEngineDlg::WriteFolder(plStringView sFolder)
{
  GetSurface()->GetCanvas().SetTextBinding(s_szFolderText, sFolder);
  GetSurface()->GetBlackboard().SetEntryValue(s_szFolderText, plString(sFolder));
}

void plSequenceRenderEngineDlg::RememberFolder()
{
  // saved with the document's preferences
  plSequenceRenderPreferencesUser* pPreferences = plPreferences::QueryPreferences<plSequenceRenderPreferencesUser>(m_pDocument);
  pPreferences->m_sOutputFolder = m_sRenderFolder;
  pPreferences->TriggerPreferencesChangedEvent();
}

void plSequenceRenderEngineDlg::Browse()
{
  // the file dialog walks up to the nearest existing folder itself, and its "New folder" button covers the rest
  const plString sDir = plFileDialog::GetExistingDirectory("Render output folder", ReadFolder());

  if (!sDir.IsEmpty())
    WriteFolder(sDir);
}

//////////////////////////////////////////////////////////////////////////
// The body

plVec2 plSequenceRenderEngineDlg::GetPreferredSize() const
{
  using namespace plEditorUi;

  const float fWidth = s_fWidth - 2.0f * plEditorUiDialogHost::Padding;
  const float fRows = s_uiSettingRows * (ControlHeight + plEditorUiForm::RowGap);
  const float fNote = MeasureWrappedHeight(s_szNote, fWidth, FontSize - 2, s_fNoteLineHeight);

  // the form's last row gap counts towards the gap before the note, the note's towards the gap after it
  const float fBody = s_fTop + fRows + (s_fGap - plEditorUiForm::RowGap) + fNote + s_fGap + s_fProcessHeight + s_fBottom;
  return plVec2(s_fWidth, fBody + plEditorUiDialogHost::FooterHeight);
}

void plSequenceRenderEngineDlg::BuildBody(plUiElement* pBody, const plVec2& vSize)
{
  const float x = plEditorUiDialogHost::Padding;
  const float fWidth = vSize.x - 2.0f * x;

  plEditorUiForm form(pBody, plRectFloat(x, s_fTop, fWidth, plMath::Max(vSize.y - s_fTop, 0.0f)), s_fLabelWidth);

  BuildFolderRow(pBody, form.Row("Output folder"));
  BuildSettings(pBody, form);

  // the simulation note, then the progress
  form.Gap(s_fGap - plEditorUiForm::RowGap);
  form.Hint(s_szNote, true);
  form.Gap(s_fGap - plEditorUiForm::RowGap);

  m_pProcess->Build(pBody, plRectFloat(x, form.GetY(), fWidth, s_fProcessHeight));

  AddBodyToolTips(form.GetToolTips());
}

void plSequenceRenderEngineDlg::BuildFolderRow(plUiElement* pBody, const plRectFloat& rect)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();
  const plRectFloat field(rect.x, rect.y, rect.width - s_fBrowseWidth - 4.0f, rect.height);

  // the folder cannot change while a render writes to it
  if (m_bRendering)
    ReadOnlyField(pBody, field, ReadFolder(), true);
  else
    TextField(pBody, field, s_szFolder, s_szFolderText, {}, {}, true);

  PushButton(pBody, board, plRectFloat(field.Right() + 4.0f, rect.y + 1.0f, s_fBrowseWidth, rect.height - 2.0f), "...", s_szBrowse, !m_bRendering);
  SetToolTip(board, s_szBrowse, "Render output folder");
}

void plSequenceRenderEngineDlg::BuildSettings(plUiElement* pBody, plEditorUiForm& ref_form)
{
  using namespace plEditorUi;

  plBlackboard& board = GetSurface()->GetBlackboard();
  const bool bIdle = !m_bRendering;
  const bool bEncoding = IsEncoding();

  m_ResolutionRect = ref_form.Row("Resolution");
  Dropdown(pBody, board, m_ResolutionRect, s_Resolutions[m_uiResolution].m_szLabel, s_szResolution, bIdle);

  m_FrameRateRect = ref_form.Row("Frame rate");
  Dropdown(pBody, board, m_FrameRateRect, s_FrameRates[m_uiFrameRate].m_szLabel, s_szFrameRate, bIdle);

  m_VideoRect = ref_form.Row("Video");
  Dropdown(pBody, board, m_VideoRect, m_Encoders[m_uiEncoder].m_sLabel, s_szVideo, bIdle);

  // the codec, the quality and the PNG frames apply to the encoders only
  const plStringView sCodec = bEncoding && !m_Codecs.IsEmpty() ? m_Codecs[m_uiCodec].m_sLabel.GetView() : plStringView();
  m_CodecRect = ref_form.Row("Codec");
  Dropdown(pBody, board, m_CodecRect, sCodec, s_szCodec, bIdle && bEncoding && m_Codecs.GetCount() > 1);
  SetToolTip(board, s_szCodec, GetCodecToolTip());

  m_QualityRect = ref_form.Row("Quality");
  Dropdown(pBody, board, m_QualityRect, s_Qualities[m_uiQuality].m_szLabel, s_szQuality, bIdle && bEncoding);
  SetToolTip(board, s_szQuality, s_szQualityTip);

  const plRectFloat keep = ref_form.WideRow(ControlHeight);
  const float fKeepWidth = plMath::Min(keep.width, PushButtonWidth(s_szKeepFramesLabel) + 24.0f);
  CheckButton(pBody, board, plRectFloat(keep.x, keep.y, fKeepWidth, keep.height), s_szKeepFramesLabel, s_szKeepFrames, m_bKeepFrames, bIdle && bEncoding);
  SetToolTip(board, s_szKeepFrames, s_szKeepFramesTip);
}

void plSequenceRenderEngineDlg::ShowMenu(Menu menu, const plRectFloat& field, plArrayPtr<const plString> items, plUInt32 uiChecked)
{
  plEditorUiMenu popup;
  popup.m_fMinWidth = field.width;

  for (plUInt32 i = 0; i < items.GetCount(); ++i)
  {
    plEditorUiMenuItem& item = popup.AddAction(items[i], i);
    item.m_bCheckable = true;
    item.m_bChecked = i == uiChecked;
  }

  const plVec2 vPos(GetBodyRect().x + field.x, GetBodyRect().y + field.y + field.height);
  plEditorUiMenuView::Show(*GetSurface(), vPos, popup, [this, menu](plUInt32 uiPicked)
    { OnMenuPicked(menu, uiPicked); });
}

void plSequenceRenderEngineDlg::OnMenuPicked(Menu menu, plUInt32 uiPicked)
{
  if (m_bRendering)
    return;

  switch (menu)
  {
    case Menu::Resolution:
      if (uiPicked < PL_ARRAY_SIZE(s_Resolutions))
        m_uiResolution = uiPicked;
      break;

    case Menu::FrameRate:
      if (uiPicked < PL_ARRAY_SIZE(s_FrameRates))
        m_uiFrameRate = uiPicked;
      break;

    case Menu::Video:
      if (uiPicked < m_Encoders.GetCount() && uiPicked != m_uiEncoder)
      {
        m_uiEncoder = uiPicked;
        GatherCodecs();
      }
      break;

    case Menu::Codec:
      if (uiPicked < m_Codecs.GetCount())
        m_uiCodec = uiPicked;
      break;

    case Menu::Quality:
      if (uiPicked < PL_ARRAY_SIZE(s_Qualities))
        m_uiQuality = uiPicked;
      break;
  }

  Rebuild();
}

void plSequenceRenderEngineDlg::UpdateButtons()
{
  GetHost().SetButtonLabel(s_szRender, m_bRendering ? "Cancel" : "Render");

  // after a cancel, Render waits for the frame that was still being rendered
  GetHost().SetButtonEnabled(s_szRender, m_bRendering || !m_bFrameInFlight);
  GetHost().SetButtonEnabled(s_szClose, !m_bRendering);
}

void plSequenceRenderEngineDlg::OnUiUpdate()
{
  if (m_pProcess->Update())
    RequestRedraw();
}

void plSequenceRenderEngineDlg::OnUiEvent(const plMsgUiEvent& event)
{
  if (m_bRendering)
    return;

  const plStringView sId = event.m_sIdentifier.GetView();

  if (sId == s_szBrowse)
  {
    Browse();
  }
  else if (sId == s_szKeepFrames)
  {
    if (IsEncoding())
    {
      m_bKeepFrames = !m_bKeepFrames;
      Rebuild();
    }
  }
  else if (sId == s_szResolution)
  {
    ShowMenu(Menu::Resolution, m_ResolutionRect, GetLabels(s_Resolutions), m_uiResolution);
  }
  else if (sId == s_szFrameRate)
  {
    ShowMenu(Menu::FrameRate, m_FrameRateRect, GetLabels(s_FrameRates), m_uiFrameRate);
  }
  else if (sId == s_szQuality)
  {
    ShowMenu(Menu::Quality, m_QualityRect, GetLabels(s_Qualities), m_uiQuality);
  }
  else if (sId == s_szVideo)
  {
    plHybridArray<plString, 3> labels;
    for (const Encoder& encoder : m_Encoders)
      labels.PushBack(encoder.m_sLabel);

    ShowMenu(Menu::Video, m_VideoRect, labels, m_uiEncoder);
  }
  else if (sId == s_szCodec)
  {
    plHybridArray<plString, 4> labels;
    for (const Codec& codec : m_Codecs)
      labels.PushBack(codec.m_sLabel);

    ShowMenu(Menu::Codec, m_CodecRect, labels, m_uiCodec);
  }
}

void plSequenceRenderEngineDlg::OnButton(plStringView sId)
{
  if (sId == s_szRender)
  {
    // outside the engine UI's event: starting can open a message box, stopping re-clones the context scene
    m_Timers.Start(plTime::MakeZero(), [this]()
      {
        if (m_bRendering)
          StopRender(StopReason::Cancelled);
        else
          StartRender(); });
    return;
  }

  // Close, Escape and the frame's close button: a running render keeps the dialog open
  if (m_bRendering)
    return;

  plEngineDialog::OnButton(sId);
}

//////////////////////////////////////////////////////////////////////////
// The render

void plSequenceRenderEngineDlg::StartRender()
{
  if (m_bRendering || m_bFrameInFlight)
    return;

  const Resolution& resolution = s_Resolutions[m_uiResolution];
  const plInt32 iEncoder = GetEncoderKind();

  plSequenceCaptureDesc desc;
  desc.m_sOutputFolder = ReadFolder();
  desc.m_uiWidth = resolution.m_uiWidth;
  desc.m_uiHeight = resolution.m_uiHeight;
  desc.m_uiFramesPerSecond = s_FrameRates[m_uiFrameRate].m_uiFramesPerSecond;
  desc.m_bWriteFrames = m_bKeepFrames || iEncoder < 0;
  desc.m_bEncodeVideo = iEncoder >= 0;

  if (iEncoder >= 0)
  {
    desc.m_EncoderKind = static_cast<plVideoEncoderKind::Enum>(iEncoder);
    desc.m_Codec = GetSelectedCodec();
    desc.m_Quality = s_Qualities[m_uiQuality].m_Quality;
  }

  // every run starts with an empty log
  m_pProcess->ClearLog();
  m_pProcess->SetTitle({});
  m_pProcess->SetProgress(0.0f);
  m_pProcess->Start();

  plResult began = PL_FAILURE;
  {
    RenderLog log(*m_pProcess);
    began = m_Capture.Begin(desc, m_pDocument->GetDurationTicks());
  }

  if (began.Failed())
  {
    m_pProcess->Finish(plEditorUiProcessState::Failed);
    RequestRedraw();
    plEditorUiServices::MessageBoxWarning(plFmt("The render could not be started. See the log for details."));
    return;
  }

  m_sRenderFolder = desc.m_sOutputFolder;
  RememberFolder();

  m_bRendering = true;
  UpdateButtons();
  Rebuild();

  // The filmstrip renders through the same one-at-a-time engine channel, so it stands down for the duration or the two
  // would interleave requests and each would get the other's frames.
  m_pDocument->SetThumbnailRenderingSuspended(true);

  // The engine steps the world once per captured frame at exactly this rate, so particles, physics and animation are
  // in the output rather than frozen.
  SendRenderMode(true, desc.m_uiFramesPerSecond);

  UpdateProgress();
  RequestNextFrame();
}

void plSequenceRenderEngineDlg::SendRenderMode(bool bEnabled, plUInt32 uiFramesPerSecond)
{
  plStringBuilder sValue;
  sValue.SetFormat("{0}|{1}", bEnabled ? 1 : 0, uiFramesPerSecond);

  plDocumentConfigMsgToEngine msg;
  msg.m_DocumentGuid = m_pDocument->GetGuid();
  msg.m_sWhatToDo = "SequenceRenderMode";
  msg.m_sValue = sValue;
  m_pDocument->GetEditorEngineConnection()->SendMessage(&msg);
}

void plSequenceRenderEngineDlg::RequestNextFrame()
{
  if (!m_bRendering)
    return;

  if (!m_Capture.IsCapturing())
  {
    StopRender(StopReason::Finished);
    return;
  }

  const plUInt64 uiTick = m_Capture.GetCurrentTick();

  // Pose first, then read the camera: the cameras may themselves be animated by this sequence.
  m_pDocument->BeginThumbnailPose(uiTick);
  m_bPosed = true;

  plTransform cameraTransform;
  float fFov = 60.0f;

  // without a cut there is no defined viewpoint: the render stops rather than emit frames from an arbitrary camera
  if (!m_pDocument->GetCutCameraAtTick(static_cast<plInt64>(uiTick), cameraTransform, fFov))
  {
    FailNoCameraCut(uiTick);
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

  const Resolution& resolution = s_Resolutions[m_uiResolution];

  plCreateThumbnailMsgToEngine thumbMsg;
  thumbMsg.m_DocumentGuid = m_pDocument->GetGuid();
  thumbMsg.m_uiWidth = static_cast<plUInt16>(resolution.m_uiWidth);
  thumbMsg.m_uiHeight = static_cast<plUInt16>(resolution.m_uiHeight);
  m_pDocument->GetEditorEngineConnection()->SendMessage(&thumbMsg);

  m_bFrameInFlight = true;
  const plUInt32 uiRequest = ++m_uiFrameRequest;
  m_Timers.Start(plTime::MakeFromMilliseconds(s_iFrameTimeoutMs), [this, uiRequest]()
    { OnFrameTimeout(uiRequest); });
}

void plSequenceRenderEngineDlg::OnFrameTimeout(plUInt32 uiRequest)
{
  if (!m_bFrameInFlight || uiRequest != m_uiFrameRequest)
    return;

  if (m_bRendering)
    RetryFrame();
  else
    ReleaseFrameSlot();
}

void plSequenceRenderEngineDlg::RetryFrame()
{
  // The engine renders one offscreen frame at a time and drops a request that arrives while another is open, such as a
  // filmstrip tile requested just before the render started. The same frame is posed and requested again.
  if (!m_bRendering)
  {
    ReleaseFrameSlot();
    return;
  }

  m_bFrameInFlight = false;

  if (m_bPosed)
  {
    m_pDocument->EndThumbnailPose();
    m_bPosed = false;
  }

  RequestNextFrame();
}

void plSequenceRenderEngineDlg::ReleaseFrameSlot()
{
  m_bFrameInFlight = false;
  m_pDocument->SetThumbnailRenderingSuspended(false);
  UpdateButtons();
}

void plSequenceRenderEngineDlg::ThumbnailArrivedHandler(const plCreateThumbnailMsgToEditor& msg)
{
  // nothing of ours is open: a filmstrip tile
  if (!m_bFrameInFlight)
    return;

  // the frame a cancelled render left open: dropped, and the filmstrip may render again
  if (!m_bRendering)
  {
    ReleaseFrameSlot();
    return;
  }

  const Resolution& resolution = s_Resolutions[m_uiResolution];
  const plUInt32 uiExpected = resolution.m_uiWidth * resolution.m_uiHeight * 4;

  // A filmstrip tile that was open when the render started, so the engine dropped this frame's request. Asked again
  // after this handler unwound; the open request's timeout no longer applies.
  if (msg.m_ThumbnailData.GetCount() == s_uiTileBytes && uiExpected != s_uiTileBytes)
  {
    ++m_uiFrameRequest;
    m_Timers.Start(plTime::MakeZero(), [this]()
      { RetryFrame(); });
    return;
  }

  m_bFrameInFlight = false;

  if (m_bPosed)
  {
    m_pDocument->EndThumbnailPose();
    m_bPosed = false;
  }

  if (msg.m_ThumbnailData.GetCount() != uiExpected)
  {
    {
      RenderLog log(*m_pProcess);
      plLog::Error("Sequence render: frame came back as {0} bytes, expected {1}.", msg.m_ThumbnailData.GetCount(), uiExpected);
    }

    StopRender(StopReason::Failed);
    return;
  }

  plImageHeader header;
  header.SetWidth(resolution.m_uiWidth);
  header.SetHeight(resolution.m_uiHeight);
  header.SetImageFormat(plImageFormat::R8G8B8A8_UNORM_SRGB);

  plImage frame;
  frame.ResetAndAlloc(header);
  plMemoryUtils::RawByteCopy(frame.GetByteBlobPtr().GetPtr(), msg.m_ThumbnailData.GetData(), uiExpected);

  plResult submitted = PL_FAILURE;
  {
    RenderLog log(*m_pProcess);
    submitted = m_Capture.SubmitFrame(frame);
  }

  if (submitted.Failed())
  {
    StopRender(StopReason::Failed);
    return;
  }

  UpdateProgress();

  // Queued rather than recursed: this runs inside the message handler, and the next request goes out after it unwound.
  m_Timers.Start(plTime::MakeZero(), [this]()
    { RequestNextFrame(); });
}

void plSequenceRenderEngineDlg::UpdateProgress()
{
  const plUInt32 uiDone = m_Capture.GetFramesWritten();
  const plUInt32 uiTotal = m_Capture.GetTotalFrames();

  plStringBuilder sTitle;
  sTitle.SetFormat("frame {} / {}", uiDone, uiTotal);
  m_pProcess->SetTitle(sTitle);
  m_pProcess->SetProgress(uiTotal > 0 ? static_cast<float>(uiDone) / static_cast<float>(uiTotal) : 0.0f);

  RequestRedraw();
}

void plSequenceRenderEngineDlg::FailNoCameraCut(plUInt64 uiTick)
{
  const double fSeconds = static_cast<double>(uiTick) / static_cast<double>(plSequenceTicksPerSecond);

  plStringBuilder sLine;
  sLine.SetFormat("No camera cut is active at {0}s, so there is nothing to render from.", fSeconds);
  m_pProcess->AppendLine(sLine, plEditorUiProcessLineStyle::Error);

  StopRender(StopReason::Failed);

  plEditorUiServices::MessageBoxWarning(
    plFmt("No camera cut is active at {0}s, so there is nothing to render from.\n\nAdd a camera cut covering the range.", fSeconds));
}

void plSequenceRenderEngineDlg::StopRender(StopReason reason)
{
  if (!m_bRendering)
    return;

  m_bRendering = false;

  if (m_bPosed)
  {
    m_pDocument->EndThumbnailPose();
    m_bPosed = false;
  }

  plString sAudioNote;
  if (reason == StopReason::Failed)
  {
    // the capture logs an abort as "cancelled", so that line stays in the editor's log: the block shows the error
    m_Capture.Abort();
  }
  else
  {
    RenderLog log(*m_pProcess);

    if (reason == StopReason::Cancelled)
      m_Capture.Abort();
    else if (m_Capture.Finish().Succeeded())
      sAudioNote = WriteAndAttachAudio();
    else
      reason = StopReason::Failed; // the video could not be finalised; the encoder logged why
  }

  SendRenderMode(false, 0);

  // Simulating moved things that did not come from the document, so the preview world no longer matches it. Re-cloning
  // the context scene puts it back.
  const plUuid contextDoc = m_pDocument->GetContextDocumentGuid();
  if (contextDoc.IsValid())
    m_pDocument->SetContext(contextDoc, m_pDocument->GetContextObjectGuid()).LogFailure();

  // A frame still being rendered keeps the engine's offscreen slot: the filmstrip waits for it (or its timeout), or it
  // would receive this frame as its tile and its own request would be dropped.
  if (!m_bFrameInFlight)
    m_pDocument->SetThumbnailRenderingSuspended(false);

  FinishProcess(reason, sAudioNote);
  UpdateButtons();
  Rebuild();
}

void plSequenceRenderEngineDlg::FinishProcess(StopReason reason, plStringView sAudioNote)
{
  const plUInt32 uiFrames = m_Capture.GetFramesWritten();
  plStringBuilder sTitle;

  switch (reason)
  {
    case StopReason::Finished:
      sTitle.SetFormat("Done \xE2\x80\x94 {} frames.{}", uiFrames, sAudioNote);
      m_pProcess->Finish(plEditorUiProcessState::Finished, sTitle);
      break;

    case StopReason::Cancelled:
      sTitle.SetFormat("Cancelled \xE2\x80\x94 {} frames kept.", uiFrames);
      m_pProcess->Finish(plEditorUiProcessState::Cancelled, sTitle);
      break;

    case StopReason::Failed:
      m_pProcess->Finish(plEditorUiProcessState::Failed);
      break;
  }

  RequestRedraw();
}

plString plSequenceRenderEngineDlg::WriteAndAttachAudio()
{
  // The preview render evaluates the document, which never fires clips, so the audio clips are mixed offline into a WAV
  // next to the frames, and muxed into the video when ffmpeg is around to do it.
  plStringBuilder sWav = m_sRenderFolder;
  sWav.AppendPath("render.wav");
  sWav.MakeCleanPath();

  bool bHadClips = false;
  if (plSequenceAudioMix::WriteMixToWav(m_pDocument, 0, m_pDocument->GetDurationTicks(), sWav, bHadClips).Failed())
    return " Audio mix failed \xE2\x80\x94 see log.";

  if (!bHadClips)
  {
    // no audio clip resolved to anything: a silent wav next to the frames would only mislead
    plOSFile::DeleteFile(sWav).IgnoreResult();
    return plString();
  }

  // Only ffmpeg can mux; Media Foundation's writer was closed by Finish and cannot take a late audio stream.
  if (GetEncoderKind() != static_cast<plInt32>(plVideoEncoderKind::FfmpegCli) || plVideoEncoderFactory::GetFfmpegPath().IsEmpty())
    return " Audio written to render.wav.";

  // whatever container the codec chose: .mov for ProRes, .mp4 otherwise
  const plVideoCodec::Enum codec = GetSelectedCodec();
  const plStringView sExtension = plVideoEncoderFactory::GetContainerExtension(codec);

  plStringBuilder sVideo = m_sRenderFolder;
  sVideo.AppendPath("render.");
  sVideo.Append(sExtension);
  sVideo.MakeCleanPath();

  plStringBuilder sMuxed = m_sRenderFolder;
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

  // a ProRes .mov is an intermediate headed for an NLE, so its audio stays uncompressed; a delivery mp4 gets AAC
  opt.AddArgument(codec == plVideoCodec::ProRes ? "pcm_s16le" : "aac");

  opt.AddArgument("-shortest");
  opt.AddArgument(sMuxed);

  plInt32 iExitCode = 0;
  if (plProcess::Execute(opt, &iExitCode).Failed() || iExitCode != 0)
  {
    plLog::Error("Sequence render: audio mux failed (exit {0}); the wav is kept beside the video.", iExitCode);
    return " Audio written to render.wav (mux failed).";
  }

  // the muxed file takes the video's place, so the deliverable stays "render.<ext>, with audio"
  plOSFile::DeleteFile(sVideo).IgnoreResult();
  if (plOSFile::MoveFileOrDirectory(sMuxed, sVideo).Failed())
  {
    plStringBuilder sNote;
    sNote.SetFormat(" Audio muxed to render_mux.{}.", sExtension);
    return sNote;
  }

  return " Audio mixed in.";
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceRenderEngineDlg);
