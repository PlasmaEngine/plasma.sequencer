# plasma.sequencer

Cinematic timeline tool for PlasmaEngine: a `Sequence` asset type for building in-game
cutscenes — multi-object property/transform tracks, camera cuts, events, animation clips
and audio — with editor preview and a runtime player component.

Design plan and UI mockups: `PlasmaEngine2/Documentation/Sequencer/` (index.html).

## Status

**P0 skeleton.** The package builds and installs; it registers the Sequence asset type
(create/open/save, transform to an empty-but-loadable `plBinSequence` resource) and opens
a document window with a 3D viewport of the attached context scene and an empty Sequencer
panel. Track editing (P1), runtime playback (P2), camera cuts (P3) and clip tracks (P4)
follow the phased roadmap in the plan.

## Layout

- `Source/SequencerPlugin/` — runtime: `plSequenceResource`, `plSequencePlayerComponent`.
- `Source/EnginePluginSequencer/` — editor-engine-process side: `plSequenceContext`, a
  `plSceneContext` subclass registered for document type "Sequence" (the same mechanism
  PropertyAnim uses, which plSceneContext serves under "Scene;Prefab;PropertyAnim").
- `Source/EditorPluginSequencer/` — editor UI: document, manager, window.

## Building

Out of tree, against a **built** engine SDK:

```bash
PLASMA_SDK_ROOT=E:/PlasmaTech/PlasmaEngine2 plasmabuild build SequencerPlugin
PLASMA_SDK_ROOT=E:/PlasmaTech/PlasmaEngine2 plasmabuild build EnginePluginSequencer
PLASMA_SDK_ROOT=E:/PlasmaTech/PlasmaEngine2 plasmabuild build EditorPluginSequencer
```

Output lands in `Bin/Windows_x64_Development/`. Remember: the editor loads the *installed*
copy under `<project>/Plugins/plasma.sequencer/<version>/`, not this build output — copy the
binaries plus `Sequencer.plPluginBundle` there to test.

Packaging procedure: `LocalPackages/PackagingProcess.md`.
