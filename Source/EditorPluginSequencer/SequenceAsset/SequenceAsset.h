#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EditorFramework/Assets/SimpleAssetDocument.h>
#include <EditorFramework/Document/GameObjectContextDocument.h>
#include <EditorFramework/Object/ObjectPropertyPath.h>
#include <EditorPluginAssets/ColorGradientAsset/ColorGradientAsset.h>
#include <EditorPluginSequencer/EditorPluginSequencerDLL.h>
#include <GuiFoundation/Widgets/CurveEditData.h>
#include <GuiFoundation/Widgets/EventTrackEditData.h>
#include <SequencerPlugin/Resources/SequenceResource.h>

/// \brief One animated property on a binding's object: which component / property, and the curve
/// or gradient that drives it. Same shape as plPropertyAnimationTrack so the GuiFoundation curve
/// and gradient editors bind to the payloads directly.
class plSequenceTrack : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceTrack, plReflectedClass);

public:
  plString m_sComponentType; ///< Empty to animate game object properties (position etc.)
  plString m_sPropertyPath;
  plEnum<plPropertyAnimTarget> m_Target;

  plSingleCurveData m_FloatCurve;
  plColorGradientAssetData m_ColorGradient;
};

/// \brief A span on a binding: something that starts, runs and stops - a sound trigger, an
/// animation clip, a nested sequence. What the payload names depends on the kind.
class plSequenceClip : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceClip, plReflectedClass);

public:
  plEnum<plSequenceClipKind> m_Kind;
  plString m_sPayload;     ///< audio trigger name / animation clip asset / sequence asset
  plString m_sStopPayload; ///< audio only: explicit stop trigger, empty to stop the play trigger
  plUInt64 m_uiStartTick = 0;
  plUInt64 m_uiDurationTicks = plSequenceTicksPerSecond; // a second, so a new clip is grabbable
  float m_fSpeed = 1.0f;
  plUInt64 m_uiBlendInTicks = 0;
  plUInt64 m_uiBlendOutTicks = 0;

  // The payloads are reflected by the derived types, not here, so each kind can name them for what
  // they are and carry the right editing attribute - a trigger dropdown is wrong for a sub-sequence.
  const char* GetPayload() const { return m_sPayload; }
  void SetPayload(const char* szValue) { m_sPayload = szValue; }
  const char* GetStopPayload() const { return m_sStopPayload; }
  void SetStopPayload(const char* szValue) { m_sStopPayload = szValue; }
};

/// \brief A clip that fires an audio trigger.
///
/// Its payloads are trigger names from the AudioSystem.Trigger dynamic enum - the same list
/// plAudioTriggerComponent offers - so the property grid picks from the project's actual triggers
/// instead of accepting any string and failing silently at runtime.
class plSequenceAudioClip : public plSequenceClip
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceAudioClip, plSequenceClip);

public:
  plSequenceAudioClip() { m_Kind = plSequenceClipKind::AudioTrigger; }
};

/// \brief A clip that plays an animation on the bound object.
class plSequenceAnimationClip : public plSequenceClip
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceAnimationClip, plSequenceClip);

public:
  plSequenceAnimationClip() { m_Kind = plSequenceClipKind::AnimationClip; }
};

/// \brief A clip that plays another sequence.
class plSequenceSubSequenceClip : public plSequenceClip
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceSubSequenceClip, plSequenceClip);

public:
  plSequenceSubSequenceClip() { m_Kind = plSequenceClipKind::SubSequence; }
};

/// \brief The concrete clip type for a kind, which is what carries the right payload editor.
const plRTTI* plSequenceClipTypeForKind(plSequenceClipKind::Enum kind);

/// \brief One scene object this sequence animates: display name + named-object search path,
/// owning the tracks that key its properties and the clips that fire on it.
class plSequenceBinding : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceBinding, plReflectedClass);

public:
  plSequenceBinding() = default;
  plSequenceBinding(const plSequenceBinding&) = delete;
  plSequenceBinding& operator=(const plSequenceBinding&) = delete;
  ~plSequenceBinding();

  plString m_sName;
  plString m_sObjectSearchSequence;

  plDynamicArray<plSequenceTrack*> m_Tracks;
  plDynamicArray<plSequenceClip*> m_Clips;

  /// Which clip lanes the outliner shows, so an empty lane can be added before its first clip.
  /// Editorial only - the runtime reads m_Clips and does not care which lane a clip was drawn on.
  /// Held as raw plSequenceClipKind values: an array of plEnum is not reflectable.
  plDynamicArray<plUInt8> m_ClipLanes;
};

/// \brief One camera cut: from this tick until the next cut, this camera is the view.
class plSequenceCameraCut : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceCameraCut, plReflectedClass);

public:
  plString m_sCameraName;       ///< Display name, the camera object's name.
  plString m_sCameraObjectPath; ///< Named-object search path of the camera object.
  plUInt64 m_uiStartTick = 0;

  /// How far this shot extends. A cut stays the active camera until the next one starts regardless,
  /// so this only decides the tail: drag it past the next cut's start and the overlap is the blend.
  /// Authoring a crossfade by overlapping two shots is how a timeline should express it - a blend
  /// length typed into a field is the same information with the shape hidden.
  plUInt64 m_uiDurationTicks = 2 * plSequenceTicksPerSecond;
};

/// \brief A named time anchor on the ruler.
///
/// Purely an authoring aid - it names a moment ("DoorOpen", "Beat_2") so keys, clips and cuts can
/// be snapped to it and so a long sequence can be navigated by landmark. Nothing is emitted to the
/// runtime resource; the event track is what carries timing out to the game.
class plSequenceMarker : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceMarker, plReflectedClass);

public:
  plString m_sName;
  plUInt64 m_uiTick = 0;
};

/// \brief The reflected, undo-mirrored data a Sequence asset stores.
///
/// Storage is ticks at plSequenceTicksPerSecond; the FPS is display/snapping only.
class plSequenceData : public plReflectedClass
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceData, plReflectedClass);

public:
  plSequenceData() = default;
  plSequenceData(const plSequenceData&) = delete;
  plSequenceData& operator=(const plSequenceData&) = delete;
  ~plSequenceData();

  plUInt16 m_uiFramesPerSecond = 30;
  plUInt64 m_uiDurationTicks = 5 * plSequenceTicksPerSecond;

  plDynamicArray<plSequenceBinding*> m_Bindings;
  plDynamicArray<plSequenceCameraCut*> m_CameraCuts;

  /// Clips attached to nothing in the scene - music, VO, stingers. They need no component to be
  /// placed in the level; the player creates what it needs.
  plDynamicArray<plSequenceClip*> m_Clips;

  /// Which sequence-level clip lanes the outliner shows. Same editorial-only role as the per-binding
  /// list; see plSequenceBinding::m_ClipLanes.
  plDynamicArray<plUInt8> m_ClipLanes;

  /// Named time anchors on the ruler. Editorial only, like the clip lanes.
  plDynamicArray<plSequenceMarker*> m_Markers;

  plEventTrackData m_EventTrack;
};

class plSequenceAssetDocument;

struct plSequenceAssetEvent
{
  enum class Type
  {
    DurationChanged,
    ScrubberPositionChanged,
    PlaybackChanged,
    ContextChanged, ///< the attached scene was re-cloned, so anything derived from it is stale
  };

  const plSequenceAssetDocument* m_pDocument;
  Type m_Type;
};

/// \brief Document for the Sequence asset type.
///
/// A game-object context document, like PropertyAnim: the sequence attaches to a scene document as
/// its editing context, so its bindings resolve against and preview into your actual level.
class plSequenceAssetDocument : public plSimpleAssetDocument<plSequenceData, plGameObjectContextDocument>
{
  using BaseClass = plSimpleAssetDocument<plSequenceData, plGameObjectContextDocument>;
  PL_ADD_DYNAMIC_REFLECTION(plSequenceAssetDocument, BaseClass);

public:
  plSequenceAssetDocument(plStringView sDocumentPath);
  ~plSequenceAssetDocument();

  plEvent<const plSequenceAssetEvent&> m_SequenceEvents;

  /// \brief Raised when the engine returns a rendered thumbnail for this document. Document
  /// messages are routed here by the framework, so this is the channel the thumbnail cache listens
  /// on rather than snooping the global connection event.
  plEvent<const plCreateThumbnailMsgToEditor&> m_ThumbnailArrived;

  // ---- timeline framing ----------------------------------------------------
  plTime GetDuration() const;
  plUInt64 GetDurationTicks() const;
  void SetDurationTicks(plUInt64 uiNumTicks); // undo-tracked

  /// \brief Ticks of one display frame at the sequence's FPS setting, for snapping.
  plUInt64 GetTicksPerFrame() const;

  // ---- scrubbing & playback (editor preview state, not undo-tracked) -------
  bool SetScrubberPosition(plUInt64 uiTick);
  plUInt64 GetScrubberPosition() const { return m_uiScrubberTickPos; }

  void SetPlayAnimation(bool bPlay);
  bool GetPlayAnimation() const { return m_bPlayAnimation; }
  void SetRepeatAnimation(bool bRepeat);
  bool GetRepeatAnimation() const { return m_bRepeatAnimation; }

  /// \brief Advances the playhead while playing. Called from the window's redraw tick.
  void ExecuteAnimationPlaybackStep();

  /// \name Thumbnail posing
  ///
  /// Poses the mirrored context scene at an arbitrary time without moving the playhead, so a cut's
  /// preview can be rendered showing the world as it is at that cut - not as it is right now. The
  /// engine renders from the mirrored objects, so posing them is what makes the image time-accurate.
  /// The pose has to be held until the render completes (it converges over several frames), which
  /// is visible in the viewport; keep the window short and always pair the two calls.
  ///@{
  void BeginThumbnailPose(plUInt64 uiTick);
  void EndThumbnailPose();
  bool IsThumbnailPoseActive() const { return m_uiPoseOverrideTick != plMath::MaxValue<plUInt64>(); }
  ///@}

  /// \brief The native (mirrored) track for a track document object - what evaluation reads.
  const plSequenceTrack* GetTrack(const plUuid& trackGuid) const;

  /// \brief The native clip object, of whichever concrete kind it is.
  const plSequenceClip* GetClip(const plUuid& clipGuid) const;

  /// \brief Resolves a binding's object path to the mirrored context object it animates.
  /// Returns nullptr while no context is attached or the path no longer matches.
  const plDocumentObject* ResolveBindingObject(const plUuid& bindingGuid) const;

  // ---- editing (all through commands, undo-safe) ---------------------------
  /// \brief Adds a binding for the given context object (a temporary mirrored from the attached
  /// scene). Name and search path are derived from the object. Returns the binding object's guid.
  plUuid AddBindingForContextObject(const plDocumentObject* pObject);

  /// \brief Adds a float track to a binding. Returns the track object's guid.
  plUuid AddTrack(const plUuid& bindingGuid, const char* szComponentType, const char* szPropertyPath, plPropertyAnimTarget::Enum target);

  /// \brief Adds a track for a property of an object in the attached context, creating the binding
  /// for it if none exists. The object may be any descendant of the context, not just one already
  /// bound - which is what lets the picker offer properties on a bound group's children.
  plUuid AddTrackForContextProperty(const plUuid& objectGuid, const char* szPropertyName, plPropertyAnimTarget::Enum target);

  /// \brief Points an existing binding at a different object, keeping its tracks and clips.
  ///
  /// The repair for a binding whose object was renamed, moved or replaced: redoing it from scratch
  /// would throw away everything keyed on it.
  plStatus RebindTo(const plUuid& bindingGuid, const plDocumentObject* pObject);

  /// \brief Display name and named-object search path for a context object, as a binding stores them.
  plStatus DeriveBindingPath(const plDocumentObject* pObject, plStringBuilder& out_sName, plStringBuilder& out_sSearchSequence) const;

  /// \brief Adds every axis of a vector or quaternion property as one group of tracks.
  ///
  /// Position, rotation and scale are authored as a whole value, not as three unrelated channels,
  /// so they are created together and the outliner shows them as one row.
  void AddVectorTrack(const plUuid& bindingGuid, const char* szComponentType, const char* szPropertyPath, bool bRotation, plUInt32 uiAxisCount = 3);

  /// \brief Shows a clip lane of this kind on the binding, if it does not have one already.
  /// \brief Adds a named time anchor. Returns the new marker's document object guid.
  plUuid AddMarker(plInt64 iTick, const char* szName);

  /// \brief Every marker's tick, so drags can snap to them.
  void GetMarkerTicks(plDynamicArray<plInt64>& out_ticks) const;

  void AddClipLane(const plUuid& bindingGuid, plSequenceClipKind::Enum kind);
  void RemoveClipLane(const plUuid& bindingGuid, plSequenceClipKind::Enum kind);

  /// \brief Fires and stops audio clips as the playhead crosses them, so pressing Play here sounds
  /// like the sequence will in game.
  ///
  /// Only while playing: firing a trigger on every scrub step would make dragging the playhead a
  /// machine gun. The engine process owns the actual audio, so this only sends it the edges.
  void UpdateClipPreview();
  void StopAllPreviewClips();

  // A vector group's row stands for all of its axis tracks, so a key on it is a key on every axis.
  // Each operates as one transaction, or a vector key would tear into per-axis keys that drift.
  void InsertVectorKey(plArrayPtr<const plUuid> trackGuids, plInt64 iTickX);
  void MoveVectorKey(plArrayPtr<const plUuid> trackGuids, plInt64 iFromTick, plInt64 iToTick);
  void RemoveVectorKey(plArrayPtr<const plUuid> trackGuids, plInt64 iTickX);

  plUuid InsertCurveCpAt(const plUuid& trackGuid, plInt64 iTickX, double fNewPosY);

  /// \brief Adds a clip to a binding, starting at the given tick.
  plUuid AddClip(const plUuid& bindingGuid, plSequenceClipKind::Enum kind, plUInt64 uiStartTick, const char* szPayload);

  /// \brief The sequence-level event track's document object, which the event dock edits.
  const plDocumentObject* GetEventTrackObject() const;
  plUuid InsertEventTrackCpAt(plInt64 iTickX, const char* szValue);

  // Colour tracks are driven by a gradient rather than a curve, so each channel has its own
  // control points. Find returns an invalid guid when nothing sits exactly on that tick.
  plUuid FindGradientColorCp(const plUuid& trackGuid, plInt64 iTickX) const;
  plUuid InsertGradientColorCpAt(const plUuid& trackGuid, plInt64 iTickX, const plColorGammaUB& color);
  plUuid FindGradientAlphaCp(const plUuid& trackGuid, plInt64 iTickX) const;
  plUuid InsertGradientAlphaCpAt(const plUuid& trackGuid, plInt64 iTickX, plUInt8 uiAlpha);
  plUuid FindGradientIntensityCp(const plUuid& trackGuid, plInt64 iTickX) const;
  plUuid InsertGradientIntensityCpAt(const plUuid& trackGuid, plInt64 iTickX, float fIntensity);

  /// \brief One colour key as the timeline presents it: the colour, alpha and intensity stops that
  /// share a tick, treated as a single RGBA+intensity sample.
  ///
  /// A gradient can key the three channels independently, which suits a gradient editor but not a
  /// cinematic timeline - there, one key is one colour. The sequencer therefore always writes the
  /// three channels together, and a key here is identified by its colour stop.
  struct ColorKey
  {
    plInt64 m_iTick = 0;
    plUuid m_ColorCp;              ///< always valid
    plUuid m_AlphaCp;              ///< invalid if the gradient has no alpha stop on this tick
    plUuid m_IntensityCp;          ///< invalid if the gradient has no intensity stop on this tick
    plColor m_Value = plColor::White; ///< linear, with alpha and intensity already folded in
  };

  void GetColorKeys(const plUuid& trackGuid, plDynamicArray<ColorKey>& out_keys) const;

  /// \brief Adds the colour key at a tick, or replaces the value of the one already there.
  void SetColorKey(const plUuid& trackGuid, plInt64 iTickX, const plColor& value);
  void MoveColorKey(const plUuid& trackGuid, plInt64 iFromTick, plInt64 iToTick);
  void RemoveColorKey(const plUuid& trackGuid, plInt64 iTickX);

  // ---- auto-key support (used by plSequenceObjectAccessor) ------------------
  /// \brief Whether edits to context objects record keys. Not persisted; a session-level mode.
  bool GetAutoKey() const { return m_bAutoKey; }
  void SetAutoKey(bool bEnable) { m_bAutoKey = bEnable; }

  /// \brief Whether the given property of a context object can be animated at all.
  plStatus CanAnimate(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target) const;

  /// \brief The (object search path, component type, property path) triple identifying a track,
  /// derived from a live context object + property.
  void GenerateTrackInfo(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plStringBuilder& out_sObjectSearchSequence,
    plStringBuilder& out_sComponentType, plStringBuilder& out_sPropertyPath) const;

  /// \brief Finds the track animating this property, or an invalid guid.
  plUuid FindTrack(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target) const;

  /// \brief Creates the track animating this property, adding its binding first if needed.
  plUuid CreateTrack(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target);

  /// \brief The curve colour for an axis - X red, Y green, Z blue, W grape. False for targets that
  /// are not one axis of several, which need no distinguishing colour.
  static bool CurveColorForTarget(plPropertyAnimTarget::Enum target, plColorGammaUB& out_color);

  /// \brief Colours a track's curve by which axis it drives, so three curves shown together in the
  /// curve editor can be told apart. Stored on the curve, which is where the editor reads it from.
  void ApplyAxisCurveColor(plObjectAccessorBase& ref_accessor, const plDocumentObject* pTrackObj, plPropertyAnimTarget::Enum target);

  /// \brief Finds the control point of a track at exactly the given tick, or an invalid guid.
  plUuid FindCurveCp(const plUuid& trackGuid, plInt64 iTickX) const;

  /// \brief Adds a camera cut at the playhead for the given context camera object.
  plUuid AddCameraCut(const plDocumentObject* pCameraObject, plUInt64 uiStartTick);

  /// \brief The context objects carrying a camera component, for the "add cut" picker.
  void CollectContextCameras(plDynamicArray<const plDocumentObject*>& out_cameras) const;

  /// \brief Whether the viewport shows the active cut camera's view while scrubbing.
  bool GetPilotCamera() const { return m_bPilotCamera; }
  void SetPilotCamera(bool bEnable);

  /// \brief World transform and field of view of the camera the active cut selects at the playhead.
  /// Inside an overlap with the previous cut this is the blend between the two, so the pilot view
  /// shows the crossfade the timeline is describing. Returns false when no cut is active or its
  /// camera cannot be resolved.
  bool GetActiveCutCamera(plTransform& out_transform, float& out_fFov) const
  {
    return GetCutCameraAtTick((plInt64)m_uiScrubberTickPos, out_transform, out_fFov);
  }

  /// \brief The same, at an arbitrary tick. What the cut lane's filmstrip renders from, so a
  /// thumbnail inside an overlap shows the crossfade rather than the incoming camera outright.
  bool GetCutCameraAtTick(plInt64 iAtTick, plTransform& out_transform, float& out_fFov) const;

  /// \brief World transform and FOV of one cut's camera, composed from the mirrored context.
  bool ResolveCutCamera(const plDocumentObject* pCutObj, plTransform& out_transform, float& out_fFov, plStringBuilder& out_sWhyNot) const;

  /// \brief Whether this cut's camera path still resolves to an object in the attached context.
  bool CanResolveCameraCut(const plUuid& cutGuid) const;

  /// \brief The context object this cut's camera path points at, or nullptr.
  const plDocumentObject* ResolveCameraCutObject(const plUuid& cutGuid) const;

  /// \brief The camera cut active at a tick - the last one to have started. Invalid before the
  /// first cut. Cuts with no camera path are skipped so a broken one cannot shadow a working one.
  plUuid FindCameraCutAtTick(plInt64 iTick) const;

  virtual plManipulatorSearchStrategy GetManipulatorSearchStrategy() const override
  {
    return plManipulatorSearchStrategy::ChildrenOfSelectedObject;
  }

protected:
  virtual plTransformStatus InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag, const plPlatformProfile* pAssetProfile,
    const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags) override;
  virtual void InitializeAfterLoading(bool bFirstTimeCreation) override;
  virtual void HandleEngineMessage(const plEditorEngineDocumentMsg* pMsg) override;

private:
  // The context-attach machinery clones the picked scene into this document as temporary objects.
  // plSequenceObjectManager rejects structure changes on temporaries, so it has to be unlocked for
  // exactly the span of a context rebuild - same wiring as PropertyAnim.
  void GameObjectContextEventHandler(const plGameObjectContextEvent& e);
  void TreeStructureEventHandler(const plDocumentObjectStructureEvent& e);
  void TreePropertyEventHandler(const plDocumentObjectPropertyEvent& e);

  // ---- preview: track evaluation into the context objects ------------------
  // Same machinery as PropertyAnim: each track resolves (via its binding's object path) to concrete
  // property references on the mirrored context objects. Values are written through the object
  // manager - no commands, so preview neither dirties the document nor pollutes undo - and the
  // remote mirror forwards them to the engine viewport. Removing the last track of a property
  // restores its initial value.
  struct PropertyValue
  {
    plVariant m_InitialValue;
    plHybridArray<plUuid, 3> m_Tracks;
  };
  struct PropertyKeyHash
  {
    PL_ALWAYS_INLINE static plUInt32 Hash(const plPropertyReference& key)
    {
      return plHashingUtils::xxHash32(&key.m_Object, sizeof(plUuid)) + plHashingUtils::xxHash32(&key.m_pProperty, sizeof(const plAbstractProperty*)) +
             (plUInt32)key.m_Index.ComputeHash();
    }

    PL_ALWAYS_INLINE static bool Equal(const plPropertyReference& a, const plPropertyReference& b)
    {
      return a.m_Object == b.m_Object && a.m_pProperty == b.m_pProperty && a.m_Index == b.m_Index;
    }
  };

  void RebuildMapping();
  void ClearMapping();
  void AddTrackMapping(const plUuid& trackGuid);
  void RemoveTrackMapping(const plUuid& trackGuid);
  plStatus FindTrackKeys(const char* szObjectSearchSequence, const char* szComponentType, const char* szPropertyPath,
    plHybridArray<plPropertyReference, 1>& keys) const;
  void ApplyAnimation();
  void ApplyAnimation(const plPropertyReference& key, const PropertyValue& value);

  plHashTable<plPropertyReference, PropertyValue, PropertyKeyHash> m_PropertyTable;
  plHashTable<plUuid, plHybridArray<plPropertyReference, 1>> m_TrackTable;

  /// The cached context scene is a clone taken when the context was set, so edits to the source
  /// scene are invisible here until it is saved - at which point this re-clones it.
  void DocumentEventHandler(const plDocumentEvent& e);

  /// The time evaluation actually uses: the playhead, unless a thumbnail pose is overriding it.
  plUInt64 EffectiveTick() const { return IsThumbnailPoseActive() ? m_uiPoseOverrideTick : m_uiScrubberTickPos; }

  plUInt64 m_uiScrubberTickPos = 0;
  plUInt64 m_uiPoseOverrideTick = plMath::MaxValue<plUInt64>();
  bool m_bPlayAnimation = false;
  bool m_bRepeatAnimation = false;
  bool m_bAutoKey = true;

  /// Audio clips currently sounding in the engine process, mapped to the object they were started
  /// on. The playhead crossing a clip's edge is what starts and stops it rather than every frame
  /// re-triggering it, and the owner is kept because stopping needs it after the clip has ended.
  plMap<plUuid, plUuid> m_RunningPreviewClips;
  bool m_bPilotCamera = true;
  mutable plString m_sLastPilotFailure; ///< so a per-frame failure is reported once, not every frame
  plTime m_LastFrameTime;
};
