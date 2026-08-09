#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <Foundation/Containers/DynamicArray.h>
#include <QAbstractItemModel>
#include <QIcon>

/// \brief Tree model over a Sequence document's bindings, tracks and clip lanes.
///
/// Three levels: bindings, then their tracks and lanes, then the per-axis tracks of a vector group
/// (Position / Rotation / Scale are authored as one row, not three). Nodes are held in one flat
/// array addressed by index, so depth is not baked into the model the way a fixed parent encoding
/// would bake it. Each node carries the document object guid, which is what every edit and the
/// timeline canvas key on. The model rebuilds on any document structure/property change - coarse,
/// but sequences are small and it keeps the mapping trivially correct.
class plQtSequenceModel : public QAbstractItemModel
{
  Q_OBJECT

public:
  enum UserRoles
  {
    ObjectGuidRole = Qt::UserRole + 1, ///< plUuid of the binding / track document object
    IsBindingRole,                     ///< bool: true for binding rows
    TrackColorRole,                    ///< QColor: type strip color for track rows / accent for bindings
    SubTextRole,                       ///< QString: dimmed secondary text (object path / component type)
    IsCameraCutLaneRole,               ///< bool: true for the sequence-level camera cut row
    IsClipLaneRole,                    ///< bool: true for a clip row; its guid is the owning binding
    IconRole,                          ///< QIcon: the bound object's component icon
    ClipKindRole,                      ///< int: plSequenceClipKind of a clip lane row
    UnresolvedRole,                    ///< bool: the binding points at an object that is not there
    AxisTracksRole,                    ///< QStringList: a vector group's per-axis track guids, in axis order
  };

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

  explicit plQtSequenceModel(plSequenceAssetDocument* pDocument, QObject* pParent = nullptr);
  ~plQtSequenceModel();

  struct Node
  {
    plUuid m_ObjectGuid;
    plInt32 m_iParent = -1; ///< index into m_Nodes, -1 for top-level rows
    plHybridArray<plInt32, 4> m_Children;
    QString m_sDisplay;
    QString m_sSubText;
    QColor m_Color;
    QIcon m_Icon;
    RowKind m_Kind = RowKind::Track;
    bool m_bUnresolved = false; ///< Binding only: its object is not in the attached scene.

    /// VectorGroup only: the per-axis track guids it stands for, in axis order.
    plHybridArray<plUuid, 4> m_AxisTracks;

    /// ClipLane only: which clips of the binding this lane holds.
    plEnum<plSequenceClipKind> m_ClipKind;
  };

  virtual QModelIndex index(int iRow, int iColumn, const QModelIndex& parent = QModelIndex()) const override;
  virtual QModelIndex parent(const QModelIndex& index) const override;
  virtual int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  virtual QVariant data(const QModelIndex& index, int iRole = Qt::DisplayRole) const override;

  /// \brief The flattened visible row list, which is the row space the timeline canvas paints in.
  struct FlatRow
  {
    plUuid m_ObjectGuid;
    RowKind m_Kind = RowKind::Track;
    plHybridArray<plUuid, 4> m_AxisTracks; ///< VectorGroup only
    plEnum<plSequenceClipKind> m_ClipKind; ///< ClipLane only

    /// Header-styled rows: they carry no keys of their own kind and are painted as lane headers.
    bool IsBinding() const { return m_Kind == RowKind::Binding || m_Kind == RowKind::CameraCutLane || m_Kind == RowKind::EventLane; }
    bool IsCameraCutLane() const { return m_Kind == RowKind::CameraCutLane; }
    bool IsClipLane() const { return m_Kind == RowKind::ClipLane; }
    bool IsEventLane() const { return m_Kind == RowKind::EventLane; }
  };
  const plDynamicArray<FlatRow>& GetFlatRows() const { return m_FlatRows; }

  plSequenceAssetDocument* GetDocument() const { return m_pDocument; }

Q_SIGNALS:
  void ModelRebuilt();

  /// \brief Key-level data changed (control points added / removed / moved). The row set is
  /// untouched, so views only need a repaint - crucially, this does NOT reset the model, which
  /// would clear the outliner's selection mid-drag.
  void KeysChanged();

private:
  void DocumentStructureEventHandler(const plDocumentObjectStructureEvent& e);
  void DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e);
  void RebuildModel();

  /// Appends a node under a parent index (-1 for top level) and returns its index.
  plInt32 AddNode(plInt32 iParent, RowKind kind);

  /// Adds one lane row per clip kind the owner has. The owner is a binding, or the sequence itself
  /// for clips attached to nothing in the scene - both expose the same Clips / ClipLanes arrays.
  void AddClipLaneNodes(const plDocumentObject* pOwnerObj, plInt32 iParentNode, const char* szDisplayPrefix);

  /// Appends the node and its descendants to the flat row list, depth first.
  void FlattenNode(plInt32 iNode);

  const Node* NodeFromIndex(const QModelIndex& index) const;

  plSequenceAssetDocument* m_pDocument = nullptr;

  plDynamicArray<Node> m_Nodes;
  plHybridArray<plInt32, 8> m_RootNodes;
  plDynamicArray<FlatRow> m_FlatRows;
};
