#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessViewContext.h>

class plSequenceContext;

/// \brief Engine-process view for the Sequence editor's 3D viewport.
///
/// Beyond creating the view, this exists to answer picking. Gizmos and manipulators are driven by
/// the editor asking the engine what is under the cursor; a view context that does not reply leaves
/// the viewport rendering correctly but completely inert - no click-select, no grabbing a gizmo
/// axis. plSceneViewContext does the same work for scenes and prefabs.
class plSequenceViewContext : public plEngineProcessViewContext
{
public:
  explicit plSequenceViewContext(plSequenceContext* pContext);

  /// \brief The view's camera, so the document context can place it for thumbnail renders.
  plCamera& GetCamera() { return m_Camera; }

protected:
  virtual void HandleViewMessage(const plEditorEngineViewMsg* pMsg) override;
  virtual plViewHandle CreateView() override;

private:
  void PickObjectAt(plUInt16 x, plUInt16 y);

  plSequenceContext* m_pSequenceContext = nullptr;
};
