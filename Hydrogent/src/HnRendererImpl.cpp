/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "HnRendererImpl.hpp"
#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"
#include "HnTokens.hpp"
#include "Tasks/HnTaskController.hpp"
#include "Tasks/HnSetupRenderingTask.hpp"

#include "EngineMemory.h"
#include "USD_Renderer.hpp"
#include "EnvMapRenderer.hpp"
#include "MapHelper.hpp"
#include "CommonlyUsedStates.h"
#include "HnShaderSourceFactory.hpp"

#include "pxr/imaging/hd/task.h"
#include "pxr/imaging/hd/renderPass.h"

namespace Diligent
{

namespace USD
{

namespace HLSL
{

namespace
{
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "../shaders/HnPostProcessStructures.fxh"
} // namespace

} // namespace HLSL

void CreateHnRenderer(IRenderDevice* pDevice, IDeviceContext* pContext, const HnRendererCreateInfo& CI, IHnRenderer** ppRenderer)
{
    auto* pRenderer = NEW_RC_OBJ(GetRawAllocator(), "HnRenderer instance", HnRendererImpl)(pDevice, pContext, CI);
    pRenderer->QueryInterface(IID_HnRenderer, reinterpret_cast<IObject**>(ppRenderer));
}

HnRendererImpl::HnRendererImpl(IReferenceCounters*         pRefCounters,
                               IRenderDevice*              pDevice,
                               IDeviceContext*             pContext,
                               const HnRendererCreateInfo& CI) :
    TBase{pRefCounters},
    m_Device{pDevice},
    m_Context{pContext},
    m_CameraAttribsCB{CI.pCameraAttribsCB},
    m_LightAttribsCB{CI.pLightAttribsCB},
    m_PostProcessAttribsCB{m_Device.CreateBuffer("Post process attribs CB", sizeof(HLSL::PostProcessAttribs))},
    m_ConvertOutputToSRGB{CI.ConvertOutputToSRGB},
    m_MeshIdReadBackQueue{pDevice}
{
}

HnRendererImpl::~HnRendererImpl()
{
    DestroyStageResources();
}

void HnRendererImpl::DestroyStageResources()
{
    m_TaskController.reset();
    m_ImagingDelegate.reset();
    m_RenderIndex.reset();
    m_RenderDelegate.reset();
    m_Stage.Reset();
}

void HnRendererImpl::LoadUSDStage(pxr::UsdStageRefPtr& Stage)
{
    DestroyStageResources();

    m_Stage = Stage;

    m_RenderDelegate = HnRenderDelegate::Create({m_Device, m_Context, nullptr, m_CameraAttribsCB, m_LightAttribsCB});
    m_RenderIndex.reset(pxr::HdRenderIndex::New(m_RenderDelegate.get(), pxr::HdDriverVector{}));

    const pxr::SdfPath SceneDelegateId = pxr::SdfPath::AbsoluteRootPath();

    m_ImagingDelegate = std::make_unique<pxr::UsdImagingDelegate>(m_RenderIndex.get(), SceneDelegateId);
    m_ImagingDelegate->Populate(m_Stage->GetPseudoRoot());

    const pxr::SdfPath TaskControllerId = SceneDelegateId.AppendChild(pxr::TfToken{"_HnTaskController_"});
    m_TaskController                    = std::make_unique<HnTaskController>(*m_RenderIndex, TaskControllerId);
}

void HnRendererImpl::SetParams(const HnRenderParams& Params)
{
    m_RenderParams        = Params;
    m_RenderParamsChanged = true;
}

void HnRendererImpl::Update()
{
    if (!m_RenderDelegate || !m_ImagingDelegate)
        return;

    if (m_RenderParamsChanged)
    {
        HnSetupRenderingTaskParams Params;
        Params.ColorFormat       = ColorBufferFormat;
        Params.MeshIdFormat      = MeshIdFormat;
        Params.DepthFormat       = DepthFormat;
        Params.RenderMode        = m_RenderParams.RenderMode;
        Params.FrontFaceCCW      = m_RenderParams.FrontFaceCCW;
        Params.DebugView         = m_RenderParams.DebugView;
        Params.OcclusionStrength = m_RenderParams.OcclusionStrength;
        Params.EmissionScale     = m_RenderParams.EmissionScale;
        Params.IBLScale          = m_RenderParams.IBLScale;
        Params.Transform         = m_RenderParams.Transform;
        m_TaskController->SetTaskParams(HnTaskController::TaskUID_SetupRendering, Params);

        m_RenderParamsChanged = false;
    }


    m_ImagingDelegate->ApplyPendingUpdates();
}

void HnRendererImpl::PrepareRenderTargets(ITextureView* pDstRtv)
{
    VERIFY(pDstRtv != nullptr, "Destination render target view must not be null");
    const auto& DstTexDesc = pDstRtv->GetTexture()->GetDesc();
    if (m_ColorBuffer)
    {
        const auto& ColBuffDesc = m_ColorBuffer->GetDesc();
        if (ColBuffDesc.Width != DstTexDesc.Width || ColBuffDesc.Height != DstTexDesc.Height)
        {
            m_ColorBuffer.Release();
            m_MeshIdTexture.Release();
            m_DepthBufferDSV.Release();
        }
    }

    if (!m_ColorBuffer)
    {
        TextureDesc TexDesc;
        TexDesc.Name      = "Color buffer";
        TexDesc.Type      = RESOURCE_DIM_TEX_2D;
        TexDesc.Width     = DstTexDesc.Width;
        TexDesc.Height    = DstTexDesc.Height;
        TexDesc.Format    = ColorBufferFormat;
        TexDesc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        TexDesc.MipLevels = 1;
        m_ColorBuffer     = m_Device.CreateTexture(TexDesc);

        TexDesc.Name      = "Mesh ID buffer";
        TexDesc.Format    = MeshIdFormat;
        TexDesc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
        m_MeshIdTexture   = m_Device.CreateTexture(TexDesc);

        TexDesc.Name      = "Depth buffer";
        TexDesc.Format    = DepthFormat;
        TexDesc.BindFlags = BIND_DEPTH_STENCIL;
        m_DepthBufferDSV  = m_Device.CreateTexture(TexDesc)->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    }
}

void HnRendererImpl::PreparePostProcess(TEXTURE_FORMAT RTVFmt)
{
    if (m_PostProcess.PSO)
    {
        if (m_PostProcess.PSO->GetGraphicsPipelineDesc().RTVFormats[0] != RTVFmt)
            m_PostProcess.PSO.Release();
    }

    if (!m_PostProcess.PSO)
    {
        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.pShaderSourceStreamFactory = &HnShaderSourceFactory::GetInstance();

        ShaderMacroHelper Macros;
        Macros.Add("CONVERT_OUTPUT_TO_SRGB", m_ConvertOutputToSRGB);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Post process VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "HnPostProcess.vsh";

            pVS = m_Device.CreateShader(ShaderCI);
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Post process PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.FilePath   = "HnPostProcess.psh";

            pPS = m_Device.CreateShader(ShaderCI);
        }

        PipelineResourceLayoutDescX ResourceLauout;
        ResourceLauout
            .AddVariable(SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
            .AddVariable(SHADER_TYPE_PIXEL, "g_MeshId", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

        GraphicsPipelineStateCreateInfoX PsoCI{"Post process"};
        PsoCI
            .AddRenderTarget(RTVFmt)
            .AddShader(pVS)
            .AddShader(pPS)
            .SetDepthStencilDesc(DSS_DisableDepth)
            .SetRasterizerDesc(RS_SolidFillNoCull)
            .SetResourceLayout(ResourceLauout)
            .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        m_PostProcess.PSO = m_Device.CreateGraphicsPipelineState(PsoCI);
        m_PostProcess.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbPostProcessAttribs")->Set(m_PostProcessAttribsCB);
        m_PostProcess.PSO->CreateShaderResourceBinding(&m_PostProcess.SRB, true);
    }
}

void HnRendererImpl::Draw(IDeviceContext* pCtx, const HnDrawAttribs& Attribs)
{
    if (!m_RenderDelegate)
        return;

    PrepareRenderTargets(Attribs.pDstRTV);
    PreparePostProcess(Attribs.pDstRTV->GetDesc().Format);

    {
        ITextureView* pRTVs[] =
            {
                m_ColorBuffer->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
                m_MeshIdTexture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            };
        pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthBufferDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        constexpr float ClearColor[] = {0.35f, 0.35f, 0.35f, 0};
        pCtx->ClearRenderTarget(pRTVs[0], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        constexpr float Zero[] = {0, 0, 0, 0};
        pCtx->ClearRenderTarget(pRTVs[1], Zero, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->ClearDepthStencil(m_DepthBufferDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    pxr::HdTaskSharedPtrVector tasks = m_TaskController->GetTasks();
    m_Engine.Execute(&m_ImagingDelegate->GetRenderIndex(), &tasks);

    PerformPostProcess(pCtx, Attribs);
}

void HnRendererImpl::PerformPostProcess(IDeviceContext* pCtx, const HnDrawAttribs& Attribs)
{
    ITextureView* pRTVs[] = {Attribs.pDstRTV};
    pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<HLSL::PostProcessAttribs> pDstShaderAttribs{pCtx, m_PostProcessAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        pDstShaderAttribs->SelectionOutlineColor          = Attribs.SlectionColor;
        pDstShaderAttribs->NonselectionDesaturationFactor = Attribs.SelectedPrim != nullptr && !Attribs.SelectedPrim->IsEmpty() ? 0.5f : 0.f;
    }

    pCtx->SetPipelineState(m_PostProcess.PSO);
    m_PostProcess.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer")->Set(m_ColorBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    m_PostProcess.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_MeshId")->Set(m_MeshIdTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    pCtx->CommitShaderResources(m_PostProcess.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
}

void HnRendererImpl::SetEnvironmentMap(IDeviceContext* pCtx, ITextureView* pEnvironmentMapSRV)
{
    m_RenderDelegate->GetUSDRenderer()->PrecomputeCubemaps(pCtx, pEnvironmentMapSRV);
}

const pxr::SdfPath* HnRendererImpl::QueryPrimId(IDeviceContext* pCtx, Uint32 X, Uint32 Y)
{
    if (!m_MeshIdTexture)
        return nullptr;

    Uint32 MeshUid = ~0u;
    while (auto pStagingTex = m_MeshIdReadBackQueue.GetFirstCompleted())
    {
        {
            MappedTextureSubresource MappedData;
            pCtx->MapTextureSubresource(pStagingTex, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, MappedData);
            MeshUid = static_cast<Uint32>(std::abs(*static_cast<const float*>(MappedData.pData)));
            pCtx->UnmapTextureSubresource(pStagingTex, 0, 0);
        }
        m_MeshIdReadBackQueue.Recycle(std::move(pStagingTex));
    }

    auto pStagingTex = m_MeshIdReadBackQueue.GetRecycled();
    if (!pStagingTex)
    {
        TextureDesc StagingTexDesc;
        StagingTexDesc.Name           = "Mesh ID staging tex";
        StagingTexDesc.Usage          = USAGE_STAGING;
        StagingTexDesc.Type           = RESOURCE_DIM_TEX_2D;
        StagingTexDesc.BindFlags      = BIND_NONE;
        StagingTexDesc.Format         = m_MeshIdTexture->GetDesc().Format;
        StagingTexDesc.Width          = 1;
        StagingTexDesc.Height         = 1;
        StagingTexDesc.MipLevels      = 1;
        StagingTexDesc.CPUAccessFlags = CPU_ACCESS_READ;
        pStagingTex                   = m_Device.CreateTexture(StagingTexDesc, nullptr);
        VERIFY_EXPR(pStagingTex);
    }

    CopyTextureAttribs CopyAttribs;
    CopyAttribs.pSrcTexture = m_MeshIdTexture;
    CopyAttribs.pDstTexture = pStagingTex;
    Box SrcBox{X, X + 1, Y, Y + 1};
    CopyAttribs.pSrcBox                  = &SrcBox;
    CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    pCtx->CopyTexture(CopyAttribs);
    m_MeshIdReadBackQueue.Enqueue(pCtx, std::move(pStagingTex));

    static const pxr::SdfPath EmptyPath;
    if (MeshUid == ~0u)
        return nullptr;
    else
        return MeshUid != 0 ? m_RenderDelegate->GetMeshPrimId(MeshUid) : &EmptyPath;
}

} // namespace USD

} // namespace Diligent
