#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <Core/World/Component.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceModel.h>
#include <EditorUi/Widgets.h>
#include <ToolsFoundation/Object/DocumentObjectManager.h>

namespace
{
  /// A category colour (plColorScheme) as the rows use it.
  plColorGammaUB CategoryColor(const char* szCategory)
  {
    plColorGammaUB color = plColorScheme::GetCategoryColor(szCategory, plColorScheme::CategoryColorUsage::OverlayIcon);
    color.a = 255;
    return color;
  }

  /// The SVG path of the icon of the most telling component on a bound object - the same icon the scene tree and
  /// property grid use for that component type.
  plString IconForBoundObject(const plDocumentObject* pObject)
  {
    const plString sDefault = ":/TypeIcons/DefaultIcon.svg";
    if (pObject == nullptr)
      return sDefault;

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
      return sDefault;

    plStringView sRawName = pBest->GetTypeName();
    if (const plIconAttribute* pIcon = pBest->GetAttributeByType<plIconAttribute>())
      sRawName = pIcon->GetIcon();

    plStringBuilder sIconName;
    sIconName.Set(":/TypeIcons/", sRawName, ".svg");

    if (!plEditorUi::IconExists(sIconName))
      return sDefault;

    return sIconName;
  }

  // Track colours are category colours: transforms gameplay, component properties XR, colours effects.
  plColorGammaUB TrackColorFor(plStringView sComponentType, plInt64 iTarget)
  {
    if (iTarget == plPropertyAnimTarget::Color)
      return CategoryColor("Effects");

    if (sComponentType.IsEmpty())
      return CategoryColor("Gameplay");

    return CategoryColor("XR");
  }

  /// The icon of a track row: a gradient for colours, a curve for numbers, the move gizmo for transforms.
  plString IconForTrack(plStringView sComponentType, plInt64 iTarget)
  {
    // the asset a track resembles: a colour track is a gradient, a component property a curve; the track view
    // draws the artwork neutral and tints it
    if (iTarget == plPropertyAnimTarget::Color)
      return ":/AssetIcons/ColorGradient.svg";

    return sComponentType.IsEmpty() ? plEditorUi::IconPath(plEditorUi::Icon::Move) : plString(":/AssetIcons/Curve1D.svg");
  }

  plColorGammaUB ColorForClipKind(plSequenceClipKind::Enum kind)
  {
    switch (kind)
    {
      case plSequenceClipKind::AnimationClip:
        return CategoryColor("Animation");
      case plSequenceClipKind::SubSequence:
        return CategoryColor("Scripting");
      case plSequenceClipKind::AudioTrigger:
      default:
        return CategoryColor("Audio");
    }
  }

  plString IconForClipKind(plSequenceClipKind::Enum kind)
  {
    switch (kind)
    {
      case plSequenceClipKind::AnimationClip:
        return ":/AssetIcons/Animation_Clip.svg";
      case plSequenceClipKind::SubSequence:
        return ":/AssetIcons/Sequence.svg";
      case plSequenceClipKind::AudioTrigger:
      default:
        return plEditorUi::IconPath(plEditorUi::Icon::Volume);
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
  plString FriendlyPropertyName(plStringView sProperty)
  {
    plStringBuilder s = sProperty;
    s.TrimWordStart("Local");
    if (s.IsEqual("Scaling"))
      s = "Scale";
    return s;
  }

  /// "3 cuts", or szNone without any.
  plString CountLabel(plInt32 iCount, const char* szNone, const char* szUnit)
  {
    if (iCount == 0)
      return szNone;

    plStringBuilder s;
    s.SetFormat("{} {}", iCount, szUnit);
    return s;
  }
} // namespace

plSequenceModel::plSequenceModel(plSequenceAssetDocument* pDocument)
  : m_pDocument(pDocument)
{
  m_pDocument->GetObjectManager()->m_StructureEvents.AddEventHandler(plMakeDelegate(&plSequenceModel::DocumentStructureEventHandler, this));
  m_pDocument->GetObjectManager()->m_PropertyEvents.AddEventHandler(plMakeDelegate(&plSequenceModel::DocumentPropertyEventHandler, this));

  RebuildModel();
}

plSequenceModel::~plSequenceModel()
{
  m_pDocument->GetObjectManager()->m_StructureEvents.RemoveEventHandler(plMakeDelegate(&plSequenceModel::DocumentStructureEventHandler, this));
  m_pDocument->GetObjectManager()->m_PropertyEvents.RemoveEventHandler(plMakeDelegate(&plSequenceModel::DocumentPropertyEventHandler, this));
}

void plSequenceModel::Broadcast(plSequenceModelEvent::Type type)
{
  plSequenceModelEvent e;
  e.m_Type = type;
  m_Events.Broadcast(e);
}

void plSequenceModel::DocumentStructureEventHandler(const plDocumentObjectStructureEvent& e)
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
      Broadcast(plSequenceModelEvent::Type::KeysChanged);
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

void plSequenceModel::DocumentPropertyEventHandler(const plDocumentObjectPropertyEvent& e)
{
  if (e.m_pObject == nullptr)
    return;

  const plRTTI* pType = e.m_pObject->GetType();

  if (pType == plGetStaticRTTI<plCurveControlPointData>() || pType == plGetStaticRTTI<plColorControlPoint>() ||
      pType == plGetStaticRTTI<plAlphaControlPoint>() || pType == plGetStaticRTTI<plIntensityControlPoint>() ||
      pType == plGetStaticRTTI<plSequenceCameraCut>() || pType->IsDerivedFrom<plSequenceClip>())
  {
    Broadcast(plSequenceModelEvent::Type::KeysChanged);
    return;
  }

  if (pType != plGetStaticRTTI<plSequenceBinding>() && pType != plGetStaticRTTI<plSequenceTrack>())
    return;

  RebuildModel();
}

plInt32 plSequenceModel::AddNode(plInt32 iParent, RowKind kind)
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

void plSequenceModel::FlattenNode(plInt32 iNode, plUInt8 uiDepth)
{
  const Node& node = m_Nodes[iNode];

  FlatRow& row = m_FlatRows.ExpandAndGetRef();
  row.m_ObjectGuid = node.m_ObjectGuid;
  row.m_Kind = node.m_Kind;
  row.m_AxisTracks = node.m_AxisTracks;
  row.m_ClipKind = node.m_ClipKind;
  row.m_iNode = iNode;
  row.m_uiDepth = uiDepth;

  for (plInt32 iChild : node.m_Children)
  {
    FlattenNode(iChild, uiDepth + 1);
  }
}

void plSequenceModel::AddClipLaneNodes(const plDocumentObject* pOwnerObj, plInt32 iParentNode, const char* szDisplayPrefix)
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
    laneNode.m_sDisplay = plStringBuilder(szDisplayPrefix, NameForClipKind(kind));
    laneNode.m_Color = ColorForClipKind(kind);
    laneNode.m_sIconPath = IconForClipKind(kind);
    laneNode.m_sSubText = CountLabel(iClipsPerKind[k], "empty", "clips");
  }
}

void plSequenceModel::RebuildModel()
{
  m_Nodes.Clear();
  m_RootNodes.Clear();
  m_FlatRows.Clear();

  const plDocumentObject* pPropObj = m_pDocument->GetPropertyObject();

  // Camera cuts are sequence-level, not per-binding: one lane pinned above the bindings, always
  // present so there is somewhere to drop the first cut.
  {
    Node& cutNode = m_Nodes[AddNode(-1, RowKind::CameraCutLane)];
    cutNode.m_sDisplay = "Camera Cuts";
    cutNode.m_Color = CategoryColor("Rendering");
    cutNode.m_sIconPath = plEditorUi::IconPath(plEditorUi::Icon::Camera);

    const plInt32 iCutCount = pPropObj->GetTypeAccessor().GetCount("CameraCuts");
    cutNode.m_sSubText = CountLabel(iCutCount, "no cuts", "cuts");
  }

  // The event track is sequence-level too. It had a dock but no lane, which meant events could not
  // be seen against the shots and clips they are supposed to line up with.
  if (const plDocumentObject* pEventTrackObj = m_pDocument->GetEventTrackObject())
  {
    Node& eventNode = m_Nodes[AddNode(-1, RowKind::EventLane)];
    eventNode.m_ObjectGuid = pEventTrackObj->GetGuid();
    eventNode.m_sDisplay = "Events";
    eventNode.m_Color = CategoryColor("Gameplay");
    eventNode.m_sIconPath = plEditorUi::IconPath(plEditorUi::Icon::Event);

    const plInt32 iEventCount = pEventTrackObj->GetTypeAccessor().GetCount("ControlPoints");
    eventNode.m_sSubText = CountLabel(iEventCount, "no events", "events");
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
      bindingNode.m_sDisplay = name.IsA<plString>() ? name.Get<plString>() : plString("<binding>");

      const plDocumentObject* pBoundObj = m_pDocument->ResolveBindingObject(pBindingObj->GetGuid());
      const plString sPath = path.IsA<plString>() ? path.Get<plString>() : plString();

      // A binding that resolves to nothing animates nothing, in the editor and at runtime alike.
      // Saying so here is the difference between a silent cutscene and an obvious broken row.
      bindingNode.m_bUnresolved = pBoundObj == nullptr;

      if (bindingNode.m_bUnresolved)
      {
        bindingNode.m_sSubText = plStringBuilder("missing: ", sPath.IsEmpty() ? plStringView("<no path>") : sPath.GetView());
        bindingNode.m_Color = plEditorUi::Danger();
        bindingNode.m_Color.a = 255;
      }
      else
      {
        bindingNode.m_sSubText = sPath;
        bindingNode.m_Color = CategoryColor("Animation");
      }

      // what the bound object actually is, at a glance
      bindingNode.m_sIconPath = IconForBoundObject(pBoundObj);
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

      plString sComponent;
      plStringBuilder sRawComponent;
      if (componentType.IsA<plString>() && !componentType.Get<plString>().IsEmpty())
      {
        sRawComponent = componentType.Get<plString>();
        plStringBuilder sTrimmed = sRawComponent;
        sTrimmed.TrimWordStart("pl");
        sComponent = sTrimmed;
      }

      const plString sProperty = propertyPath.IsA<plString>() ? propertyPath.Get<plString>() : plString();
      const plInt64 iTarget = target.IsValid() ? target.ConvertTo<plInt64>() : (plInt64)plPropertyAnimTarget::Number;
      const int iAxis = AxisOfTarget(iTarget);

      plInt32 iParentNode = iBindingNode;
      plString sDisplay = FriendlyPropertyName(sProperty);

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
          group.m_sIconPath = plEditorUi::IconPath(plEditorUi::Icon::Move);

          it = vectorGroups.Insert(sKey, iGroupNode);
        }

        iParentNode = it.Value();
        m_Nodes[iParentNode].m_AxisTracks.PushBack(pTrackObj->GetGuid());
        sDisplay = AxisName(iAxis); // just "X" under a "Position" group
      }

      const plInt32 iTrackNode = AddNode(iParentNode, RowKind::Track);
      Node& trackNode = m_Nodes[iTrackNode];
      trackNode.m_ObjectGuid = pTrackObj->GetGuid();
      trackNode.m_sDisplay = sDisplay;
      trackNode.m_sSubText = iAxis >= 0 ? plString() : sComponent;
      trackNode.m_Color = TrackColorFor(sComponent, iTarget);
      trackNode.m_iAxis = iAxis;
      if (iAxis < 0)
        trackNode.m_sIconPath = IconForTrack(sComponent, iTarget);
    }

    // A group's sub-text says which axes it actually holds, so a partially keyed vector is obvious.
    for (auto it : vectorGroups)
    {
      Node& group = m_Nodes[it.Value()];
      plStringBuilder sAxes;
      for (plInt32 iChild : group.m_Children)
      {
        sAxes.Append(m_Nodes[iChild].m_sDisplay);
      }

      if (group.m_sSubText.IsEmpty())
        group.m_sSubText = sAxes;
      else
        group.m_sSubText = plStringBuilder(group.m_sSubText, " \xC2\xB7 ", sAxes);
    }

    AddClipLaneNodes(pBindingObj, iBindingNode, "");
  }

  for (plInt32 iRoot : m_RootNodes)
  {
    FlattenNode(iRoot, 0);
  }

  Broadcast(plSequenceModelEvent::Type::ModelRebuilt);
}
