#pragma once

#include <Core/ResourceManager/Resource.h>
#include <Foundation/Time/Time.h>
#include <Foundation/Tracks/ColorGradient.h>
#include <Foundation/Tracks/Curve1D.h>
#include <Foundation/Tracks/EventTrack.h>
#include <GameEngine/Animation/PropertyAnimResource.h>
#include <SequencerPlugin/SequencerPluginDLL.h>

using plSequenceResourceHandle = plTypedResourceHandle<class plSequenceResource>;

/// \brief Fixed tick rate sequence timelines are stored at. Same rate PropertyAnim uses, so frame
/// rates of 24/25/30/60/90/120 fps all map to whole tick counts.
inline constexpr plUInt32 plSequenceTicksPerSecond = 4800;

/// \brief One compiled track: a property on the bound object (or one of its components), driven by
/// a float curve or a color gradient depending on the target.
struct PL_SEQUENCER_DLL plSequenceTrackDesc
{
  plString m_sComponentType; ///< Empty to animate game object properties (position etc.)
  plString m_sPropertyPath;
  plEnum<plPropertyAnimTarget> m_Target;

  plCurve1D m_FloatCurve;        ///< Used for all targets except Color.
  plColorGradient m_ColorGradient; ///< Used for the Color target.
};

/// \brief What a clip does when the playhead is inside it.
struct PL_SEQUENCER_DLL plSequenceClipKind
{
  using StorageType = plUInt8;

  enum Enum
  {
    AudioTrigger,  ///< fires an audio trigger by name on the bound object's plAudioTriggerComponent
    AnimationClip, ///< plays a skeletal animation on the bound object
    SubSequence,   ///< plays another sequence asset

    Default = AudioTrigger,
  };
};

PL_DECLARE_REFLECTABLE_TYPE(PL_SEQUENCER_DLL, plSequenceClipKind);

/// \brief A span on a clip track: something that starts, runs for a while, and stops.
///
/// The payload is deliberately a string rather than a typed reference, because what it names
/// depends on the kind - an audio trigger name, an animation clip asset, a nested sequence.
struct PL_SEQUENCER_DLL plSequenceClipDesc
{
  plEnum<plSequenceClipKind> m_Kind;
  plString m_sPayload;    ///< trigger name / asset reference, per kind
  plString m_sStopPayload; ///< audio only: explicit stop trigger, empty to stop the play trigger
  plUInt64 m_uiStartTick = 0;
  plUInt64 m_uiDurationTicks = 0;
  float m_fSpeed = 1.0f;
  plUInt64 m_uiBlendInTicks = 0;
  plUInt64 m_uiBlendOutTicks = 0;

  plUInt64 GetEndTick() const { return m_uiStartTick + m_uiDurationTicks; }
};

/// \brief One compiled binding: a scene object (found by named-object search from the player's
/// root) and the tracks and clips that drive it.
struct PL_SEQUENCER_DLL plSequenceBindingDesc
{
  plString m_sName;                 ///< Display name, kept for diagnostics.
  plString m_sObjectSearchSequence; ///< Sequence of named objects to search for the target.

  plDynamicArray<plSequenceTrackDesc> m_Tracks;
  plDynamicArray<plSequenceClipDesc> m_Clips; ///< sorted by start tick
};

/// \brief One camera cut: from this tick until the next cut (or the end), this camera is the view.
struct PL_SEQUENCER_DLL plSequenceCameraCutDesc
{
  plString m_sCameraObjectPath; ///< Named-object search path of the camera object.
  plUInt64 m_uiStartTick = 0;

  /// How long this cut eases in from the previous one. Zero is a hard cut, which is the default -
  /// a blend is a deliberate choice, not something a cut should acquire by accident.
  plUInt64 m_uiBlendInTicks = 0;
};

/// \brief The compiled, runtime-consumable form of a Sequence asset.
struct PL_SEQUENCER_DLL plSequenceResourceDescriptor
{
  plUInt16 m_uiFramesPerSecond = 30; ///< Display / snapping rate. Storage is always ticks.
  plUInt64 m_uiDurationTicks = 5 * plSequenceTicksPerSecond;

  plDynamicArray<plSequenceBindingDesc> m_Bindings;

  /// Clips not attached to any scene object - music, VO, stingers. They play on the player
  /// component's own object, so a cutscene can carry audio the level knows nothing about.
  plDynamicArray<plSequenceClipDesc> m_Clips;

  plDynamicArray<plSequenceCameraCutDesc> m_CameraCuts; ///< sorted by start tick
  plEventTrack m_EventTrack;

  plTime GetDuration() const { return plTime::MakeFromSeconds((double)m_uiDurationTicks / (double)plSequenceTicksPerSecond); }

  void Save(plStreamWriter& inout_stream) const;
  plResult Load(plStreamReader& inout_stream);
};

/// \brief Loadable resource for Sequence assets, played back by plSequencePlayerComponent.
class PL_SEQUENCER_DLL plSequenceResource : public plResource
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceResource, plResource);
  PL_RESOURCE_DECLARE_COMMON_CODE(plSequenceResource);
  PL_RESOURCE_DECLARE_CREATEABLE(plSequenceResource, plSequenceResourceDescriptor);

public:
  plSequenceResource();

  const plSequenceResourceDescriptor& GetDescriptor() const { return m_Descriptor; }

private:
  virtual plResourceLoadDesc UnloadData(Unload WhatToUnload) override;
  virtual plResourceLoadDesc UpdateContent(plStreamReader* pStream) override;
  virtual void UpdateMemoryUsage(MemoryUsage& out_memoryUsage) override;

  plSequenceResourceDescriptor m_Descriptor;
};
