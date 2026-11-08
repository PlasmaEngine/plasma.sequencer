#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceTrackView.h>
#include <EditorUi/Surface.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Algorithm/HashingUtils.h>
#include <GuiFoundation/EngineUi/EngineColorDialog.h>
#include <GuiFoundation/EngineUi/EnginePrompt.h>
#include <GuiFoundation/UIServices/UIServices.h>
#include <ToolsFoundation/Command/TreeCommands.h>
#include <ToolsFoundation/Selection/SelectionManager.h>

namespace
{
  plColorGammaUB Mix(plColorGammaUB a, plColorGammaUB b, float t)
  {
    return plColorGammaUB(static_cast<plUInt8>(a.r + (b.r - a.r) * t), static_cast<plUInt8>(a.g + (b.g - a.g) * t), static_cast<plUInt8>(a.b + (b.b - a.b) * t), 255);
  }

  plColorGammaUB Category(const char* szCategory)
  {
    return plColorScheme::GetCategoryColor(szCategory, plColorScheme::CategoryColorUsage::OverlayIcon);
  }

  // menu ids of the key, clip, cut, marker and empty-lane menus
  enum MenuId : plUInt32
  {
    RenameEvent = 1,
    EaseAuto,
    EaseLinear,
    EaseIn,
    EaseOut,
    EaseInOut,
    DeleteKeys,
    CopyKeys,
    FitToTrigger,
    DeleteClip,
    DeleteCut,
    DeleteMarker,
    RenameMarkerItem,
    PasteKeys,
    SelectAll,
    AddMarker,
  };
} // namespace

plSequenceTrackView::plSequenceTrackView(plEditorUiTrackView& ref_view)
  : m_pView(&ref_view)
{
  m_pView->SetHeaderWidth(HeadWidth);
  m_pView->SetHeaderTitle("BINDINGS / TRACKS");
  m_pView->SetPersistKey("Sequencer/TimeUnit");
  m_pView->SetEmptyText("Add a binding for an object selected in the Scenegraph panel");
  m_pView->m_GetThumbnail = [this](double fTime)
  { return GetThumbnail(fTime); };
  m_pView->m_Events.AddEventHandler(plMakeDelegate(&plSequenceTrackView::OnViewEvent, this));

  SetSnapMode(SnapMode::Frame);
}

plSequenceTrackView::~plSequenceTrackView()
{
  m_Timers.StopAll();

  m_pView->m_GetThumbnail = nullptr;
  m_pView->m_Events.RemoveEventHandler(plMakeDelegate(&plSequenceTrackView::OnViewEvent, this));

  SetModel(nullptr);
}

void plSequenceTrackView::SetModel(plSequenceModel* pModel)
{
  if (m_pModel != nullptr)
  {
    m_pModel->m_Events.RemoveEventHandler(plMakeDelegate(&plSequenceTrackView::OnModelEvent, this));
    m_pModel->GetDocument()->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plSequenceTrackView::DocumentPropertyEventHandler, this));
  }

  m_pModel = pModel;

  if (m_pModel == nullptr)
    return;

  m_pModel->m_Events.AddEventHandler(plMakeDelegate(&plSequenceTrackView::OnModelEvent, this));

  if (m_pThumbnails == nullptr)
  {
    m_pThumbnails = PL_DEFAULT_NEW(plSequenceThumbnailCache, m_pModel->GetDocument());
    m_pThumbnails->m_OnThumbnailReady = [this]()
    { Refresh(); };
  }

  m_pModel->GetDocument()->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plSequenceTrackView::DocumentPropertyEventHandler, this));

  Refresh();
}

void plSequenceTrackView::OnModelEvent(const plSequenceModelEvent& e)
{
  // A rebuild means bindings or cuts changed - a cut may now point at a different camera, so
  // its cached image no longer describes it.
  if (e.m_Type == plSequenceModelEvent::Type::ModelRebuilt && m_pThumbnails != nullptr)
    m_pThumbnails->InvalidateAll();

  Refresh();
}

void plSequenceTrackView::SetSnapMode(SnapMode mode)
{
  m_SnapMode = mode;
  m_pView->SetSnapStep(GetSnapStep());
}

double plSequenceTrackView::GetSnapStep() const
{
  const plSequenceAssetDocument* pDoc = GetDocument();
  if (pDoc == nullptr || m_SnapMode == SnapMode::Free)
    return 0.0;

  const double fFrame = ToSeconds(static_cast<plInt64>(pDoc->GetTicksPerFrame()));
  return m_SnapMode == SnapMode::HalfFrame ? fFrame * 0.5 : fFrame;
}

plInt64 plSequenceTrackView::SnapTick(plInt64 iTick) const
{
  const plSequenceAssetDocument* pDoc = GetDocument();
  const plInt64 iMax = pDoc != nullptr ? static_cast<plInt64>(pDoc->GetDurationTicks()) : iTick;
  return plMath::Clamp<plInt64>(ToTicks(m_pView->Snap(ToSeconds(iTick))), 0, iMax);
}

plUInt64 plSequenceTrackView::IdOf(const plUuid& guid)
{
  return plHashingUtils::xxHash64(&guid, sizeof(plUuid)) | 1; // never 0, which means none
}

void plSequenceTrackView::DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  if (e.m_EventType != plDocumentObjectPropertyEvent::Type::PropertySet || e.m_pObject == nullptr)
    return;

  // markers and event keys are not part of the model, so their names and times are redrawn from here
  if (e.m_pObject->GetType() == plGetStaticRTTI<plSequenceMarker>() || e.m_pObject->GetType() == plGetStaticRTTI<plEventTrackControlPointData>())
  {
    Refresh();
    return;
  }

  if (e.m_pObject->GetType() != plGetStaticRTTI<plSequenceAudioClip>() || e.m_sProperty != "PlayTrigger")
    return;

  const plString sTrigger = e.m_NewValue.ConvertTo<plString>();
  if (sTrigger.IsEmpty())
    return;

  // Only while the clip is still its default length: once a length has been dialled in by hand,
  // changing the trigger must not silently throw that away.
  const plInt64 iCurrentDuration = e.m_pObject->GetTypeAccessor().GetValue("DurationTicks").ConvertTo<plInt64>();
  if (iCurrentDuration != (plInt64)plSequenceTicksPerSecond)
    return;

  const auto* pPeaks = m_Waveforms.GetPeaks(sTrigger);
  if (pPeaks == nullptr || pPeaks->m_fDurationSeconds <= 0.0)
    return;

  const plUuid clipGuid = e.m_pObject->GetGuid();

  // Queued: this fires from inside the transaction that set the trigger, and starting another one
  // there would nest the resize into the edit that caused it.
  m_Timers.Start(plTime::MakeZero(), [this, clipGuid]()
    { FitClipToTrigger(clipGuid); });
}

//////////////////////////////////////////////////////////////////////////
// Rows and content

bool plSequenceTrackView::IsSelected(const plSequenceModel::FlatRow& row) const
{
  return m_SelectedRow.IsValid() && row.m_ObjectGuid == m_SelectedRow && row.m_Kind == m_SelectedKind && (row.m_Kind != plSequenceModel::RowKind::ClipLane || row.m_ClipKind.GetValue() == m_uiSelectedClipKind);
}

const plSequenceModel::FlatRow* plSequenceTrackView::GetSelectedRow() const
{
  if (m_pModel == nullptr)
    return nullptr;

  for (const auto& row : m_pModel->GetFlatRows())
  {
    if (IsSelected(row))
      return &row;
  }

  return nullptr;
}

void plSequenceTrackView::SelectRow(const plSequenceModel::FlatRow& row)
{
  if (IsSelected(row))
    return;

  m_SelectedRow = row.m_ObjectGuid;
  m_SelectedKind = row.m_Kind;
  m_uiSelectedClipKind = static_cast<plUInt8>(row.m_ClipKind.GetValue());
  Refresh();

  if (m_OnRowSelectionChanged)
    m_OnRowSelectionChanged();
}

void plSequenceTrackView::SelectRowFor(const plUuid& objectGuid)
{
  if (m_pModel == nullptr)
    return;

  for (const auto& row : m_pModel->GetFlatRows())
  {
    if (row.m_ObjectGuid == objectGuid && row.m_Kind != plSequenceModel::RowKind::ClipLane)
    {
      SelectRow(row);
      return;
    }
  }
}

bool plSequenceTrackView::IsRowCollapsed(const plSequenceModel::FlatRow& row) const
{
  // hidden when any ancestor is folded
  for (plInt32 iParent = m_pModel->GetNode(row.m_iNode).m_iParent; iParent >= 0; iParent = m_pModel->GetNode(iParent).m_iParent)
  {
    if (m_Collapsed.Contains(m_pModel->GetNode(iParent).m_ObjectGuid))
      return true;
  }

  return false;
}

const plSequenceModel::FlatRow* plSequenceTrackView::GetRowOfLane(plInt32 iLane) const
{
  if (m_pModel == nullptr || iLane < 0 || static_cast<plUInt32>(iLane) >= m_LaneRows.GetCount())
    return nullptr;

  return &m_pModel->GetFlatRows()[m_LaneRows[iLane]];
}

void plSequenceTrackView::Refresh()
{
  plEngineTrackContent content;
  BuildContent(content);
  m_pView->SetContent(std::move(content));

  if (const plSequenceAssetDocument* pDoc = GetDocument())
    m_pView->SetPlayhead(ToSeconds(static_cast<plInt64>(pDoc->GetScrubberPosition())));

  m_pView->SetSnapStep(GetSnapStep());
}

void plSequenceTrackView::BuildContent(plEngineTrackContent& out_content)
{
  m_LaneRows.Clear();
  m_Keys.Clear();
  m_Clips.Clear();
  m_Markers.Clear();

  plSequenceAssetDocument* pDoc = GetDocument();
  if (pDoc == nullptr || m_pModel == nullptr)
    return;

  out_content.m_fDuration = ToSeconds(static_cast<plInt64>(pDoc->GetDurationTicks()));
  out_content.m_uiFramesPerSecond = plMath::Max<plUInt32>(1, pDoc->GetProperties()->m_uiFramesPerSecond);

  const auto& rows = m_pModel->GetFlatRows();
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    if (IsRowCollapsed(rows[r]))
      continue;

    const plUInt32 uiLane = out_content.m_Lanes.GetCount();
    m_LaneRows.PushBack(r);
    AddLaneContent(uiLane, r, out_content);
  }

  // markers: a flag on the ruler with a line down the lanes
  const plDocumentObject* pPropObj = pDoc->GetPropertyObject();
  const plInt32 iMarkerCount = pPropObj->GetTypeAccessor().GetCount("Markers");
  for (plInt32 i = 0; i < iMarkerCount; ++i)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("Markers", i);
    const plDocumentObject* pMarkerObj = guid.IsA<plUuid>() ? pDoc->GetObjectManager()->GetObject(guid.Get<plUuid>()) : nullptr;
    if (pMarkerObj == nullptr)
      continue;

    plEngineTrackMarker& marker = out_content.m_Markers.ExpandAndGetRef();
    marker.m_uiId = IdOf(pMarkerObj->GetGuid());
    marker.m_fTime = ToSeconds(pMarkerObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>());
    marker.m_sLabel = pMarkerObj->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
    m_Markers.Insert(marker.m_uiId, pMarkerObj->GetGuid());
  }
}

void plSequenceTrackView::AddLaneContent(plUInt32 uiLane, plUInt32 uiRow, plEngineTrackContent& ref_content)
{
  using Kind = plSequenceModel::RowKind;

  const plSequenceModel::FlatRow& row = m_pModel->GetFlatRows()[uiRow];
  const plSequenceModel::Node& node = m_pModel->GetNode(row.m_iNode);

  // the head: strip and icon in the row's category colour, the name, its path or component, a twisty and an add
  // button on bindings
  plEngineTrackLane& lane = ref_content.m_Lanes.ExpandAndGetRef();
  lane.m_sLabel = node.m_sDisplay;
  lane.m_sSubLabel = node.m_sSubText;
  lane.m_StripColor = node.m_Color;
  lane.m_sIcon = node.m_sIconPath;
  lane.m_uiDepth = row.m_uiDepth;
  lane.m_bGroup = row.m_Kind == Kind::Binding;
  lane.m_bExpandable = (row.m_Kind == Kind::Binding || row.m_Kind == Kind::VectorGroup) && !node.m_Children.IsEmpty();
  lane.m_bExpanded = !m_Collapsed.Contains(row.m_ObjectGuid);
  lane.m_bAddButton = row.m_Kind == Kind::Binding && !node.m_bUnresolved;
  lane.m_bHighlighted = IsSelected(row);
  lane.m_fHeight = row.IsCameraCutLane() ? CameraLaneHeight : row.IsClipLane() ? ClipLaneHeight : RowHeight;

  if (node.m_iAxis >= 0)
    lane.m_Swatches.PushBack(plEditorUi::Axis(static_cast<plUInt32>(node.m_iAxis)));

  if (node.m_bUnresolved)
    lane.m_LabelColor = plEditorUi::Danger();

  switch (row.m_Kind)
  {
    case Kind::Binding:
      AddBindingSummary(uiLane, uiRow, ref_content);
      break;

    case Kind::CameraCutLane:
      AddCameraCuts(uiLane, ref_content);
      break;

    case Kind::ClipLane:
      AddClips(uiLane, row, ref_content);
      break;

    case Kind::EventLane:
      AddEvents(uiLane, row, ref_content);
      break;

    default:
      if (IsColorTrack(row.m_ObjectGuid))
        AddColorKeys(uiLane, row, ref_content);
      else
        AddCurveKeys(uiLane, row, ref_content);
      break;
  }
}

void plSequenceTrackView::AddBindingSummary(plUInt32 uiLane, plUInt32 uiRow, plEngineTrackContent& ref_content)
{
  // Every tick any of the binding's tracks is keyed on, small and grey: collapsing a binding still
  // shows where its timing sits.
  const auto& rows = m_pModel->GetFlatRows();
  plSet<plInt64> ticks;
  for (plUInt32 s = uiRow + 1; s < rows.GetCount() && rows[s].m_uiDepth > rows[uiRow].m_uiDepth; ++s)
  {
    if (rows[s].m_Kind == plSequenceModel::RowKind::VectorGroup)
      continue; // its axes are listed separately, so counting it too would double up

    ForEachKey(rows[s], [&](const plUuid&, plInt64 iTick, double)
      { ticks.Insert(iTick); });

    if (IsColorTrack(rows[s].m_ObjectGuid))
    {
      plDynamicArray<plSequenceAssetDocument::ColorKey> colorKeys;
      GetDocument()->GetColorKeys(rows[s].m_ObjectGuid, colorKeys);
      for (const auto& colorKey : colorKeys)
        ticks.Insert(colorKey.m_iTick);
    }
  }

  for (plInt64 iTick : ticks)
  {
    plEngineTrackKey& key = ref_content.m_Keys.ExpandAndGetRef();
    key.m_uiId = IdOf(rows[uiRow].m_ObjectGuid) ^ static_cast<plUInt64>(iTick);
    key.m_uiLane = uiLane;
    key.m_fTime = ToSeconds(iTick);
    key.m_bSelectable = false;
  }
}

void plSequenceTrackView::AddCameraCuts(plUInt32 uiLane, plEngineTrackContent& ref_content)
{
  plSequenceAssetDocument* pDoc = GetDocument();
  const plDocumentObject* pPropObj = pDoc->GetPropertyObject();
  const plInt32 iCutCount = pPropObj->GetTypeAccessor().GetCount("CameraCuts");

  struct Cut
  {
    PL_DECLARE_POD_TYPE();

    const plDocumentObject* m_pObject;
    plInt64 m_iStart;
    plInt64 m_iDuration;
  };

  plHybridArray<Cut, 8> cuts;
  for (plInt32 c = 0; c < iCutCount; ++c)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("CameraCuts", c);
    const plDocumentObject* pCutObj = guid.IsA<plUuid>() ? pDoc->GetObjectManager()->GetObject(guid.Get<plUuid>()) : nullptr;
    if (pCutObj != nullptr)
      cuts.PushBack({pCutObj, pCutObj->GetTypeAccessor().GetValue("StartTick").ConvertTo<plInt64>(), plMath::Max<plInt64>(1, pCutObj->GetTypeAccessor().GetValue("DurationTicks").ConvertTo<plInt64>())});
  }

  cuts.Sort([](const Cut& lhs, const Cut& rhs)
    { return lhs.m_iStart < rhs.m_iStart; });

  const plColorGammaUB rendering = Category("Rendering");
  for (plUInt32 c = 0; c < cuts.GetCount(); ++c)
  {
    const Cut& cut = cuts[c];
    const bool bBroken = !pDoc->CanResolveCameraCut(cut.m_pObject->GetGuid());

    plEngineTrackClip& clip = ref_content.m_Clips.ExpandAndGetRef();
    clip.m_uiId = IdOf(cut.m_pObject->GetGuid());
    clip.m_uiLane = uiLane;
    clip.m_fStart = ToSeconds(cut.m_iStart);
    clip.m_fDuration = ToSeconds(cut.m_iDuration);

    // A cut holds the camera until the next one starts even if its own tail is shorter; the tail is
    // what trims, and dragging it past the next cut is how a blend is made.
    const plInt64 iNextStart = c + 1 < cuts.GetCount() ? cuts[c + 1].m_iStart : static_cast<plInt64>(pDoc->GetDurationTicks());
    clip.m_fHoldUntil = ToSeconds(plMath::Max(cut.m_iStart + cut.m_iDuration, iNextStart));
    clip.m_bCanTrimStart = false;

    // the blend is the overlap with the previous shot
    if (c > 0)
      clip.m_fBlendIn = ToSeconds(plMath::Max<plInt64>(0, cuts[c - 1].m_iStart + cuts[c - 1].m_iDuration - cut.m_iStart));

    // A cut whose camera cannot be resolved will not pilot and will not play; say so on the bar.
    plStringBuilder sLabel = cut.m_pObject->GetTypeAccessor().GetValue("CameraName").ConvertTo<plString>();
    if (bBroken)
    {
      sLabel.Append("  (camera not found)");
      clip.m_Color = Mix(plEditorUi::Background(), plEditorUi::Danger(), 0.3f);
      clip.m_TextColor = plEditorUi::Danger();
    }
    else
    {
      clip.m_Color = Mix(plEditorUi::Background(), rendering, 0.35f);
      clip.m_bFilmstrip = true;
    }

    clip.m_sLabel = sLabel;

    ClipRef ref;
    ref.m_Guid = cut.m_pObject->GetGuid();
    ref.m_bCut = true;
    ref.m_iStart = cut.m_iStart;
    ref.m_iDuration = cut.m_iDuration;
    m_Clips.Insert(clip.m_uiId, ref);
  }
}

void plSequenceTrackView::AddClips(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content)
{
  const plColorGammaUB laneColor = m_pModel->GetNode(row.m_iNode).m_Color;

  ForEachClip(row, [&](const plDocumentObject* pClipObj)
    {
    const plInt64 iStart = pClipObj->GetTypeAccessor().GetValue("StartTick").ConvertTo<plInt64>();
    const plInt64 iDuration = plMath::Max<plInt64>(1, pClipObj->GetTypeAccessor().GetValue("DurationTicks").ConvertTo<plInt64>());
    const plInt64 iKind = pClipObj->GetTypeAccessor().GetValue("Kind").ConvertTo<plInt64>();

    // Each clip kind names its payload differently (PlayTrigger / Clip / Sequence), so the label
    // comes off the native object; the trigger's name, not its asset GUID.
    const plSequenceClip* pClip = GetDocument()->GetClip(pClipObj->GetGuid());
    const plString sPayload = pClip != nullptr ? pClip->m_sPayload : plString();
    const plString sFriendly = plSequenceWaveformCache::PayloadDisplayName(sPayload);

    plEngineTrackClip& clip = ref_content.m_Clips.ExpandAndGetRef();
    clip.m_uiId = IdOf(pClipObj->GetGuid());
    clip.m_uiLane = uiLane;
    clip.m_fStart = ToSeconds(iStart);
    clip.m_fDuration = ToSeconds(iDuration);
    clip.m_fBlendIn = ToSeconds(pClipObj->GetTypeAccessor().GetValue("BlendInTicks").ConvertTo<plInt64>());
    clip.m_fBlendOut = ToSeconds(pClipObj->GetTypeAccessor().GetValue("BlendOutTicks").ConvertTo<plInt64>());
    clip.m_sLabel = sFriendly.IsEmpty() ? plString("<empty>") : sFriendly;
    clip.m_Color = Mix(plEditorUi::Background(), laneColor, 0.32f);

    // Audio clips draw the sample's real peak envelope over the sample's own length at the clip's
    // speed; a payload with no decodable file keeps the plain bar.
    if (iKind == plSequenceClipKind::AudioTrigger)
    {
      if (const auto* pPeaks = m_Waveforms.GetPeaks(sPayload))
      {
        const double fSpeed = plMath::Max(0.0001, pClipObj->GetTypeAccessor().GetValue("Speed").ConvertTo<double>());
        clip.m_Waveform.m_fSeconds = plMath::Max(0.0001, pPeaks->m_fDurationSeconds) / fSpeed;
        for (plUInt32 b = 0; b < plSequenceWaveformCache::BucketCount; ++b)
        {
          clip.m_Waveform.m_Min.PushBack(pPeaks->m_Min[b]);
          clip.m_Waveform.m_Max.PushBack(pPeaks->m_Max[b]);
        }
      }
    }

    ClipRef ref;
    ref.m_Guid = pClipObj->GetGuid();
    ref.m_iStart = iStart;
    ref.m_iDuration = iDuration;
    m_Clips.Insert(clip.m_uiId, ref); });
}

void plSequenceTrackView::AddEvents(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content)
{
  // a diamond per event with its name beside it, so events read against the shots and clips
  const plDocumentObject* pTrackObj = GetDocument()->GetObjectManager()->GetObject(row.m_ObjectGuid);
  if (pTrackObj == nullptr)
    return;

  const plColorGammaUB gameplay = Category("Gameplay");
  const plInt32 iCount = pTrackObj->GetTypeAccessor().GetCount("ControlPoints");
  for (plInt32 i = 0; i < iCount; ++i)
  {
    const plVariant cpGuid = pTrackObj->GetTypeAccessor().GetValue("ControlPoints", i);
    const plDocumentObject* pCpObj = cpGuid.IsA<plUuid>() ? GetDocument()->GetObjectManager()->GetObject(cpGuid.Get<plUuid>()) : nullptr;
    if (pCpObj == nullptr)
      continue;

    const plInt64 iTick = pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();

    plEngineTrackKey& key = ref_content.m_Keys.ExpandAndGetRef();
    key.m_uiId = IdOf(pCpObj->GetGuid());
    key.m_uiLane = uiLane;
    key.m_fTime = ToSeconds(iTick);
    key.m_Color = gameplay;
    key.m_sLabel = pCpObj->GetTypeAccessor().GetValue("Event").ConvertTo<plString>();

    KeyRef ref;
    ref.m_TrackGuid = row.m_ObjectGuid;
    ref.m_CpGuid = pCpObj->GetGuid();
    ref.m_iTick = iTick;
    m_Keys.Insert(key.m_uiId, ref);
  }
}

void plSequenceTrackView::AddColorKeys(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content)
{
  const plSequenceTrack* pTrack = GetDocument()->GetTrack(row.m_ObjectGuid);
  if (pTrack == nullptr)
    return;

  // The evaluated ramp: a gradient has a value at every tick, so the lane shows the actual animation. Sampled finely
  // enough that alpha and intensity stops read correctly.
  plEngineTrackLane& lane = ref_content.m_Lanes[uiLane];
  const plInt64 iDuration = plMath::Max<plInt64>(1, static_cast<plInt64>(GetDocument()->GetDurationTicks()));
  constexpr plUInt32 uiSamples = 128;
  for (plUInt32 s = 0; s <= uiSamples; ++s)
  {
    const plInt64 iTick = iDuration * s / uiSamples;
    const plColorGammaUB color = pTrack->m_ColorGradient.Evaluate(iTick);
    lane.m_Gradient.PushBack({ToSeconds(iTick), color});
  }

  // Colour keys sit on the ramp as swatch diamonds - the same shape as every other key, so they
  // select, drag and delete alike, but filled with the colour they hold.
  plDynamicArray<plSequenceAssetDocument::ColorKey> colorKeys;
  GetDocument()->GetColorKeys(row.m_ObjectGuid, colorKeys);
  for (const auto& colorKey : colorKeys)
  {
    plEngineTrackKey& key = ref_content.m_Keys.ExpandAndGetRef();
    key.m_uiId = IdOf(colorKey.m_ColorCp);
    key.m_uiLane = uiLane;
    key.m_fTime = ToSeconds(colorKey.m_iTick);
    key.m_Color = colorKey.m_Value;
    key.m_bSwatch = true;

    KeyRef ref;
    ref.m_TrackGuid = row.m_ObjectGuid;
    ref.m_CpGuid = colorKey.m_ColorCp;
    ref.m_iTick = colorKey.m_iTick;
    m_Keys.Insert(key.m_uiId, ref);
  }
}

void plSequenceTrackView::AddCurveKeys(plUInt32 uiLane, const plSequenceModel::FlatRow& row, plEngineTrackContent& ref_content)
{
  ForEachKey(row, [&](const plUuid& cpGuid, plInt64 iTick, double)
    {
    plEngineTrackKey& key = ref_content.m_Keys.ExpandAndGetRef();
    key.m_uiId = IdOf(cpGuid);
    key.m_uiLane = uiLane;
    key.m_fTime = ToSeconds(iTick);

    KeyRef ref;
    ref.m_TrackGuid = row.m_ObjectGuid;
    ref.m_CpGuid = cpGuid;
    ref.m_iTick = iTick;
    ref.m_AxisTracks = row.m_AxisTracks;
    m_Keys.Insert(key.m_uiId, ref); });
}

plString plSequenceTrackView::GetThumbnail(double fTime)
{
  if (m_pThumbnails == nullptr)
    return {};

  const plSequenceThumbnailCache::Thumbnail* pThumb = m_pThumbnails->GetThumbnail(ToTicks(fTime));
  if (pThumb == nullptr)
    return {};

  // each image becomes a texture once
  plString* pResource = nullptr;
  if (m_ThumbnailTextures.TryGetValue(pThumb->m_uiSerial, pResource))
    return *pResource;

  plStringBuilder sName;
  sName.SetFormat("Sequencer/{}", pThumb->m_uiSerial);
  const plString sResource = plEditorUi::ImageTexture(sName, plSequenceThumbnailCache::ThumbnailWidth, plSequenceThumbnailCache::ThumbnailHeight, pThumb->m_Pixels.GetArrayPtr()).GetResourceID();
  m_ThumbnailTextures.Insert(pThumb->m_uiSerial, sResource);
  return sResource;
}

void plSequenceTrackView::ForEachKey(const plSequenceModel::FlatRow& row, plDelegate<void(const plUuid&, plInt64, double)> callback) const
{
  if (row.IsBinding() || m_pModel == nullptr)
    return;

  const plDocumentObjectManager* pManager = m_pModel->GetDocument()->GetObjectManager();

  // A vector group's row is one key per tick over all of its axes: a tick where any axis is keyed
  // shows a key, and it stands for the whole vector.
  if (row.m_Kind == plSequenceModel::RowKind::VectorGroup)
  {
    plMap<plInt64, plUuid> byTick;

    for (const plUuid& trackGuid : row.m_AxisTracks)
    {
      plSequenceModel::FlatRow axisRow;
      axisRow.m_ObjectGuid = trackGuid;
      ForEachKey(axisRow, [&](const plUuid& cpGuid, plInt64 iTick, double)
        {
        // first axis to key a tick owns the group's key identity, so selection is stable
        if (!byTick.Contains(iTick))
          byTick.Insert(iTick, cpGuid); });
    }

    for (auto it : byTick)
      callback(it.Value(), it.Key(), 0.0);

    return;
  }

  const plDocumentObject* pTrackObj = pManager->GetObject(row.m_ObjectGuid);
  if (pTrackObj == nullptr)
    return;

  // A colour track is driven by its gradient; its float curve is unused.
  if (pTrackObj->GetTypeAccessor().GetValue("Target").ConvertTo<plInt64>() == plPropertyAnimTarget::Color)
    return;

  const plVariant curveGuid = pTrackObj->GetTypeAccessor().GetValue("FloatCurve");
  const plDocumentObject* pCurveObj = curveGuid.IsA<plUuid>() ? pManager->GetObject(curveGuid.Get<plUuid>()) : nullptr;
  if (pCurveObj == nullptr)
    return;

  const plInt32 iCpCount = pCurveObj->GetTypeAccessor().GetCount("ControlPoints");
  for (plInt32 i = 0; i < iCpCount; ++i)
  {
    const plVariant cpGuid = pCurveObj->GetTypeAccessor().GetValue("ControlPoints", i);
    const plDocumentObject* pCpObj = cpGuid.IsA<plUuid>() ? pManager->GetObject(cpGuid.Get<plUuid>()) : nullptr;
    if (pCpObj == nullptr)
      continue;

    const plInt64 iTick = pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();
    const double fValue = pCpObj->GetTypeAccessor().GetValue("Value").ConvertTo<double>();
    callback(pCpObj->GetGuid(), iTick, fValue);
  }
}

void plSequenceTrackView::ForEachClip(const plSequenceModel::FlatRow& row, plDelegate<void(const plDocumentObject*)> callback) const
{
  if (!row.IsClipLane() || m_pModel == nullptr)
    return;

  const plDocumentObjectManager* pManager = GetDocument()->GetObjectManager();
  const plDocumentObject* pOwnerObj = pManager->GetObject(row.m_ObjectGuid);
  if (pOwnerObj == nullptr)
    return;

  const plInt32 iCount = pOwnerObj->GetTypeAccessor().GetCount("Clips");
  for (plInt32 c = 0; c < iCount; ++c)
  {
    const plVariant guid = pOwnerObj->GetTypeAccessor().GetValue("Clips", c);
    const plDocumentObject* pClipObj = guid.IsA<plUuid>() ? pManager->GetObject(guid.Get<plUuid>()) : nullptr;
    if (pClipObj == nullptr)
      continue;

    // Clips live on the binding but are drawn on the lane of their kind, so audio does not share a
    // row with animation.
    if (pClipObj->GetTypeAccessor().GetValue("Kind").ConvertTo<plInt64>() != (plInt64)row.m_ClipKind.GetValue())
      continue;

    callback(pClipObj);
  }
}

bool plSequenceTrackView::IsColorTrack(const plUuid& trackGuid) const
{
  const plSequenceTrack* pTrack = GetDocument() != nullptr ? GetDocument()->GetTrack(trackGuid) : nullptr;
  return pTrack != nullptr && pTrack->m_Target == plPropertyAnimTarget::Color;
}

//////////////////////////////////////////////////////////////////////////
// View events

void plSequenceTrackView::OnViewEvent(const plEngineTrackViewEvent& e)
{
  using Type = plEngineTrackViewEvent::Type;

  plSequenceAssetDocument* pDoc = GetDocument();
  if (pDoc == nullptr)
    return;

  switch (e.m_Type)
  {
    case Type::Scrub:
      if (m_OnScrubRequested)
        m_OnScrubRequested(static_cast<plUInt64>(plMath::Max<plInt64>(0, SnapTick(ToTicks(e.m_fTime)))));
      break;

    case Type::SelectionChanged:
      m_bHeadFocus = false;
      OnSelectionChanged();
      break;

    case Type::MoveKeys:
      MoveSelectedKeys(ToTicks(e.m_fDelta));
      break;

    case Type::ScaleKeys:
      ScaleSelectedKeys(ToTicks(e.m_fPivot), e.m_fFactor);
      break;

    case Type::EditClip:
      OnEditClip(e.m_uiId, e.m_fStart, e.m_fDuration);
      break;

    case Type::MoveMarker:
    {
      plUuid markerGuid;
      if (m_Markers.TryGetValue(e.m_uiId, markerGuid))
      {
        plCommandHistory* pHistory = pDoc->GetCommandHistory();
        pHistory->StartTransaction("Move Marker");

        plSetObjectPropertyCommand cmd;
        cmd.m_Object = markerGuid;
        cmd.m_sProperty = "Tick";
        cmd.m_NewValue = static_cast<plUInt64>(plMath::Max<plInt64>(0, ToTicks(e.m_fTime)));
        pHistory->AddCommand(cmd).AssertSuccess();

        pHistory->FinishTransaction();
      }
      break;
    }

    case Type::AddKey:
      OnAddKey(e.m_iLane, e.m_fTime);
      break;

    case Type::KeyActivated:
      OnKeyActivated(e.m_uiId);
      break;

    case Type::MarkerActivated:
    {
      plUuid markerGuid;
      if (m_Markers.TryGetValue(e.m_uiId, markerGuid))
        RenameMarker(markerGuid);
      break;
    }

    case Type::RulerActivated:
      // double-clicking the ruler names the moment under the cursor, which is how a marker is made
      AddMarkerAt(ToTicks(e.m_fTime));
      break;

    case Type::DeleteSelection:
    {
      ClipRef clip;
      plUuid markerGuid;
      if (m_Clips.TryGetValue(m_pView->GetSelectedClip(), clip))
        RemoveObject(clip.m_bCut ? "Delete Camera Cut" : "Remove Clip", clip.m_Guid);
      else if (m_Markers.TryGetValue(m_pView->GetSelectedMarker(), markerGuid))
        RemoveObject("Delete Marker", markerGuid);
      else
        DeleteSelectedKeys();

      m_pView->ClearSelection();
      break;
    }

    case Type::ContextMenu:
      OnContextMenu(e);
      break;

    case Type::KeyPress:
      OnKeyPress(*e.m_pKey);
      break;

    case Type::LaneClicked:
    case Type::LaneToggled:
    case Type::LaneButton:
    case Type::LaneContextMenu:
      OnLaneEvent(e);
      break;

    default:
      break;
  }
}

void plSequenceTrackView::OnLaneEvent(const plEngineTrackViewEvent& e)
{
  using Type = plEngineTrackViewEvent::Type;

  const plSequenceModel::FlatRow* pRow = GetRowOfLane(e.m_iLane);
  if (pRow == nullptr)
    return;

  if (e.m_Type == Type::LaneToggled)
  {
    if (!m_Collapsed.Remove(pRow->m_ObjectGuid))
      m_Collapsed.Insert(pRow->m_ObjectGuid);

    Refresh();
    return;
  }

  // A head click selects its row; Del then removes it, so the key selection steps aside.
  const plSequenceModel::FlatRow row = *pRow;
  m_bHeadFocus = true;
  m_pView->ClearSelection();
  SelectRow(row);

  if (e.m_Type == Type::LaneButton && m_OnRowAddRequested)
    m_OnRowAddRequested(e.m_vPosition);
  else if (e.m_Type == Type::LaneContextMenu && m_OnRowContextMenuRequested)
    m_OnRowContextMenuRequested(e.m_vPosition);
}

void plSequenceTrackView::OnSelectionChanged()
{
  plSequenceAssetDocument* pDoc = GetDocument();

  // selecting a cut or clip puts it in the property grid, where its camera, trigger, speed and blends are edited
  ClipRef clip;
  if (m_Clips.TryGetValue(m_pView->GetSelectedClip(), clip))
  {
    pDoc->GetSelectionManager()->SetSelection(pDoc->GetObjectManager()->GetObject(clip.m_Guid));
    return;
  }

  // the key last clicked selects its track's row, so the curve dock shows that curve, then reports the key
  const auto& keys = m_pView->GetSelectedKeys();
  KeyRef key;
  if (!keys.IsEmpty() && m_Keys.TryGetValue(keys.PeekBack(), key))
  {
    SelectRowFor(key.m_AxisTracks.IsEmpty() ? key.m_TrackGuid : key.m_AxisTracks[0]);

    if (m_OnKeySelected)
      m_OnKeySelected(key.m_TrackGuid, key.m_CpGuid);
  }
}

void plSequenceTrackView::OnEditClip(plUInt64 uiClip, double fStart, double fDuration)
{
  ClipRef clip;
  if (!m_Clips.TryGetValue(uiClip, clip))
    return;

  const plInt64 iStart = plMath::Max<plInt64>(0, ToTicks(fStart));
  const plInt64 iDuration = plMath::Max<plInt64>(1, ToTicks(fDuration));
  const bool bMove = iDuration == clip.m_iDuration;

  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  if (clip.m_bCut)
    pHistory->StartTransaction(bMove ? "Move Camera Cut" : "Trim Camera Cut");
  else
    pHistory->StartTransaction(bMove ? "Move Clip" : "Trim Clip");

  plSetObjectPropertyCommand cmd;
  cmd.m_Object = clip.m_Guid;

  cmd.m_sProperty = "StartTick";
  cmd.m_NewValue = static_cast<plUInt64>(iStart);
  pHistory->AddCommand(cmd).AssertSuccess();

  cmd.m_sProperty = "DurationTicks";
  cmd.m_NewValue = static_cast<plUInt64>(iDuration);
  pHistory->AddCommand(cmd).AssertSuccess();

  pHistory->FinishTransaction();
}

void plSequenceTrackView::OnAddKey(plInt32 iLane, double fTime)
{
  const plSequenceModel::FlatRow* pRow = GetRowOfLane(iLane);
  plSequenceAssetDocument* pDoc = GetDocument();
  if (pRow == nullptr)
    return;

  const plSequenceModel::FlatRow row = *pRow;
  const plInt64 iTick = SnapTick(ToTicks(fTime));

  // Events are named, and an unnamed one fires nothing, so the name is asked for up front.
  if (row.IsEventLane())
  {
    bool bOk = false;
    const plString sName = plEnginePrompt::GetText("Add Event", "Event name:", {}, &bOk);
    if (bOk && !sName.IsEmpty())
    {
      const plUInt64 uiKey = IdOf(pDoc->InsertEventTrackCpAt(iTick, sName));
      SelectKeyIds(plMakeArrayPtr(&uiKey, 1));
    }
    return;
  }

  if (row.IsBinding())
    return;

  // A clip lane drops a clip of its own kind and selects it, ready to be given a payload in the property grid.
  if (row.IsClipLane())
  {
    const plUuid clipGuid = pDoc->AddClip(row.m_ObjectGuid, row.m_ClipKind, static_cast<plUInt64>(iTick), "");
    if (clipGuid.IsValid())
    {
      m_pView->SetSelectedClip(IdOf(clipGuid));
      pDoc->GetSelectionManager()->SetSelection(pDoc->GetObjectManager()->GetObject(clipGuid));
    }
    return;
  }

  // Colour tracks key their gradient. The new key takes the colour the gradient already has at that tick, so
  // inserting one never changes what the track evaluates to, and then opens its picker.
  const plSequenceTrack* pTrack = pDoc->GetTrack(row.m_ObjectGuid);
  if (pTrack != nullptr && pTrack->m_Target == plPropertyAnimTarget::Color)
  {
    pDoc->SetColorKey(row.m_ObjectGuid, iTick, pTrack->m_ColorGradient.Evaluate(iTick));
    const plUInt64 uiKey = IdOf(pDoc->FindGradientColorCp(row.m_ObjectGuid, iTick));
    SelectKeyIds(plMakeArrayPtr(&uiKey, 1));
    EditColorKey(row.m_ObjectGuid, iTick);
    return;
  }

  // On a vector group, one double-click keys the whole vector - each axis at the value it already holds.
  if (row.m_Kind == plSequenceModel::RowKind::VectorGroup)
  {
    pDoc->InsertVectorKey(row.m_AxisTracks, iTick);
    const plUInt64 uiKey = IdOf(pDoc->FindCurveCp(row.m_AxisTracks[0], iTick));
    SelectKeyIds(plMakeArrayPtr(&uiKey, 1));
    return;
  }

  const plUuid cpGuid = pDoc->InsertCurveCpAt(row.m_ObjectGuid, iTick, 0.0);
  const plUInt64 uiKey = IdOf(cpGuid);
  SelectKeyIds(plMakeArrayPtr(&uiKey, 1));

  if (m_OnKeySelected)
    m_OnKeySelected(row.m_ObjectGuid, cpGuid);
}

void plSequenceTrackView::OnKeyActivated(plUInt64 uiKey)
{
  KeyRef key;
  if (!m_Keys.TryGetValue(uiKey, key))
    return;

  // a colour key opens its picker; an event renames
  if (IsColorTrack(key.m_TrackGuid))
    EditColorKey(key.m_TrackGuid, key.m_iTick);
  else if (GetDocument()->GetObjectManager()->GetObject(key.m_CpGuid) != nullptr && GetDocument()->GetObjectManager()->GetObject(key.m_CpGuid)->GetTypeAccessor().GetValue("Event").IsA<plString>())
    RenameEvent(key);
}

void plSequenceTrackView::OnContextMenu(const plEngineTrackViewEvent& e)
{
  using Target = plEngineTrackViewEvent::Target;
  using plEditorUi::Icon;
  using plEditorUi::IconPath;

  plSequenceAssetDocument* pDoc = GetDocument();
  plEditorUiMenu menu;

  // what is under the pointer decides the menu; the view selected it already
  if (e.m_Target == Target::Marker)
  {
    menu.AddAction("Rename Marker...", MenuId::RenameMarkerItem);
    menu.AddSeparator();
    auto& item = menu.AddAction("Delete Marker", MenuId::DeleteMarker, "Del");
    item.m_sIcon = IconPath(Icon::Clear);
    item.m_bDanger = true;
  }
  else if (e.m_Target == Target::Clip)
  {
    ClipRef clip;
    m_Clips.TryGetValue(e.m_uiId, clip);
    if (clip.m_bCut)
    {
      auto& item = menu.AddAction("Delete Camera Cut", MenuId::DeleteCut, "Del");
      item.m_sIcon = IconPath(Icon::Clear);
      item.m_bDanger = true;
    }
    else
    {
      // Fitting after a manual resize: the auto-fit only fires the first time a trigger is set.
      if (const plSequenceClip* pClip = pDoc->GetClip(clip.m_Guid); pClip != nullptr && pClip->m_Kind == plSequenceClipKind::AudioTrigger && !pClip->m_sPayload.IsEmpty())
      {
        menu.AddAction("Fit to Trigger Length", MenuId::FitToTrigger);
        menu.AddSeparator();
      }

      auto& item = menu.AddAction("Delete Clip", MenuId::DeleteClip, "Del");
      item.m_sIcon = IconPath(Icon::Clear);
      item.m_bDanger = true;
    }
  }
  else if (e.m_Target == Target::Key)
  {
    plDynamicArray<KeyRef> keys;
    GetSelectedKeyRefs(keys);

    const plSequenceModel::FlatRow* pRow = GetRowOfLane(e.m_iLane);
    if (pRow != nullptr && pRow->IsEventLane() && keys.GetCount() == 1)
      menu.AddAction("Rename Event...", MenuId::RenameEvent);

    // Ease presets, applied to both sides of every selected curve key; colour stops and events are skipped.
    auto& ease = menu.AddSubmenu("Ease");
    auto addEase = [&](const char* szLabel, plUInt32 uiId)
    {
      auto& item = ease.m_Children.ExpandAndGetRef();
      item.m_sLabel = szLabel;
      item.m_uiId = uiId;
    };
    addEase("Auto", MenuId::EaseAuto);
    addEase("Linear", MenuId::EaseLinear);
    ease.m_Children.ExpandAndGetRef().m_Type = plEditorUiMenuItem::Type::Separator;
    addEase("Ease In", MenuId::EaseIn);
    addEase("Ease Out", MenuId::EaseOut);
    addEase("Ease In/Out", MenuId::EaseInOut);

    menu.AddSeparator();
    plStringBuilder sDelete;
    if (keys.GetCount() > 1)
      sDelete.SetFormat("Delete {} Keys", keys.GetCount());
    else
      sDelete = "Delete Key";
    auto& item = menu.AddAction(sDelete, MenuId::DeleteKeys, "Del");
    item.m_sIcon = IconPath(Icon::Clear);
    item.m_bDanger = true;

    menu.AddSeparator();
    menu.AddAction("Copy", MenuId::CopyKeys, "Ctrl+C").m_sIcon = IconPath(Icon::Copy);
  }
  else
  {
    // empty lane space
    auto& paste = menu.AddAction("Paste Keys at Playhead", MenuId::PasteKeys, "Ctrl+V");
    paste.m_bEnabled = !m_Clipboard.IsEmpty();
    menu.AddAction("Select All Keys", MenuId::SelectAll, "Ctrl+A");
    menu.AddSeparator();
    menu.AddAction("Add Marker Here...", MenuId::AddMarker).m_sIcon = IconPath(Icon::Bookmark);
  }

  const plUInt64 uiTarget = e.m_uiId;
  const plInt64 iTick = SnapTick(ToTicks(e.m_fTime));
  m_pView->ShowContextMenu(e.m_vPosition, std::move(menu), [this, uiTarget, iTick](plUInt32 uiId)
    {
    ClipRef clip;
    plUuid markerGuid;
    plDynamicArray<KeyRef> keys;

    switch (uiId)
    {
      case MenuId::RenameEvent:
        GetSelectedKeyRefs(keys);
        if (keys.GetCount() == 1)
          RenameEvent(keys[0]);
        break;
      case MenuId::EaseAuto:
        SetSelectedKeyTangents(plCurveTangentMode::Auto, plCurveTangentMode::Auto);
        break;
      case MenuId::EaseLinear:
        SetSelectedKeyTangents(plCurveTangentMode::Linear, plCurveTangentMode::Linear);
        break;
      case MenuId::EaseIn:
        SetSelectedKeyTangents(plCurveTangentMode::Linear, plCurveTangentMode::Auto);
        break;
      case MenuId::EaseOut:
        SetSelectedKeyTangents(plCurveTangentMode::Auto, plCurveTangentMode::Linear);
        break;
      case MenuId::EaseInOut:
        SetSelectedKeyTangents(plCurveTangentMode::Bezier, plCurveTangentMode::Bezier);
        break;
      case MenuId::DeleteKeys:
        DeleteSelectedKeys();
        break;
      case MenuId::CopyKeys:
        CopySelectedKeys();
        break;
      case MenuId::FitToTrigger:
        if (m_Clips.TryGetValue(uiTarget, clip))
          FitClipToTrigger(clip.m_Guid);
        break;
      case MenuId::DeleteClip:
      case MenuId::DeleteCut:
        if (m_Clips.TryGetValue(uiTarget, clip))
          RemoveObject(clip.m_bCut ? "Delete Camera Cut" : "Delete Clip", clip.m_Guid);
        break;
      case MenuId::DeleteMarker:
        if (m_Markers.TryGetValue(uiTarget, markerGuid))
          RemoveObject("Delete Marker", markerGuid);
        break;
      case MenuId::RenameMarkerItem:
        if (m_Markers.TryGetValue(uiTarget, markerGuid))
          RenameMarker(markerGuid);
        break;
      case MenuId::PasteKeys:
        PasteKeysAtPlayhead();
        break;
      case MenuId::SelectAll:
        SelectAllKeys();
        break;
      case MenuId::AddMarker:
        AddMarkerAt(iTick);
        break;
    } });
}

void plSequenceTrackView::OnKeyPress(const plEditorUiKeyEvent& key)
{
  using Key = plEditorUiKeyEvent::Key;
  const bool bCtrl = key.m_uiModifiers == plEditorUiPointerEvent::Control;

  if (bCtrl && key.m_Key == Key::Character && (key.m_uiCharacter == 'c' || key.m_uiCharacter == 'C'))
    CopySelectedKeys();
  else if (bCtrl && key.m_Key == Key::Character && (key.m_uiCharacter == 'v' || key.m_uiCharacter == 'V'))
    PasteKeysAtPlayhead();
  else if (bCtrl && key.m_Key == Key::Character && (key.m_uiCharacter == 'a' || key.m_uiCharacter == 'A'))
    SelectAllKeys();
  else if ((key.m_Key == Key::Left || key.m_Key == Key::Right) && key.m_uiModifiers == 0 && !m_pView->GetSelectedKeys().IsEmpty())
  {
    // nudge the whole selection by a frame, which is how a beat gets fixed without a mouse
    const plInt64 iStep = plMath::Max<plInt64>(1, static_cast<plInt64>(GetDocument()->GetTicksPerFrame()));
    MoveSelectedKeys(key.m_Key == Key::Left ? -iStep : iStep);
  }
  else if (key.m_Key == Key::Delete && key.m_uiModifiers == 0 && m_bHeadFocus && m_OnRowDeleteRequested)
    m_OnRowDeleteRequested();
  else
    return;

  key.m_bHandled = true;
}

//////////////////////////////////////////////////////////////////////////
// Edits

void plSequenceTrackView::GetSelectedKeyRefs(plDynamicArray<KeyRef>& out_keys) const
{
  out_keys.Clear();
  for (plUInt64 uiKey : m_pView->GetSelectedKeys())
  {
    const KeyRef* pKey = nullptr;
    if (m_Keys.TryGetValue(uiKey, pKey))
      out_keys.PushBack(*pKey);
  }
}

void plSequenceTrackView::SelectKeyIds(plArrayPtr<const plUInt64> keys)
{
  Refresh();
  m_pView->SetSelectedKeys(keys);
}

void plSequenceTrackView::SelectAllKeys()
{
  plDynamicArray<plUInt64> keys;
  for (auto it = m_Keys.GetIterator(); it.IsValid(); ++it)
    keys.PushBack(it.Key());

  m_pView->SetSelectedKeys(keys);
}

void plSequenceTrackView::MoveKeyTo(KeyRef& ref_key, plInt64 iTo, plCommandHistory* pHistory)
{
  if (ref_key.m_iTick == iTo)
    return;

  plSequenceAssetDocument* pDoc = GetDocument();

  if (IsColorTrack(ref_key.m_TrackGuid))
  {
    pDoc->MoveColorKey(ref_key.m_TrackGuid, ref_key.m_iTick, iTo);
  }
  else if (!ref_key.m_AxisTracks.IsEmpty())
  {
    pDoc->MoveVectorKey(ref_key.m_AxisTracks, ref_key.m_iTick, iTo);
  }
  else
  {
    plSetObjectPropertyCommand cmd;
    cmd.m_Object = ref_key.m_CpGuid;
    cmd.m_sProperty = "Tick";
    cmd.m_NewValue = iTo;
    pHistory->AddCommand(cmd).AssertSuccess();
  }

  ref_key.m_iTick = iTo;
}

void plSequenceTrackView::MoveSelectedKeys(plInt64 iDeltaTicks)
{
  plDynamicArray<KeyRef> keys;
  GetSelectedKeyRefs(keys);
  if (iDeltaTicks == 0 || keys.IsEmpty())
    return;

  // the selection stays on the moved keys: colour and vector keys get new identities when they move
  const plDynamicArray<plUInt64> selection = m_pView->GetSelectedKeys();

  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Move Keys");

  for (KeyRef& key : keys)
    MoveKeyTo(key, plMath::Max<plInt64>(0, key.m_iTick + iDeltaTicks), pHistory);

  pHistory->FinishTransaction();

  Refresh();
  m_pView->SetSelectedKeys(selection);
}

void plSequenceTrackView::ScaleSelectedKeys(plInt64 iPivotTick, double fFactor)
{
  plDynamicArray<KeyRef> keys;
  GetSelectedKeyRefs(keys);
  if (keys.GetCount() < 2 || fFactor <= 0.0)
    return;

  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Retime Keys");

  // Moved outward-first so two keys never briefly land on the same tick: a colour or vector key is
  // found by tick, and a collision mid-batch would make the second one retarget the first.
  const bool bGrowing = fFactor > 1.0;
  keys.Sort([&](const KeyRef& a, const KeyRef& b) -> bool
    {
    const plInt64 iDistA = plMath::Abs(a.m_iTick - iPivotTick);
    const plInt64 iDistB = plMath::Abs(b.m_iTick - iPivotTick);
    return bGrowing ? (iDistA > iDistB) : (iDistA < iDistB); });

  for (KeyRef& key : keys)
  {
    const plInt64 iTo = plMath::Max<plInt64>(0, iPivotTick + static_cast<plInt64>(plMath::Round(static_cast<double>(key.m_iTick - iPivotTick) * fFactor)));
    MoveKeyTo(key, SnapTick(iTo), pHistory);
  }

  pHistory->FinishTransaction();
}

void plSequenceTrackView::SetSelectedKeyTangents(plInt64 iLeftMode, plInt64 iRightMode)
{
  plDynamicArray<KeyRef> keys;
  GetSelectedKeyRefs(keys);
  if (keys.IsEmpty())
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  plCommandHistory* pHistory = pDoc->GetCommandHistory();
  pHistory->StartTransaction("Set Key Tangents");

  for (const KeyRef& key : keys)
  {
    // Only curve control points have tangents - colour stops and events do not.
    const plDocumentObject* pCpObj = pDoc->GetObjectManager()->GetObject(key.m_CpGuid);
    if (pCpObj == nullptr || pCpObj->GetType() != plGetStaticRTTI<plCurveControlPointData>())
      continue;

    plSetObjectPropertyCommand cmd;
    cmd.m_Object = key.m_CpGuid;

    cmd.m_sProperty = "LeftTangentMode";
    cmd.m_NewValue = iLeftMode;
    pHistory->AddCommand(cmd).AssertSuccess();

    cmd.m_sProperty = "RightTangentMode";
    cmd.m_NewValue = iRightMode;
    pHistory->AddCommand(cmd).AssertSuccess();
  }

  pHistory->FinishTransaction();
}

void plSequenceTrackView::DeleteSelectedKeys()
{
  plDynamicArray<KeyRef> keys;
  GetSelectedKeyRefs(keys);
  if (keys.IsEmpty())
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  plCommandHistory* pHistory = pDoc->GetCommandHistory();
  pHistory->StartTransaction("Delete Keys");

  for (const KeyRef& key : keys)
  {
    if (IsColorTrack(key.m_TrackGuid))
    {
      pDoc->RemoveColorKey(key.m_TrackGuid, key.m_iTick);
    }
    else if (!key.m_AxisTracks.IsEmpty())
    {
      pDoc->RemoveVectorKey(key.m_AxisTracks, key.m_iTick);
    }
    else
    {
      plRemoveObjectCommand cmd;
      cmd.m_Object = key.m_CpGuid;
      pHistory->AddCommand(cmd).AssertSuccess();
    }
  }

  pHistory->FinishTransaction();
  m_pView->ClearSelection();
}

void plSequenceTrackView::CopySelectedKeys()
{
  plDynamicArray<KeyRef> keys;
  GetSelectedKeyRefs(keys);

  m_Clipboard.Clear();
  if (keys.IsEmpty())
    return;

  // Offsets are relative to the earliest key, so a paste keeps the shape of what was copied.
  plInt64 iEarliest = plMath::MaxValue<plInt64>();
  for (const KeyRef& key : keys)
    iEarliest = plMath::Min(iEarliest, key.m_iTick);

  for (const KeyRef& key : keys)
  {
    const plDocumentObject* pCpObj = GetDocument()->GetObjectManager()->GetObject(key.m_CpGuid);
    if (pCpObj == nullptr)
      continue;

    CopiedKey& copied = m_Clipboard.ExpandAndGetRef();
    copied.m_TrackGuid = key.m_TrackGuid;
    copied.m_iOffsetTicks = key.m_iTick - iEarliest;

    const plVariant event = pCpObj->GetTypeAccessor().GetValue("Event");
    if (event.IsA<plString>())
    {
      copied.m_bIsEvent = true;
      copied.m_sEvent = event.Get<plString>();
    }
    else
    {
      copied.m_fValue = pCpObj->GetTypeAccessor().GetValue("Value").ConvertTo<double>();
    }
  }
}

void plSequenceTrackView::PasteKeysAtPlayhead()
{
  if (m_Clipboard.IsEmpty())
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  const plInt64 iBase = SnapTick(static_cast<plInt64>(pDoc->GetScrubberPosition()));

  plCommandHistory* pHistory = pDoc->GetCommandHistory();
  pHistory->StartTransaction("Paste Keys");

  plDynamicArray<plUInt64> pasted;
  for (const CopiedKey& copied : m_Clipboard)
  {
    // The track may have been deleted between copy and paste; that key is dropped.
    if (pDoc->GetObjectManager()->GetObject(copied.m_TrackGuid) == nullptr)
      continue;

    const plInt64 iTick = iBase + copied.m_iOffsetTicks;
    const plUuid cpGuid = copied.m_bIsEvent ? pDoc->InsertEventTrackCpAt(iTick, copied.m_sEvent) : pDoc->InsertCurveCpAt(copied.m_TrackGuid, iTick, copied.m_fValue);
    if (cpGuid.IsValid())
      pasted.PushBack(IdOf(cpGuid));
  }

  pHistory->FinishTransaction();
  SelectKeyIds(pasted);
}

void plSequenceTrackView::RemoveObject(const char* szUndoName, const plUuid& guid)
{
  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  pHistory->StartTransaction(szUndoName);

  plRemoveObjectCommand cmd;
  cmd.m_Object = guid;
  if (pHistory->AddCommand(cmd).Failed())
  {
    pHistory->CancelTransaction();
    return;
  }

  pHistory->FinishTransaction();

  // a removed marker is not part of the model, so nothing else redraws the ruler
  Refresh();
}

void plSequenceTrackView::AddMarkerAt(plInt64 iTick)
{
  bool bOk = false;
  const plString sName = plEnginePrompt::GetText("Add Marker", "Marker name:", {}, &bOk);
  if (bOk && !sName.IsEmpty())
    GetDocument()->AddMarker(iTick, sName);
}

void plSequenceTrackView::RenameMarker(const plUuid& markerGuid)
{
  const plDocumentObject* pMarkerObj = GetDocument()->GetObjectManager()->GetObject(markerGuid);
  if (pMarkerObj == nullptr)
    return;

  const plString sCurrent = pMarkerObj->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();

  bool bOk = false;
  const plString sName = plEnginePrompt::GetText("Rename Marker", "Marker name:", sCurrent, &bOk);
  if (!bOk || sName.IsEmpty())
    return;

  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Rename Marker");

  plSetObjectPropertyCommand cmd;
  cmd.m_Object = markerGuid;
  cmd.m_sProperty = "Name";
  cmd.m_NewValue = sName;
  pHistory->AddCommand(cmd).AssertSuccess();

  pHistory->FinishTransaction();
}

void plSequenceTrackView::RenameEvent(const KeyRef& key)
{
  const plDocumentObject* pCpObj = GetDocument()->GetObjectManager()->GetObject(key.m_CpGuid);
  if (pCpObj == nullptr)
    return;

  const plString sCurrent = pCpObj->GetTypeAccessor().GetValue("Event").ConvertTo<plString>();

  bool bOk = false;
  const plString sName = plEnginePrompt::GetText("Rename Event", "Event name:", sCurrent, &bOk);
  if (!bOk || sName.IsEmpty())
    return;

  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Rename Event");

  plSetObjectPropertyCommand cmd;
  cmd.m_Object = key.m_CpGuid;
  cmd.m_sProperty = "Event";
  cmd.m_NewValue = sName;
  pHistory->AddCommand(cmd).AssertSuccess();

  pHistory->FinishTransaction();
}

void plSequenceTrackView::FitClipToTrigger(const plUuid& clipGuid)
{
  plSequenceAssetDocument* pDoc = GetDocument();
  const plSequenceClip* pClip = pDoc != nullptr ? pDoc->GetClip(clipGuid) : nullptr;
  if (pClip == nullptr || pDoc->GetObjectManager()->GetObject(clipGuid) == nullptr)
    return;

  const auto* pPeaks = m_Waveforms.GetPeaks(pClip->m_sPayload);
  if (pPeaks == nullptr || pPeaks->m_fDurationSeconds <= 0.0)
  {
    plEditorUiServices::ShowAllDocumentsTemporaryStatusBarMessage(plFmt("That trigger does not resolve to a decodable sample."), plTime::MakeFromSeconds(4));
    return;
  }

  plCommandHistory* pHistory = pDoc->GetCommandHistory();
  pHistory->StartTransaction("Fit Clip to Trigger");

  plSetObjectPropertyCommand cmd;
  cmd.m_Object = clipGuid;
  cmd.m_sProperty = "DurationTicks";
  cmd.m_NewValue = static_cast<plUInt64>(plMath::Max(1.0, pPeaks->m_fDurationSeconds * static_cast<double>(plSequenceTicksPerSecond)));
  pHistory->AddCommand(cmd).AssertSuccess();

  pHistory->FinishTransaction();
}

void plSequenceTrackView::EditColorKey(const plUuid& trackGuid, plInt64 iTick)
{
  const plSequenceTrack* pTrack = GetDocument()->GetTrack(trackGuid);
  if (pTrack == nullptr)
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  plColor color = pTrack->m_ColorGradient.Evaluate(iTick);

  // Temporary commands so live picking previews on the lane and in the viewport, and cancelling
  // leaves the undo stack exactly as it was.
  pDoc->GetCommandHistory()->BeginTemporaryCommands("Edit Colour Key");

  const bool bAccepted = plEngineColorDialog::PickColor(color, true, true, "Colour Key", [pDoc, trackGuid, iTick](const plColor& current)
    { pDoc->SetColorKey(trackGuid, iTick, current); });

  if (bAccepted)
  {
    pDoc->SetColorKey(trackGuid, iTick, color);
    pDoc->GetCommandHistory()->FinishTemporaryCommands();
  }
  else
  {
    pDoc->GetCommandHistory()->CancelTemporaryCommands();
  }
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceTrackView);
