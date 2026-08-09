#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceTrackView.moc.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <ToolsFoundation/Command/TreeCommands.h>

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QMenu>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

plQtSequenceTrackView::plQtSequenceTrackView(QWidget* pParent)
  : QWidget(pParent)
{
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setMinimumHeight(RulerHeight + RowHeight);
}

plQtSequenceTrackView::~plQtSequenceTrackView()
{
  if (m_pModel != nullptr)
  {
    m_pModel->GetDocument()->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(
      plMakeDelegate(&plQtSequenceTrackView::DocumentPropertyEventHandler, this));
  }
}

void plQtSequenceTrackView::SetModel(plQtSequenceModel* pModel)
{
  if (m_pModel != nullptr)
  {
    disconnect(m_pModel, &plQtSequenceModel::ModelRebuilt, this, nullptr);
    m_pModel->GetDocument()->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(
      plMakeDelegate(&plQtSequenceTrackView::DocumentPropertyEventHandler, this));
  }

  m_pModel = pModel;

  if (m_pModel != nullptr)
  {
    // Scrub/duration repaints are driven by the owning panel, which has a removable
    // subscription to the document's sequence events.
    connect(m_pModel, &plQtSequenceModel::ModelRebuilt, this, [this]() {
      m_SelectedKey.Invalidate();

      // A rebuild means bindings or cuts changed - a cut may now point at a different camera, so
      // its cached image no longer describes it.
      if (m_pThumbnails != nullptr)
        m_pThumbnails->InvalidateAll();

      update(); });
    connect(m_pModel, &plQtSequenceModel::KeysChanged, this, [this]() { update(); });

    if (m_pThumbnails == nullptr)
    {
      m_pThumbnails = new plQtSequenceThumbnailCache(m_pModel->GetDocument(), this);
      connect(m_pThumbnails, &plQtSequenceThumbnailCache::ThumbnailReady, this, [this]() { update(); });
    }

    m_pModel->GetDocument()->GetObjectManager()->m_PropertyEvents.AddEventHandler(
      plMakeDelegate(&plQtSequenceTrackView::DocumentPropertyEventHandler, this));
  }

  update();
}

void plQtSequenceTrackView::DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  if (e.m_EventType != plDocumentObjectPropertyEvent::Type::PropertySet || e.m_pObject == nullptr)
    return;

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

  const plUInt64 uiTicks = (plUInt64)plMath::Max(1.0, pPeaks->m_fDurationSeconds * (double)plSequenceTicksPerSecond);
  const plUuid clipGuid = e.m_pObject->GetGuid();

  // Queued: this fires from inside the transaction that set the trigger, and starting another one
  // there would nest the resize into the edit that caused it.
  QMetaObject::invokeMethod(
    this,
    [this, clipGuid, uiTicks]()
    {
      plSequenceAssetDocument* pDoc = GetDocument();
      if (pDoc == nullptr || pDoc->GetObjectManager()->GetObject(clipGuid) == nullptr)
        return;

      plCommandHistory* pHistory = pDoc->GetCommandHistory();
      pHistory->StartTransaction("Fit Clip to Trigger");

      plSetObjectPropertyCommand cmd;
      cmd.m_Object = clipGuid;
      cmd.m_sProperty = "DurationTicks";
      cmd.m_NewValue = uiTicks;
      pHistory->AddCommand(cmd).AssertSuccess();

      pHistory->FinishTransaction();
      update();
    },
    Qt::QueuedConnection);
}

void plQtSequenceTrackView::SetVerticalOffset(int iOffset)
{
  if (m_iVerticalOffset == iOffset)
    return;

  m_iVerticalOffset = iOffset;
  update();
}

void plQtSequenceTrackView::SetHighlightedTrack(const plUuid& trackGuid)
{
  if (m_HighlightedTrack == trackGuid)
    return;

  m_HighlightedTrack = trackGuid;
  update();
}

double plQtSequenceTrackView::PixelsPerTick() const
{
  const plSequenceAssetDocument* pDoc = GetDocument();
  if (pDoc == nullptr)
    return 1.0;

  const double fDuration = (double)plMath::Max<plUInt64>(1, pDoc->GetDurationTicks());
  return plMath::Max(0.000001, (double)(width() - 1) / fDuration);
}

plInt64 plQtSequenceTrackView::TickAtPixel(int x) const
{
  return plMath::Max<plInt64>(0, (plInt64)((double)x / PixelsPerTick()));
}

int plQtSequenceTrackView::PixelAtTick(plInt64 iTick) const
{
  return (int)((double)iTick * PixelsPerTick());
}

plInt64 plQtSequenceTrackView::SnapTick(plInt64 iTick) const
{
  const plSequenceAssetDocument* pDoc = GetDocument();
  if (pDoc == nullptr)
    return iTick;

  const plInt64 iMax = (plInt64)pDoc->GetDurationTicks();

  // Markers win over the frame grid: a marker exists precisely so things can be pinned to it, and
  // landing one frame off would defeat the point. Only within a few pixels, so it never fights an
  // ordinary drag elsewhere on the timeline.
  {
    plDynamicArray<plInt64> markers;
    pDoc->GetMarkerTicks(markers);

    const int x = PixelAtTick(iTick);
    constexpr int iMagnet = 6;

    plInt64 iBest = -1;
    int iBestDist = iMagnet + 1;

    for (plInt64 iMarker : markers)
    {
      const int iDist = plMath::Abs(PixelAtTick(iMarker) - x);
      if (iDist < iBestDist)
      {
        iBestDist = iDist;
        iBest = iMarker;
      }
    }

    if (iBest >= 0)
      return plMath::Clamp<plInt64>(iBest, 0, iMax);
  }

  if (m_SnapMode == SnapMode::Free)
    return plMath::Clamp<plInt64>(iTick, 0, iMax);

  plInt64 iStep = (plInt64)pDoc->GetTicksPerFrame();
  if (m_SnapMode == SnapMode::HalfFrame)
    iStep = plMath::Max<plInt64>(1, iStep / 2);

  const plInt64 iSnapped = ((iTick + iStep / 2) / iStep) * iStep;
  return plMath::Clamp<plInt64>(iSnapped, 0, iMax);
}

int plQtSequenceTrackView::RowHeightAt(plUInt32 uiRow) const
{
  if (m_pModel == nullptr || uiRow >= m_pModel->GetFlatRows().GetCount())
    return RowHeight;

  const auto& row = m_pModel->GetFlatRows()[uiRow];
  if (row.IsCameraCutLane())
    return CameraLaneHeight;
  if (row.IsClipLane())
    return ClipLaneHeight;

  return RowHeight;
}

int plQtSequenceTrackView::RowTop(plUInt32 uiRow) const
{
  int y = RulerHeight - m_iVerticalOffset;

  for (plUInt32 r = 0; r < uiRow; ++r)
  {
    y += RowHeightAt(r);
  }

  return y;
}

int plQtSequenceTrackView::RowAtPixel(int y) const
{
  if (m_pModel == nullptr || y < RulerHeight)
    return -1;

  const auto& rows = m_pModel->GetFlatRows();
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    const int iTop = RowTop(r);
    if (y >= iTop && y < iTop + RowHeightAt(r))
      return (int)r;
  }

  return -1;
}

void plQtSequenceTrackView::ForEachKey(const plQtSequenceModel::FlatRow& row, plDelegate<void(const plUuid&, plInt64, double)> callback) const
{
  if (row.IsBinding() || m_pModel == nullptr)
    return;

  const plDocumentObjectManager* pManager = m_pModel->GetDocument()->GetObjectManager();

  // A vector group's row is one key per tick over all of its axes: a tick where any axis is keyed
  // shows a key, and it stands for the whole vector.
  if (row.m_Kind == plQtSequenceModel::RowKind::VectorGroup)
  {
    plMap<plInt64, plUuid> byTick;

    for (const plUuid& trackGuid : row.m_AxisTracks)
    {
      plQtSequenceModel::FlatRow axisRow;
      axisRow.m_ObjectGuid = trackGuid;
      ForEachKey(axisRow, [&](const plUuid& cpGuid, plInt64 iTick, double fValue) {
        PL_IGNORE_UNUSED(fValue);
        // first axis to key a tick owns the group's key identity, so selection is stable
        if (!byTick.Contains(iTick))
          byTick.Insert(iTick, cpGuid); });
    }

    for (auto it : byTick)
    {
      callback(it.Value(), it.Key(), 0.0);
    }

    return;
  }

  const plDocumentObject* pTrackObj = pManager->GetObject(row.m_ObjectGuid);
  if (pTrackObj == nullptr)
    return;

  // A colour track is driven by its gradient; its float curve is unused. Yielding curve CPs here
  // would paint and pick keys that drive nothing - the gradient lane shows the real stops instead.
  if (pTrackObj->GetTypeAccessor().GetValue("Target").ConvertTo<plInt64>() == plPropertyAnimTarget::Color)
    return;

  const plVariant curveGuid = pTrackObj->GetTypeAccessor().GetValue("FloatCurve");
  if (!curveGuid.IsA<plUuid>())
    return;

  const plDocumentObject* pCurveObj = pManager->GetObject(curveGuid.Get<plUuid>());
  if (pCurveObj == nullptr)
    return;

  const plInt32 iCpCount = pCurveObj->GetTypeAccessor().GetCount("ControlPoints");
  for (plInt32 i = 0; i < iCpCount; ++i)
  {
    const plVariant cpGuid = pCurveObj->GetTypeAccessor().GetValue("ControlPoints", i);
    if (!cpGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pCpObj = pManager->GetObject(cpGuid.Get<plUuid>());
    if (pCpObj == nullptr)
      continue;

    const plInt64 iTick = pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();
    const double fValue = pCpObj->GetTypeAccessor().GetValue("Value").ConvertTo<double>();
    callback(pCpObj->GetGuid(), iTick, fValue);
  }
}

void plQtSequenceTrackView::ForEachClip(const plQtSequenceModel::FlatRow& row, plDelegate<void(const plDocumentObject*)> callback) const
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
    if (!guid.IsA<plUuid>())
      continue;

    const plDocumentObject* pClipObj = pManager->GetObject(guid.Get<plUuid>());
    if (pClipObj == nullptr)
      continue;

    // Clips live on the binding but are drawn on the lane of their kind, so audio does not share a
    // row with animation.
    if (pClipObj->GetTypeAccessor().GetValue("Kind").ConvertTo<plInt64>() != (plInt64)row.m_ClipKind.GetValue())
      continue;

    callback(pClipObj);
  }
}

void plQtSequenceTrackView::CollectCameraCuts(plHybridArray<CutRef, 8>& out_cuts) const
{
  out_cuts.Clear();

  const plSequenceAssetDocument* pDoc = GetDocument();
  if (pDoc == nullptr)
    return;

  const plDocumentObject* pPropObj = const_cast<plSequenceAssetDocument*>(pDoc)->GetPropertyObject();
  const plInt32 iCutCount = pPropObj->GetTypeAccessor().GetCount("CameraCuts");

  for (plInt32 c = 0; c < iCutCount; ++c)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("CameraCuts", c);
    if (!guid.IsA<plUuid>())
      continue;

    const plDocumentObject* pCutObj = pDoc->GetObjectManager()->GetObject(guid.Get<plUuid>());
    if (pCutObj == nullptr)
      continue;

    CutRef& cut = out_cuts.ExpandAndGetRef();
    cut.m_CutGuid = pCutObj->GetGuid();
    cut.m_iStart = pCutObj->GetTypeAccessor().GetValue("StartTick").ConvertTo<plInt64>();
    cut.m_iDuration = plMath::Max<plInt64>(1, pCutObj->GetTypeAccessor().GetValue("DurationTicks").ConvertTo<plInt64>());
    cut.m_sName = QString::fromUtf8(pCutObj->GetTypeAccessor().GetValue("CameraName").ConvertTo<plString>().GetData());
    cut.m_bUnresolved = !pDoc->CanResolveCameraCut(pCutObj->GetGuid());
  }

  out_cuts.Sort([](const CutRef& lhs, const CutRef& rhs) -> bool { return lhs.m_iStart < rhs.m_iStart; });
}

plQtSequenceTrackView::CutRef plQtSequenceTrackView::PickCut(const QPoint& pos, ClipDrag& out_mode) const
{
  CutRef result;
  out_mode = ClipDrag::None;

  const int iRow = RowAtPixel(pos.y());
  if (iRow < 0 || m_pModel == nullptr || !m_pModel->GetFlatRows()[iRow].IsCameraCutLane())
    return result;

  plHybridArray<CutRef, 8> cuts;
  CollectCameraCuts(cuts);

  constexpr int iEdgeGrab = 6;

  // Reverse order so the cut drawn on top - the later one, which overlaps its predecessor's tail -
  // is the one grabbed in an overlap region.
  for (plUInt32 i = cuts.GetCount(); i-- > 0;)
  {
    const int xStart = PixelAtTick(cuts[i].m_iStart);
    const int xEnd = PixelAtTick(cuts[i].m_iStart + cuts[i].m_iDuration);

    if (pos.x() < xStart - iEdgeGrab || pos.x() > xEnd + iEdgeGrab)
      continue;

    result = cuts[i];

    const bool bWideEnoughToTrim = (xEnd - xStart) > iEdgeGrab * 3;
    if (bWideEnoughToTrim && pos.x() >= xEnd - iEdgeGrab)
      out_mode = ClipDrag::TrimEnd; // dragging the tail past the next cut is how a blend is made
    else
      out_mode = ClipDrag::Move;

    return result;
  }

  return result;
}

plQtSequenceTrackView::ClipRef plQtSequenceTrackView::PickClip(const QPoint& pos, ClipDrag& out_mode) const
{
  ClipRef result;
  out_mode = ClipDrag::None;

  const int iRow = RowAtPixel(pos.y());
  if (iRow < 0 || m_pModel == nullptr)
    return result;

  const auto& row = m_pModel->GetFlatRows()[iRow];
  if (!row.IsClipLane())
    return result;

  // Edge zones trim, the body moves. Kept narrow so a short clip is still grabbable in the middle.
  constexpr int iEdgeGrab = 5;

  ForEachClip(row, [&](const plDocumentObject* pClipObj) {
    if (result.IsValid())
      return;

    const plInt64 iStart = pClipObj->GetTypeAccessor().GetValue("StartTick").ConvertTo<plInt64>();
    const plInt64 iDuration = plMath::Max<plInt64>(1, pClipObj->GetTypeAccessor().GetValue("DurationTicks").ConvertTo<plInt64>());

    const int xStart = PixelAtTick(iStart);
    const int xEnd = PixelAtTick(iStart + iDuration);

    if (pos.x() < xStart - iEdgeGrab || pos.x() > xEnd + iEdgeGrab)
      return;

    result.m_OwnerGuid = row.m_ObjectGuid;
    result.m_ClipGuid = pClipObj->GetGuid();
    result.m_iStart = iStart;
    result.m_iDuration = iDuration;

    const bool bWideEnoughToTrim = (xEnd - xStart) > iEdgeGrab * 3;

    if (bWideEnoughToTrim && pos.x() <= xStart + iEdgeGrab)
      out_mode = ClipDrag::TrimStart;
    else if (bWideEnoughToTrim && pos.x() >= xEnd - iEdgeGrab)
      out_mode = ClipDrag::TrimEnd;
    else
      out_mode = ClipDrag::Move; });

  return result;
}

bool plQtSequenceTrackView::IsKeySelected(const plUuid& cpGuid) const
{
  for (const KeyRef& key : m_SelectedKeys)
  {
    if (key.m_CpGuid == cpGuid)
      return true;
  }

  return false;
}

void plQtSequenceTrackView::SelectKey(const KeyRef& key, bool bToggle)
{
  if (bToggle)
  {
    for (plUInt32 i = 0; i < m_SelectedKeys.GetCount(); ++i)
    {
      if (m_SelectedKeys[i].m_CpGuid == key.m_CpGuid)
      {
        m_SelectedKeys.RemoveAtAndCopy(i);
        // the primary has to stay inside the set, or the curve editor would mirror a key that is
        // no longer selected
        m_SelectedKey = m_SelectedKeys.IsEmpty() ? KeyRef() : m_SelectedKeys.PeekBack();
        return;
      }
    }

    m_SelectedKeys.PushBack(key);
  }
  else if (!IsKeySelected(key.m_CpGuid))
  {
    // clicking outside the current selection replaces it; clicking inside keeps it, so a
    // multi-selection can be dragged as one
    m_SelectedKeys.Clear();
    m_SelectedKeys.PushBack(key);
  }

  m_SelectedKey = key;
}

void plQtSequenceTrackView::SelectKeysInRect(const QRect& rect)
{
  m_SelectedKeys.Clear();
  m_SelectedKey.Invalidate();

  if (m_pModel == nullptr)
    return;

  const auto& rows = m_pModel->GetFlatRows();

  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    const int yTop = RowTop(r);
    const int yBottom = yTop + RowHeightAt(r);

    if (yBottom < rect.top() || yTop > rect.bottom())
      continue;

    ForEachKey(rows[r], [&](const plUuid& cpGuid, plInt64 iTick, double fValue) {
      PL_IGNORE_UNUSED(fValue);

      const int x = PixelAtTick(iTick);
      if (x < rect.left() || x > rect.right())
        return;

      KeyRef key;
      key.m_TrackGuid = rows[r].m_ObjectGuid;
      key.m_CpGuid = cpGuid;
      key.m_iTick = iTick;
      key.m_AxisTracks = rows[r].m_AxisTracks;
      m_SelectedKeys.PushBack(key); });

    // colour tracks key their gradient, which ForEachKey deliberately does not yield
    if (IsColorTrack(rows[r].m_ObjectGuid))
    {
      plDynamicArray<plSequenceAssetDocument::ColorKey> colorKeys;
      GetDocument()->GetColorKeys(rows[r].m_ObjectGuid, colorKeys);

      for (const auto& colorKey : colorKeys)
      {
        const int x = PixelAtTick(colorKey.m_iTick);
        if (x < rect.left() || x > rect.right())
          continue;

        KeyRef key;
        key.m_TrackGuid = rows[r].m_ObjectGuid;
        key.m_CpGuid = colorKey.m_ColorCp;
        key.m_iTick = colorKey.m_iTick;
        m_SelectedKeys.PushBack(key);
      }
    }
  }

  if (!m_SelectedKeys.IsEmpty())
    m_SelectedKey = m_SelectedKeys.PeekBack();
}

void plQtSequenceTrackView::MoveKeyTo(KeyRef& ref_key, plInt64 iTo, plCommandHistory* pHistory)
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

void plQtSequenceTrackView::MoveSelectedKeys(plInt64 iDeltaTicks)
{
  if (iDeltaTicks == 0 || m_SelectedKeys.IsEmpty())
    return;

  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Move Keys");

  for (KeyRef& key : m_SelectedKeys)
  {
    MoveKeyTo(key, plMath::Max<plInt64>(0, key.m_iTick + iDeltaTicks), pHistory);
  }

  pHistory->FinishTransaction();

  if (m_SelectedKey.IsValid())
    m_SelectedKey.m_iTick += iDeltaTicks;
}

void plQtSequenceTrackView::ScaleSelectedKeys(plInt64 iPivotTick, double fFactor)
{
  if (m_SelectedKeys.GetCount() < 2 || fFactor <= 0.0)
    return;

  plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
  pHistory->StartTransaction("Retime Keys");

  // Moved outward-first so two keys never briefly land on the same tick: a colour or vector key is
  // found by tick, and a collision mid-batch would make the second one retarget the first.
  plHybridArray<plUInt32, 16> order;
  for (plUInt32 i = 0; i < m_SelectedKeys.GetCount(); ++i)
    order.PushBack(i);

  const bool bGrowing = fFactor > 1.0;
  order.Sort([&](plUInt32 a, plUInt32 b) -> bool {
    const plInt64 iDistA = plMath::Abs(m_SelectedKeys[a].m_iTick - iPivotTick);
    const plInt64 iDistB = plMath::Abs(m_SelectedKeys[b].m_iTick - iPivotTick);
    return bGrowing ? (iDistA > iDistB) : (iDistA < iDistB); });

  for (plUInt32 i : order)
  {
    KeyRef& key = m_SelectedKeys[i];
    const plInt64 iTo = plMath::Max<plInt64>(0, iPivotTick + (plInt64)plMath::Round((double)(key.m_iTick - iPivotTick) * fFactor));
    MoveKeyTo(key, SnapTick(iTo), pHistory);
  }

  pHistory->FinishTransaction();
}

void plQtSequenceTrackView::SetSelectedKeyTangents(plInt64 iLeftMode, plInt64 iRightMode)
{
  if (m_SelectedKeys.IsEmpty())
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  plCommandHistory* pHistory = pDoc->GetCommandHistory();
  pHistory->StartTransaction("Set Key Tangents");

  for (const KeyRef& key : m_SelectedKeys)
  {
    // Only curve control points have tangents - colour stops and events do not, and asking them to
    // would fail the property set rather than doing nothing.
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

void plQtSequenceTrackView::DeleteSelectedKeys()
{
  if (m_SelectedKeys.IsEmpty())
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  plCommandHistory* pHistory = pDoc->GetCommandHistory();
  pHistory->StartTransaction("Delete Keys");

  for (const KeyRef& key : m_SelectedKeys)
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

  m_SelectedKeys.Clear();
  m_SelectedKey.Invalidate();
}

void plQtSequenceTrackView::CopySelectedKeys()
{
  m_Clipboard.Clear();

  if (m_SelectedKeys.IsEmpty())
    return;

  // Offsets are relative to the earliest key, so a paste keeps the shape of what was copied.
  plInt64 iEarliest = plMath::MaxValue<plInt64>();
  for (const KeyRef& key : m_SelectedKeys)
    iEarliest = plMath::Min(iEarliest, key.m_iTick);

  for (const KeyRef& key : m_SelectedKeys)
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

void plQtSequenceTrackView::PasteKeysAtPlayhead()
{
  if (m_Clipboard.IsEmpty())
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  const plInt64 iBase = SnapTick((plInt64)pDoc->GetScrubberPosition());

  plCommandHistory* pHistory = pDoc->GetCommandHistory();
  pHistory->StartTransaction("Paste Keys");

  m_SelectedKeys.Clear();

  for (const CopiedKey& copied : m_Clipboard)
  {
    // The track may have been deleted between copy and paste, in which case that key is dropped
    // rather than the whole paste failing.
    if (pDoc->GetObjectManager()->GetObject(copied.m_TrackGuid) == nullptr)
      continue;

    const plInt64 iTick = iBase + copied.m_iOffsetTicks;
    plUuid cpGuid;

    if (copied.m_bIsEvent)
      cpGuid = pDoc->InsertEventTrackCpAt(iTick, copied.m_sEvent);
    else
      cpGuid = pDoc->InsertCurveCpAt(copied.m_TrackGuid, iTick, copied.m_fValue);

    if (cpGuid.IsValid())
    {
      KeyRef& key = m_SelectedKeys.ExpandAndGetRef();
      key.m_TrackGuid = copied.m_TrackGuid;
      key.m_CpGuid = cpGuid;
      key.m_iTick = iTick;
    }
  }

  pHistory->FinishTransaction();

  if (!m_SelectedKeys.IsEmpty())
    m_SelectedKey = m_SelectedKeys.PeekBack();
}

plUuid plQtSequenceTrackView::PickMarker(const QPoint& pos) const
{
  if (pos.y() > RulerHeight || GetDocument() == nullptr)
    return plUuid();

  const plDocumentObject* pPropObj = GetDocument()->GetPropertyObject();
  const plInt32 iCount = pPropObj->GetTypeAccessor().GetCount("Markers");

  for (plInt32 i = 0; i < iCount; ++i)
  {
    const plVariant guid = pPropObj->GetTypeAccessor().GetValue("Markers", i);
    if (!guid.IsA<plUuid>())
      continue;

    const plDocumentObject* pMarkerObj = GetDocument()->GetObjectManager()->GetObject(guid.Get<plUuid>());
    if (pMarkerObj == nullptr)
      continue;

    // the flag hangs to the right of its tick, which is where the grab zone has to be
    const int x = PixelAtTick(pMarkerObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>());
    if (pos.x() >= x - 3 && pos.x() <= x + 10)
      return pMarkerObj->GetGuid();
  }

  return plUuid();
}

bool plQtSequenceTrackView::GetSelectionRange(plInt64& out_iMin, plInt64& out_iMax) const
{
  if (m_SelectedKeys.GetCount() < 2)
    return false;

  out_iMin = plMath::MaxValue<plInt64>();
  out_iMax = plMath::MinValue<plInt64>();

  for (const KeyRef& key : m_SelectedKeys)
  {
    out_iMin = plMath::Min(out_iMin, key.m_iTick);
    out_iMax = plMath::Max(out_iMax, key.m_iTick);
  }

  // All keys on one tick has no range to scale about.
  return out_iMax > out_iMin;
}

plQtSequenceTrackView::RetimeDrag plQtSequenceTrackView::PickRetimeHandle(const QPoint& pos) const
{
  plInt64 iMin = 0, iMax = 0;
  if (!GetSelectionRange(iMin, iMax))
    return RetimeDrag::None;

  if (pos.y() < RulerHeight || pos.y() > RulerHeight + RetimeBandHeight)
    return RetimeDrag::None;

  constexpr int iGrab = 6;

  if (plMath::Abs(PixelAtTick(iMin) - pos.x()) <= iGrab)
    return RetimeDrag::Start;

  if (plMath::Abs(PixelAtTick(iMax) - pos.x()) <= iGrab)
    return RetimeDrag::End;

  return RetimeDrag::None;
}

bool plQtSequenceTrackView::IsColorTrack(const plUuid& trackGuid) const
{
  const plSequenceTrack* pTrack = GetDocument() != nullptr ? GetDocument()->GetTrack(trackGuid) : nullptr;
  return pTrack != nullptr && pTrack->m_Target == plPropertyAnimTarget::Color;
}

plQtSequenceTrackView::KeyRef plQtSequenceTrackView::PickKey(const QPoint& pos) const
{
  KeyRef result;

  const int iRow = RowAtPixel(pos.y());
  if (iRow < 0 || m_pModel == nullptr)
    return result;

  const auto& row = m_pModel->GetFlatRows()[iRow];

  constexpr int iPickRadius = 6;
  int iBestDist = iPickRadius + 1;

  // Events are keys too - same diamond, same select/drag/delete - they just live on the sequence's
  // own track rather than on a binding's.
  if (row.IsEventLane())
  {
    const plDocumentObject* pTrackObj = GetDocument()->GetObjectManager()->GetObject(row.m_ObjectGuid);
    if (pTrackObj == nullptr)
      return result;

    const plInt32 iCount = pTrackObj->GetTypeAccessor().GetCount("ControlPoints");
    for (plInt32 i = 0; i < iCount; ++i)
    {
      const plVariant cpGuid = pTrackObj->GetTypeAccessor().GetValue("ControlPoints", i);
      if (!cpGuid.IsA<plUuid>())
        continue;

      const plDocumentObject* pCpObj = GetDocument()->GetObjectManager()->GetObject(cpGuid.Get<plUuid>());
      if (pCpObj == nullptr)
        continue;

      const plInt64 iTick = pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();
      const int iDist = plMath::Abs(PixelAtTick(iTick) - pos.x());

      if (iDist < iBestDist)
      {
        iBestDist = iDist;
        result.m_TrackGuid = row.m_ObjectGuid;
        result.m_CpGuid = pCpObj->GetGuid();
        result.m_iTick = iTick;
      }
    }

    return result;
  }

  if (row.IsBinding())
    return result;

  // Colour tracks have no curve CPs, so ForEachKey yields nothing for them - their keys are the
  // gradient's colour stops.
  if (IsColorTrack(row.m_ObjectGuid))
  {
    plDynamicArray<plSequenceAssetDocument::ColorKey> colorKeys;
    GetDocument()->GetColorKeys(row.m_ObjectGuid, colorKeys);

    for (const auto& key : colorKeys)
    {
      const int iDist = plMath::Abs(PixelAtTick(key.m_iTick) - pos.x());
      if (iDist < iBestDist)
      {
        iBestDist = iDist;
        result.m_TrackGuid = row.m_ObjectGuid;
        result.m_CpGuid = key.m_ColorCp;
        result.m_iTick = key.m_iTick;
      }
    }

    return result;
  }

  ForEachKey(row, [&](const plUuid& cpGuid, plInt64 iTick, double fValue) {
    PL_IGNORE_UNUSED(fValue);
    const int iDist = plMath::Abs(PixelAtTick(iTick) - pos.x());
    if (iDist < iBestDist)
    {
      iBestDist = iDist;
      result.m_TrackGuid = row.m_ObjectGuid;
      result.m_CpGuid = cpGuid;
      result.m_iTick = iTick;
      result.m_AxisTracks = row.m_AxisTracks;
    } });

  return result;
}

void plQtSequenceTrackView::paintEvent(QPaintEvent* e)
{
  PL_IGNORE_UNUSED(e);

  QPainter p(this);
  p.fillRect(rect(), QColor(0x14, 0x14, 0x14)); // QPalette::Base

  const plSequenceAssetDocument* pDoc = GetDocument();
  if (pDoc == nullptr || m_pModel == nullptr)
    return;

  const auto& rows = m_pModel->GetFlatRows();
  const plUInt64 uiDuration = pDoc->GetDurationTicks();

  // lanes
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    const int y = RowTop(r);
    const int iHeight = RowHeightAt(r);
    if (y + iHeight < RulerHeight || y > height())
      continue;

    const QRect lane(0, y, width(), iHeight);

    // selected track lane tint, mockup's .lane.sel
    QColor laneColor;
    if (!rows[r].IsBinding() && rows[r].m_ObjectGuid == m_HighlightedTrack)
      laneColor = QColor(0x0d, 0x21, 0x1c);
    else
      laneColor = rows[r].IsBinding() ? QColor(0x1b, 0x1b, 0x1b) : QColor(0x14, 0x14, 0x14);

    p.fillRect(lane, laneColor);
    p.setPen(QColor(0x23, 0x23, 0x23));
    p.drawLine(lane.bottomLeft(), lane.bottomRight());
  }

  // Adaptive tick density: pick the finest step from a human-readable ladder that still leaves
  // room for a label. One label per second is unreadable on a long sequence and wasteful on a
  // short one - this keeps labels ~70px apart at any zoom.
  const double fPixelsPerSecond = PixelsPerTick() * (double)plSequenceTicksPerSecond;
  double fLabelStep = 1.0;
  {
    static const double stepLadder[] = {0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0};
    fLabelStep = stepLadder[PL_ARRAY_SIZE(stepLadder) - 1];

    for (double fStep : stepLadder)
    {
      if (fStep * fPixelsPerSecond >= 70.0)
      {
        fLabelStep = fStep;
        break;
      }
    }
  }
  const double fMinorStep = fLabelStep / 5.0;
  const double fDurationSeconds = (double)uiDuration / (double)plSequenceTicksPerSecond;

  auto secondsToTick = [](double fSeconds) -> plInt64 { return (plInt64)(fSeconds * (double)plSequenceTicksPerSecond); };

  auto formatLabel = [&](double fSeconds) -> QString
  {
    if (fSeconds >= 60.0)
    {
      const int iMinutes = (int)(fSeconds / 60.0);
      const double fRest = fSeconds - iMinutes * 60.0;
      return QString("%1:%2").arg(iMinutes).arg(fRest, 4, 'f', 1, QChar('0'));
    }

    if (fLabelStep >= 1.0)
      return QString("%1s").arg((int)(fSeconds + 0.5));

    // Enough decimals to actually distinguish the step: a 0.25 ladder rendered at one decimal
    // reads 0.3 / 0.5 / 0.8, which is simply wrong.
    const int iDecimals = (plMath::Abs(fLabelStep * 10.0 - plMath::Round(fLabelStep * 10.0)) < 0.0001) ? 1 : 2;
    return QString("%1s").arg(fSeconds, 0, 'f', iDecimals);
  };

  // grid lines under the lanes, on label steps only - minor ticks stay in the ruler
  {
    p.setPen(QColor(0x26, 0x26, 0x26));

    for (double fSec = 0.0; fSec <= fDurationSeconds + 0.0001; fSec += fLabelStep)
    {
      const int x = PixelAtTick(secondsToTick(fSec));
      p.drawLine(x, RulerHeight, x, height());
    }
  }

  // ruler
  {
    p.fillRect(QRect(0, 0, width(), RulerHeight), QColor(0x50, 0x50, 0x50)); // QPalette::Light, like plQtTimeScrubberWidget

    // minor ticks
    p.setPen(QColor(0, 0, 0, 70));
    for (double fSec = 0.0; fSec <= fDurationSeconds + 0.0001; fSec += fMinorStep)
    {
      const int x = PixelAtTick(secondsToTick(fSec));
      p.drawLine(x, RulerHeight - 4, x, RulerHeight);
    }

    // major ticks + labels
    for (double fSec = 0.0; fSec <= fDurationSeconds + 0.0001; fSec += fLabelStep)
    {
      const int x = PixelAtTick(secondsToTick(fSec));

      p.setPen(QColor(0, 0, 0, 115));
      p.drawLine(x, RulerHeight - 9, x, RulerHeight);

      p.setPen(QColor(0xe0, 0xe0, 0xe0));
      p.drawText(QRect(x + 3, 0, 64, RulerHeight - 3), Qt::AlignVCenter | Qt::AlignLeft, formatLabel(fSec));
    }
  }

  // camera cut spans: each runs from its start tick to the next cut's start, or the end
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    if (!rows[r].IsCameraCutLane())
      continue;

    const int y = RowTop(r);
    const int iLaneHeight = RowHeightAt(r);
    if (y + iLaneHeight < RulerHeight || y > height())
      break;

    const plDocumentObject* pPropObj = GetDocument()->GetPropertyObject();
    const plInt32 iCutCount = pPropObj->GetTypeAccessor().GetCount("CameraCuts");

    PL_IGNORE_UNUSED(iCutCount);

    plHybridArray<CutRef, 8> cuts;
    CollectCameraCuts(cuts);

    for (plUInt32 c = 0; c < cuts.GetCount(); ++c)
    {
      plInt64 iStart = cuts[c].m_iStart;
      plInt64 iCutDuration = cuts[c].m_iDuration;

      // while dragging, draw where it would land rather than where it still is
      if (m_bCutDragActive && cuts[c].m_CutGuid == m_DraggedCut.m_CutGuid)
      {
        iStart = m_iCutPreviewStart;
        iCutDuration = m_iCutPreviewDuration;
      }

      // A cut holds the camera until the next one starts even if its own tail is shorter, so the
      // bar covers whichever is longer - drawing only the authored tail would understate when the
      // shot is actually live.
      const plInt64 iNextStart = (c + 1 < cuts.GetCount()) ? cuts[c + 1].m_iStart : (plInt64)uiDuration;
      const plInt64 iEnd = plMath::Max(iStart + iCutDuration, iNextStart);

      const int xStart = PixelAtTick(iStart);
      const int xEnd = PixelAtTick(iEnd);

      QRect span(xStart, y + 3, plMath::Max(2, xEnd - xStart - 1), iLaneHeight - 6);

      // A cut whose camera cannot be resolved is dead weight - it will not pilot and will not play.
      // Say so on the span rather than letting it look like every other cut.
      const bool bBroken = cuts[c].m_bUnresolved;

      const bool bCutSelected = cuts[c].m_CutGuid == m_SelectedCut;

      p.setPen(bCutSelected ? QPen(QColor(0x06, 0xfa, 0xc9), 2)
                            : QPen(bBroken ? QColor(0x8a, 0x3b, 0x3b) : QColor(0x0c, 0x6b, 0x56), 1));
      p.setBrush(bBroken ? QColor(0x4a, 0x26, 0x26) : QColor(0x08, 0x52, 0x42));
      p.drawRoundedRect(span, 3, 3);

      // Filmstrip: each tile is rendered at its own time, so the span shows what happens during the
      // shot rather than the same frame repeated. Tiles are placed on fixed pixel boundaries and
      // their ticks derived from that, so panning or zooming asks for the same handful of images
      // instead of a fresh set every repaint.
      if (!bBroken && m_pThumbnails != nullptr && span.width() > 24)
      {
        p.save();
        QPainterPath clip;
        clip.addRoundedRect(span, 3, 3);
        p.setClipPath(clip);

        const int iTileHeight = span.height();
        const int iTileWidth = plMath::Max(
          16, (plQtSequenceThumbnailCache::ThumbnailWidth * iTileHeight) / plMath::Max(1, plQtSequenceThumbnailCache::ThumbnailHeight));

        const int xFrom = plMath::Max(span.left(), 0);
        const int xTo = plMath::Min(span.right(), width());

        // Aligned to the span's own left edge so a tile's time does not shift as the lane scrolls.
        for (int x = span.left(); x < xTo; x += iTileWidth)
        {
          if (x + iTileWidth < xFrom)
            continue; // off-screen to the left

          const QImage thumb = m_pThumbnails->GetThumbnail(TickAtPixel(x));
          if (!thumb.isNull())
            p.drawImage(QRect(x, span.top(), iTileWidth, iTileHeight), thumb);
        }

        // darken so the label stays readable over the images
        p.fillRect(span, QColor(0, 0, 0, 90));
        p.restore();

        p.setPen(QColor(0x0c, 0x6b, 0x56));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(span, 3, 3);
      }

      // Blend wedge, the mockup's diagonal. The blend is the overlap with the previous shot, so
      // this is drawn from the data rather than from a separate field that could disagree with it.
      plInt64 iBlendIn = 0;
      if (c > 0)
      {
        const plInt64 iPrevEnd = cuts[c - 1].m_iStart + cuts[c - 1].m_iDuration;
        iBlendIn = plMath::Max<plInt64>(0, iPrevEnd - iStart);
      }

      if (iBlendIn > 0)
      {
        const int w = plMath::Min(span.width(), PixelAtTick(iBlendIn));

        p.save();
        QPainterPath wedgeClip;
        wedgeClip.addRoundedRect(span, 3, 3);
        p.setClipPath(wedgeClip);

        QPolygon wedge;
        wedge << span.topLeft() << QPoint(span.left() + w, span.top()) << span.bottomLeft();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 45));
        p.drawPolygon(wedge);

        p.setPen(QPen(QColor(255, 255, 255, 110), 1));
        p.drawLine(span.left() + w, span.top(), span.left(), span.bottom());
        p.restore();
      }

      if (span.width() > 26)
      {
        p.setPen(bBroken ? QColor(0xff, 0x8f, 0x8f) : QColor(0xff, 0xff, 0xff));
        const QRect textRect = span.adjusted(6, 0, -4, 0);
        const QString sLabel = bBroken ? QString("%1  (camera not found)").arg(cuts[c].m_sName) : cuts[c].m_sName;
        p.drawText(textRect, Qt::AlignTop | Qt::AlignLeft, p.fontMetrics().elidedText(sLabel, Qt::ElideRight, textRect.width()));
      }
    }
  }

  // clip bars
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    if (!rows[r].IsClipLane())
      continue;

    const int y = RowTop(r);
    const int iHeight = RowHeightAt(r);
    if (y + iHeight < RulerHeight || y > height())
      continue;

    // via ForEachClip so the lane only paints clips of its own kind, exactly as picking sees them
    ForEachClip(rows[r], [&](const plDocumentObject* pClipObj) {
      plInt64 iStart = pClipObj->GetTypeAccessor().GetValue("StartTick").ConvertTo<plInt64>();
      plInt64 iDuration = plMath::Max<plInt64>(1, pClipObj->GetTypeAccessor().GetValue("DurationTicks").ConvertTo<plInt64>());

      // while dragging, draw where it would land rather than where it still is
      if (m_bClipDragActive && pClipObj->GetGuid() == m_DraggedClip.m_ClipGuid)
      {
        iStart = m_iClipPreviewStart;
        iDuration = m_iClipPreviewDuration;
      }

      const plInt64 iKind = pClipObj->GetTypeAccessor().GetValue("Kind").ConvertTo<plInt64>();
      // Each clip kind names its payload differently (PlayTrigger / Clip / Sequence), so the label
      // comes off the native object rather than from a property name the canvas would have to guess.
      const plSequenceClip* pClip = GetDocument()->GetClip(pClipObj->GetGuid());
      const plString sPayload = pClip != nullptr ? pClip->m_sPayload : plString();

      const int xStart = PixelAtTick(iStart);
      const int xEnd = PixelAtTick(iStart + iDuration);
      const QRect bar(xStart, y + 4, plMath::Max(3, xEnd - xStart - 1), iHeight - 8);

      // clip type colours follow the mockup's track ramp
      QColor fill;
      switch (iKind)
      {
        case plSequenceClipKind::AudioTrigger: fill = QColor(0x2c, 0x1f, 0x28); break; // audio, mauve-tinted
        case plSequenceClipKind::AnimationClip: fill = QColor(0x1c, 0x3f, 0x63); break; // LightUI(Blue) family
        default: fill = QColor(0x2a, 0x20, 0x45); break;                                // sub-sequence, violet family
      }

      const bool bSelected = pClipObj->GetGuid() == m_SelectedClip;

      p.setBrush(fill);
      p.setPen(bSelected ? QColor(0x06, 0xfa, 0xc9) : QColor(0x7a, 0x55, 0x70));
      p.drawRoundedRect(bar, 3, 3);

      // Audio clips draw the sample's actual peak envelope, so the clip shows where the sound is
      // rather than only where it was placed. A payload that resolves to no decodable file keeps
      // the plain bar - nothing here invents a waveform.
      if (iKind == plSequenceClipKind::AudioTrigger && bar.width() > 8)
      {
        if (const auto* pPeaks = m_Waveforms.GetPeaks(sPayload))
        {
          p.save();
          QPainterPath clipPath;
          clipPath.addRoundedRect(bar, 3, 3);
          p.setClipPath(clipPath);

          // The clip's own length is authored and may be trimmed shorter or stretched longer than
          // the sample, so the envelope is mapped over the sample's real duration and simply runs
          // out - a clip shorter than its sound visibly cuts it off.
          const double fSampleSeconds = plMath::Max(0.0001, pPeaks->m_fDurationSeconds);
          const double fSpeed = plMath::Max(0.0001, (double)pClipObj->GetTypeAccessor().GetValue("Speed").ConvertTo<double>());
          const int iSampleWidth = (int)(fSampleSeconds / fSpeed * (double)plSequenceTicksPerSecond * PixelsPerTick());

          // The waveform lives below the label strip rather than behind it - overlapping the two
          // made both hard to read, which is why the lane is taller now.
          const QRect waveArea = bar.adjusted(0, LabelStripHeight, 0, 0);
          const int yMid = waveArea.center().y();
          const int iHalf = plMath::Max(1, waveArea.height() / 2 - 1);

          p.setPen(QColor(0xc8, 0x8c, 0xb9)); // cat-audio, mockup's waveform stroke

          const int xFrom = plMath::Max(bar.left(), 0);
          const int xTo = plMath::Min(bar.left() + iSampleWidth, plMath::Min(bar.right(), width()));

          for (int x = xFrom; x <= xTo; ++x)
          {
            const double fNorm = (double)(x - bar.left()) / plMath::Max(1, iSampleWidth);
            const plUInt32 uiBucket = plMath::Min(plSequenceWaveformCache::BucketCount - 1,
              (plUInt32)(fNorm * (double)plSequenceWaveformCache::BucketCount));

            const int yTop = yMid - (int)(pPeaks->m_Max[uiBucket] * iHalf);
            const int yBottom = yMid - (int)(pPeaks->m_Min[uiBucket] * iHalf);
            p.drawLine(x, yTop, x, plMath::Max(yBottom, yTop + 1));
          }

          p.restore();
        }
      }

      // blend wedges, so a clip that eases in reads differently from one that cuts in
      const plInt64 iBlendIn = pClipObj->GetTypeAccessor().GetValue("BlendInTicks").ConvertTo<plInt64>();
      const plInt64 iBlendOut = pClipObj->GetTypeAccessor().GetValue("BlendOutTicks").ConvertTo<plInt64>();

      p.setPen(Qt::NoPen);
      p.setBrush(QColor(255, 255, 255, 40));
      if (iBlendIn > 0)
      {
        const int w = plMath::Min(bar.width(), PixelAtTick(iBlendIn));
        QPolygon wedge;
        wedge << bar.topLeft() << QPoint(bar.left() + w, bar.top()) << bar.bottomLeft();
        p.drawPolygon(wedge);
      }
      if (iBlendOut > 0)
      {
        const int w = plMath::Min(bar.width(), PixelAtTick(iBlendOut));
        QPolygon wedge;
        wedge << bar.topRight() << QPoint(bar.right() - w, bar.top()) << bar.bottomRight();
        p.drawPolygon(wedge);
      }

      if (bar.width() > 24)
      {
        p.setPen(QColor(0xe7, 0xce, 0xde));
        const QRect textRect(bar.left() + 6, bar.top() + 1, bar.width() - 10, LabelStripHeight);
        // the trigger's name, not its asset GUID, which is what the payload actually stores
        const plString sFriendly = plSequenceWaveformCache::PayloadDisplayName(sPayload);
        const QString sLabel = sFriendly.IsEmpty() ? QString("<empty>") : QString::fromUtf8(sFriendly.GetData());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(sLabel, Qt::ElideRight, textRect.width()));
      }
    });
  }

  // Colour track lanes: a gradient strip rather than diamonds. A colour track is driven by a
  // gradient, not a curve, so it has a value at every tick - painting the evaluated ramp shows the
  // actual animation, which a row of keys never could.
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    if (rows[r].IsBinding() || rows[r].IsClipLane() || rows[r].IsCameraCutLane())
      continue;

    const plSequenceTrack* pTrack = pDoc->GetTrack(rows[r].m_ObjectGuid);
    if (pTrack == nullptr || pTrack->m_Target != plPropertyAnimTarget::Color)
      continue;

    // Painted even with no stops yet: an empty ramp still reads as "this lane is a gradient",
    // which is the point of styling it differently in the first place.
    const plColorGradientAssetData& gradient = pTrack->m_ColorGradient;

    const int y = RowTop(r);
    const int iHeight = RowHeightAt(r);
    if (y + iHeight < RulerHeight || y > height())
      continue;

    const QRect bar(PixelAtTick(0), y + 5, plMath::Max(2, PixelAtTick((plInt64)uiDuration) - PixelAtTick(0)), iHeight - 10);
    const QRect visible = bar.intersected(QRect(0, RulerHeight, width(), height() - RulerHeight));
    if (visible.isEmpty())
      continue;

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(bar, 2, 2);
    p.setClipPath(clip);

    // checkerboard, so a keyed alpha ramp reads as transparency instead of as a darker colour
    for (int cy = bar.top(); cy < bar.bottom(); cy += 6)
    {
      for (int cx = bar.left(); cx < bar.right(); cx += 6)
      {
        const bool bDark = (((cx - bar.left()) / 6) + ((cy - bar.top()) / 6)) % 2 == 0;
        p.fillRect(QRect(cx, cy, 6, 6), bDark ? QColor(0x2a, 0x2a, 0x2a) : QColor(0x3a, 0x3a, 0x3a));
      }
    }

    // one evaluation per pixel column - the gradient is not linear in screen space once alpha and
    // intensity CPs are involved, so a QLinearGradient with stops would be wrong
    for (int x = visible.left(); x <= visible.right(); ++x)
    {
      const plColor c = gradient.Evaluate(TickAtPixel(x));
      const plColorGammaUB srgb = c;
      p.fillRect(QRect(x, bar.top(), 1, bar.height()), QColor(srgb.r, srgb.g, srgb.b, srgb.a));
    }

    p.restore();

    p.setPen(QColor(0x50, 0x50, 0x50)); // QPalette::Mid, mockup's .grad border
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(bar, 2, 2);

    // Colour keys sit on the ramp as swatch diamonds - the same shape as every other key, so they
    // select, drag and delete alike, but filled with the colour they hold.
    plDynamicArray<plSequenceAssetDocument::ColorKey> colorKeys;
    pDoc->GetColorKeys(rows[r].m_ObjectGuid, colorKeys);

    const int yMid = bar.center().y();

    for (const auto& key : colorKeys)
    {
      const bool bSelected = IsKeySelected(key.m_ColorCp);

      plInt64 iDrawTick = key.m_iTick;
      if (bSelected && m_bDraggingKey && m_bDragActive)
        iDrawTick = key.m_iTick + m_iDragDeltaTicks;

      const int x = PixelAtTick(iDrawTick);
      if (x < -8 || x > width() + 8)
        continue;

      const int s = bSelected ? 7 : 6;
      QPolygon diamond;
      diamond << QPoint(x, yMid - s) << QPoint(x + s, yMid) << QPoint(x, yMid + s) << QPoint(x - s, yMid);

      const plColorGammaUB srgb = key.m_Value;
      p.setBrush(QColor(srgb.r, srgb.g, srgb.b));
      // white outline so a dark swatch still reads against the ramp, yellow when selected to match
      // the curve editor's selected control point
      p.setPen(QPen(bSelected ? QColor(0xfc, 0xc4, 0x19) : QColor(0xf0, 0xf0, 0xf0), bSelected ? 2 : 1));
      p.drawPolygon(diamond);
    }
  }

  // Event lane: a diamond per event with its name beside it, so events read against the shots and
  // clips they are meant to line up with.
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    if (!rows[r].IsEventLane())
      continue;

    const int y = RowTop(r);
    const int iHeight = RowHeightAt(r);
    if (y + iHeight < RulerHeight || y > height())
      continue;

    const plDocumentObject* pTrackObj = pDoc->GetObjectManager()->GetObject(rows[r].m_ObjectGuid);
    if (pTrackObj == nullptr)
      continue;

    const int yMid = y + iHeight / 2;
    const plInt32 iCount = pTrackObj->GetTypeAccessor().GetCount("ControlPoints");

    for (plInt32 i = 0; i < iCount; ++i)
    {
      const plVariant cpGuid = pTrackObj->GetTypeAccessor().GetValue("ControlPoints", i);
      if (!cpGuid.IsA<plUuid>())
        continue;

      const plDocumentObject* pCpObj = pDoc->GetObjectManager()->GetObject(cpGuid.Get<plUuid>());
      if (pCpObj == nullptr)
        continue;

      plInt64 iTick = pCpObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();
      const bool bSelected = IsKeySelected(pCpObj->GetGuid());

      if (bSelected && m_bDraggingKey && m_bDragActive)
        iTick += m_iDragDeltaTicks;

      const int x = PixelAtTick(iTick);
      if (x < -8 || x > width() + 8)
        continue;

      const int s = bSelected ? 6 : 5;
      QPolygon diamond;
      diamond << QPoint(x, yMid - s) << QPoint(x + s, yMid) << QPoint(x, yMid + s) << QPoint(x - s, yMid);

      p.setBrush(bSelected ? QColor(0xfc, 0xc4, 0x19) : QColor(0xc8, 0x96, 0x00));
      p.setPen(bSelected ? QColor(0xff, 0xff, 0xff) : QColor(0x8a, 0x68, 0x00));
      p.drawPolygon(diamond);

      const plString sEvent = pCpObj->GetTypeAccessor().GetValue("Event").ConvertTo<plString>();
      if (!sEvent.IsEmpty())
      {
        p.setPen(QColor(0xb9, 0xa1, 0x5c)); // the mockup's event label colour
        p.drawText(QRect(x + s + 3, y, 140, iHeight), Qt::AlignVCenter | Qt::AlignLeft, QString::fromUtf8(sEvent.GetData()));
      }
    }
  }

  // Binding summary rows: every tick any of the binding's tracks is keyed on, drawn small and grey
  // (mockup 3 §2). Collapsing a binding then still shows where its timing sits, which is the whole
  // point of a dope sheet.
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    if (rows[r].m_Kind != plQtSequenceModel::RowKind::Binding)
      continue;

    const int y = RowTop(r);
    const int iHeight = RowHeightAt(r);
    if (y + iHeight < RulerHeight || y > height())
      continue;

    // the binding's own rows are the ones that follow it until the next top-level row
    plSet<plInt64> ticks;
    for (plUInt32 s = r + 1; s < rows.GetCount(); ++s)
    {
      if (rows[s].m_Kind == plQtSequenceModel::RowKind::Binding || rows[s].IsCameraCutLane() || rows[s].IsEventLane())
        break;

      if (rows[s].m_Kind == plQtSequenceModel::RowKind::VectorGroup)
        continue; // its axes are listed separately, so counting it too would double up

      ForEachKey(rows[s], [&](const plUuid& cpGuid, plInt64 iTick, double fValue) {
        PL_IGNORE_UNUSED(cpGuid);
        PL_IGNORE_UNUSED(fValue);
        ticks.Insert(iTick); });

      if (IsColorTrack(rows[s].m_ObjectGuid))
      {
        plDynamicArray<plSequenceAssetDocument::ColorKey> colorKeys;
        pDoc->GetColorKeys(rows[s].m_ObjectGuid, colorKeys);
        for (const auto& colorKey : colorKeys)
          ticks.Insert(colorKey.m_iTick);
      }
    }

    const int yMid = y + iHeight / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x8e, 0x8e, 0x8e));

    for (plInt64 iTick : ticks)
    {
      const int x = PixelAtTick(iTick);
      if (x < -4 || x > width() + 4)
        continue;

      QPolygon diamond;
      diamond << QPoint(x, yMid - 3) << QPoint(x + 3, yMid) << QPoint(x, yMid + 3) << QPoint(x - 3, yMid);
      p.drawPolygon(diamond);
    }
  }

  // keys
  for (plUInt32 r = 0; r < rows.GetCount(); ++r)
  {
    if (rows[r].IsBinding())
      continue;

    const int y = RowTop(r) + RowHeightAt(r) / 2;
    if (y + RowHeight < RulerHeight || y - RowHeight > height())
      continue;

    ForEachKey(rows[r], [&](const plUuid& cpGuid, plInt64 iTick, double fValue) {
      PL_IGNORE_UNUSED(fValue);

      const bool bSelected = IsKeySelected(cpGuid);
      plInt64 iDrawTick = iTick;
      if (bSelected && m_bDraggingKey && m_bDragActive)
        iDrawTick = iTick + m_iDragDeltaTicks;

      const int x = PixelAtTick(iDrawTick);

      QPolygon diamond;
      const int s = bSelected ? 6 : 5;
      diamond << QPoint(x, y - s) << QPoint(x + s, y) << QPoint(x, y + s) << QPoint(x - s, y);

      // plQtCurveEditWidget colors: LightUI(Gray) CPs, LightUI(Yellow) selected
      p.setBrush(bSelected ? QColor(0xfc, 0xc4, 0x19) : QColor(0xce, 0xd4, 0xda));
      p.setPen(bSelected ? QColor(0xc7, 0x9a, 0x10) : QColor(0x8e, 0x8e, 0x8e));
      p.drawPolygon(diamond); });
  }

  // Markers: a teal flag on the ruler with a faint line down the lanes, so a named moment can be
  // read against every track at once (mockup 3 §2).
  {
    const plDocumentObject* pPropObj = GetDocument()->GetPropertyObject();
    const plInt32 iCount = pPropObj->GetTypeAccessor().GetCount("Markers");

    for (plInt32 i = 0; i < iCount; ++i)
    {
      const plVariant guid = pPropObj->GetTypeAccessor().GetValue("Markers", i);
      if (!guid.IsA<plUuid>())
        continue;

      const plDocumentObject* pMarkerObj = pDoc->GetObjectManager()->GetObject(guid.Get<plUuid>());
      if (pMarkerObj == nullptr)
        continue;

      plInt64 iTick = pMarkerObj->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();
      const bool bSelected = pMarkerObj->GetGuid() == m_SelectedMarker;

      if (bSelected && m_bMarkerDragActive)
        iTick = m_iMarkerPreviewTick;

      const int x = PixelAtTick(iTick);
      if (x < -60 || x > width() + 4)
        continue;

      const QColor color = bSelected ? QColor(0x06, 0xfa, 0xc9) : QColor(0x64, 0xbe, 0xb9); // Animation accent

      p.setPen(QPen(color, 1, Qt::DotLine));
      p.drawLine(x, RulerHeight, x, height());

      QPolygon flag;
      flag << QPoint(x, 2) << QPoint(x + 9, 2) << QPoint(x + 6, 7) << QPoint(x + 9, 12) << QPoint(x, 12);
      p.setPen(Qt::NoPen);
      p.setBrush(color);
      p.drawPolygon(flag);

      const plString sName = pMarkerObj->GetTypeAccessor().GetValue("Name").ConvertTo<plString>();
      if (!sName.IsEmpty())
      {
        p.setPen(QColor(0x10, 0x10, 0x10));
        p.drawText(QRect(x + 12, 0, 120, RulerHeight - 2), Qt::AlignVCenter | Qt::AlignLeft,
          p.fontMetrics().elidedText(QString::fromUtf8(sName.GetData()), Qt::ElideRight, 118));
      }
    }
  }

  // Retime band: the span the selection covers, with a grab handle at each end. Dragging one
  // scales the selection about the other, which is how a beat gets stretched without re-keying it.
  {
    plInt64 iMin = 0, iMax = 0;
    if (GetSelectionRange(iMin, iMax))
    {
      plInt64 iDrawMin = iMin;
      plInt64 iDrawMax = iMax;

      if (m_RetimeDrag != RetimeDrag::None)
      {
        const plInt64 iScaled = m_iRetimePivot + (plInt64)plMath::Round((double)(m_iRetimeGrabbed - m_iRetimePivot) * m_fRetimePreviewFactor);
        iDrawMin = plMath::Min(m_iRetimePivot, iScaled);
        iDrawMax = plMath::Max(m_iRetimePivot, iScaled);
      }

      const int xMin = PixelAtTick(iDrawMin);
      const int xMax = PixelAtTick(iDrawMax);
      const QRect band(xMin, RulerHeight, plMath::Max(2, xMax - xMin), RetimeBandHeight);

      p.setPen(Qt::NoPen);
      p.setBrush(QColor(0x06, 0xfa, 0xc9, 45));
      p.drawRect(band);

      p.setBrush(QColor(0x06, 0xfa, 0xc9));
      p.drawRect(QRect(xMin - 2, RulerHeight, 4, RetimeBandHeight));
      p.drawRect(QRect(xMax - 2, RulerHeight, 4, RetimeBandHeight));

      if (m_RetimeDrag != RetimeDrag::None)
      {
        p.setPen(QColor(0x06, 0xfa, 0xc9));
        p.drawText(QRect(xMin, RulerHeight + RetimeBandHeight, 90, 14), Qt::AlignLeft | Qt::AlignVCenter,
          QString::asprintf("%.2fx", m_fRetimePreviewFactor));
      }
    }
  }

  // box select rubber band
  if (m_bBoxSelecting)
  {
    const QRect box = QRect(m_BoxSelectStart, m_BoxSelectEnd).normalized();
    p.setPen(QPen(QColor(0x06, 0xfa, 0xc9), 1, Qt::DashLine));
    p.setBrush(QColor(0x06, 0xfa, 0xc9, 28));
    p.drawRect(box);
  }

  // playhead - LightUI(PlasmaBranding), the one deliberate accent exception (see Plan §6)
  {
    const int x = PixelAtTick((plInt64)pDoc->GetScrubberPosition());
    p.setPen(QPen(QColor(0x06, 0xfa, 0xc9), 1));
    p.drawLine(x, 0, x, height());

    QPolygon grab;
    grab << QPoint(x - 5, 0) << QPoint(x + 5, 0) << QPoint(x, 8);
    p.setBrush(QColor(0x06, 0xfa, 0xc9));
    p.setPen(Qt::NoPen);
    p.drawPolygon(grab);
  }
}

void plQtSequenceTrackView::mousePressEvent(QMouseEvent* e)
{
  if (e->button() != Qt::LeftButton)
  {
    QWidget::mousePressEvent(e);
    return;
  }

  setFocus();

  // markers own the ruler strip, so they are picked before scrubbing gets a look in
  if (const plUuid marker = PickMarker(e->pos()); marker.IsValid())
  {
    m_SelectedMarker = marker;
    m_bMarkerDragActive = false;
    m_iMarkerPreviewTick = GetDocument()->GetObjectManager()->GetObject(marker)->GetTypeAccessor().GetValue("Tick").ConvertTo<plInt64>();
    m_iDragStartX = e->pos().x();
    update();
    return;
  }

  // retime handles sit above everything, in their own band under the ruler
  if (const RetimeDrag retime = PickRetimeHandle(e->pos()); retime != RetimeDrag::None)
  {
    plInt64 iMin = 0, iMax = 0;
    GetSelectionRange(iMin, iMax);

    m_RetimeDrag = retime;
    // scaling is about the handle you are not holding
    m_iRetimePivot = (retime == RetimeDrag::Start) ? iMax : iMin;
    m_iRetimeGrabbed = (retime == RetimeDrag::Start) ? iMin : iMax;
    m_fRetimePreviewFactor = 1.0;
    update();
    return;
  }

  // camera cuts, on their own lane above everything else
  {
    ClipDrag mode = ClipDrag::None;
    const CutRef cut = PickCut(e->pos(), mode);

    if (cut.IsValid())
    {
      m_SelectedCut = cut.m_CutGuid;
      m_SelectedClip = plUuid();
      m_DraggedCut = cut;
      m_CutDragMode = mode;
      m_bCutDragActive = false;
      m_iCutDragStartX = e->pos().x();
      m_iCutPreviewStart = cut.m_iStart;
      m_iCutPreviewDuration = cut.m_iDuration;

      GetDocument()->GetSelectionManager()->SetSelection(GetDocument()->GetObjectManager()->GetObject(cut.m_CutGuid));

      update();
      return;
    }
  }

  // clips next: they occupy their own lanes, so this cannot steal a key click
  {
    ClipDrag mode = ClipDrag::None;
    const ClipRef clip = PickClip(e->pos(), mode);

    if (clip.IsValid())
    {
      m_SelectedClip = clip.m_ClipGuid;
      m_DraggedClip = clip;
      m_ClipDragMode = mode;
      m_bClipDragActive = false;
      m_iClipDragStartX = e->pos().x();
      m_iClipPreviewStart = clip.m_iStart;
      m_iClipPreviewDuration = clip.m_iDuration;

      // selecting it puts the clip in the property grid, which is where its trigger name,
      // speed and blend times are edited
      GetDocument()->GetSelectionManager()->SetSelection(GetDocument()->GetObjectManager()->GetObject(clip.m_ClipGuid));

      update();
      return;
    }
  }

  const KeyRef picked = PickKey(e->pos());
  if (picked.IsValid())
  {
    SelectKey(picked, e->modifiers().testFlag(Qt::ControlModifier));

    m_bDraggingKey = true;
    m_bDragActive = false;
    m_iDragStartX = e->pos().x();
    m_iDragStartTick = picked.m_iTick;
    m_iDragPreviewTick = picked.m_iTick;
    m_iDragDeltaTicks = 0;
    update();

    if (m_SelectedKey.IsValid())
      Q_EMIT KeySelected(m_SelectedKey.m_TrackGuid, m_SelectedKey.m_CpGuid);

    return;
  }

  // Below the ruler, dragging empty space gathers keys instead of scrubbing - scrubbing stays on
  // the ruler, where a timeline expects it.
  if (e->pos().y() >= RulerHeight)
  {
    m_bBoxSelecting = true;
    m_BoxSelectStart = e->pos();
    m_BoxSelectEnd = e->pos();
    update();
    return;
  }

  m_SelectedKeys.Clear();
  m_SelectedKey.Invalidate();
  m_bScrubbing = true;
  Q_EMIT ScrubRequested((plUInt64)SnapTick(TickAtPixel(e->pos().x())));
  update();
}

void plQtSequenceTrackView::mouseMoveEvent(QMouseEvent* e)
{
  // cursor hints which edit a press would start
  if (m_ClipDragMode == ClipDrag::None && m_CutDragMode == ClipDrag::None && m_RetimeDrag == RetimeDrag::None && !m_bScrubbing &&
      !m_bDraggingKey)
  {
    ClipDrag hoverMode = ClipDrag::None;
    PickClip(e->pos(), hoverMode);

    if (hoverMode == ClipDrag::None)
      PickCut(e->pos(), hoverMode);

    const bool bResize = hoverMode == ClipDrag::TrimStart || hoverMode == ClipDrag::TrimEnd || PickRetimeHandle(e->pos()) != RetimeDrag::None;
    setCursor(bResize ? Qt::SizeHorCursor : Qt::ArrowCursor);
  }

  if (m_CutDragMode != ClipDrag::None && m_DraggedCut.IsValid())
  {
    if (!m_bCutDragActive && plMath::Abs(e->pos().x() - m_iCutDragStartX) > 3)
      m_bCutDragActive = true;

    if (m_bCutDragActive)
    {
      const plInt64 iDelta = TickAtPixel(e->pos().x()) - TickAtPixel(m_iCutDragStartX);

      if (m_CutDragMode == ClipDrag::Move)
      {
        m_iCutPreviewStart = plMath::Max<plInt64>(0, SnapTick(m_DraggedCut.m_iStart + iDelta));
        m_iCutPreviewDuration = m_DraggedCut.m_iDuration;
      }
      else
      {
        m_iCutPreviewStart = m_DraggedCut.m_iStart;
        // one frame minimum, so a cut cannot be trimmed into nothing and become ungrabbable
        const plInt64 iMin = plMath::Max<plInt64>(1, (plInt64)GetDocument()->GetTicksPerFrame());
        m_iCutPreviewDuration = plMath::Max(iMin, SnapTick(m_DraggedCut.m_iStart + m_DraggedCut.m_iDuration + iDelta) - m_DraggedCut.m_iStart);
      }

      update();
    }
  }

  if (m_ClipDragMode != ClipDrag::None && m_DraggedClip.IsValid())
  {
    if (!m_bClipDragActive && plMath::Abs(e->pos().x() - m_iClipDragStartX) > 3)
      m_bClipDragActive = true;

    if (m_bClipDragActive)
    {
      const plInt64 iDelta = TickAtPixel(e->pos().x()) - TickAtPixel(m_iClipDragStartX);

      switch (m_ClipDragMode)
      {
        case ClipDrag::Move:
          m_iClipPreviewStart = plMath::Max<plInt64>(0, SnapTick(m_DraggedClip.m_iStart + iDelta));
          break;

        case ClipDrag::TrimStart:
        {
          // trimming the start moves the start and shortens by the same amount, so the end stays put
          const plInt64 iEnd = m_DraggedClip.m_iStart + m_DraggedClip.m_iDuration;
          const plInt64 iNewStart = plMath::Clamp<plInt64>(SnapTick(m_DraggedClip.m_iStart + iDelta), 0, iEnd - 1);
          m_iClipPreviewStart = iNewStart;
          m_iClipPreviewDuration = iEnd - iNewStart;
          break;
        }

        case ClipDrag::TrimEnd:
          m_iClipPreviewDuration = plMath::Max<plInt64>(1, SnapTick(m_DraggedClip.m_iStart + m_DraggedClip.m_iDuration + iDelta) - m_DraggedClip.m_iStart);
          break;

        default:
          break;
      }

      update();
    }
    return;
  }

  if (m_bScrubbing)
  {
    Q_EMIT ScrubRequested((plUInt64)SnapTick(TickAtPixel(e->pos().x())));
    return;
  }

  if (m_SelectedMarker.IsValid() && (e->buttons() & Qt::LeftButton))
  {
    if (!m_bMarkerDragActive && plMath::Abs(e->pos().x() - m_iDragStartX) > 3)
      m_bMarkerDragActive = true;

    if (m_bMarkerDragActive)
    {
      // Snapped through the shared path, minus its own magnet - a marker must not stick to itself.
      m_iMarkerPreviewTick = plMath::Max<plInt64>(0, TickAtPixel(e->pos().x()));
      update();
      return;
    }
  }

  if (m_RetimeDrag != RetimeDrag::None)
  {
    const plInt64 iSpan = m_iRetimeGrabbed - m_iRetimePivot;
    if (iSpan != 0)
    {
      const plInt64 iNew = TickAtPixel(e->pos().x()) - m_iRetimePivot;
      // Clamped positive: dragging a handle through the pivot would mirror the selection in time,
      // which is never what a retime means.
      m_fRetimePreviewFactor = plMath::Max(0.01, (double)iNew / (double)iSpan);
      update();
    }
    return;
  }

  if (m_bBoxSelecting)
  {
    m_BoxSelectEnd = e->pos();
    update();
    return;
  }

  if (m_bDraggingKey)
  {
    if (!m_bDragActive && plMath::Abs(e->pos().x() - m_iDragStartX) > 3)
      m_bDragActive = true;

    if (m_bDragActive)
    {
      const plInt64 iDelta = TickAtPixel(e->pos().x()) - TickAtPixel(m_iDragStartX);
      m_iDragPreviewTick = SnapTick(m_iDragStartTick + iDelta);
      // Every selected key shifts by what the anchor shifted, so the group keeps its shape and
      // only the key under the cursor lands exactly on the snap.
      m_iDragDeltaTicks = m_iDragPreviewTick - m_iDragStartTick;
      update();
    }
  }
}

void plQtSequenceTrackView::mouseReleaseEvent(QMouseEvent* e)
{
  if (e->button() != Qt::LeftButton)
  {
    QWidget::mouseReleaseEvent(e);
    return;
  }

  m_bScrubbing = false;

  if (m_CutDragMode != ClipDrag::None)
  {
    const bool bChanged = m_bCutDragActive && m_DraggedCut.IsValid() &&
                          (m_iCutPreviewStart != m_DraggedCut.m_iStart || m_iCutPreviewDuration != m_DraggedCut.m_iDuration);

    if (bChanged)
    {
      plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
      pHistory->StartTransaction(m_CutDragMode == ClipDrag::Move ? "Move Camera Cut" : "Trim Camera Cut");

      plSetObjectPropertyCommand cmd;
      cmd.m_Object = m_DraggedCut.m_CutGuid;

      cmd.m_sProperty = "StartTick";
      cmd.m_NewValue = (plUInt64)m_iCutPreviewStart;
      pHistory->AddCommand(cmd).AssertSuccess();

      cmd.m_sProperty = "DurationTicks";
      cmd.m_NewValue = (plUInt64)m_iCutPreviewDuration;
      pHistory->AddCommand(cmd).AssertSuccess();

      pHistory->FinishTransaction();
    }

    m_CutDragMode = ClipDrag::None;
    m_bCutDragActive = false;
    m_DraggedCut.Invalidate();
    update();
    return;
  }

  if (m_ClipDragMode != ClipDrag::None)
  {
    const bool bChanged = m_bClipDragActive && m_DraggedClip.IsValid() &&
                          (m_iClipPreviewStart != m_DraggedClip.m_iStart || m_iClipPreviewDuration != m_DraggedClip.m_iDuration);

    if (bChanged)
    {
      plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
      pHistory->StartTransaction(m_ClipDragMode == ClipDrag::Move ? "Move Clip" : "Trim Clip");

      plSetObjectPropertyCommand cmd;
      cmd.m_Object = m_DraggedClip.m_ClipGuid;

      cmd.m_sProperty = "StartTick";
      cmd.m_NewValue = (plUInt64)m_iClipPreviewStart;
      pHistory->AddCommand(cmd).AssertSuccess();

      cmd.m_sProperty = "DurationTicks";
      cmd.m_NewValue = (plUInt64)m_iClipPreviewDuration;
      pHistory->AddCommand(cmd).AssertSuccess();

      pHistory->FinishTransaction();
    }

    m_ClipDragMode = ClipDrag::None;
    m_bClipDragActive = false;
    m_DraggedClip.Invalidate();
    update();
    return;
  }

  if (m_bMarkerDragActive && m_SelectedMarker.IsValid())
  {
    plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
    pHistory->StartTransaction("Move Marker");

    plSetObjectPropertyCommand cmd;
    cmd.m_Object = m_SelectedMarker;
    cmd.m_sProperty = "Tick";
    cmd.m_NewValue = (plUInt64)m_iMarkerPreviewTick;
    pHistory->AddCommand(cmd).AssertSuccess();

    pHistory->FinishTransaction();

    m_bMarkerDragActive = false;
    update();
    return;
  }

  if (m_RetimeDrag != RetimeDrag::None)
  {
    if (plMath::Abs(m_fRetimePreviewFactor - 1.0) > 0.0001)
      ScaleSelectedKeys(m_iRetimePivot, m_fRetimePreviewFactor);

    m_RetimeDrag = RetimeDrag::None;
    m_fRetimePreviewFactor = 1.0;
    update();
    return;
  }

  if (m_bBoxSelecting)
  {
    m_bBoxSelecting = false;

    const QRect box = QRect(m_BoxSelectStart, m_BoxSelectEnd).normalized();

    // A box with no drag in it is a click on empty space: clear the selection rather than
    // selecting whatever happened to sit under a one-pixel rect.
    if (box.width() < 3 && box.height() < 3)
    {
      m_SelectedKeys.Clear();
      m_SelectedKey.Invalidate();
    }
    else
    {
      SelectKeysInRect(box);
    }

    update();

    if (m_SelectedKey.IsValid())
      Q_EMIT KeySelected(m_SelectedKey.m_TrackGuid, m_SelectedKey.m_CpGuid);

    return;
  }

  if (m_bDraggingKey)
  {
    const bool bIsColorKey = m_SelectedKey.IsValid() && IsColorTrack(m_SelectedKey.m_TrackGuid);

    if (m_bDragActive && m_SelectedKey.IsValid() && m_iDragDeltaTicks != 0)
    {
      MoveSelectedKeys(m_iDragDeltaTicks);
    }

    const bool bWasClick = !m_bDragActive;

    m_bDraggingKey = false;
    m_bDragActive = false;
    m_iDragDeltaTicks = 0;
    update();

    // A colour key has no curve to edit, so clicking one is the gesture that opens its picker.
    // Deferred to release rather than press, or the modal dialog would swallow the drag.
    if (bWasClick && bIsColorKey)
    {
      EditColorKey(m_SelectedKey.m_TrackGuid, m_SelectedKey.m_iTick);
    }
  }
}

void plQtSequenceTrackView::EditColorKey(const plUuid& trackGuid, plInt64 iTick)
{
  const plSequenceTrack* pTrack = GetDocument()->GetTrack(trackGuid);
  if (pTrack == nullptr)
    return;

  m_ColorEditTrack = trackGuid;
  m_iColorEditTick = iTick;
  m_ColorEditValue = pTrack->m_ColorGradient.Evaluate(iTick);

  // Temporary commands so live picking previews on the lane and in the viewport, and cancelling
  // leaves the undo stack exactly as it was.
  GetDocument()->GetCommandHistory()->BeginTemporaryCommands("Edit Colour Key");

  plQtUiServices::GetSingleton()->ShowColorDialog(m_ColorEditValue, true, true, this,
    SLOT(onColorPickerChanged(const plColor&)), SLOT(onColorPickerAccepted()), SLOT(onColorPickerRejected()));
}

void plQtSequenceTrackView::onColorPickerChanged(const plColor& color)
{
  if (!m_ColorEditTrack.IsValid())
    return;

  m_ColorEditValue = color;
  GetDocument()->SetColorKey(m_ColorEditTrack, m_iColorEditTick, color);
  update();
}

void plQtSequenceTrackView::onColorPickerAccepted()
{
  if (!m_ColorEditTrack.IsValid())
    return;

  GetDocument()->SetColorKey(m_ColorEditTrack, m_iColorEditTick, m_ColorEditValue);
  GetDocument()->GetCommandHistory()->FinishTemporaryCommands();
  m_ColorEditTrack = plUuid();
  update();
}

void plQtSequenceTrackView::onColorPickerRejected()
{
  if (!m_ColorEditTrack.IsValid())
    return;

  GetDocument()->GetCommandHistory()->CancelTemporaryCommands();
  m_ColorEditTrack = plUuid();
  update();
}

void plQtSequenceTrackView::mouseDoubleClickEvent(QMouseEvent* e)
{
  if (e->button() != Qt::LeftButton || m_pModel == nullptr)
    return;

  // Double-clicking the ruler names the moment under the cursor, which is how a marker is made.
  if (e->pos().y() < RulerHeight)
  {
    const plUuid existing = PickMarker(e->pos());
    const plDocumentObject* pMarkerObj = existing.IsValid() ? GetDocument()->GetObjectManager()->GetObject(existing) : nullptr;

    const QString sCurrent =
      pMarkerObj != nullptr ? QString::fromUtf8(pMarkerObj->GetTypeAccessor().GetValue("Name").ConvertTo<plString>().GetData()) : QString();

    bool bOk = false;
    const QString sName = QInputDialog::getText(
      this, pMarkerObj != nullptr ? "Rename Marker" : "Add Marker", "Marker name:", QLineEdit::Normal, sCurrent, &bOk);

    if (bOk && !sName.isEmpty())
    {
      if (pMarkerObj != nullptr)
      {
        plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
        pHistory->StartTransaction("Rename Marker");

        plSetObjectPropertyCommand cmd;
        cmd.m_Object = existing;
        cmd.m_sProperty = "Name";
        cmd.m_NewValue = plString(sName.toUtf8().constData());
        pHistory->AddCommand(cmd).AssertSuccess();

        pHistory->FinishTransaction();
      }
      else
      {
        m_SelectedMarker = GetDocument()->AddMarker(SnapTick(TickAtPixel(e->pos().x())), sName.toUtf8().constData());
      }
    }

    update();
    return;
  }

  const int iRow = RowAtPixel(e->pos().y());
  if (iRow < 0)
    return;

  const auto& row = m_pModel->GetFlatRows()[iRow];

  // The event lane is header-styled, so IsBinding covers it - but unlike a binding it does take a
  // double-click, so it has to be let through before that guard.
  if (row.IsBinding() && !row.IsEventLane())
    return;

  const plInt64 iTick = SnapTick(TickAtPixel(e->pos().x()));

  // Events are named, and an unnamed one fires nothing, so the name is asked for up front rather
  // than leaving an empty event on the track to be found later.
  if (row.IsEventLane())
  {
    // On an existing event, rename it; on empty lane, add one. Double-clicking a key you can see
    // and getting a second key on top of it would be the wrong answer.
    const KeyRef existing = PickKey(e->pos());

    QString sCurrent;
    const plDocumentObject* pCpObj = existing.IsValid() ? GetDocument()->GetObjectManager()->GetObject(existing.m_CpGuid) : nullptr;
    if (pCpObj != nullptr)
      sCurrent = QString::fromUtf8(pCpObj->GetTypeAccessor().GetValue("Event").ConvertTo<plString>().GetData());

    bool bOk = false;
    const QString sName = QInputDialog::getText(
      this, pCpObj != nullptr ? "Rename Event" : "Add Event", "Event name:", QLineEdit::Normal, sCurrent, &bOk);

    if (bOk && !sName.isEmpty())
    {
      if (pCpObj != nullptr)
      {
        plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
        pHistory->StartTransaction("Rename Event");

        plSetObjectPropertyCommand cmd;
        cmd.m_Object = existing.m_CpGuid;
        cmd.m_sProperty = "Event";
        cmd.m_NewValue = plString(sName.toUtf8().constData());
        pHistory->AddCommand(cmd).AssertSuccess();

        pHistory->FinishTransaction();

        m_SelectedKey = existing;
      }
      else
      {
        const plUuid cpGuid = GetDocument()->InsertEventTrackCpAt(iTick, sName.toUtf8().constData());
        m_SelectedKey.m_TrackGuid = row.m_ObjectGuid;
        m_SelectedKey.m_CpGuid = cpGuid;
        m_SelectedKey.m_iTick = iTick;
        m_SelectedKey.m_AxisTracks.Clear();
      }
    }

    update();
    return;
  }

  // A clip lane has no curve to key - its guid is the binding, not a track - so double-click drops
  // a clip of the lane's own kind and selects it, ready to be given a payload in the property grid.
  if (row.IsClipLane())
  {
    const plUuid clipGuid = GetDocument()->AddClip(row.m_ObjectGuid, row.m_ClipKind, (plUInt64)iTick, "");
    if (clipGuid.IsValid())
    {
      m_SelectedClip = clipGuid;
      GetDocument()->GetSelectionManager()->SetSelection(GetDocument()->GetObjectManager()->GetObject(clipGuid));
    }

    update();
    return;
  }

  // Colour tracks key their gradient, not their curve. The new key takes the colour the gradient
  // already has at that tick, so inserting one never changes what the track evaluates to, and then
  // opens its picker - inserting a key you cannot immediately set is not useful.
  const plSequenceTrack* pTrack = GetDocument()->GetTrack(row.m_ObjectGuid);
  if (pTrack != nullptr && pTrack->m_Target == plPropertyAnimTarget::Color)
  {
    GetDocument()->SetColorKey(row.m_ObjectGuid, iTick, pTrack->m_ColorGradient.Evaluate(iTick));

    m_SelectedKey.m_TrackGuid = row.m_ObjectGuid;
    m_SelectedKey.m_CpGuid = GetDocument()->FindGradientColorCp(row.m_ObjectGuid, iTick);
    m_SelectedKey.m_iTick = iTick;

    update();
    EditColorKey(row.m_ObjectGuid, iTick);
    return;
  }

  // On a vector group, one double-click keys the whole vector - each axis at the value it already
  // holds, so the object does not jump.
  if (row.m_Kind == plQtSequenceModel::RowKind::VectorGroup)
  {
    GetDocument()->InsertVectorKey(row.m_AxisTracks, iTick);

    m_SelectedKey.m_TrackGuid = row.m_ObjectGuid;
    m_SelectedKey.m_CpGuid = GetDocument()->FindCurveCp(row.m_AxisTracks[0], iTick);
    m_SelectedKey.m_iTick = iTick;
    m_SelectedKey.m_AxisTracks = row.m_AxisTracks;
    update();
    return;
  }

  const plUuid cpGuid = GetDocument()->InsertCurveCpAt(row.m_ObjectGuid, iTick, 0.0);

  m_SelectedKey.m_TrackGuid = row.m_ObjectGuid;
  m_SelectedKey.m_CpGuid = cpGuid;
  m_SelectedKey.m_iTick = iTick;
  m_SelectedKey.m_AxisTracks.Clear();
  update();
  Q_EMIT KeySelected(row.m_ObjectGuid, cpGuid);
}

void plQtSequenceTrackView::wheelEvent(QWheelEvent* e)
{
  // A long sequence has far more lanes than fit, and reaching for the outliner's scrollbar to see
  // the track you are already pointing at is the wrong gesture.
  const int iDegrees = e->angleDelta().y() / 8;
  if (iDegrees == 0)
  {
    QWidget::wheelEvent(e);
    return;
  }

  // A notch is 15 degrees; three lanes per notch matches how a list scrolls elsewhere.
  const int iDelta = -(iDegrees * RowHeight * 3) / 15;

  Q_EMIT ScrollByRequested(iDelta);
  e->accept();
}

void plQtSequenceTrackView::contextMenuEvent(QContextMenuEvent* e)
{
  if (m_pModel == nullptr || GetDocument() == nullptr)
    return;

  plSequenceAssetDocument* pDoc = GetDocument();
  QMenu menu(this);

  auto removeObject = [pDoc](const char* szUndoName, const plUuid& guid)
  {
    plCommandHistory* pHistory = pDoc->GetCommandHistory();
    pHistory->StartTransaction(szUndoName);

    plRemoveObjectCommand cmd;
    cmd.m_Object = guid;
    pHistory->AddCommand(cmd).AssertSuccess();

    pHistory->FinishTransaction();
  };

  // What is under the cursor decides the menu, in the same priority order a click resolves:
  // cut, then clip, then key. Right-clicking something also selects it, so the action that follows
  // acts on what was aimed at rather than on whatever happened to be selected before.
  ClipDrag mode = ClipDrag::None;

  if (e->pos().y() < RulerHeight)
  {
    const plUuid marker = PickMarker(e->pos());

    if (marker.IsValid())
    {
      m_SelectedMarker = marker;
      update();
      menu.addAction("Delete Marker", this, [this, removeObject, marker]() {
        removeObject("Delete Marker", marker);
        m_SelectedMarker = plUuid();
        update(); });
    }
    else
    {
      const plInt64 iTick = SnapTick(TickAtPixel(e->pos().x()));
      menu.addAction("Add Marker Here...", this, [this, iTick]() {
        bool bOk = false;
        const QString sName = QInputDialog::getText(this, "Add Marker", "Marker name:", QLineEdit::Normal, QString(), &bOk);
        if (bOk && !sName.isEmpty())
          m_SelectedMarker = GetDocument()->AddMarker(iTick, sName.toUtf8().constData());
        update(); });
    }

    menu.exec(e->globalPos());
    return;
  }

  if (const CutRef cut = PickCut(e->pos(), mode); cut.IsValid())
  {
    m_SelectedCut = cut.m_CutGuid;
    pDoc->GetSelectionManager()->SetSelection(pDoc->GetObjectManager()->GetObject(cut.m_CutGuid));
    update();

    menu.addAction(QString("Delete Camera Cut  '%1'").arg(cut.m_sName), this,
      [this, removeObject, cut]() { removeObject("Delete Camera Cut", cut.m_CutGuid); m_SelectedCut = plUuid(); update(); });
  }
  else if (const ClipRef clip = PickClip(e->pos(), mode); clip.IsValid())
  {
    m_SelectedClip = clip.m_ClipGuid;
    pDoc->GetSelectionManager()->SetSelection(pDoc->GetObjectManager()->GetObject(clip.m_ClipGuid));
    update();

    // Fitting after a manual resize had no way back before - the auto-fit only fires the first
    // time a trigger is set.
    if (const plSequenceClip* pClip = pDoc->GetClip(clip.m_ClipGuid);
        pClip != nullptr && pClip->m_Kind == plSequenceClipKind::AudioTrigger && !pClip->m_sPayload.IsEmpty())
    {
      const plString sPayload = pClip->m_sPayload;
      const plUuid clipGuid = clip.m_ClipGuid;

      menu.addAction("Fit to Trigger Length", this, [this, pDoc, sPayload, clipGuid]() {
        const auto* pPeaks = m_Waveforms.GetPeaks(sPayload);
        if (pPeaks == nullptr || pPeaks->m_fDurationSeconds <= 0.0)
        {
          plQtUiServices::GetSingleton()->ShowAllDocumentsTemporaryStatusBarMessage(
            plFmt("That trigger does not resolve to a decodable sample."), plTime::MakeFromSeconds(4));
          return;
        }

        plCommandHistory* pHistory = pDoc->GetCommandHistory();
        pHistory->StartTransaction("Fit Clip to Trigger");

        plSetObjectPropertyCommand cmd;
        cmd.m_Object = clipGuid;
        cmd.m_sProperty = "DurationTicks";
        cmd.m_NewValue = (plUInt64)plMath::Max(1.0, pPeaks->m_fDurationSeconds * (double)plSequenceTicksPerSecond);
        pHistory->AddCommand(cmd).AssertSuccess();

        pHistory->FinishTransaction();
        update(); });

      menu.addSeparator();
    }

    menu.addAction("Delete Clip", this,
      [this, removeObject, clip]() { removeObject("Delete Clip", clip.m_ClipGuid); m_SelectedClip = plUuid(); update(); });
  }
  else if (const KeyRef key = PickKey(e->pos()); key.IsValid())
  {
    // keep an existing multi-selection if the right-click landed inside it
    if (!IsKeySelected(key.m_CpGuid))
      SelectKey(key, false);

    update();

    const plUInt32 uiCount = m_SelectedKeys.GetCount();

    const int iRow = RowAtPixel(e->pos().y());
    if (iRow >= 0 && m_pModel->GetFlatRows()[iRow].IsEventLane() && uiCount == 1)
    {
      menu.addAction("Rename Event...", this, [this, e]() {
        QMouseEvent fake(QEvent::MouseButtonDblClick, e->pos(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        mouseDoubleClickEvent(&fake); });
    }

    // Ease presets, applied to both sides of every selected curve key. Colour stops and events are
    // skipped rather than erroring, so a mixed selection still does the sensible thing.
    QMenu* pEase = menu.addMenu("Ease");
    pEase->addAction("Auto", this, [this]() { SetSelectedKeyTangents(plCurveTangentMode::Auto, plCurveTangentMode::Auto); update(); });
    pEase->addAction("Linear", this, [this]() { SetSelectedKeyTangents(plCurveTangentMode::Linear, plCurveTangentMode::Linear); update(); });
    pEase->addSeparator();
    pEase->addAction("Ease In", this, [this]() { SetSelectedKeyTangents(plCurveTangentMode::Linear, plCurveTangentMode::Auto); update(); });
    pEase->addAction("Ease Out", this, [this]() { SetSelectedKeyTangents(plCurveTangentMode::Auto, plCurveTangentMode::Linear); update(); });
    pEase->addAction("Ease In/Out", this, [this]() { SetSelectedKeyTangents(plCurveTangentMode::Bezier, plCurveTangentMode::Bezier); update(); });

    menu.addSeparator();

    menu.addAction(uiCount > 1 ? QString("Delete %1 Keys").arg(uiCount) : QString("Delete Key"), this,
      [this]() { DeleteSelectedKeys(); update(); });

    menu.addSeparator();
    menu.addAction("Copy", this, [this]() { CopySelectedKeys(); });
  }
  else
  {
    // empty lane space
    QAction* pPaste = menu.addAction("Paste Keys at Playhead", this, [this]() { PasteKeysAtPlayhead(); update(); });
    pPaste->setEnabled(!m_Clipboard.IsEmpty());

    menu.addAction("Select All Keys", this, [this]() {
      SelectKeysInRect(QRect(0, RulerHeight, width(), height()));
      update(); });
  }

  if (!menu.isEmpty())
    menu.exec(e->globalPos());
}

void plQtSequenceTrackView::keyPressEvent(QKeyEvent* e)
{
  if (e->key() == Qt::Key_Delete && m_SelectedCut.IsValid())
  {
    plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
    pHistory->StartTransaction("Delete Camera Cut");

    plRemoveObjectCommand cmd;
    cmd.m_Object = m_SelectedCut;
    pHistory->AddCommand(cmd).AssertSuccess();

    pHistory->FinishTransaction();

    m_SelectedCut = plUuid();
    update();
    return;
  }

  if (e->key() == Qt::Key_Delete && m_SelectedClip.IsValid())
  {
    plCommandHistory* pHistory = GetDocument()->GetCommandHistory();
    pHistory->StartTransaction("Remove Clip");

    plRemoveObjectCommand cmd;
    cmd.m_Object = m_SelectedClip;
    pHistory->AddCommand(cmd).AssertSuccess();

    pHistory->FinishTransaction();

    m_SelectedClip = plUuid();
    update();
    return;
  }

  if (e->matches(QKeySequence::Copy))
  {
    CopySelectedKeys();
    return;
  }

  if (e->matches(QKeySequence::Paste))
  {
    PasteKeysAtPlayhead();
    update();
    return;
  }

  if (e->matches(QKeySequence::SelectAll))
  {
    SelectKeysInRect(QRect(0, RulerHeight, width(), height()));
    update();
    return;
  }

  // Nudge the whole selection by a frame, which is how a beat gets fixed without a mouse.
  if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right)
  {
    if (!m_SelectedKeys.IsEmpty())
    {
      const plInt64 iStep = plMath::Max<plInt64>(1, (plInt64)GetDocument()->GetTicksPerFrame());
      MoveSelectedKeys(e->key() == Qt::Key_Left ? -iStep : iStep);
      update();
      return;
    }
  }

  if (e->key() == Qt::Key_Delete && !m_SelectedKeys.IsEmpty())
  {
    DeleteSelectedKeys();

    m_SelectedKey.Invalidate();
    update();
    return;
  }

  QWidget::keyPressEvent(e);
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceTrackView);
