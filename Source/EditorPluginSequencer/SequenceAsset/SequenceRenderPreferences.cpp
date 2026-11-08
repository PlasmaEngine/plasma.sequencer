#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceRenderPreferences.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceRenderPreferencesUser, 1, plRTTIDefaultAllocator<plSequenceRenderPreferencesUser>)
{
  PL_BEGIN_PROPERTIES
  {
    PL_MEMBER_PROPERTY("OutputFolder", m_sOutputFolder)->AddAttributes(new plHiddenAttribute),
  }
  PL_END_PROPERTIES;
}
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plSequenceRenderPreferencesUser::plSequenceRenderPreferencesUser()
  : plPreferences(Domain::Document, "SequenceRender")
{
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceRenderPreferences);
