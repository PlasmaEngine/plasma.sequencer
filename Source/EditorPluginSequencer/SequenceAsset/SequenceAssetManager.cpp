#include <EditorPluginSequencer/EditorPluginSequencerPCH.h>

#include <EditorPluginSequencer/SequenceAsset/SequenceAsset.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAssetManager.h>
#include <EditorPluginSequencer/SequenceAsset/SequenceAssetWindow.h>

PL_BEGIN_DYNAMIC_REFLECTED_TYPE(plSequenceAssetDocumentManager, 1, plRTTIDefaultAllocator<plSequenceAssetDocumentManager>)
PL_END_DYNAMIC_REFLECTED_TYPE;

plSequenceAssetDocumentManager::plSequenceAssetDocumentManager()
{
  plDocumentManager::s_Events.AddEventHandler(plMakeDelegate(&plSequenceAssetDocumentManager::OnDocumentManagerEvent, this));

  m_DocTypeDesc.m_sDocumentTypeName = "Sequence";
  m_DocTypeDesc.m_sFileExtension = "plSequenceAsset";
  m_DocTypeDesc.m_sIcon = ":/AssetIcons/Sequence.svg";
  m_DocTypeDesc.m_sAssetCategory = "Animation";
  m_DocTypeDesc.m_pDocumentType = plGetStaticRTTI<plSequenceAssetDocument>();
  m_DocTypeDesc.m_pManager = this;
  m_DocTypeDesc.m_CompatibleTypes.PushBack("CompatibleAsset_Sequence");
  m_DocTypeDesc.m_sResourceFileExtension = "plBinSequence";
  m_DocTypeDesc.m_AssetDocumentFlags = plAssetDocumentFlags::AutoTransformOnSave;
}

plSequenceAssetDocumentManager::~plSequenceAssetDocumentManager()
{
  plDocumentManager::s_Events.RemoveEventHandler(plMakeDelegate(&plSequenceAssetDocumentManager::OnDocumentManagerEvent, this));
}

void plSequenceAssetDocumentManager::OnDocumentManagerEvent(const plDocumentManager::Event& e)
{
  if (e.m_Type == plDocumentManager::Event::Type::DocumentWindowRequested &&
      e.m_pDocument->GetDynamicRTTI() == plGetStaticRTTI<plSequenceAssetDocument>())
  {
    new plSequenceAssetDocumentWindow(static_cast<plSequenceAssetDocument*>(e.m_pDocument)); // NOLINT: deletes itself when the document closes
  }
}

void plSequenceAssetDocumentManager::InternalCreateDocument(plStringView sDocumentTypeName, plStringView sPath, bool bCreateNewDocument,
  plDocument*& out_pDocument, const plDocumentObject* pOpenContext)
{
  PL_IGNORE_UNUSED(sDocumentTypeName);
  PL_IGNORE_UNUSED(bCreateNewDocument);
  PL_IGNORE_UNUSED(pOpenContext);

  out_pDocument = new plSequenceAssetDocument(sPath);
}

void plSequenceAssetDocumentManager::InternalGetSupportedDocumentTypes(plDynamicArray<const plDocumentTypeDescriptor*>& inout_documentTypes) const
{
  inout_documentTypes.PushBack(&m_DocTypeDesc);
}

PL_STATICLINK_FILE(EditorPluginSequencer, EditorPluginSequencer_SequenceAsset_SequenceAssetManager);
