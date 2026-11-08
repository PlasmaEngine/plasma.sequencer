#pragma once

/// \brief The sequence asset's panels in the engine shell.
///
/// The shell shows the sequence's 3D view and properties by itself (plEngineAssetDocumentContentProvider), with the
/// panels the window declares (plSequenceAssetDocumentWindow); this provider arranges them: the scene tree on the left
/// of the view, the Sequencer panel below it with the curve editor and the event track as tabs beside it.
namespace plSequenceShellPanels
{
  void Register();
  void Unregister();
} // namespace plSequenceShellPanels
