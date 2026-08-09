#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <Core/World/Component.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceModel.moc.h>
#include <GuiFoundation/UIServices/UIServices.moc.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

#include <QColor>

namespace
{
  /// The icon of the most telling component on a bound object, tinted by its category - the same
  /// icon and colour the scene tree and property grid use for that component type.
  QIcon IconForBoundObject(const plDocumentObject* pObject)
  {
    if (pObject == nullptr)
      return QIcon();

    const plRTTI* pBest = nullptr;

    for (const plDocumentObject* pChild : pObject->GetChildren())
    {
      const plRTTI* pType = pChild->GetType();
      if (!pType->IsDerivedFrom<plComponent>())
        continue;

      // First component wins, except that a mesh/light/camera says more about an object than a
      // physics shape or a script does - prefer one carrying a category.
      if (pBest == nullptr)
      {
        pBest = pType;
        continue;
      }

      if (pType->GetAttributeByType<plCategoryAttribute>() != nullptr && pBest->GetAttributeByType<plCategoryAttribute>() == nullptr)
        pBest = pType;
    }

    if (pBest == nullptr)
      return QIcon();

    plColor tint = plColor::MakeZero();
    if (const plCategoryAttribute* pCategory = pBest->GetAttributeByType<plCategoryAttribute>())
    {
      tint = plColorScheme::GetCategoryColor(pCategory->GetCategory(), plColorScheme::CategoryColorUsage::SceneTreeIcon);
    }

    plStringView sRawName = pBest->GetTypeName();
    if (const plIconAttribute* pIcon = pBest->GetAttributeByType<plIconAttribute>())
      sRawName = pIcon->GetIcon();

    plStringBuilder sIconName;
    sIconName.Set(":/TypeIcons/", sRawName, ".svg");

    QIcon icon = plQtUiServices::GetCachedIconResource(sIconName.GetData(), tint);
    if (icon.isNull())
    {
      sIconName.Set(":/TypeIcons/", "DefaultIcon", ".svg");
      icon = plQtUiServices::GetCachedIconResource(sIconName.GetData(), tint);
    }

    return icon;
  }

  // Track type strip colors - plColorScheme::LightUI values, per the mockup's theming table
  // (Documentation/Sequencer/Mockup_MainWindow.html): Transform=Orange, Property=Cyan, Color=Grape.
  QColor TrackColorFor(const QString& sComponentType, plInt64 iTarget)
  {
    if (iTarget == plPropertyAnimTarget::Color)
      return QColor(0xcc, 0x5d, 0xe8); // LightUI(Grape)

    if (sComponentType.isEmpty())
      return QColor(0xff, 0x92, 0x2b); // LightUI(Orange) - game object transform

    return QColor(0x22, 0xb8, 0xcf); // LightUI(Cyan) - component property
  }

  QColor ColorForClipKind(plSequenceClipKind::Enum kind)
  {
    switch (kind)
    {
      case plSequenceClipKind::AnimationClip:
        return QColor(0x4d, 0xab, 0xf7); // LightUI(Blue)
      case plSequenceClipKind::SubSequence:
        return QColor(0x84, 0x5e, 0xf7); // LightUI(Violet)
      case plSequenceClipKind::AudioTrigger:
      default:
        return QColor(0xc8, 0x8c, 0xb9); // GetCategoryColor("Audio") PastelMauve
    }
  }

  const char* NameForClipKind(plSequenceClipKind::Enum kind)
  {
    switch (kind)
    {
      case plSequenceClipKind::AnimationClip:
        return "Animation";
      case plSequenceClipKind::SubSequence:
        return "Sub-Sequence";
      case plSequenceClipKind::AudioTrigger:
      default:
        return "Audio";
    }
  }

  /// The axis a target selects within a vector or quaternion property, or -1 for a whole-value
  /// target. Vector and rotation channels share an ordering, so one group covers both.
  int AxisOfTarget(plInt64 iTarget)
  {
    switch (iTarget)
    {
      case plPropertyAnimTarget::VectorX:
      case plPropertyAnimTarget::RotationX:
        return 0;
      case plPropertyAnimTarget::VectorY:
      case plPropertyAnimTarget::RotationY:
        return 1;
      case plPropertyAnimTarget::VectorZ:
      case plPropertyAnimTarget::RotationZ:
        return 2;
      case plPropertyAnimTarget::VectorW:
        return 3;
      default:
        return -1;
    }
  }

  const char* AxisName(int iAxis)
  {
    switch (iAxis)
    {
      case 0:
        return "X";
      case 1:
        return "Y";
      case 2:
        return "Z";
      case 3:
        return "W";
      default:
        return "";
    }
  }

  /// "LocalPosition" reads as "Position" on a transform row - the Local prefix is noise when the
  /// row already sits under the object it belongs to.
  QString FriendlyPropertyName(plStringView sProperty)
  {
    plStringBuilder s = sProperty;
    s.TrimWordStart("Local");
    if (s.IsEqual("Scaling"))
      s = "Scale";
    return QString::fromUtf8(s.GetData());
  }
} // namespace

plQtSequenceModel::plQtSequenceModel(plSequenceAssetDocument* pDocument, QObject* pParent)
  : QAbstractItemModel(pParent)
  , m_pDocument(pDocument)
{
  m_pDocument->GetObjectManager()->m_StructureEvents.AddEventHandler(plMakeDelegate(&plQtSequenceModel::DocumentStructureEventHandler, this));
  m_pDocument->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plQtSequenceModel::DocumentPropertyEventHandler, this));

  RebuildModel();
}

plQtSequenceModel::~plQtSequenceModel()
{
  m_pDocument->GetObjectManager()->m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plQtSequenceModel::DocumentStructureEventHandler, this));
  m_pDocument->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plQtSequenceModel::DocumentPropertyEventHandler, this));
}

void plQtSequenceModel::DocumentStructureEventHandler(const plDocumentObjectStructureEvent& e)
{
  if (e.m_pObject == nullptr)
    return;

  const plRTTI* pType = e.m_pObject->GetType();

  // Key-level changes leave the row set untouched - repaint only, never reset (a reset clears the
  // outliner selection, which clears the curve editor mid-drag).
  if (pType == plGetStaticRTTI<plCurveControlPointData>() || pType == plGetStaticRTTI<plSingleCurveData>() ||
      pType == plGetStaticRTTI<plColorControlPoint>() || pType == plGetStaticRTTI<plAlphaControlPoint>() ||
      pType == plGetStaticRTTI<plIntensityControlPoint>())
  {
    if (e.m_EventType == plDocumentObjectStructureEvent::Type::AfterObjectAdded ||
        e.m_EventType == plDocumentObjectStructureEvent::Type::AfterObjectRemoved ||
        e.m_EventType == plDocumentObjectStructureEvent::Type::AfterObjectMoved)
    {
      Q_EMIT KeysChanged();
    }
    return;
  }

  // Bindings, tracks, clips and cuts shape the rows: a clip decides which lane exists for its kind,
  // and a cut changes the lane's "N cuts" sub-text.
  // IsDerivedFrom for clips: each kind is its own concrete type.
  if (pType != plGetStaticRTTI<plSequenceBinding>() && pType != plGetStaticRTTI<plSequenceTrack>() &&
      !pType->IsDerivedFrom<plSequenceClip>() && pType != plGetStaticRTTI<plSequenceCameraCut>())
    return;

  RebuildModel();
}

void plQtSequenceModel::DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  if (e.m_pObject == nullptr)
    return;

  const plRTTI* pType = e.m_pObject->GetType();

  if (pType == plGetStaticRTTI<plCurveControlPointData>() || pType == plGetStaticRTTI<plColorControlPoint>() ||
      pType == plGetStaticRTTI<plAlphaControlPoint>() || pType == plGetStaticRTTI<plIntensityControlPoint>() ||
      pType == plGetStaticRTTI<plSequenceCameraCut>() || pType->IsDerivedFrom<plSequenceClip>())
  {
    Q_EMIT KeysChanged();
    return;
  }

  if (pType != plGetStaticRTTI<plSequenceBinding>() && pType != plGetStaticRTTI<plSequenceTrack>())
    return;

  RebuildModel();
}

plInt32 plQtSequenceModel::AddNode(plInt32 iParent, RowKind kind)
{
  const plInt32 iIndex = (plInt32)m_Nodes.GetCount();
  Node& node = m_Nodes.ExpandAndGetRef();
  node.m_iParent = iParent;
  node.m_Kind = kind;

  if (iParent < 0)
    m_RootNodes.PushBack(iIndex);
  else
    m_Nodes[iParent].m_Children.PushBack(iIndex);

  return iIndex;
}

void plQtSequenceModel::FlattenNode(plInt32 iNode)
{
  const Node& node = m_Nodes[iNode];

  FlatRow& row = m_FlatRows.ExpandAndGetRef();
  row.m_ObjectGuid = node.m_ObjectGuid;
  row.m_Kind = node.m_Kind;
  row.m_AxisTracks = node.m_AxisTracks;
  row.m_ClipKind = node.m_ClipKind;

  for (plInt32 iChild : node.m_Children)
  {
    FlattenNode(iChild);
  }
}

void plQtSequenceModel::AddClipLaneNodes(const plDocumentObject* pOwnerObj, plInt32 iParentNode, const char* szDisplayPrefix)
{
  if (pOwnerObj == nullptr)
    return;

  // A lane exists when the owner declares it, and also when clips of that kind are present - so
  // data authored before lanes were explicit still has a home.
  bool bLaneWanted[3] = {false, false, false};

  const plInt32 iLaneCount = pOwnerObj->GetTypeAccessor().GetCount("ClipLanes");
  for (plInt32 l = 0; l < iLaneCount; ++l)
  {
    const plInt64 iKind = pOwnerObj->GetTypeAccessor().GetValue("ClipLanes", l).ConvertTo<plInt64>();
    if (iKind >= 0 && iKind < 3)
      bLaneWanted[iKind] = true;
  }

  plInt32 iClipsPerKind[3] = {0, 0, 0};
  const plInt32 iClipCount = pOwnerObj->GetTypeAccessor().GetCount("Clips");
  for (plInt32 c = 0; c < iClipCount; ++c)
  {
    const plVariant clipGuid = pOwnerObj->GetTypeAccessor().GetValue("Clips", c);
    if (!clipGuid.IsA<plUuid>())
      continue;

    const plDocumentObject* pClipObj = m_pDocument->GetObjectManager()->GetObject(clipGuid.Get<plUuid>());
    if (pClipObj == nullptr)
      continue;

    const plInt64 iKind = pClipObj->GetTypeAccessor().GetValue("Kind").ConvertTo<plInt64>();
    if (iKind >= 0 && iKind < 3)
    {
      bLaneWanted[iKind] = true;
      ++iClipsPerKind[iKind];
    }
  }

  for (int k = 0; k < 3; ++k)
  {
    if (!bLaneWanted[k])
      continue;

    const plSequenceClipKind::Enum kind = (plSequenceClipKind::Enum)k;

    Node& laneNode = m_Nodes[AddNode(iParentNode, RowKind::ClipLane)];
    laneNode.m_ObjectGuid = pOwnerObj->GetGuid(); // clips belong to their owner, binding or sequence
    laneNode.m_ClipKind = kind;
    laneNode.m_sDisplay = QString::fromUtf8(szDisplayPrefix) + QString::fromUtf8(NameForClipKind(kind));
    laneNode.m_Color = ColorForClipKind(kind);
    laneNode.m_sSubText = iClipsPerKind[k] == 0 ? QString("empty") : QString("%1 clips").arg(iClipsPerKind[k]);
  }
}

void plQtSequenceModel::RebuildModel()
{
  beginResetModel();

  m_Nodes.Clear();
  m_RootNodes.Clear();
  m_FlatRows.Clear();

  const plDocumentObject* pPropObj = m_pDocument->GetPropertyObject();

  // Camera cuts are sequence-level, not per-binding: one lane pinned above the bindings, always
  // present so there is somewhere to drop the first cut.
  {
    Node& cutNode = m_Nodes[AddNode(-1, RowKind::CameraCutLane)];
    cutNode.m_sDisplay = "Camera Cuts";
    cutNode.m_Color = QColor(0x08, 0x52, 0x42); // the accent: cuts are the sequence's spine
    cutNode.m_Icon = plQtUiServices::GetCachedIconResource(
      ":/TypeIcons/plCameraComponent.svg", plColorScheme::GetCategoryColor("Rendering", plColorScheme::CategoryColorUsage::SceneTreeIcon));

    const plInt32 iCutCount = pPropObj->GetTypeAccessor().GetCount("CameraCuts");
    cutNode.m_sSubText = iCutCount == 0 ? QString("no cuts") : QString("%1 cuts").arg(iCutCount);
  }

  // The event track is sequence-level too. It had a dock but no lane, which meant events could not
  // be seen against the shots and clips they are supposed to line up with.
  if (const plDocumentObject* pEventTrackObj = m_pDocument->GetEventTrackObject())
  {
    Node& eventNode = m_Nodes[AddNode(-1, RowKind::EventLane)];
    eventNode.m_ObjectGuid = pEventTrackObj->GetGuid();
    eventNode.m_sDisplay = "Events";
    eventNode.m_Color = QColor(0xc8, 0x96, 0x00); // plQtEventTrackWidget's control-point colour
    eventNode.m_Icon = plQtUiServices::GetCachedIconResource(
      ":/TypeIcons/plMsgGenericEvent.svg", plColorScheme::GetCategoryColor("Logic", plColorScheme::CategoryColorUsage::SceneTreeIcon));

    const plInt32 iEventCount = pEventTrackObj->GetTypeAccessor().GetCount("ControlPoints");
    eventNode.m_sSubText = iEventCount == 0 ? QString("no events") : QString("%1 events").arg(iEventCount);
  }

  // Clip lanes attached to nothing in the scene - music, VO, stingers - sit at the top next to the
  // cuts, because they belong to the sequence rather than to any one object in it.
  AddClipLaneNodes(pPropObj, -1, "Sequence ");

  // Walk the document objects (not the native mirror) so nodes carry command-addressable guids.
  const plInt32 iBindingCount = pPropObj->GetTypeAccessor().GetCount("Bindings");

  for (plInt32 b = 0; b < iBindingCount; ++b)
  {
    const plVariant bindingGuidVar = pPropObj->GetTypeAccessor().GetValue("Bindings", b);
    if (!bindingGuidVar.IsA<plUuid>())
      continue;

    const plDocumentObject* pBindingObj = m_pDocument->GetObjectManager()->GetObject(bindingGuidVar.Get<plUuid>());
    if (pBindingObj == nullptr)
      continue;

    const plInt32 iBindingNode = AddNode(-1, RowKind::Binding);
    {
      Node& bindingNode = m_Nodes[iBindingNode];
      bindingNode.m_ObjectGuid = pBindingObj->GetGuid();

      const plVariant name = pBindingObj->GetTypeAccessor().GetValue("Name");
      const plVariant path = pBindingObj->GetTypeAccessor().GetValue("ObjectPath");
      bindingNode.m_sDisplay = name.IsA<plString>() ? QString::fromUtf8(name.Get<plString>().GetData()) : QString("<binding>");

      const plDocumentObject* pBoundObj = m_pDocument->ResolveBindingObject(pBindingObj->GetGuid());
      const QString sPath = path.IsA<plString>() ? QString::fromUtf8(path.Get<plString>().GetData()) : QString();

      // A binding that resolves to nothing animates nothing, in the editor and at runtime alike.
      // Saying so here is the difference between a silent cutscene and an obvious broken row.
      bindingNode.m_bUnresolved = pBoundObj == nullptr;

      if (bindingNode.m_bUnresolved)
      {
        bindingNode.m_sSubText = QString("missing: %1").arg(sPath.isEmpty() ? QString("<no path>") : sPath);
        bindingNode.m_Color = QColor(0xe0, 0x31, 0x31); // LightUI(Red)
      }
      else
      {
        bindingNode.m_sSubText = sPath;
        bindingNode.m_Color = QColor(0x64, 0xbe, 0xb9); // GetCategoryColor("Animation") PastelTeal
      }

      // what the bound object actually is, at a glance
      bindingNode.m_Icon = IconForBoundObject(pBoundObj);
    }

    // Per-axis tracks of the same property are one row. Keyed by component+property so a group is
    // formed the moment a second axis appears, whichever order the axes were added in.
    plMap<plString, plInt32> vectorGroups;

    const plInt32 iTrackCount = pBindingObj->GetTypeAccessor().GetCount("Tracks");
    for (plInt32 t = 0; t < iTrackCount; ++t)
    {
      const plVariant trackGuidVar = pBindingObj->GetTypeAccessor().GetValue("Tracks", t);
      if (!trackGuidVar.IsA<plUuid>())
        continue;

      const plDocumentObject* pTrackObj = m_pDocument->GetObjectManager()->GetObject(trackGuidVar.Get<plUuid>());
      if (pTrackObj == nullptr)
        continue;

      const plVariant componentType = pTrackObj->GetTypeAccessor().GetValue("ComponentType");
      const plVariant propertyPath = pTrackObj->GetTypeAccessor().GetValue("Property");
      const plVariant target = pTrackObj->GetTypeAccessor().GetValue("Target");

      QString sComponent;
      plStringBuilder sRawComponent;
      if (componentType.IsA<plString>() && !componentType.Get<plString>().IsEmpty())
      {
        sRawComponent = componentType.Get<plString>();
        plStringBuilder sTrimmed = sRawComponent;
        sTrimmed.TrimWordStart("pl");
        sComponent = QString::fromUtf8(sTrimmed.GetData());
      }

      const plString sProperty = propertyPath.IsA<plString>() ? propertyPath.Get<plString>() : plString();
      const plInt64 iTarget = target.IsValid() ? target.ConvertTo<plInt64>() : (plInt64)plPropertyAnimTarget::Number;
      const int iAxis = AxisOfTarget(iTarget);

      plInt32 iParentNode = iBindingNode;
      QString sDisplay = FriendlyPropertyName(sProperty);

      if (iAxis >= 0)
      {
        plStringBuilder sKey;
        sKey.Set(sRawComponent, "|", sProperty);

        auto it = vectorGroups.Find(sKey);
        if (!it.IsValid())
        {
          const plInt32 iGroupNode = AddNode(iBindingNode, RowKind::VectorGroup);
          Node& group = m_Nodes[iGroupNode];
          group.m_ObjectGuid = pTrackObj->GetGuid(); // the group answers as its first axis for selection
          group.m_sDisplay = FriendlyPropertyName(sProperty);
          group.m_sSubText = sComponent;
          group.m_Color = TrackColorFor(sComponent, iTarget);

          it = vectorGroups.Insert(sKey, iGroupNode);
        }

        iParentNode = it.Value();
        m_Nodes[iParentNode].m_AxisTracks.PushBack(pTrackObj->GetGuid());
        sDisplay = QString::fromUtf8(AxisName(iAxis)); // just "X" under a "Position" group
      }

      const plInt32 iTrackNode = AddNode(iParentNode, RowKind::Track);
      Node& trackNode = m_Nodes[iTrackNode];
      trackNode.m_ObjectGuid = pTrackObj->GetGuid();
      trackNode.m_sDisplay = sDisplay;
      trackNode.m_sSubText = iAxis >= 0 ? QString() : sComponent;
      trackNode.m_Color = TrackColorFor(sComponent, iTarget);
    }

    // A group's sub-text says which axes it actually holds, so a partially keyed vector is obvious.
    for (auto it : vectorGroups)
    {
      Node& group = m_Nodes[it.Value()];
      QStringList axes;
      for (plInt32 iChild : group.m_Children)
      {
        axes << m_Nodes[iChild].m_sDisplay;
      }
      group.m_sSubText = group.m_sSubText.isEmpty() ? axes.join("") : QString("%1 · %2").arg(group.m_sSubText, axes.join(""));
    }

    AddClipLaneNodes(pBindingObj, iBindingNode, "");
  }

  for (plInt32 iRoot : m_RootNodes)
  {
    FlattenNode(iRoot);
  }

  endResetModel();

  Q_EMIT ModelRebuilt();
}

const plQtSequenceModel::Node* plQtSequenceModel::NodeFromIndex(const QModelIndex& index) const
{
  if (!index.isValid())
    return nullptr;

  // internalId is the node index biased by one, so 0 can mean "no node".
  const quintptr id = index.internalId();
  if (id == 0 || id > (quintptr)m_Nodes.GetCount())
    return nullptr;

  return &m_Nodes[(plUInt32)(id - 1)];
}

QModelIndex plQtSequenceModel::index(int iRow, int iColumn, const QModelIndex& parent) const
{
  if (iColumn != 0 || iRow < 0)
    return QModelIndex();

  if (!parent.isValid())
  {
    if (iRow >= (int)m_RootNodes.GetCount())
      return QModelIndex();

    return createIndex(iRow, iColumn, (quintptr)(m_RootNodes[iRow] + 1));
  }

  const Node* pParent = NodeFromIndex(parent);
  if (pParent == nullptr || iRow >= (int)pParent->m_Children.GetCount())
    return QModelIndex();

  return createIndex(iRow, iColumn, (quintptr)(pParent->m_Children[iRow] + 1));
}

QModelIndex plQtSequenceModel::parent(const QModelIndex& index) const
{
  const Node* pNode = NodeFromIndex(index);
  if (pNode == nullptr || pNode->m_iParent < 0)
    return QModelIndex();

  // The parent's own row is its position among its grandparent's children, or among the roots.
  const Node& parentNode = m_Nodes[pNode->m_iParent];
  const plArrayPtr<const plInt32> siblings =
    parentNode.m_iParent < 0 ? m_RootNodes.GetArrayPtr() : m_Nodes[parentNode.m_iParent].m_Children.GetArrayPtr();

  int iRow = 0;
  for (plUInt32 i = 0; i < siblings.GetCount(); ++i)
  {
    if (siblings[i] == pNode->m_iParent)
    {
      iRow = (int)i;
      break;
    }
  }

  return createIndex(iRow, 0, (quintptr)(pNode->m_iParent + 1));
}

int plQtSequenceModel::rowCount(const QModelIndex& parent) const
{
  if (!parent.isValid())
    return (int)m_RootNodes.GetCount();

  const Node* pNode = NodeFromIndex(parent);
  return pNode != nullptr ? (int)pNode->m_Children.GetCount() : 0;
}

int plQtSequenceModel::columnCount(const QModelIndex& parent) const
{
  PL_IGNORE_UNUSED(parent);
  return 1;
}

QVariant plQtSequenceModel::data(const QModelIndex& index, int iRole) const
{
  const Node* pNode = NodeFromIndex(index);
  if (pNode == nullptr)
    return QVariant();

  switch (iRole)
  {
    case Qt::DisplayRole:
      return pNode->m_sDisplay;

    case Qt::ToolTipRole:
      return pNode->m_sSubText;

    case ObjectGuidRole:
    {
      // plUuid does not fit QVariant directly; hand out the string form.
      plStringBuilder s;
      plConversionUtils::ToString(pNode->m_ObjectGuid, s);
      return QString::fromUtf8(s.GetData());
    }

    case IsBindingRole:
      return pNode->m_Kind == RowKind::Binding || pNode->m_Kind == RowKind::CameraCutLane;

    case TrackColorRole:
      return pNode->m_Color;

    case SubTextRole:
      return pNode->m_sSubText;

    case IsCameraCutLaneRole:
      return pNode->m_Kind == RowKind::CameraCutLane;

    case IsClipLaneRole:
      return pNode->m_Kind == RowKind::ClipLane;

    case IconRole:
      return pNode->m_Icon;

    case ClipKindRole:
      return (int)pNode->m_ClipKind.GetValue();

    case UnresolvedRole:
      return pNode->m_bUnresolved;

    case AxisTracksRole:
    {
      QStringList guids;
      for (const plUuid& trackGuid : pNode->m_AxisTracks)
      {
        plStringBuilder s;
        plConversionUtils::ToString(trackGuid, s);
        guids << QString::fromUtf8(s.GetData());
      }
      return guids;
    }

    default:
      return QVariant();
  }
}
