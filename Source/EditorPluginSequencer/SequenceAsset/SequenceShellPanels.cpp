#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorFramework/EngineShell/AssetDocumentContent.h>
#include <EditorFramework/EngineShell/DocumentFrame.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceShellPanels.h>
#include <EditorUi/DockManager.h>
#include <EditorUi/Widgets.h>

namespace
{
  void ArrangeSequencePanels(plEditorUiDocumentFrame& ref_frame)
  {
    using plEditorUi::Icon;
    using plEditorUi::IconPath;

    plEditorUiDockManager& panels = ref_frame.GetPanels();

    // the context scene's tree left of the view
    panels.DockPanel("SceneTree", "View", plEditorUiDockSide::Left, 0.22f);

    // the timeline below the view, the selected track's curves and the sequence's events as tabs beside it
    panels.SetPanelIcon("Sequencer", IconPath(Icon::AnimationClip));
    panels.SetPanelIcon("Events", IconPath(Icon::Event));

    panels.DockPanel("Sequencer", "View", plEditorUiDockSide::Bottom, 0.4f);
    panels.DockPanel("Curves", "Sequencer", plEditorUiDockSide::Tab);
    panels.DockPanel("Events", "Sequencer", plEditorUiDockSide::Tab);
    panels.SetCurrentPanel("Sequencer");
  }

  plEngineAssetPanelsProvider s_SequencePanelsProvider(plGetStaticRTTI<plSequenceAssetDocument>(), &ArrangeSequencePanels);
} // namespace

void plSequenceShellPanels::Register()
{
  plEditorUiDocumentContentProvider::Register(&s_SequencePanelsProvider);
}

void plSequenceShellPanels::Unregister()
{
  plEditorUiDocumentContentProvider::Unregister(&s_SequencePanelsProvider);
}
