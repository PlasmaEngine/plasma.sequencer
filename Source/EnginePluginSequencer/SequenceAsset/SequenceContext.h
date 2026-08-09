#pragma once

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessDocumentContext.h>
#include <EnginePluginSequencer/EnginePluginSequencerDLL.h>

/// \brief Engine-process document context for Sequence assets.
///
/// Derives directly from plEngineProcessDocumentContext - NOT from plSceneContext. The context
/// factory resolves document types via FindPropertyByName("DocumentType"), which walks the type
/// hierarchy, so a derived type redefining that constant trips the RTTI correctness assert
/// (plLayerContext and water's profile context derive directly for the same reason). The base
/// class already owns the world and the IPC object mirror, so the attached context scene's
/// objects arrive here without scene-context help.
class PL_ENGINEPLUGINSEQUENCER_DLL plSequenceContext : public plEngineProcessDocumentContext
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceContext, plEngineProcessDocumentContext);

public:
  plSequenceContext();

  virtual void HandleMessage(const plEditorEngineDocumentMsg* pMsg) override;

protected:
  virtual plEngineProcessViewContext* CreateViewContext() override;
  virtual void DestroyViewContext(plEngineProcessViewContext* pContext) override;

  /// Places the thumbnail camera on the object the editor nominated, which is how a camera cut's
  /// preview image gets rendered from that cut's camera. Returning false holds the convergence
  /// counter back, so a thumbnail is never captured before there is something to capture.
  virtual bool UpdateThumbnailViewContext(plEngineProcessViewContext* pThumbnailViewContext) override;

private:
  // Audio preview. The editor knows where the playhead is and sends only clip edges; this process
  // owns the world and the audio system, so it is where triggers actually fire.
  plGameObject* ResolvePreviewAudioObject(plStringView sObjectGuid);
  void StartPreviewAudio(plStringView sObjectGuid, plStringView sTrigger);
  void StopPreviewAudio(plStringView sObjectGuid);
  void StopAllPreviewAudio();

  /// Host for sequence-level clips, which have no bound object. Created on first use.
  plGameObjectHandle m_hPreviewAudioObject;

  /// Where the next thumbnail is rendered from, set by a config message just before the request.
  /// A transform rather than an object: inside a camera-cut blend the view is a mix of two cameras
  /// and sits at neither of them, so the editor resolves it and sends the result.
  plTransform m_ThumbnailCameraTransform = plTransform::MakeIdentity();
  float m_fThumbnailCameraFov = 60.0f;
  bool m_bThumbnailCameraValid = false;
};
