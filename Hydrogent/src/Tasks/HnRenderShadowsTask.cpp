/*
 *  Copyright 2024 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include "Tasks/HnRenderShadowsTask.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderPassState.hpp"
#include "HnTokens.hpp"
#include "HnRenderPass.hpp"
#include "HnRenderParam.hpp"
#include "HnLight.hpp"
#include "HnShadowMapManager.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"

namespace Diligent
{

namespace USD
{

HnRenderShadowsTask::HnRenderShadowsTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id) :
    HnTask{Id}
{
}

HnRenderShadowsTask::~HnRenderShadowsTask()
{
}

void HnRenderShadowsTask::Sync(pxr::HdSceneDelegate* Delegate,
                               pxr::HdTaskContext*   TaskCtx,
                               pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits & pxr::HdChangeTracker::DirtyCollection)
    {
        pxr::VtValue           CollectionVal = Delegate->Get(GetId(), pxr::HdTokens->collection);
        pxr::HdRprimCollection Collection    = CollectionVal.Get<pxr::HdRprimCollection>();

        if (Collection.GetName().IsEmpty())
        {
            m_RenderPass.reset();
        }
        else
        {
            if (!m_RenderPass)
            {
                pxr::HdRenderIndex&    Index          = Delegate->GetRenderIndex();
                pxr::HdRenderDelegate* RenderDelegate = Index.GetRenderDelegate();

                m_RenderPass = std::static_pointer_cast<HnRenderPass>(RenderDelegate->CreateRenderPass(&Index, Collection));

                {
                    pxr::VtValue ParamsValue = Delegate->Get(GetId(), HnTokens->renderPassParams);
                    if (ParamsValue.IsHolding<HnRenderPassParams>())
                    {
                        HnRenderPassParams RenderPassParams = ParamsValue.UncheckedGet<HnRenderPassParams>();
                        m_RenderPass->SetParams(RenderPassParams);
                    }
                    else
                    {
                        UNEXPECTED("Unexpected type of render pass parameters ", ParamsValue.GetTypeName());
                    }
                }

                // Need to set params for the new render pass.
                *DirtyBits |= pxr::HdChangeTracker::DirtyParams;
            }
            else
            {
                m_RenderPass->SetRprimCollection(Collection);
            }
        }
    }

    if (*DirtyBits & pxr::HdChangeTracker::DirtyParams)
    {
        HnRenderShadowsTaskParams Params;
        if (GetTaskParams(Delegate, Params))
        {
            m_RPState.SetDepthBias(Params.State.DepthBias, Params.State.SlopeScaledDepthBias);
            m_RPState.SetDepthFunc(Params.State.DepthFunc);
            m_RPState.SetDepthBiasEnabled(Params.State.DepthBiasEnabled);
            m_RPState.SetEnableDepthTest(Params.State.DepthTestEnabled);
            m_RPState.SetEnableDepthClamp(Params.State.DepthClampEnabled);

            m_RPState.SetCullStyle(Params.State.CullStyle);
            m_RPState.SetFrontFaceCCW(Params.State.FrontFaceCCW);
        }
    }

    if (*DirtyBits & pxr::HdChangeTracker::DirtyRenderTags)
    {
        m_RenderTags = _GetTaskRenderTags(Delegate);
    }

    if (m_RenderPass)
    {
        m_RenderPass->Sync();
    }

    *DirtyBits = pxr::HdChangeTracker::Clean;
}

static constexpr char ClearDepthVS[] = R"(
void main(in float4 inPos : ATTRIB0, out float4 outPos : SV_Position)
{
    outPos = inPos;
}
)";

void HnRenderShadowsTask::PrepareClearDepthPSO(const HnRenderDelegate& RenderDelegate)
{
    const HnShadowMapManager* ShadowMapMgr = RenderDelegate.GetShadowMapManager();
    if (ShadowMapMgr == nullptr)
        return;

    const TEXTURE_FORMAT DSVFormat = ShadowMapMgr->GetAtlasDesc().Format;
    if (m_ClearDepthPSO && m_ClearDepthPSO->GetGraphicsPipelineDesc().DSVFormat != DSVFormat)
        m_ClearDepthPSO.Release();

    if (m_ClearDepthPSO)
        return;

    try
    {
        // RenderDeviceWithCache_E throws exceptions in case of errors
        RenderDeviceWithCache_E Device{RenderDelegate.GetDevice(), RenderDelegate.GetRenderStateCache()};

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc         = {"Clear Depth VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint   = "main";
            ShaderCI.Source       = ClearDepthVS;
            ShaderCI.SourceLength = sizeof(ClearDepthVS) - 1;

            pVS = Device.CreateShader(ShaderCI); // Throws exception in case of error
        }

        InputLayoutDescX InputLayout{
            LayoutElement{0, 0, 4, VT_FLOAT32},
        };
        GraphicsPipelineStateCreateInfoX PsoCI{"Clear Depth"};
        PsoCI
            .AddShader(pVS)
            .SetDepthFormat(DSVFormat)
            .SetInputLayout(InputLayout)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        PsoCI.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_ALWAYS;

        m_ClearDepthPSO = Device.CreateGraphicsPipelineState(PsoCI); // Throws exception in case of error
    }
    catch (const std::runtime_error& err)
    {
        LOG_ERROR_MESSAGE("Failed to axes PSO: ", err.what());
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to axes PSO");
    }
}

void HnRenderShadowsTask::PrepareClearDepthVB(const HnRenderDelegate& RenderDelegate)
{
    if (m_ClearDepthVB && m_ClearDepthValue == m_RPState.GetClearDepth())
        return;

    m_ClearDepthValue = m_RPState.GetClearDepth();

    const float4 Verts[3] = {
        {-1.0, -1.0, m_ClearDepthValue, 1.0},
        {-1.0, +3.0, m_ClearDepthValue, 1.0},
        {+3.0, -1.0, m_ClearDepthValue, 1.0},
    };
    if (!m_ClearDepthVB)
    {
        BufferDesc BuffDesc{
            "Clear depth VB",
            sizeof(Verts),
            BIND_VERTEX_BUFFER,
            USAGE_DEFAULT,
        };
        BufferData InitData{Verts, sizeof(Verts)};
        RenderDelegate.GetDevice()->CreateBuffer(BuffDesc, &InitData, &m_ClearDepthVB);
        VERIFY_EXPR(m_ClearDepthVB);
    }
    else
    {
        RenderDelegate.GetDeviceContext()->UpdateBuffer(m_ClearDepthVB, 0, sizeof(Verts), Verts, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    StateTransitionDesc Barrier{m_ClearDepthVB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
    RenderDelegate.GetDeviceContext()->TransitionResourceStates(1, &Barrier);
}

void HnRenderShadowsTask::Prepare(pxr::HdTaskContext* TaskCtx,
                                  pxr::HdRenderIndex* RenderIndex)
{
    m_RenderIndex = RenderIndex;

    m_LightsByShadowSlice.clear();

    const HnRenderDelegate* RenderDelegate = static_cast<const HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    const HnRenderParam*    pRenderParam   = static_cast<const HnRenderParam*>(RenderDelegate->GetRenderParam());
    if (pRenderParam == nullptr)
    {
        UNEXPECTED("Render param is null");
        return;
    }

    if (!pRenderParam->GetUseShadows())
        return;

    PrepareClearDepthPSO(*RenderDelegate);
    PrepareClearDepthVB(*RenderDelegate);

    const Uint32 GeometryVersion =
        (pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::GeometrySubsetDrawItems) +
         pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshGeometry) +
         pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshTransform) +
         pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshVisibility) +
         pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshMaterial));
    static_assert(static_cast<int>(HnRenderParam::GlobalAttrib::Count) == 7, "Please update the code above to handle the new attribute, if necessary.");

    bool GeometryChanged  = m_LastGeometryVersion != GeometryVersion;
    m_LastGeometryVersion = GeometryVersion;

    const auto& Lights = RenderDelegate->GetLights();

    // Sort all shadow lights with dirty shadow maps by shadow map slice
    for (HnLight* Light : Lights)
    {
        if (!Light->ShadowsEnabled())
            continue;

        if (GeometryChanged)
        {
            // Make shadow map dirty even if the light is disabled so that
            // when it is enabled, the shadow map will be updated.
            Light->SetShadowMapDirty(true);
        }

        if (!Light->IsShadowMapDirty())
            continue;

        Int32 ShadowCatingLightId = Light->GetFrameAttribsIndex();
        if (ShadowCatingLightId < 0)
            continue;

        VERIFY(Light->IsVisible(), "Invisible lights should not be assigned shadow casting light index");

        ITextureAtlasSuballocation* pAtlasRegion = Light->GetShadowMapSuballocation();
        m_LightsByShadowSlice.emplace(pAtlasRegion->GetSlice(), Light);
    }
}

void HnRenderShadowsTask::Execute(pxr::HdTaskContext* TaskCtx)
{
    if (m_RenderIndex == nullptr)
    {
        UNEXPECTED("Render index is null. This likely indicates that Prepare() has not been called.");
        return;
    }

    if (m_LightsByShadowSlice.empty())
        return;

    const HnRenderDelegate*   RenderDelegate = static_cast<const HnRenderDelegate*>(m_RenderIndex->GetRenderDelegate());
    const HnShadowMapManager* ShadowMapMgr   = RenderDelegate->GetShadowMapManager();
    if (ShadowMapMgr == nullptr)
    {
        UNEXPECTED("Shadow map manager is null, which indicates that shadows are disabled");
    }

    const TextureDesc& ShadowAtlasDesc = ShadowMapMgr->GetAtlasDesc();
    m_RPState.SetDepthStencilFormat(ShadowAtlasDesc.Format);

    IRenderDevice*          pDevice    = RenderDelegate->GetDevice();
    IDeviceContext*         pCtx       = RenderDelegate->GetDeviceContext();
    const RenderDeviceInfo& DeviceInfo = pDevice->GetDeviceInfo();

    int LastSlice = -1;
    for (const auto it : m_LightsByShadowSlice)
    {
        Uint32   Slice = it.first;
        HnLight* Light = it.second;
        VERIFY_EXPR(Light->ShadowsEnabled() && Light->IsShadowMapDirty());

        Int32 ShadowCatingLightId = Light->GetFrameAttribsIndex();
        VERIFY_EXPR(ShadowCatingLightId >= 0);
        m_RPState.SetFrameAttribsSRB(RenderDelegate->GetShadowPassFrameAttribsSRB(ShadowCatingLightId));

        ITextureAtlasSuballocation* pAtlasRegion = Light->GetShadowMapSuballocation();
        VERIFY_EXPR(pAtlasRegion->GetSlice() == Slice);
        VERIFY(static_cast<int>(Slice) >= LastSlice, "Shadow map slices must be sorted in ascending order");

        const uint2 ShadowMapSize = pAtlasRegion->GetSize();
        const bool  IsEntireSlice = ShadowMapSize.x == ShadowAtlasDesc.Width && ShadowMapSize.y == ShadowAtlasDesc.Height;
        if (static_cast<int>(Slice) > LastSlice)
        {
            ITextureView* pShadowSliceDSV = ShadowMapMgr->GetShadowDSV(Slice);
            // Only clear shadow map if it uses the entire slice
            m_RPState.Begin(0, nullptr, pShadowSliceDSV, nullptr, m_RPState.GetClearDepth(), IsEntireSlice ? HnRenderPassState::ClearDepthBit : 0);
            LastSlice = static_cast<int>(Slice);
        }

        if (!IsEntireSlice)
        {
            m_RPState.Commit(pCtx);

            const uint2 ShadowMapOffset = pAtlasRegion->GetOrigin();
            Viewport    VP;
            VP.TopLeftX = static_cast<float>(ShadowMapOffset.x);
            VP.TopLeftY = static_cast<float>(DeviceInfo.IsGLDevice() ? ShadowAtlasDesc.Height - (ShadowMapOffset.y + ShadowMapSize.y) : ShadowMapOffset.y);
            VP.Width    = static_cast<float>(ShadowMapSize.x);
            VP.Height   = static_cast<float>(ShadowMapSize.y);
            pCtx->SetViewports(1, &VP, ShadowAtlasDesc.Width, ShadowAtlasDesc.Height);

            pCtx->SetPipelineState(m_ClearDepthPSO);
            IBuffer* pVBs[] = {m_ClearDepthVB};
            pCtx->SetVertexBuffers(0, 1, pVBs, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
            pCtx->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
        }

        m_RenderPass->Execute(m_RPState, GetRenderTags());
        Light->SetShadowMapDirty(false);
    }
}

} // namespace USD

} // namespace Diligent
