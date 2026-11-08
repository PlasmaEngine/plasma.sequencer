#pragma once

#include <EditorFramework/Preferences/Preferences.h>

/// \brief Render to Disk settings kept per sequence and user: the output folder of the last render, which the dialog
/// opens on again.
class plSequenceRenderPreferencesUser : public plPreferences
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceRenderPreferencesUser, plPreferences);

public:
  plSequenceRenderPreferencesUser();

  plString m_sOutputFolder;
};
