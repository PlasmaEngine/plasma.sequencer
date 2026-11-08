#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <EditorUi/Runtime.h>

/// \brief Renders and caches preview images of the sequence at given ticks, so a camera cut span
/// reads as a filmstrip of what actually happens during the shot.
///
/// Images are keyed by tick, not by cut: the camera is whichever cut is active at that tick, so one
/// cache serves every tile and moving a cut simply changes which images the lane asks for.
///
/// They are produced by the engine's existing thumbnail pipeline: the scene is posed at the tick,
/// a config message nominates the camera, plCreateThumbnailMsgToEngine triggers an offscreen
/// render, and the reply carries raw RGBA back. That pipeline renders one thumbnail at a time - the
/// engine asserts if a second is requested while one is in progress - so requests are queued and
/// issued strictly serially, and the strip fills in progressively.
class plSequenceThumbnailCache
{
public:
  explicit plSequenceThumbnailCache(plSequenceAssetDocument* pDocument);
  ~plSequenceThumbnailCache();

  static constexpr int ThumbnailWidth = 160;
  static constexpr int ThumbnailHeight = 90; // 16:9, the aspect a cut span is usually read at

  /// \brief A rendered image: ThumbnailWidth x ThumbnailHeight pixels, row by row from the top; empty when the render
  /// failed. m_uiSerial tells images apart, across all caches (a re-render of the same tick gets a new one).
  struct Thumbnail
  {
    plDynamicArray<plColorGammaUB> m_Pixels;
    plUInt32 m_uiSerial = 0;
  };

  /// \brief The cached image of the sequence at a tick, or nullptr if it is not rendered yet (or its render failed).
  /// Asking for a missing one schedules it.
  const Thumbnail* GetThumbnail(plInt64 iTick);

  /// \brief Drops cached images, e.g. after the context scene changed under us.
  void InvalidateAll();

  /// A strip fills in one render at a time, so an unbounded queue would spend minutes catching up
  /// with a drag that is already over. Excess requests are dropped and re-made on the next repaint,
  /// which naturally prioritises whatever is on screen now.
  static constexpr plUInt32 MaxPendingRequests = 24;

  /// How long the document has to sit still before rendering resumes. Rendering a thumbnail poses
  /// the whole scene at that tick, which fights any edit in progress and costs a frame each - so
  /// nothing is rendered until the user stops.
  static constexpr int IdleDelayMs = 500;

  /// Called when an image arrived or the cached ones went stale, so the strip repaints.
  plDelegate<void()> m_OnThumbnailReady;

private:
  void ThumbnailArrivedHandler(const plCreateThumbnailMsgToEditor& msg);
  void DocumentEventHandler(const plSequenceAssetEvent& e);
  void CommandHistoryEventHandler(const plCommandHistoryEvent& e);
  void ProcessNextRequest();
  void ReportReady();

  /// Postpones rendering, and abandons anything in flight so the scene stops being posed.
  void DeferRendering();

  /// (Re)starts the wait before rendering resumes.
  void StartIdleTimer();
  bool IsIdleTimerActive() const;

  /// Remembers a failed render, so it is not retried every repaint.
  void CacheFailure(plInt64 iTick);

  plSequenceAssetDocument* m_pDocument = nullptr;
  plHashTable<plInt64, Thumbnail> m_Cache;
  plDeque<plInt64> m_Pending;
  plInt64 m_iInFlight = -1; ///< -1 when nothing is rendering; ticks are never negative
  plTime m_RequestSentAt;
  static plUInt32 s_uiNextSerial;
  plUInt32 m_uiIdleTimer = 0; ///< 0 while it does not wait
  plEditorUiTimerGroup m_Timers;
};
