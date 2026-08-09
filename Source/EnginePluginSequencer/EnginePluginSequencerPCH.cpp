#include <EnginePluginSequencer/EnginePluginSequencerPCH.h>

PL_STATICLINK_LIBRARY(EnginePluginSequencer)
{
  if (bReturn)
    return;

  PL_STATICLINK_REFERENCE(EnginePluginSequencer_SequenceAsset_SequenceContext);
}
