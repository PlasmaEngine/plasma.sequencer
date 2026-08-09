#pragma once

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <ToolsFoundation/Object/ObjectCommandAccessor.h>

class plSequenceObjectManager;

/// \brief The object accessor that makes editing the attached context scene mean "record a key".
///
/// Everything that edits a document object goes through the document's accessor - the property
/// grid, and crucially the transform gizmos. Objects of the mirrored context scene are temporaries:
/// writing to them directly would be meaningless (they are never saved) and would fight the
/// scrub evaluation. So writes to a temporary are redirected into curve keys at the playhead,
/// creating the binding and track on demand. That is auto-key, and it is also what makes the
/// manipulators useful in this window.
///
/// Structure changes on temporaries stay rejected - a sequence animates the scene, it does not
/// restructure it.
class plSequenceObjectAccessor : public plObjectCommandAccessor
{
  PL_ADD_DYNAMIC_REFLECTION(plSequenceObjectAccessor, plObjectCommandAccessor);

public:
  plSequenceObjectAccessor(plSequenceAssetDocument* pDoc, plCommandHistory* pHistory);

  virtual plStatus SetValue(
    const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index = plVariant()) override;

  virtual plStatus InsertValue(
    const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& newValue, plVariant index = plVariant()) override;
  virtual plStatus RemoveValue(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index = plVariant()) override;
  virtual plStatus MoveValue(
    const plDocumentObject* pObject, const plAbstractProperty* pProp, const plVariant& oldIndex, const plVariant& newIndex) override;

  virtual plStatus AddObject(const plDocumentObject* pParent, const plAbstractProperty* pParentProp, const plVariant& index, const plRTTI* pType,
    plUuid& inout_objectGuid) override;
  virtual plStatus RemoveObject(const plDocumentObject* pObject) override;
  virtual plStatus MoveObject(
    const plDocumentObject* pObject, const plDocumentObject* pNewParent, const plAbstractProperty* pParentProp, const plVariant& index) override;

private:
  bool IsTemporary(const plDocumentObject* pObject) const;
  bool IsTemporary(const plDocumentObject* pParent, const plAbstractProperty* pParentProp) const;

  /// Finds the track for this property, creating it - seeded with a key holding the pre-edit value
  /// at tick 0, so the animation starts from where the scene was - if it does not exist yet.
  plUuid FindOrAddTrack(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target,
    double fValueBeforeEdit);

  plStatus SetCurveCp(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plPropertyAnimTarget::Enum target,
    double fOldValue, double fNewValue);
  plStatus SetOrInsertCurveCp(const plUuid& trackGuid, double fValue);

  // Colour is keyed as three channels on the track's gradient rather than one curve.
  plStatus SetColorCurveCp(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, const plColorGammaUB& oldValue,
    const plColorGammaUB& newValue);
  plStatus SetAlphaCurveCp(const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plUInt8 uiOldValue, plUInt8 uiNewValue);
  plStatus SetIntensityCurveCp(
    const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, float fOldValue, float fNewValue);

  /// Splits an HDR colour into the gamma colour, alpha and intensity the gradient stores separately.
  static void SeparateColor(const plColor& color, plColorGammaUB& out_gamma, plUInt8& out_uiAlpha, float& out_fIntensity);

  plUuid FindOrAddColorTrack(
    const plDocumentObject* pObject, const plAbstractProperty* pProp, plVariant index, plDelegate<void(const plUuid&)> onCreated);

  plUniquePtr<plObjectAccessorBase> m_pDirectAccessor;
  plSequenceAssetDocument* m_pDocument = nullptr;
  plSequenceObjectManager* m_pObjectManager = nullptr;
};
