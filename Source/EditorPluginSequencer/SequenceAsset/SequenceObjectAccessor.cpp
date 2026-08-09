#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceObjectAccessor.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceObjectManager.h>
#include <Foundation/Reflection/ReflectionUtils.h>

// clang-format off
PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceObjectAccessor, 1, plRTTINoAllocator)
PL_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

plSequenceObjectAccessor::plSequenceObjectAccessor(plSequenceAssetDocument* pDoc, plCommandHistory* pHistory)
  : plObjectCommandAccessor(pHistory)
  , m_pDocument(pDoc)
  , m_pObjectManager(static_cast<plSequenceObjectManager*>(pDoc->GetObjectManager()))
{
  m_pDirectAccessor = PL_DEFAULT_NEW(plObjectCommandAccessor, pHistory);
}

bool plSequenceObjectAccessor::IsTemporary(const plDocumentObject* pObject) const
{
  return m_pObjectManager->IsTemporary(pObject);
}

bool plSequenceObjectAccessor::IsTemporary(const plDocumentObject* pParent, const plAbstractProperty* pParentProp) const
{
  return m_pObjectManager->IsTemporary(pParent, pParentProp->GetPropertyName());
}

plStatus plSequenceObjectAccessor::SetValue(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index)
{
  if (!IsTemporary(pObject))
    return plObjectCommandAccessor::SetValue(pObject, pProp, newValue, index);

  if (!m_pDocument->GetAutoKey())
    return plStatus("Auto-Key is off: enable it in the Sequencer toolbar to record changes to the scene.");

  plVariant oldValue;
  PL_SUCCEED_OR_RETURN(m_pDirectAccessor->GetValue(pObject, pProp, oldValue, index));

  const plVariantType::Enum type = pProp->GetSpecificType()->GetVariantType();

  if (type >= plVariantType::Bool && type <= plVariantType::Double)
  {
    return SetCurveCp(pObject, pProp, index, plPropertyAnimTarget::Number, oldValue.ConvertTo<double>(), newValue.ConvertTo<double>());
  }

  if (type >= plVariantType::Vector2 && type <= plVariantType::Vector4U)
  {
    const plUInt32 uiComponents = plReflectionUtils::GetComponentCount(type);

    for (plUInt32 c = 0; c < uiComponents; ++c)
    {
      const double fOldValue = plReflectionUtils::GetComponent(oldValue, c);
      const double fNewValue = plReflectionUtils::GetComponent(newValue, c);

      // only key the axes the user actually moved
      if (plMath::IsEqual(fOldValue, fNewValue, plMath::SmallEpsilon<double>()))
        continue;

      PL_SUCCEED_OR_RETURN(
        SetCurveCp(pObject, pProp, index, static_cast<plPropertyAnimTarget::Enum>((int)plPropertyAnimTarget::VectorX + c), fOldValue, fNewValue));
    }

    return plStatus(PL_SUCCESS);
  }

  if (type == plVariantType::Quaternion)
  {
    plAngle oldEuler[3];
    oldValue.Get<plQuat>().GetAsEulerAngles(oldEuler[0], oldEuler[1], oldEuler[2]);
    plAngle newEuler[3];
    newValue.Get<plQuat>().GetAsEulerAngles(newEuler[0], newEuler[1], newEuler[2]);

    // Rotation is keyed per euler axis, and the curve - not the object - is the authority on where
    // the rotation currently is, so re-read each axis from its track before diffing.
    for (plUInt32 c = 0; c < 3; ++c)
    {
      const auto target = static_cast<plPropertyAnimTarget::Enum>((int)plPropertyAnimTarget::RotationX + c);
      PL_SUCCEED_OR_RETURN(m_pDocument->CanAnimate(pObject, pProp, index, target));

      const plUuid trackGuid = FindOrAddTrack(pObject, pProp, index, target, oldEuler[c].GetDegree());
      if (const plSequenceTrack* pTrack = m_pDocument->GetTrack(trackGuid))
      {
        oldEuler[c] = plAngle::MakeFromDegree((float)pTrack->m_FloatCurve.Evaluate((plInt64)m_pDocument->GetScrubberPosition()));
      }
    }

    for (plUInt32 c = 0; c < 3; ++c)
    {
      // assume the change is less than 180 degrees, so the curve keeps winding rather than snapping
      float fDiff = (newEuler[c] - oldEuler[c]).GetDegree();
      fDiff -= plMath::RoundToMultiple(fDiff, 360.0f);
      newEuler[c] = oldEuler[c] + plAngle::MakeFromDegree(fDiff);

      if (oldEuler[c].IsEqualSimple(newEuler[c], plAngle::MakeFromDegree(0.01f)))
        continue;

      PL_SUCCEED_OR_RETURN(SetCurveCp(pObject, pProp, index, static_cast<plPropertyAnimTarget::Enum>((int)plPropertyAnimTarget::RotationX + c),
        oldEuler[c].GetDegree(), newEuler[c].GetDegree()));
    }

    return plStatus(PL_SUCCESS);
  }

  if (type == plVariantType::Color || type == plVariantType::ColorGamma)
  {
    // A colour is three separately keyed channels: the gradient's colour, alpha and intensity
    // control points. Splitting it here keeps HDR intensity out of the 8-bit colour value.
    plColorGammaUB oldGamma, newGamma;
    plUInt8 uiOldAlpha, uiNewAlpha;
    float fOldIntensity, fNewIntensity;

    if (type == plVariantType::Color)
    {
      SeparateColor(oldValue.Get<plColor>(), oldGamma, uiOldAlpha, fOldIntensity);
      SeparateColor(newValue.Get<plColor>(), newGamma, uiNewAlpha, fNewIntensity);
    }
    else
    {
      oldGamma = oldValue.Get<plColorGammaUB>();
      uiOldAlpha = oldGamma.a;
      oldGamma.a = 255;
      fOldIntensity = 1.0f;

      newGamma = newValue.Get<plColorGammaUB>();
      uiNewAlpha = newGamma.a;
      newGamma.a = 255;
      fNewIntensity = 1.0f;
    }

    if (oldGamma != newGamma)
    {
      PL_SUCCEED_OR_RETURN(SetColorCurveCp(pObject, pProp, index, oldGamma, newGamma));
    }

    if (uiOldAlpha != uiNewAlpha)
    {
      PL_SUCCEED_OR_RETURN(SetAlphaCurveCp(pObject, pProp, index, uiOldAlpha, uiNewAlpha));
    }

    if (!plMath::IsEqual(fOldIntensity, fNewIntensity, plMath::SmallEpsilon<float>()))
    {
      PL_SUCCEED_OR_RETURN(SetIntensityCurveCp(pObject, pProp, index, fOldIntensity, fNewIntensity));
    }

    return plStatus(PL_SUCCESS);
  }

  return plStatus(plFmt("The property '{0}' cannot be animated.", pProp->GetPropertyName()));
}

plUuid plSequenceObjectAccessor::FindOrAddTrack(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target, double fValueBeforeEdit)
{
  plUuid trackGuid = m_pDocument->FindTrack(pObject, pProp, index, target);

  if (!trackGuid.IsValid())
  {
    // Gizmo drags run inside a temporary transaction that is rolled back and replayed continuously.
    // Creating the track inside it would make it appear and vanish with every mouse move, so step
    // outside for the structural part.
    plCommandHistory* pHistory = m_pDocument->GetCommandHistory();
    const bool bWasTemporary = pHistory->InTemporaryTransaction();
    if (bWasTemporary)
      pHistory->SuspendTemporaryTransaction();

    trackGuid = m_pDocument->CreateTrack(pObject, pProp, index, target);

    // seed the curve with where the scene was, so the animation starts from the authored pose
    m_pDocument->InsertCurveCpAt(trackGuid, 0, fValueBeforeEdit);

    if (bWasTemporary)
      pHistory->ResumeTemporaryTransaction();
  }

  return trackGuid;
}

plStatus plSequenceObjectAccessor::SetCurveCp(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index,
  plPropertyAnimTarget::Enum target, double fOldValue, double fNewValue)
{
  PL_SUCCEED_OR_RETURN(m_pDocument->CanAnimate(pObject, pProp, index, target));

  const plUuid trackGuid = FindOrAddTrack(pObject, pProp, index, target, fOldValue);
  return SetOrInsertCurveCp(trackGuid, fNewValue);
}

void plSequenceObjectAccessor::SeparateColor(const plColor& color, plColorGammaUB& out_gamma, plUInt8& out_uiAlpha, float& out_fIntensity)
{
  out_uiAlpha = static_cast<plColorGammaUB>(color).a;
  out_fIntensity = plMath::Max(color.r, color.g, color.b);

  if (out_fIntensity > 1.0f)
  {
    out_gamma = (color / out_fIntensity);
  }
  else
  {
    out_fIntensity = 1.0f;
    out_gamma = color;
  }

  out_gamma.a = 255;
}

plStatus plSequenceObjectAccessor::SetColorCurveCp(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, const plColorGammaUB& oldValue, const plColorGammaUB& newValue)
{
  PL_SUCCEED_OR_RETURN(m_pDocument->CanAnimate(pObject, pProp, index, plPropertyAnimTarget::Color));

  const plUuid trackGuid = FindOrAddColorTrack(pObject, pProp, index,
    [this, &oldValue](const plUuid& guid) { m_pDocument->InsertGradientColorCpAt(guid, 0, oldValue); });

  const plInt64 iTick = (plInt64)m_pDocument->GetScrubberPosition();
  const plUuid cpGuid = m_pDocument->FindGradientColorCp(trackGuid, iTick);

  if (cpGuid.IsValid())
  {
    const plDocumentObject* pCp = GetObject(cpGuid);
    PL_SUCCEED_OR_RETURN(m_pDirectAccessor->SetValueByName(pCp, "Red", newValue.r));
    PL_SUCCEED_OR_RETURN(m_pDirectAccessor->SetValueByName(pCp, "Green", newValue.g));
    PL_SUCCEED_OR_RETURN(m_pDirectAccessor->SetValueByName(pCp, "Blue", newValue.b));
    return plStatus(PL_SUCCESS);
  }

  plCommandHistory* pHistory = m_pDocument->GetCommandHistory();
  const bool bWasTemporary = pHistory->InTemporaryTransaction();
  if (bWasTemporary)
    pHistory->SuspendTemporaryTransaction();

  m_pDocument->InsertGradientColorCpAt(trackGuid, iTick, newValue);

  if (bWasTemporary)
    pHistory->ResumeTemporaryTransaction();

  return plStatus(PL_SUCCESS);
}

plStatus plSequenceObjectAccessor::SetAlphaCurveCp(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plUInt8 uiOldValue, plUInt8 uiNewValue)
{
  PL_SUCCEED_OR_RETURN(m_pDocument->CanAnimate(pObject, pProp, index, plPropertyAnimTarget::Color));

  const plUuid trackGuid = FindOrAddColorTrack(pObject, pProp, index,
    [this, uiOldValue](const plUuid& guid) { m_pDocument->InsertGradientAlphaCpAt(guid, 0, uiOldValue); });

  const plInt64 iTick = (plInt64)m_pDocument->GetScrubberPosition();
  const plUuid cpGuid = m_pDocument->FindGradientAlphaCp(trackGuid, iTick);

  if (cpGuid.IsValid())
  {
    return m_pDirectAccessor->SetValueByName(GetObject(cpGuid), "Alpha", uiNewValue);
  }

  plCommandHistory* pHistory = m_pDocument->GetCommandHistory();
  const bool bWasTemporary = pHistory->InTemporaryTransaction();
  if (bWasTemporary)
    pHistory->SuspendTemporaryTransaction();

  m_pDocument->InsertGradientAlphaCpAt(trackGuid, iTick, uiNewValue);

  if (bWasTemporary)
    pHistory->ResumeTemporaryTransaction();

  return plStatus(PL_SUCCESS);
}

plStatus plSequenceObjectAccessor::SetIntensityCurveCp(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, float fOldValue, float fNewValue)
{
  PL_SUCCEED_OR_RETURN(m_pDocument->CanAnimate(pObject, pProp, index, plPropertyAnimTarget::Color));

  const plUuid trackGuid = FindOrAddColorTrack(pObject, pProp, index,
    [this, fOldValue](const plUuid& guid) { m_pDocument->InsertGradientIntensityCpAt(guid, 0, fOldValue); });

  const plInt64 iTick = (plInt64)m_pDocument->GetScrubberPosition();
  const plUuid cpGuid = m_pDocument->FindGradientIntensityCp(trackGuid, iTick);

  if (cpGuid.IsValid())
  {
    return m_pDirectAccessor->SetValueByName(GetObject(cpGuid), "Intensity", fNewValue);
  }

  plCommandHistory* pHistory = m_pDocument->GetCommandHistory();
  const bool bWasTemporary = pHistory->InTemporaryTransaction();
  if (bWasTemporary)
    pHistory->SuspendTemporaryTransaction();

  m_pDocument->InsertGradientIntensityCpAt(trackGuid, iTick, fNewValue);

  if (bWasTemporary)
    pHistory->ResumeTemporaryTransaction();

  return plStatus(PL_SUCCESS);
}

plUuid plSequenceObjectAccessor::FindOrAddColorTrack(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plDelegate<void(const plUuid&)> onCreated)
{
  plUuid trackGuid = m_pDocument->FindTrack(pObject, pProp, index, plPropertyAnimTarget::Color);

  if (!trackGuid.IsValid())
  {
    plCommandHistory* pHistory = m_pDocument->GetCommandHistory();
    const bool bWasTemporary = pHistory->InTemporaryTransaction();
    if (bWasTemporary)
      pHistory->SuspendTemporaryTransaction();

    trackGuid = m_pDocument->CreateTrack(pObject, pProp, index, plPropertyAnimTarget::Color);
    onCreated(trackGuid); // seed tick 0 with the pre-edit value, so the gradient starts where the scene was

    if (bWasTemporary)
      pHistory->ResumeTemporaryTransaction();
  }

  return trackGuid;
}

plStatus plSequenceObjectAccessor::SetOrInsertCurveCp(const plUuid& trackGuid, double fValue)
{
  const plInt64 iScrubberPos = (plInt64)m_pDocument->GetScrubberPosition();

  const plUuid cpGuid = m_pDocument->FindCurveCp(trackGuid, iScrubberPos);
  if (cpGuid.IsValid())
  {
    // a key already sits at the playhead - update it rather than stacking a second one
    PL_SUCCEED_OR_RETURN(m_pDirectAccessor->SetValueByName(GetObject(cpGuid), "Value", fValue));
    return plStatus(PL_SUCCESS);
  }

  plCommandHistory* pHistory = m_pDocument->GetCommandHistory();
  const bool bWasTemporary = pHistory->InTemporaryTransaction();
  if (bWasTemporary)
    pHistory->SuspendTemporaryTransaction();

  m_pDocument->InsertCurveCpAt(trackGuid, iScrubberPos, fValue);

  if (bWasTemporary)
    pHistory->ResumeTemporaryTransaction();

  return plStatus(PL_SUCCESS);
}

// ---- structure changes on the context stay forbidden ------------------------

plStatus plSequenceObjectAccessor::InsertValue(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index)
{
  if (IsTemporary(pObject))
    return plStatus("A sequence animates the context scene, it cannot restructure it.");

  return plObjectCommandAccessor::InsertValue(pObject, pProp, newValue, index);
}

plStatus plSequenceObjectAccessor::RemoveValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index)
{
  if (IsTemporary(pObject))
    return plStatus("A sequence animates the context scene, it cannot restructure it.");

  return plObjectCommandAccessor::RemoveValue(pObject, pProp, index);
}

plStatus plSequenceObjectAccessor::MoveValue(
  const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& oldIndex, const plVariant& newIndex)
{
  if (IsTemporary(pObject))
    return plStatus("A sequence animates the context scene, it cannot restructure it.");

  return plObjectCommandAccessor::MoveValue(pObject, pProp, oldIndex, newIndex);
}

plStatus plSequenceObjectAccessor::AddObject(
  const plDocumentObject* pParent, const plAbstractProperty* pParentProp, const plVariant& index, const plRTTI* pType, plUuid& inout_objectGuid)
{
  if (IsTemporary(pParent, pParentProp))
    return plStatus("A sequence animates the context scene, it cannot restructure it.");

  return plObjectCommandAccessor::AddObject(pParent, pParentProp, index, pType, inout_objectGuid);
}

plStatus plSequenceObjectAccessor::RemoveObject(const plDocumentObject* pObject)
{
  if (IsTemporary(pObject))
    return plStatus("A sequence animates the context scene, it cannot restructure it.");

  return plObjectCommandAccessor::RemoveObject(pObject);
}

plStatus plSequenceObjectAccessor::MoveObject(
  const plDocumentObject* pObject, const plDocumentObject* pNewParent, const plAbstractProperty* pParentProp, const plVariant& index)
{
  if (IsTemporary(pObject))
    return plStatus("A sequence animates the context scene, it cannot restructure it.");

  return plObjectCommandAccessor::MoveObject(pObject, pNewParent, pParentProp, index);
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceObjectAccessor);
