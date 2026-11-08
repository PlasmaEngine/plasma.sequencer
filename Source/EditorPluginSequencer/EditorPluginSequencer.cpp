#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorFramework/Actions/AssetActions.h>
#include <EditorFramework/Actions/GameObjectContextActions.h>
#include <EditorFramework/Actions/GameObjectDocumentActions.h>
#include <EditorFramework/Actions/GameObjectSelectionActions.h>
#include <EditorFramework/Actions/ProjectActions.h>
#include <EditorFramework/Actions/QuadViewActions.h>
#include <EditorFramework/Actions/TransformGizmoActions.h>
#include <EditorFramework/Actions/ViewActions.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceShellPanels.h>
#include <GuiFoundation/Action/ActionMapManager.h>
#include <GuiFoundation/Action/CommandHistoryActions.h>
#include <GuiFoundation/Action/DocumentActions.h>
#include <GuiFoundation/Action/EditActions.h>
#include <GuiFoundation/Action/StandardMenus.h>
#include <EditorUi/Widgets.h>
#include <Foundation/Configuration/Plugin.h>

/// The package's icon folder: the icons under their ids (":/AssetIcons/Sequence.svg" is
/// "EditorIcons/AssetIcons/Sequence.svg"). The plugin binary lives in <package>/Bin/<configuration>.
static plString s_sIconFolder;

static void RegisterIconFolder()
{
  plStringBuilder sPlugin, sCopy;
  plPlugin::GetPluginPaths("plEditorPluginSequencer", sPlugin, sCopy, 0);

  plStringBuilder sFolder = sPlugin.GetFileDirectory();
  sFolder.AppendPath("..", "..", "EditorIcons");
  sFolder.MakeCleanPath();

  s_sIconFolder = sFolder;
  plEditorUi::AddIconFolder(s_sIconFolder);
}

static void UnregisterIconFolder()
{
  plEditorUi::RemoveIconFolder(s_sIconFolder);
  s_sIconFolder.Clear();
}

static void ConfigureSequenceAsset()
{
  // Menu Bar
  {
    plActionMapManager::RegisterActionMap("SequenceAssetMenuBar").AssertSuccess();
    plStandardMenus::MapActions("SequenceAssetMenuBar",
      plStandardMenuTypes::Default | plStandardMenuTypes::Edit | plStandardMenuTypes::Scene | plStandardMenuTypes::View);
    plProjectActions::MapActions("SequenceAssetMenuBar");
    plDocumentActions::MapMenuActions("SequenceAssetMenuBar");
    plAssetActions::MapMenuActions("SequenceAssetMenuBar");
    plDocumentActions::MapToolsActions("SequenceAssetMenuBar");
    plCommandHistoryActions::MapActions("SequenceAssetMenuBar");
    plGameObjectSelectionActions::MapActions("SequenceAssetMenuBar");
    plGameObjectDocumentActions::MapMenuActions("SequenceAssetMenuBar");
    plGameObjectDocumentActions::MapMenuSimulationSpeed("SequenceAssetMenuBar");
    plTransformGizmoActions::MapMenuActions("SequenceAssetMenuBar");
    plTranslateGizmoAction::MapActions("SequenceAssetMenuBar");
  }

  // Tool Bar
  {
    plActionMapManager::RegisterActionMap("SequenceAssetToolBar").AssertSuccess();
    plDocumentActions::MapToolbarActions("SequenceAssetToolBar");
    plCommandHistoryActions::MapActions("SequenceAssetToolBar", "");
    plAssetActions::MapToolBarActions("SequenceAssetToolBar", true);
    plGameObjectContextActions::MapToolbarActions("SequenceAssetToolBar");
    plGameObjectDocumentActions::MapToolbarActions("SequenceAssetToolBar");
    plTransformGizmoActions::MapToolbarActions("SequenceAssetToolBar");
  }

  // View Tool Bar
  {
    plActionMapManager::RegisterActionMap("SequenceAssetViewToolBar").AssertSuccess();
    plViewActions::MapToolbarActions("SequenceAssetViewToolBar",
      plViewActions::PerspectiveMode | plViewActions::RenderMode | plViewActions::ActivateRemoteProcess);
    plQuadViewActions::MapToolbarActions("SequenceAssetViewToolBar");
  }

  // SceneGraph Context Menu
  {
    plActionMapManager::RegisterActionMap("SequenceAsset_ScenegraphContextMenu").AssertSuccess();
    plGameObjectSelectionActions::MapContextMenuActions("SequenceAsset_ScenegraphContextMenu");
    plGameObjectContextActions::MapContextMenuActions("SequenceAsset_ScenegraphContextMenu");
  }
}

PL_PLUGIN_ON_LOADED()
{
  RegisterIconFolder();
  ConfigureSequenceAsset();
  plSequenceShellPanels::Register();
}

PL_PLUGIN_ON_UNLOADED()
{
  plSequenceShellPanels::Unregister();
  UnregisterIconFolder();
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_EditorPluginSequencer);
