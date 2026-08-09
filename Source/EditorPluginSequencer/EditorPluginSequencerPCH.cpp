#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

PL_STATICLINK_LIBRARY(EditorPluginSequencer)
{
  if (bReturn)
    return;

  PL_STATICLINK_REFERENCE(EditorPluginSequencer_EditorPluginSequencer);
  PL_STATICLINK_REFERENCE(EditorPluginSequencer_SequenceAsset_SequenceAsset);
  PL_STATICLINK_REFERENCE(EditorPluginSequencer_SequenceAsset_SequenceAssetManager);
}
