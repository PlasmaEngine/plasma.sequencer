#pragma once

#include <ToolsFoundation/Object/DocumentObjectManager.h>

/// \brief Object manager for Sequence documents.
///
/// Same rule as PropertyAnim's: the attached context scene is mirrored into this document as
/// temporary objects, and a sequence must not restructure the scene it animates - it only reads it
/// and keys its properties. Structure changes on temporaries are therefore rejected unless
/// explicitly allowed (which the context-attach machinery does while it rebuilds the mirror).
class plSequenceObjectManager : public plDocumentObjectManager
{
public:
  plSequenceObjectManager();
  ~plSequenceObjectManager();

  bool GetAllowStructureChangeOnTemporaries() const { return m_bAllowStructureChangeOnTemporaries; }
  void SetAllowStructureChangeOnTemporaries(bool bVal) { m_bAllowStructureChangeOnTemporaries = bVal; }

private:
  virtual plStatus InternalCanAdd(
    const plRTTI* pRtti, const plDocumentObject* pParent, plStringView sParentProperty, const plVariant& index) const override;
  virtual plStatus InternalCanRemove(const plDocumentObject* pObject) const override;
  virtual plStatus InternalCanMove(
    const plDocumentObject* pObject, const plDocumentObject* pNewParent, plStringView sParentProperty, const plVariant& index) const override;

private:
  bool m_bAllowStructureChangeOnTemporaries = false;
};
