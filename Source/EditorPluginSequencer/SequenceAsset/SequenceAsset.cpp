#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceObjectManager.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceData, 1, plRTTIDefaultAllocator<plSequenceData>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("FPS", m_uiFramesPerSecond)->AddAttributes(new plDefaultValueAttribute(30)),
    PL_MEMBER_PROPERTY("Duration", m_uiDurationTicks)->AddAttributes(new plDefaultValueAttribute(24000)), // 5 s at plSequenceTicksPerSecond (4800)
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceAssetDocument, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plSequenceAssetDocument::plSequenceAssetDocument(plStringView sDocumentPath)
  : BaseClass(PL_DEFAULT_NEW(plSequenceObjectManager), sDocumentPath, plAssetDocEngineConnection::FullObjectMirroring)
{
}

plTime plSequenceAssetDocument::GetDuration() const
{
  return plTime::MakeFromSeconds((double)GetProperties()->m_uiDurationTicks / (double)plSequenceTicksPerSecond);
}

plTransformStatus plSequenceAssetDocument::InternalTransformAsset(plStreamWriter& stream, plStringView sOutputTag,
  const plPlatformProfile* pAssetProfile, const plAssetFileHeader& AssetHeader, plBitflags<plTransformFlags> transformFlags)
{
  PL_IGNORE_UNUSED(sOutputTag);
  PL_IGNORE_UNUSED(pAssetProfile);
  PL_IGNORE_UNUSED(AssetHeader);
  PL_IGNORE_UNUSED(transformFlags);

  const plSequenceData* pData = GetProperties();

  plSequenceResourceDescriptor descriptor;
  descriptor.m_uiFramesPerSecond = pData->m_uiFramesPerSecond;
  descriptor.m_uiDurationTicks = pData->m_uiDurationTicks;

  // P1+: compile bindings and tracks into the descriptor here.
  descriptor.Save(stream);

  return plTransformStatus(PL_SUCCESS);
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceAsset);
