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

#include "EngineMemory.h"
#include "USD_Renderer.hpp"
#include "EnvMapRenderer.hpp"
#include "MapHelper.hpp"
#include "CommonlyUsedStates.h"

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
    m_ConvertOutputToSRGB{CI.ConvertOutputToSRGB},
    m_USDRenderer{
        std::make_shared<USD_Renderer>(
            pDevice,
            nullptr,
            pContext,
            [](const HnRendererCreateInfo& CI) {
                USD_Renderer::CreateInfo USDRendererCI;

                // Use samplers from texture views
                USDRendererCI.UseImmutableSamplers = false;
                // Disable animation
                USDRendererCI.MaxJointCount = 0;
                // Use separate textures for metallic and roughness
                USDRendererCI.UseSeparateMetallicRoughnessTextures = true;

                static constexpr LayoutElement Inputs[] =
                    {
                        {0, 0, 3, VT_FLOAT32}, //float3 Pos     : ATTRIB0;
                        {1, 1, 3, VT_FLOAT32}, //float3 Normal  : ATTRIB1;
                        {2, 2, 2, VT_FLOAT32}, //float2 UV0     : ATTRIB2;
                        {3, 3, 2, VT_FLOAT32}, //float2 UV1     : ATTRIB3;
                    };

                USDRendererCI.InputLayout.LayoutElements = Inputs;
                USDRendererCI.InputLayout.NumElements    = _countof(Inputs);

                return USDRendererCI;
            }(CI)),
    },
    m_EnvMapRenderer{
        std::make_unique<EnvMapRenderer>(
            [](const HnRendererCreateInfo& CI, IRenderDevice* pDevice) {
                EnvMapRenderer::CreateInfo EnvMapRndrCI;
                EnvMapRndrCI.pDevice          = pDevice;
                EnvMapRndrCI.pCameraAttribsCB = CI.pCameraAttribsCB;
                EnvMapRndrCI.NumRenderTargets = 2;
                EnvMapRndrCI.RTVFormats[0]    = ColorBufferFormat;
                EnvMapRndrCI.RTVFormats[1]    = MeshIdFormat;
                EnvMapRndrCI.DSVFormat        = DepthFormat;

                return EnvMapRndrCI;
            }(CI, pDevice))},
    m_MeshIdReadBackQueue{pDevice}
{
    GraphicsPipelineDesc GraphicsDesc;
    GraphicsDesc.NumRenderTargets                     = 2;
    GraphicsDesc.RTVFormats[0]                        = ColorBufferFormat;
    GraphicsDesc.RTVFormats[1]                        = MeshIdFormat;
    GraphicsDesc.DSVFormat                            = DepthFormat;
    GraphicsDesc.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsDesc.RasterizerDesc.FrontCounterClockwise = CI.FrontCCW;

    m_PbrPSOCache = m_USDRenderer->GetPbrPsoCacheAccessor(GraphicsDesc);
    VERIFY_EXPR(m_PbrPSOCache);

    GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_LINE_LIST;

    m_WireframePSOCache = m_USDRenderer->GetWireframePsoCacheAccessor(GraphicsDesc);
    VERIFY_EXPR(m_WireframePSOCache);

    m_DefaultPSOFlags = PBR_Renderer::PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT;
}

HnRendererImpl::~HnRendererImpl()
{
    delete m_ImagingDelegate;
    delete m_RenderIndex;
}

void HnRendererImpl::LoadUSDStage(pxr::UsdStageRefPtr& Stage)
{
    m_Stage = Stage;

    m_RenderDelegate = HnRenderDelegate::Create({m_Device, m_Context, m_CameraAttribsCB, m_LightAttribsCB, m_USDRenderer});

    m_RenderIndex     = pxr::HdRenderIndex::New(m_RenderDelegate.get(), pxr::HdDriverVector{});
    m_ImagingDelegate = new pxr::UsdImagingDelegate(m_RenderIndex, pxr::SdfPath::AbsoluteRootPath());
    m_ImagingDelegate->Populate(m_Stage->GetPseudoRoot());

    m_RenderTags = {pxr::HdRenderTagTokens->geometry};

    auto Collection = pxr::HdRprimCollection{pxr::HdTokens->geometry, pxr::HdReprSelector(pxr::HdReprTokens->hull)};
    m_GeometryPass  = m_RenderDelegate->CreateRenderPass(m_RenderIndex, Collection);
}


namespace
{

class SyncTask final : public pxr::HdTask
{
public:
    SyncTask(pxr::HdRenderPassSharedPtr const& renderPass, pxr::TfTokenVector const& renderTags) :
        pxr::HdTask{pxr::SdfPath::EmptyPath()},
        m_RenderPass{renderPass},
        m_RenderTags{renderTags}
    {}

    void Sync(pxr::HdSceneDelegate* delegate, pxr::HdTaskContext* ctx, pxr::HdDirtyBits* dirtyBits) override final
    {
        m_RenderPass->Sync();

        *dirtyBits = pxr::HdChangeTracker::Clean;
    }

    void Prepare(pxr::HdTaskContext* ctx, pxr::HdRenderIndex* renderIndex) override final {}

    void Execute(pxr::HdTaskContext* ctx) override final
    {
        m_RenderPass->Execute({}, m_RenderTags);
    }

    const pxr::TfTokenVector& GetRenderTags() const override final
    {
        return m_RenderTags;
    }

private:
    pxr::HdRenderPassSharedPtr m_RenderPass;
    pxr::TfTokenVector         m_RenderTags;
};

} // namespace


void HnRendererImpl::Update()
{
    if (m_ImagingDelegate)
    {
        m_ImagingDelegate->ApplyPendingUpdates();
        pxr::HdTaskSharedPtrVector tasks = {
            std::make_shared<SyncTask>(m_GeometryPass, m_RenderTags)};
        m_Engine.Execute(&m_ImagingDelegate->GetRenderIndex(), &tasks);
    }
}

void HnRendererImpl::RenderMeshes(IDeviceContext* pCtx, const HnDrawAttribs& Attribs)
{
    for (auto& List : m_RenderLists)
        List.clear();

    const auto& Meshes = m_RenderDelegate->GetMeshes();
    if (Meshes.empty())
        return;

    for (auto mesh_it : Meshes)
    {
        if (!mesh_it.second)
            continue;

        auto& Mesh = *mesh_it.second;

        const auto& MaterialId = Mesh.GetMaterialId();
        const auto* pMaterial  = m_RenderDelegate->GetMaterial(MaterialId);
        if (pMaterial == nullptr)
            return;

        const auto& ShaderAttribs = pMaterial->GetShaderAttribs();
        const auto  AlphaMode     = ShaderAttribs.AlphaMode;

        m_RenderLists[AlphaMode].emplace_back(Mesh, *pMaterial);
    }

    // TODO: handle double-sided materials
    for (auto AlphaMode : {USD_Renderer::ALPHA_MODE_OPAQUE, USD_Renderer::ALPHA_MODE_MASK, USD_Renderer::ALPHA_MODE_BLEND})
    {
        const auto& List = m_RenderLists[AlphaMode];
        for (const auto& MeshRI : List)
        {
            RenderMesh(pCtx, MeshRI.Mesh, MeshRI.Material, Attribs, AlphaMode);
        }
    }
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

static constexpr char ScreeTriVSSource[] = R"(
struct PSInput 
{ 
    float4 Pos : SV_POSITION;
};
void main(in  uint    VertID : SV_VertexID,
          out PSInput PSIn) 
{
    float2 ClipXY[3];
    ClipXY[0] = float2(-1.0, -1.0);
    ClipXY[1] = float2(-1.0,  3.0);
    ClipXY[2] = float2( 3.0, -1.0);

    PSIn.Pos = float4(ClipXY[VertID], 0.0, 1.0);
}
)";

static constexpr char PostProcessPSSource[] = R"(
struct PSInput 
{ 
    float4 Pos : SV_POSITION;
};

Texture2D g_ColorBuffer;
Texture2D g_MeshId;

void main(in PSInput PSIn,
          out float4 Color : SV_Target0) 
{
    Color = g_ColorBuffer.Load(int3(PSIn.Pos.xy, 0));

    float IsSelected0 = g_MeshId.Load(int3(PSIn.Pos.xy + float2(-1.0, -1.0), 0)).r < 0.0 ? +1.0 : -1.0;
    float IsSelected1 = g_MeshId.Load(int3(PSIn.Pos.xy + float2( 0.0, -1.0), 0)).r < 0.0 ? +1.0 : -1.0;
    float IsSelected2 = g_MeshId.Load(int3(PSIn.Pos.xy + float2(+1.0, -1.0), 0)).r < 0.0 ? +1.0 : -1.0;

    float IsSelected3 = g_MeshId.Load(int3(PSIn.Pos.xy + float2(-1.0,  0.0), 0)).r < 0.0 ? +1.0 : -1.0;
    float IsSelected4 = g_MeshId.Load(int3(PSIn.Pos.xy + float2( 0.0,  0.0), 0)).r < 0.0 ? +1.0 : -1.0;
    float IsSelected5 = g_MeshId.Load(int3(PSIn.Pos.xy + float2(+1.0,  0.0), 0)).r < 0.0 ? +1.0 : -1.0;

    float IsSelected6 = g_MeshId.Load(int3(PSIn.Pos.xy + float2(-1.0, +1.0), 0)).r < 0.0 ? +1.0 : -1.0;
    float IsSelected7 = g_MeshId.Load(int3(PSIn.Pos.xy + float2( 0.0, +1.0), 0)).r < 0.0 ? +1.0 : -1.0;
    float IsSelected8 = g_MeshId.Load(int3(PSIn.Pos.xy + float2(+1.0, +1.0), 0)).r < 0.0 ? +1.0 : -1.0;

    float Outline = IsSelected0 + IsSelected1 + IsSelected2 + IsSelected3 + IsSelected4 + IsSelected5 + IsSelected6 + IsSelected7 + IsSelected8;
    Outline = saturate(1.0 - abs(Outline) / 9.0);

    Color.rgb += Outline * float3(0.25, 0.25, 0.1f);

#if CONVERT_OUTPUT_TO_SRGB
    Color.rgb = pow(Color.rgb, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
#endif
}

)";

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
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

        ShaderMacroHelper Macros;
        Macros.Add("CONVERT_OUTPUT_TO_SRGB", m_ConvertOutputToSRGB);
        ShaderCI.Macros = Macros;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc       = {"Post process VS", SHADER_TYPE_VERTEX, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.Source     = ScreeTriVSSource;

            pVS = m_Device.CreateShader(ShaderCI);
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc       = {"Post process PS", SHADER_TYPE_PIXEL, true};
            ShaderCI.EntryPoint = "main";
            ShaderCI.Source     = PostProcessPSSource;

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

    if (auto* pEnvMapSRV = m_USDRenderer->GetPrefilteredEnvMapSRV())
    {
        Diligent::HLSL::ToneMappingAttribs TMAttribs;
        TMAttribs.iToneMappingMode     = TONE_MAPPING_MODE_UNCHARTED2;
        TMAttribs.bAutoExposure        = 0;
        TMAttribs.fMiddleGray          = Attribs.MiddleGray;
        TMAttribs.bLightAdaptation     = 0;
        TMAttribs.fWhitePoint          = Attribs.WhitePoint;
        TMAttribs.fLuminanceSaturation = 1.0;

        EnvMapRenderer::RenderAttribs EnvMapAttribs;
        EnvMapAttribs.pContext      = pCtx;
        EnvMapAttribs.pEnvMap       = pEnvMapSRV;
        EnvMapAttribs.AverageLogLum = Attribs.AverageLogLum;
        EnvMapAttribs.MipLevel      = 1;

        m_EnvMapRenderer->Render(EnvMapAttribs, TMAttribs);
    }

    RenderMeshes(pCtx, Attribs);

    {
        ITextureView* pRTVs[] = {Attribs.pDstRTV};
        pCtx->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        pCtx->SetPipelineState(m_PostProcess.PSO);
        m_PostProcess.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer")->Set(m_ColorBuffer->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        m_PostProcess.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_MeshId")->Set(m_MeshIdTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        pCtx->CommitShaderResources(m_PostProcess.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->Draw({3, DRAW_FLAG_VERIFY_ALL});
    }
}

void HnRendererImpl::RenderMesh(IDeviceContext*          pCtx,
                                const HnMesh&            Mesh,
                                const HnMaterial&        Material,
                                const HnDrawAttribs&     Attribs,
                                USD_Renderer::ALPHA_MODE AlphaMode)
{
    auto* pSRB       = Material.GetSRB();
    auto* pPosVB     = Mesh.GetVertexBuffer(pxr::HdTokens->points);
    auto* pNormalsVB = Mesh.GetVertexBuffer(pxr::HdTokens->normals);

    // Our shader currently supports two texture coordinate sets.
    // Gather vertex buffers for both sets.
    const auto& TexCoordSets    = Material.GetTextureCoordinateSets();
    IBuffer*    pTexCoordVBs[2] = {};
    for (size_t i = 0; i < TexCoordSets.size(); ++i)
    {
        const auto& TexCoordSet = TexCoordSets[i];
        if (!TexCoordSet.PrimVarName.IsEmpty())
        {
            pTexCoordVBs[i] = Mesh.GetVertexBuffer(TexCoordSet.PrimVarName);
            if (!pTexCoordVBs[i])
            {
                LOG_ERROR_MESSAGE("Failed to find texture coordinates vertex buffer '", TexCoordSet.PrimVarName.GetText(), "' in mesh '", Mesh.GetId().GetText(), "'");
            }
        }
    }

    const auto& ShaderAttribs = Material.GetShaderAttribs();

    auto* pIB = (Attribs.RenderMode == HN_RENDER_MODE_MESH_EDGES) ?
        Mesh.GetEdgeIndexBuffer() :
        Mesh.GetTriangleIndexBuffer();

    if (pPosVB == nullptr || pIB == nullptr || pSRB == nullptr)
        return;

    IPipelineState* pPSO = nullptr;
    if (Attribs.RenderMode == HN_RENDER_MODE_SOLID)
    {
        auto PSOFlags = m_DefaultPSOFlags;
        if (pNormalsVB != nullptr)
            PSOFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
        if (pTexCoordVBs[0] != nullptr)
            PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
        if (pTexCoordVBs[1] != nullptr)
            PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;

        PSOFlags |=
            PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
            PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
            PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
            PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
            PBR_Renderer::PSO_FLAG_USE_AO_MAP |
            PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP |
            PBR_Renderer::PSO_FLAG_USE_IBL |
            PBR_Renderer::PSO_FLAG_ENABLE_DEBUG_VIEW;

        pPSO = m_PbrPSOCache.Get({PSOFlags, AlphaMode, /*DoubleSided = */ false}, true);
    }
    else if (Attribs.RenderMode == HN_RENDER_MODE_MESH_EDGES)
    {
        pPSO = m_WireframePSOCache.Get({m_DefaultPSOFlags, /*DoubleSided = */ false}, true);
    }
    else
    {
        UNEXPECTED("Unexpected render mode");
        return;
    }
    pCtx->SetPipelineState(pPSO);

    // Bind vertex and index buffers
    IBuffer* pBuffs[] = {pPosVB, pNormalsVB, pTexCoordVBs[0], pTexCoordVBs[1]};
    pCtx->SetVertexBuffers(0, _countof(pBuffs), pBuffs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pCtx->SetIndexBuffer(pIB, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<HLSL::PBRShaderAttribs> pDstShaderAttribs{pCtx, m_USDRenderer->GetPBRAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD};

        pDstShaderAttribs->Transforms.NodeMatrix = Mesh.GetTransform() * Attribs.Transform;
        pDstShaderAttribs->Transforms.JointCount = 0;

        static_assert(sizeof(pDstShaderAttribs->Material) == sizeof(ShaderAttribs), "The sizeof(PBRMaterialShaderInfo) is inconsistent with sizeof(ShaderAttribs)");
        memcpy(&pDstShaderAttribs->Material, &ShaderAttribs, sizeof(ShaderAttribs));

        auto& RendererParams = pDstShaderAttribs->Renderer;

        RendererParams.DebugViewType            = Attribs.DebugView;
        RendererParams.OcclusionStrength        = Attribs.OcclusionStrength;
        RendererParams.EmissionScale            = Attribs.EmissionScale;
        RendererParams.AverageLogLum            = Attribs.AverageLogLum;
        RendererParams.MiddleGray               = Attribs.MiddleGray;
        RendererParams.WhitePoint               = Attribs.WhitePoint;
        RendererParams.IBLScale                 = Attribs.IBLScale;
        RendererParams.PrefilteredCubeMipLevels = 5; //m_Settings.UseIBL ? static_cast<float>(m_pPrefilteredEnvMapSRV->GetTexture()->GetDesc().MipLevels) : 0.f;
        RendererParams.WireframeColor           = Attribs.WireframeColor;
        RendererParams.HighlightColor           = float4{0, 0, 0, 0};

        auto CustomData = float4{static_cast<float>(Mesh.GetUID()), 0, 0, 1};
        if (Attribs.SelectedPrim != nullptr && Mesh.GetId() == *Attribs.SelectedPrim)
            CustomData.x *= -1;
        RendererParams.CustomData = CustomData;
    }

    pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawIndexedAttribs DrawAttrs = (Attribs.RenderMode == HN_RENDER_MODE_MESH_EDGES) ?
        DrawIndexedAttribs{Mesh.GetNumEdges() * 2, VT_UINT32, DRAW_FLAG_VERIFY_ALL} :
        DrawIndexedAttribs{Mesh.GetNumTriangles() * 3, VT_UINT32, DRAW_FLAG_VERIFY_ALL};

    pCtx->DrawIndexed(DrawAttrs);
}

void HnRendererImpl::SetEnvironmentMap(IDeviceContext* pCtx, ITextureView* pEnvironmentMapSRV)
{
    m_USDRenderer->PrecomputeCubemaps(pCtx, pEnvironmentMapSRV);
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
