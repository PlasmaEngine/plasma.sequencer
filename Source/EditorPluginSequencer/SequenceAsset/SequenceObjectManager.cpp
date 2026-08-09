#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceObjectManager.h>

plSequenceObjectManager::plSequenceObjectManager() = default;

plSequenceObjectManager::~plSequenceObjectManager() = default;

plStatus plSequenceObjectManager::InternalCanAdd(
  const plRTTI* pRtti, const plDocumentObject* pParent, plStringView sParentProperty, const plVariant& index) const
{
  PL_IGNORE_UNUSED(pRtti);
  PL_IGNORE_UNUSED(index);

  if (m_bAllowStructureChangeOnTemporaries)
    return plStatus(PL_SUCCESS);

  if (IsTemporary(pParent, sParentProperty))
    return plStatus("The structure of the context cannot be changed by a sequence.");
  return plStatus(PL_SUCCESS);
}

plStatus plSequenceObjectManager::InternalCanRemove(const plDocumentObject* pObject) const
{
  if (m_bAllowStructureChangeOnTemporaries)
    return plStatus(PL_SUCCESS);

  if (IsTemporary(pObject))
    return plStatus("The structure of the context cannot be changed by a sequence.");
  return plStatus(PL_SUCCESS);
}

plStatus plSequenceObjectManager::InternalCanMove(
  const plDocumentObject* pObject, const plDocumentObject* pNewParent, plStringView sParentProperty, const plVariant& index) const
{
  PL_IGNORE_UNUSED(pNewParent);
  PL_IGNORE_UNUSED(sParentProperty);
  PL_IGNORE_UNUSED(index);

  if (m_bAllowStructureChangeOnTemporaries)
    return plStatus(PL_SUCCESS);

  if (IsTemporary(pObject))
    return plStatus("The structure of the context cannot be changed by a sequence.");
  return plStatus(PL_SUCCESS);
}
