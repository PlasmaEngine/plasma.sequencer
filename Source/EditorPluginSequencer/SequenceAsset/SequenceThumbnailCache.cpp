#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceThumbnailCache.h>

plUInt32 plSequenceThumbnailCache::s_uiNextSerial = 1;

plSequenceThumbnailCache::plSequenceThumbnailCache(plSequenceAssetDocument* pDocument)
  : m_pDocument(pDocument)
{
  // Document-scoped engine messages are routed to the document, not broadcast on the connection's
  // general event, so this listens to the document.
  m_pDocument->m_ThumbnailArrived.AddEventHandler(plMakeDelegate(&plSequenceThumbnailCache::ThumbnailArrivedHandler, this));
  m_pDocument->m_SequenceEvents.AddEventHandler(plMakeDelegate(&plSequenceThumbnailCache::DocumentEventHandler, this));
  m_pDocument->GetCommandHistory()->m_Events.AddEventHandler(plMakeDelegate(&plSequenceThumbnailCache::CommandHistoryEventHandler, this));
}

plSequenceThumbnailCache::~plSequenceThumbnailCache()
{
  m_Timers.StopAll();

  m_pDocument->GetCommandHistory()->m_Events.RemoveEventHandler(plMakeDelegate(&plSequenceThumbnailCache::CommandHistoryEventHandler, this));
  m_pDocument->m_SequenceEvents.RemoveEventHandler(plMakeDelegate(&plSequenceThumbnailCache::DocumentEventHandler, this));
  m_pDocument->m_ThumbnailArrived.RemoveEventHandler(plMakeDelegate(&plSequenceThumbnailCache::ThumbnailArrivedHandler, this));
}

void plSequenceThumbnailCache::ReportReady()
{
  if (m_OnThumbnailReady.IsValid())
    m_OnThumbnailReady();
}

void plSequenceThumbnailCache::StartIdleTimer()
{
  m_Timers.Restart(m_uiIdleTimer, plTime::MakeFromMilliseconds(IdleDelayMs), [this]()
    {
    m_uiIdleTimer = 0;
    ProcessNextRequest(); });
}

bool plSequenceThumbnailCache::IsIdleTimerActive() const
{
  return m_uiIdleTimer != 0 && m_Timers.IsActive(m_uiIdleTimer);
}

void plSequenceThumbnailCache::CommandHistoryEventHandler(const plCommandHistoryEvent& e)
{
  PL_IGNORE_UNUSED(e);

  // Any document change at all pushes rendering back. A gizmo drag is a stream of these, so this is
  // what keeps the strip from posing the scene out from under the transform being edited.
  DeferRendering();
}

void plSequenceThumbnailCache::DeferRendering()
{
  // Abandon whatever is rendering: its pose is holding the scene at some other tick, and the reply
  // would be a picture of a scene that has since been edited anyway. Dropped rather than cached,
  // so it is re-rendered once things are still.
  if (m_iInFlight >= 0)
  {
    m_pDocument->EndThumbnailPose();
    m_iInFlight = -1;
  }

  StartIdleTimer();
}

void plSequenceThumbnailCache::DocumentEventHandler(const plSequenceAssetEvent& e)
{
  // A cached image is a picture of the scene as it was when it was rendered. Re-cloning the
  // context replaces every object it was a picture of, so everything cached is now stale.
  if (e.m_Type == plSequenceAssetEvent::Type::ContextChanged)
  {
    InvalidateAll();
    ReportReady(); // repaint, which re-requests what is on screen
    return;
  }

  // Scrubbing and playback both move the scene continuously, and a thumbnail pose overrides where
  // the scene sits - so rendering waits for the playhead to settle too.
  if (e.m_Type == plSequenceAssetEvent::Type::ScrubberPositionChanged || e.m_Type == plSequenceAssetEvent::Type::PlaybackChanged)
  {
    DeferRendering();
  }
}

void plSequenceThumbnailCache::InvalidateAll()
{
  m_Cache.Clear();
  m_Pending.Clear();
  DeferRendering(); // an in-flight render is of a scene that no longer exists
}

const plSequenceThumbnailCache::Thumbnail* plSequenceThumbnailCache::GetThumbnail(plInt64 iTick)
{
  if (const Thumbnail* pThumbnail = m_Cache.GetValue(iTick))
    return pThumbnail->m_Pixels.IsEmpty() ? nullptr : pThumbnail;

  // not rendered yet - queue it, unless it is already queued or being rendered right now
  if (iTick != m_iInFlight && !m_Pending.Contains(iTick) && m_Pending.GetCount() < MaxPendingRequests)
  {
    m_Pending.PushBack(iTick);

    // Queued, not started. Rendering waits for the idle timer, so a lane being repainted every
    // frame during a drag simply accumulates what it wants and gets it once the drag ends.
    if (!IsIdleTimerActive() && m_iInFlight < 0)
      StartIdleTimer();
  }

  return nullptr;
}

void plSequenceThumbnailCache::CacheFailure(plInt64 iTick)
{
  Thumbnail& failed = m_Cache[iTick];
  failed.m_Pixels.Clear();
  failed.m_uiSerial = s_uiNextSerial++;
}

void plSequenceThumbnailCache::ProcessNextRequest()
{
  // A disk render has taken the engine's one offscreen-render slot. Stand down entirely rather than
  // racing it - both would receive each other's frames.
  if (m_pDocument->IsThumbnailRenderingSuspended())
    return;

  // Something happened since the last render finished, so the queue waits for the timer rather than
  // chaining straight on. Without this, draining the queue would resume mid-edit.
  if (IsIdleTimerActive())
    return;

  // The engine renders one thumbnail at a time and asserts if asked for a second, so this waits.
  // A request that never gets answered - engine restart mid-render, say - would otherwise wedge the
  // queue forever, hence the timeout.
  if (m_iInFlight >= 0)
  {
    if (plTime::Now() - m_RequestSentAt < plTime::MakeFromSeconds(10))
      return;

    // the render never answered - drop the pose, or the scene stays stuck at that tick
    m_pDocument->EndThumbnailPose();
    m_iInFlight = -1;
  }

  if (m_Pending.IsEmpty())
    return;

  if (!plEditorEngineProcessConnection::GetSingleton()->IsProcessCrashed())
  {
    const plInt64 iTick = m_Pending.PeekFront();
    m_Pending.PopFront();

    if (!m_pDocument->FindCameraCutAtTick(iTick).IsValid())
    {
      CacheFailure(iTick);
      ProcessNextRequest();
      return;
    }

    m_iInFlight = iTick;
    m_RequestSentAt = plTime::Now();

    // Pose the scene at this tick before asking for the render, so the image shows what the shot
    // looks like then rather than whatever the playhead happens to be sitting on. Held until the
    // reply arrives - the render converges over several frames - then restored.
    m_pDocument->BeginThumbnailPose((plUInt64)plMath::Max<plInt64>(0, iTick));

    // Read AFTER posing: the camera may itself be animated by this sequence. This is the same
    // blend-aware resolve the pilot uses, so a tile inside an overlap shows the crossfade rather
    // than the incoming camera outright - which is why a transform is sent rather than an object
    // to look at, since during a blend the view is at no object's transform at all.
    plTransform cameraTransform;
    float fFov = 60.0f;

    if (!m_pDocument->GetCutCameraAtTick(iTick, cameraTransform, fFov))
    {
      m_pDocument->EndThumbnailPose();
      m_iInFlight = -1;
      CacheFailure(iTick);
      ProcessNextRequest();
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

    plCreateThumbnailMsgToEngine thumbMsg;
    thumbMsg.m_DocumentGuid = m_pDocument->GetGuid();
    thumbMsg.m_uiWidth = ThumbnailWidth;
    thumbMsg.m_uiHeight = ThumbnailHeight;
    m_pDocument->GetEditorEngineConnection()->SendMessage(&thumbMsg);

    plLog::Info("Sequence thumbnail: requested render at tick {0}", iTick);
  }
}

void plSequenceThumbnailCache::ThumbnailArrivedHandler(const plCreateThumbnailMsgToEditor& msg)
{
  // Nothing of ours is outstanding, so this frame belongs to whoever else asked - a disk render.
  if (m_iInFlight < 0)
    return;

  const plInt64 iTick = m_iInFlight;
  m_iInFlight = -1;

  // the image is captured, so the scene can go back to the playhead
  m_pDocument->EndThumbnailPose();

  const plUInt32 uiPixels = (plUInt32)ThumbnailWidth * ThumbnailHeight;
  const plUInt32 uiExpected = uiPixels * 4;

  plLog::Info("Sequence thumbnail: received {0} bytes, expected {1}", msg.m_ThumbnailData.GetCount(), uiExpected);

  if (msg.m_ThumbnailData.GetCount() == uiExpected)
  {
    // the buffer is raw RGBA and outlives this call only as long as the message does, so copy it
    Thumbnail& thumbnail = m_Cache[iTick];
    thumbnail.m_Pixels.SetCountUninitialized(uiPixels);
    plMemoryUtils::Copy(reinterpret_cast<plUInt8*>(thumbnail.m_Pixels.GetData()), msg.m_ThumbnailData.GetData(), uiExpected);
    thumbnail.m_uiSerial = s_uiNextSerial++;

    ReportReady();
  }
  else
  {
    CacheFailure(iTick);
  }

  ProcessNextRequest();
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceThumbnailCache);
