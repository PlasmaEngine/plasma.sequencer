#include <SequencerPlugin/SequencerPluginPCH.h>

#include <Foundation/Utilities/AssetFileHeader.h>
#include <SequencerPlugin/Resources/SequenceResource.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceResource, 1, plRTTIDefaultAllocator<plSequenceResource>)
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_RESOURCE_IMPLEMENT_COMMON_CODE(plSequenceResource);
// clang-format on

void plSequenceResourceDescriptor::Save(plStreamWriter& inout_stream) const
{
  constexpr plUInt8 uiVersion = 1;
  inout_stream << uiVersion;

  inout_stream << m_uiFramesPerSecond;
  inout_stream << m_uiDurationTicks;

  // Version 2+: bindings, tracks, camera cuts, events (see Documentation/Sequencer/Plan.html §4).
}

plResult plSequenceResourceDescriptor::Load(plStreamReader& inout_stream)
{
  plUInt8 uiVersion = 0;
  inout_stream >> uiVersion;

  if (uiVersion != 1)
  {
    plLog::Error("Unsupported plSequenceResourceDescriptor version {0}", uiVersion);
    return PL_FAILURE;
  }

  inout_stream >> m_uiFramesPerSecond;
  inout_stream >> m_uiDurationTicks;

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
