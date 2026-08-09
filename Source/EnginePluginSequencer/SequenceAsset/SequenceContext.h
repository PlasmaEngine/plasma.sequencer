#pragma once

#include <EnginePluginScene/SceneContext/SceneContext.h>
#include <EnginePluginSequencer/EnginePluginSequencerDLL.h>

/// \brief Engine-process document context for Sequence assets.
///
/// A Sequence document is a game-object context document: it attaches to a scene and animates that
/// scene's objects. Engine-side that is exactly what plSceneContext already provides - it is the
/// context that serves "Scene;Prefab;PropertyAnim" - so this subclass adds nothing but the
/// registration for the "Sequence" document type. Sequencer-specific preview behavior (scrub
/// evaluation, pilot camera) lands here with the P1/P3 milestones.
class PL_ENGINEPLUGINSEQUENCER_DLL plSequenceContext : public plSceneContext
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceContext, plSceneContext);

public:
  plSequenceContext();
};
