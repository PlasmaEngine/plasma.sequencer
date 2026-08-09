#include <EnginePluginSequencer/EnginePluginSequencerPCH.h>

#include <EditorEngineProcessFramework/EngineProcess/EngineProcessApp.h>
#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <EnginePluginSequencer/SequenceAsset/SequenceContext.h>
#include <EnginePluginSequencer/SequenceAsset/SequenceView.h>
#include <RendererCore/Pipeline/View.h>
#include <RendererCore/RenderWorld/RenderWorld.h>

plSequenceViewContext::plSequenceViewContext(plSequenceContext* pContext)
  : plEngineProcessViewContext(pContext)
{
  m_pSequenceContext = pContext;

  m_Camera.SetCameraMode(plCameraMode::PerspectiveFixedFovY, 45.0f, 0.05f, 1000.0f);
  m_Camera.LookAt(plVec3(-4.0f, -4.0f, 3.0f), plVec3::MakeZero(), plVec3(0, 0, 1));
}

void plSequenceViewContext::HandleViewMessage(const plEditorEngineViewMsg* pMsg)
{
  plEngineProcessViewContext::HandleViewMessage(pMsg);

  if (pMsg->GetDynamicRTTI()->IsDerivedFrom<plViewRedrawMsgToEngine>())
  {
    // The picking pass only runs when the editor asks for it, so its state has to be pushed every
    // redraw rather than set once.
    const plViewRedrawMsgToEngine* pRedraw = static_cast<const plViewRedrawMsgToEngine*>(pMsg);

    plView* pView = nullptr;
    if (plRenderWorld::TryGetView(m_hView, pView))
    {
      pView->SetRenderPassProperty("EditorPickingPass", "Active", pRedraw->m_bUpdatePickingData);
      pView->SetRenderPassProperty("EditorPickingPass", "PickSelected", pRedraw->m_bEnablePickingSelected);
      pView->SetRenderPassProperty("EditorPickingPass", "PickTransparent", pRedraw->m_bEnablePickTransparent);
    }
  }
  else if (pMsg->GetDynamicRTTI()->IsDerivedFrom<plViewPickingMsgToEngine>())
  {
    const plViewPickingMsgToEngine* pPick = static_cast<const plViewPickingMsgToEngine*>(pMsg);
    PickObjectAt(pPick->m_uiPickPosX, pPick->m_uiPickPosY);
  }
}

plViewHandle plSequenceViewContext::CreateView()
{
  plView* pView = nullptr;
  plRenderWorld::CreateView("Sequence Editor - View", pView);
  pView->SetCameraUsageHint(plCameraUsageHint::EditorView);
  pView->SetRenderPipelineResource(CreateDefaultRenderPipeline());
  pView->SetWorld(GetDocumentContext()->GetWorld());
  pView->SetCamera(&m_Camera);

  const plTag& tagHidden = plTagRegistry::GetGlobalRegistry().RegisterTag("EditorHidden");
  pView->m_ExcludeTags.Set(tagHidden);

  return pView->GetHandle();
}

void plSequenceViewContext::PickObjectAt(plUInt16 x, plUInt16 y)
{
  // remote processes do not support picking, just ignore this
  if (plEditorEngineProcessApp::GetSingleton()->IsRemoteMode())
    return;

  // Sent unconditionally: the editor waits for a reply, and staying silent on a miss would leave
  // it holding a stale result rather than learning that nothing was hit.
  plViewPickingResultMsgToEditor res;
  PL_SCOPE_EXIT(SendViewMessage(&res));

  plView* pView = nullptr;
  if (!plRenderWorld::TryGetView(m_hView, pView))
    return;

  pView->SetRenderPassProperty("EditorPickingPass", "PickingPosition", plVec2(x, y));

  if (!pView->IsRenderPassReadBackPropertyExisting("EditorPickingPass", "PickedPosition"))
    return;

  const plVariant varPickedPos = pView->GetRenderPassReadBackProperty("EditorPickingPass", "PickedPosition");
  if (!varPickedPos.IsA<plVec3>())
    return;

  const plUInt32 uiPickingID = pView->GetRenderPassReadBackProperty("EditorPickingPass", "PickedID").ConvertTo<plUInt32>();
  res.m_vPickedNormal = pView->GetRenderPassReadBackProperty("EditorPickingPass", "PickedNormal").ConvertTo<plVec3>();
  res.m_vPickingRayStartPosition = pView->GetRenderPassReadBackProperty("EditorPickingPass", "PickedRayStartPosition").ConvertTo<plVec3>();
  res.m_vPickedPosition = varPickedPos.ConvertTo<plVec3>();

  const plUInt32 uiComponentID = (uiPickingID & 0x00FFFFFF);
  const plUInt32 uiPartIndex = (uiPickingID >> 24) & 0xFF;

  plWorldRttiConverterContext& context = GetDocumentContext()->m_Context;

  res.m_ComponentGuid = context.m_ComponentPickingMap.GetGuid(uiComponentID);
  if (res.m_ComponentGuid.IsValid())
  {
    const plComponentHandle hComponent = context.m_ComponentMap.GetHandle(res.m_ComponentGuid);

    plComponent* pComponent = nullptr;
    if (GetDocumentContext()->GetWorld()->TryGetComponent<plComponent>(hComponent, pComponent))
    {
      res.m_ObjectGuid = context.m_GameObjectMap.GetGuid(pComponent->GetOwner()->GetHandle());
      res.m_uiPartIndex = uiPartIndex;
    }
    else
    {
      res.m_ComponentGuid = plUuid();
    }
  }

  // Gizmo handles are not scene components - they live in the "other" picking map, and this is what
  // lets a gizmo axis be grabbed rather than only looked at.
  res.m_OtherGuid = context.m_OtherPickingMap.GetGuid(uiComponentID);
}

PL_STATICLINK_FILE(EnginePluginSequencer, EnginePluginSequencer_SequenceAsset_SequenceView);
