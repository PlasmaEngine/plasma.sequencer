#pragma once

#include <Core/ResourceManager/Resource.h>
#include <Foundation/Time/Time.h>
#include <SequencerPlugin/SequencerPluginDLL.h>

using plSequenceResourceHandle = plTypedResourceHandle<class plSequenceResource>;

/// \brief Fixed tick rate sequence timelines are stored at. Same rate PropertyAnim uses, so frame
/// rates of 24/25/30/60/90/120 fps all map to whole tick counts.
inline constexpr plUInt32 plSequenceTicksPerSecond = 4800;

/// \brief The compiled, runtime-consumable form of a Sequence asset.
///
/// P0 carries only the timeline framing. Bindings and typed tracks (transform, property, color,
/// event, camera cut, clip) land with the P1/P2 milestones of the plan; the stream is versioned so
/// those extend rather than replace this format.
struct PL_SEQUENCER_DLL plSequenceResourceDescriptor
{
  plUInt16 m_uiFramesPerSecond = 30; ///< Display / snapping rate. Storage is always ticks.
  plUInt64 m_uiDurationTicks = 5 * plSequenceTicksPerSecond;

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
