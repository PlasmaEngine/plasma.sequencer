#include <SequencerPlugin/SequencerPluginPCH.h>

#include <Foundation/Utilities/AssetFileHeader.h>
#include <SequencerPlugin/Resources/SequenceResource.h>

// clang-format off
PL_BEGIN_STATIC_REFLECTED_ENUM(plSequenceClipKind, 1)
  PL_ENUM_CONSTANT(plSequenceClipKind::AudioTrigger),
  PL_ENUM_CONSTANT(plSequenceClipKind::AnimationClip),
  PL_ENUM_CONSTANT(plSequenceClipKind::SubSequence),
PL_END_STATIC_REFLECTED_ENUM;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceResource, 1, plRTTIDefaultAllocator<plSequenceResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plSequenceResource);
// clang-format on

void plSequenceResourceDescriptor::Save(plStreamWriter& inout_stream) const
{
  constexpr plUInt8 uiVersion = 5;
  inout_stream << uiVersion;

  inout_stream << m_uiFramesPerSecond;
  inout_stream << m_uiDurationTicks;

  // Version 2: bindings with float/color tracks + the sequence event track.
  // Version 3+: camera cuts, clip tracks (see Documentation/Sequencer/Plan.html §4).
  // Version 5: camera cut blend-in.
  inout_stream << m_Bindings.GetCount();
  for (const plSequenceBindingDesc& binding : m_Bindings)
  {
    inout_stream << binding.m_sName;
    inout_stream << binding.m_sObjectSearchSequence;

    inout_stream << binding.m_Tracks.GetCount();
    for (const plSequenceTrackDesc& track : binding.m_Tracks)
    {
      inout_stream << track.m_sComponentType;
      inout_stream << track.m_sPropertyPath;
      inout_stream << track.m_Target;
      track.m_FloatCurve.Save(inout_stream);
      track.m_ColorGradient.Save(inout_stream);
    }
  }

  m_EventTrack.Save(inout_stream);

  // version 3: camera cuts
  inout_stream << m_CameraCuts.GetCount();
  for (const plSequenceCameraCutDesc& cut : m_CameraCuts)
  {
    inout_stream << cut.m_sCameraObjectPath;
    inout_stream << cut.m_uiStartTick;
    inout_stream << cut.m_uiBlendInTicks; // version 5
  }

  // version 4: clips, per binding and sequence-level
  auto saveClips = [&inout_stream](const plDynamicArray<plSequenceClipDesc>& clips)
  {
    inout_stream << clips.GetCount();
    for (const plSequenceClipDesc& clip : clips)
    {
      inout_stream << clip.m_Kind;
      inout_stream << clip.m_sPayload;
      inout_stream << clip.m_sStopPayload;
      inout_stream << clip.m_uiStartTick;
      inout_stream << clip.m_uiDurationTicks;
      inout_stream << clip.m_fSpeed;
      inout_stream << clip.m_uiBlendInTicks;
      inout_stream << clip.m_uiBlendOutTicks;
    }
  };

  for (const plSequenceBindingDesc& binding : m_Bindings)
  {
    saveClips(binding.m_Clips);
  }

  saveClips(m_Clips);
}

plResult plSequenceResourceDescriptor::Load(plStreamReader& inout_stream)
{
  plUInt8 uiVersion = 0;
  inout_stream >> uiVersion;

  if (uiVersion < 1 || uiVersion > 5)
  {
    plLog::Error("Unsupported plSequenceResourceDescriptor version {0}", uiVersion);
    return PL_FAILURE;
  }

  inout_stream >> m_uiFramesPerSecond;
  inout_stream >> m_uiDurationTicks;

  m_Bindings.Clear();
  m_CameraCuts.Clear();
  m_EventTrack.Clear();

  if (uiVersion >= 2)
  {
    plUInt32 uiNumBindings = 0;
    inout_stream >> uiNumBindings;
    m_Bindings.Reserve(uiNumBindings);

    for (plUInt32 b = 0; b < uiNumBindings; ++b)
    {
      plSequenceBindingDesc& binding = m_Bindings.ExpandAndGetRef();
      inout_stream >> binding.m_sName;
      inout_stream >> binding.m_sObjectSearchSequence;

      plUInt32 uiNumTracks = 0;
      inout_stream >> uiNumTracks;
      binding.m_Tracks.Reserve(uiNumTracks);

      for (plUInt32 t = 0; t < uiNumTracks; ++t)
      {
        plSequenceTrackDesc& track = binding.m_Tracks.ExpandAndGetRef();
        inout_stream >> track.m_sComponentType;
        inout_stream >> track.m_sPropertyPath;
        inout_stream >> track.m_Target;
        track.m_FloatCurve.Load(inout_stream);
        track.m_ColorGradient.Load(inout_stream);
      }
    }

    m_EventTrack.Load(inout_stream);
  }

  if (uiVersion >= 3)
  {
    plUInt32 uiNumCuts = 0;
    inout_stream >> uiNumCuts;
    m_CameraCuts.Reserve(uiNumCuts);

    for (plUInt32 c = 0; c < uiNumCuts; ++c)
    {
      plSequenceCameraCutDesc& cut = m_CameraCuts.ExpandAndGetRef();
      inout_stream >> cut.m_sCameraObjectPath;
      inout_stream >> cut.m_uiStartTick;

      if (uiVersion >= 5)
        inout_stream >> cut.m_uiBlendInTicks; // older cuts are hard cuts, which is the default
    }
  }

  if (uiVersion >= 4)
  {
    auto loadClips = [&inout_stream](plDynamicArray<plSequenceClipDesc>& clips)
    {
      plUInt32 uiNumClips = 0;
      inout_stream >> uiNumClips;
      clips.Reserve(uiNumClips);

      for (plUInt32 c = 0; c < uiNumClips; ++c)
      {
        plSequenceClipDesc& clip = clips.ExpandAndGetRef();
        inout_stream >> clip.m_Kind;
        inout_stream >> clip.m_sPayload;
        inout_stream >> clip.m_sStopPayload;
        inout_stream >> clip.m_uiStartTick;
        inout_stream >> clip.m_uiDurationTicks;
        inout_stream >> clip.m_fSpeed;
        inout_stream >> clip.m_uiBlendInTicks;
        inout_stream >> clip.m_uiBlendOutTicks;
      }
    };

    for (plSequenceBindingDesc& binding : m_Bindings)
    {
      loadClips(binding.m_Clips);
    }

    loadClips(m_Clips);
  }

  return PL_SUCCESS;
}

plSequenceResource::plSequenceResource()
  : plResource(DoUpdate::OnAnyThread, 1)
{
}

PL_RESOURCE_IMPLEMENT_CREATEABLE(plSequenceResource, plSequenceResourceDescriptor)
{
  m_Descriptor = descriptor;

  plResourceLoadDesc result;
  result.m_uiQualityLevelsDiscardable = 0;
  result.m_uiQualityLevelsLoadable = 0;
  result.m_State = plResourceState::Loaded;
  return result;
}

plResourceLoadDesc plSequenceResource::UnloadData(Unload WhatToUnload)
{
  PL_IGNORE_UNUSED(WhatToUnload);
  m_Descriptor = plSequenceResourceDescriptor();

  plResourceLoadDesc result;
  result.m_uiQualityLevelsDiscardable = 0;
  result.m_uiQualityLevelsLoadable = 0;
  result.m_State = plResourceState::Unloaded;
  return result;
}

plResourceLoadDesc plSequenceResource::UpdateContent(plStreamReader* pStream)
{
  PL_LOG_BLOCK("plSequenceResource::UpdateContent", GetResourceIdOrDescription());

  plResourceLoadDesc result;
  result.m_uiQualityLevelsDiscardable = 0;
  result.m_uiQualityLevelsLoadable = 0;

  if (pStream == nullptr)
  {
    result.m_State = plResourceState::LoadedResourceMissing;
    return result;
  }

  // The file-based loader prepends the absolute path, then the asset header, then the payload.
  plStringBuilder absolutePath;
  (*pStream) >> absolutePath;

  plAssetFileHeader assetHeader;
  assetHeader.Read(*pStream).IgnoreResult();

  if (m_Descriptor.Load(*pStream).Failed())
  {
    result.m_State = plResourceState::LoadedResourceMissing;
    return result;
  }

  result.m_State = plResourceState::Loaded;
  return result;
}

void plSequenceResource::UpdateMemoryUsage(MemoryUsage& out_memoryUsage)
{
  out_memoryUsage.m_uiMemoryCPU = sizeof(plSequenceResource) + sizeof(plSequenceResourceDescriptor);
  out_memoryUsage.m_uiMemoryGPU = 0;
}

PL_STATICLINK_FILE(SequencerPlugin, SequencerPlugin_Resources_Implementation_SequenceResource);
