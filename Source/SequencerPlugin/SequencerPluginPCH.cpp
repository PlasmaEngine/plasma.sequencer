#include <SequencerPlugin/SequencerPluginPCH.h>

PL_STATICLINK_LIBRARY(SequencerPlugin)
{
  if (bReturn)
    return;

  PL_STATICLINK_REFERENCE(SequencerPlugin_Startup);
  PL_STATICLINK_REFERENCE(SequencerPlugin_Resources_Implementation_SequenceResource);
  PL_STATICLINK_REFERENCE(SequencerPlugin_Components_Implementation_SequencePlayerComponent);
}
