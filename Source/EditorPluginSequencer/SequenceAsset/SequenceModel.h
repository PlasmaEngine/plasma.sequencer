#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <Foundation/Communication/Event.h>
#include <Foundation/Containers/DynamicArray.h>

/// \brief What changed in a plSequenceModel.
struct plSequenceModelEvent
{
  enum class Type : plUInt8
  {
    ModelRebuilt, ///< the rows were rebuilt from the document

    /// Key-level data changed (control points added / removed / moved). The row set is untouched, so views only need a
    /// repaint - crucially, the rows are NOT rebuilt, which would clear the outliner's selection mid-drag.
    KeysChanged,
  };

  Type m_Type = Type::ModelRebuilt;
};

/// \brief Tree of a Sequence document's bindings, tracks and clip lanes.
///
/// Three levels: bindings, then their tracks and lanes, then the per-axis tracks of a vector group
/// (Position / Rotation / Scale are authored as one row, not three). Nodes are held in one flat
/// array addressed by index, so depth is not baked into the model the way a fixed parent encoding
/// would bake it. Each node carries the document object guid, which is what every edit and the
/// timeline canvas key on. The model rebuilds on any document structure/property change - coarse,
/// but sequences are small and it keeps the mapping trivially correct.
class plSequenceModel
{
public:
  /// \brief What a row is, which decides how the canvas paints and edits its lane.
  enum class RowKind : plUInt8
  {
    Binding,       ///< header row for one bound object
    Track,         ///< a single curve or gradient track
    VectorGroup,   ///< Position / Rotation / Scale: one row over its per-axis tracks
    ClipLane,      ///< clips of one kind on a binding; the guid is the binding
    CameraCutLane, ///< the sequence-level camera cut row, which has no object
    EventLane,     ///< the sequence-level event track row; the guid is the event track object
  };

  explicit plSequenceModel(plSequenceAssetDocument* pDocument);
  ~plSequenceModel();

  struct Node
  {
    plUuid m_ObjectGuid;
    plInt32 m_iParent = -1; ///< index into m_Nodes, -1 for top-level rows
    plHybridArray<plInt32, 4> m_Children;
    plString m_sDisplay;
    plString m_sSubText;
    plColorGammaUB m_Color = plColorGammaUB(0, 0, 0, 0); ///< the row's category colour (its head strip and icon)
    plString m_sIconPath;                                ///< the row's icon as an SVG path, for the engine-drawn heads
    plInt32 m_iAxis = -1;                                ///< per-axis tracks: 0 = X, 1 = Y, 2 = Z, 3 = W
    RowKind m_Kind = RowKind::Track;
    bool m_bUnresolved = false; ///< Binding only: its object is not in the attached scene.

    /// VectorGroup only: the per-axis track guids it stands for, in axis order.
    plHybridArray<plUuid, 4> m_AxisTracks;

    /// ClipLane only: which clips of the binding this lane holds.
    plEnum<plSequenceClipKind> m_ClipKind;
  };

  /// \brief The flattened visible row list, which is the row space the timeline canvas paints in.
  struct FlatRow
  {
    plUuid m_ObjectGuid;
    RowKind m_Kind = RowKind::Track;
    plHybridArray<plUuid, 4> m_AxisTracks; ///< VectorGroup only
    plEnum<plSequenceClipKind> m_ClipKind; ///< ClipLane only
    plInt32 m_iNode = -1;  ///< the node it shows (GetNode)
    plUInt8 m_uiDepth = 0; ///< 0 for bindings and sequence lanes, 1 under a binding, 2 under a vector group

    /// Header-styled rows: they carry no keys of their own kind and are painted as lane headers.
    bool IsBinding() const { return m_Kind == RowKind::Binding || m_Kind == RowKind::CameraCutLane || m_Kind == RowKind::EventLane; }
    bool IsCameraCutLane() const { return m_Kind == RowKind::CameraCutLane; }
    bool IsClipLane() const { return m_Kind == RowKind::ClipLane; }
    bool IsEventLane() const { return m_Kind == RowKind::EventLane; }
  };
  const plDynamicArray<FlatRow>& GetFlatRows() const { return m_FlatRows; }
  const Node& GetNode(plInt32 iNode) const { return m_Nodes[iNode]; }

  plSequenceAssetDocument* GetDocument() const { return m_pDocument; }

  plEvent<const plSequenceModelEvent&> m_Events;

private:
  void DocumentStructureEventHandler(const plDocumentObjectStructureEvent& e);
  void DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void RebuildModel();
  void Broadcast(plSequenceModelEvent::Type type);

  /// Appends a node under a parent index (-1 for top level) and returns its index.
  plInt32 AddNode(plInt32 iParent, RowKind kind);

  /// Adds one lane row per clip kind the owner has. The owner is a binding, or the sequence itself
  /// for clips attached to nothing in the scene - both expose the same Clips / ClipLanes arrays.
  void AddClipLaneNodes(const plDocumentObject* pOwnerObj, plInt32 iParentNode, const char* szDisplayPrefix);

  /// Appends the node and its descendants to the flat row list, depth first.
  void FlattenNode(plInt32 iNode, plUInt8 uiDepth);

  plSequenceAssetDocument* m_pDocument = nullptr;

  plDynamicArray<Node> m_Nodes;
  plHybridArray<plInt32, 8> m_RootNodes;
  plDynamicArray<FlatRow> m_FlatRows;
};
