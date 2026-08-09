#pragma once

#include <EditorFramework/DocumentWindow/GameObjectDocumentWindow.moc.h>
#include <EditorFramework/EditTools/EditTool.h>
#include <EditorPluginSequencer/EditorPluginSequencerDLL.h>

class plSequenceAssetDocument;
class plQtQuadViewWidget;

/// \brief Document window for Sequence assets.
///
/// P0 skeleton: viewport of the attached context scene, scenegraph, property grid, and an empty
/// "Sequencer" panel where the binding outliner + plQtSequenceTrackView timeline land in P1
/// (Documentation/Sequencer/Mockup_MainWindow.html shows the target layout).
class plQtSequenceAssetDocumentWindow : public plQtGameObjectDocumentWindow, public plGameObjectGizmoInterface
{
  Q_OBJECT

public:
  plQtSequenceAssetDocumentWindow(plSequenceAssetDocument* pDocument);
  ~plQtSequenceAssetDocumentWindow();

public:
  /// \name plGameObjectGizmoInterface implementation
  ///@{
  virtual plObjectAccessorBase* GetObjectAccessor() override;
  virtual bool CanDuplicateSelection() const override { return false; }
  virtual void DuplicateSelection() override {}
  ///@}

protected:
  virtual void InternalRedraw() override;

private:
  plQtQuadViewWidget* m_pQuadViewWidget = nullptr;
};
