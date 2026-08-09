#include <EnginePluginSequencer/EnginePluginSequencerPCH.h>

#include <EnginePluginSequencer/SequenceAsset/SequenceContext.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceContext, 1, plRTTIDefaultAllocator<plSequenceContext>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_CONSTANT_PROPERTY("DocumentType", (const char*) "Sequence"),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plSequenceContext::plSequenceContext() = default;

PL_STATICLINK_FILE(EnginePluginSequencer, EnginePluginSequencer_SequenceAsset_SequenceContext);
