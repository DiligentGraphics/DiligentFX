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
#include "PBR_Renderer.hpp"
#include "EnvMapRenderer.hpp"
#include "MapHelper.hpp"

#include "pxr/imaging/hd/task.h"
#include "pxr/imaging/hd/unitTestNullRenderPass.h"

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
    m_CameraAttribsCB{CI.pCameraAttribsCB},
    m_LightAttribsCB{CI.pLightAttribsCB},
    m_PBRRenderer{
        std::make_shared<PBR_Renderer>(
            pDevice,
            nullptr,
            pContext,
            [](const HnRendererCreateInfo& CI) {
                PBR_Renderer::CreateInfo PBRRendererCI;
                PBRRendererCI.RTVFmt = CI.RTVFormat;
                PBRRendererCI.DSVFmt = CI.DSVFormat;

                PBRRendererCI.AllowDebugView = true;
                PBRRendererCI.UseIBL         = true;
                PBRRendererCI.UseAO          = true;
                PBRRendererCI.UseEmissive    = false;

                // Use samplers from texture views
                PBRRendererCI.UseImmutableSamplers = false;
                // Disable animation
                PBRRendererCI.MaxJointCount = 0;
                // Use separate textures for metallic and roughness
                PBRRendererCI.UseSeparateMetallicRoughnessTextures = true;

                PBRRendererCI.ConvertOutputToSRGB = CI.ConvertOutputToSRGB;

                static constexpr LayoutElement Inputs[] =
                    {
                        {0, 0, 3, VT_FLOAT32}, //float3 Pos     : ATTRIB0;
                        {1, 1, 3, VT_FLOAT32}, //float3 Normal  : ATTRIB1;
                        {2, 2, 2, VT_FLOAT32}, //float2 UV0     : ATTRIB2;
                        {3, 3, 2, VT_FLOAT32}, //float2 UV1     : ATTRIB3;
                    };

                PBRRendererCI.InputLayout.LayoutElements = Inputs;
                PBRRendererCI.InputLayout.NumElements    = _countof(Inputs);

                return PBRRendererCI;
            }(CI)),
    },
    m_EnvMapRenderer{
        std::make_unique<EnvMapRenderer>(
            [](const HnRendererCreateInfo& CI, IRenderDevice* pDevice) {
                EnvMapRenderer::CreateInfo EnvMapRndrCI;
                EnvMapRndrCI.pDevice             = pDevice;
                EnvMapRndrCI.pCameraAttribsCB    = CI.pCameraAttribsCB;
                EnvMapRndrCI.RTVFormat           = CI.RTVFormat;
                EnvMapRndrCI.DSVFormat           = CI.DSVFormat;
                EnvMapRndrCI.ConvertOutputToSRGB = CI.ConvertOutputToSRGB;

                return EnvMapRndrCI;
            }(CI, pDevice))},
    m_RenderDelegate{HnRenderDelegate::Create({pDevice, pContext, m_CameraAttribsCB, m_LightAttribsCB, m_PBRRenderer})}
{
}

HnRendererImpl::~HnRendererImpl()
{
    delete m_ImagingDelegate;
    delete m_RenderIndex;
}

void HnRendererImpl::LoadUSDStage(const char* FileName)
{
    m_Stage = pxr::UsdStage::Open(FileName);
    if (!m_Stage)
    {
        LOG_ERROR_MESSAGE("Failed to open USD stage '", FileName, "'");
        return;
    }

    m_RenderIndex     = pxr::HdRenderIndex::New(m_RenderDelegate.get(), pxr::HdDriverVector{});
    m_ImagingDelegate = new pxr::UsdImagingDelegate(m_RenderIndex, pxr::SdfPath::AbsoluteRootPath());
    m_ImagingDelegate->Populate(m_Stage->GetPseudoRoot());

    m_RenderTags = {pxr::HdRenderTagTokens->geometry};

    auto Collection = pxr::HdRprimCollection{pxr::HdTokens->geometry, pxr::HdReprSelector(pxr::HdReprTokens->hull)};
    m_GeometryPass  = pxr::HdRenderPassSharedPtr{new pxr::Hd_UnitTestNullRenderPass{m_RenderIndex, Collection}};
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

    void Execute(pxr::HdTaskContext* ctx) override final {}

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
    m_ImagingDelegate->ApplyPendingUpdates();
    pxr::HdTaskSharedPtrVector tasks = {
        std::make_shared<SyncTask>(m_GeometryPass, m_RenderTags)};
    m_Engine.Execute(&m_ImagingDelegate->GetRenderIndex(), &tasks);
}

void HnRendererImpl::Draw(IDeviceContext* pCtx, const HnDrawAttribs& Attribs)
{
    const auto& Meshes = m_RenderDelegate->GetMeshes();
    if (Meshes.empty())
        return;

    if (auto* pEnvMapSRV = m_PBRRenderer->GetPrefilteredEnvMapSRV())
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

    for (auto AlphaMode : {PBR_Renderer::ALPHA_MODE_OPAQUE, PBR_Renderer::ALPHA_MODE_MASK, PBR_Renderer::ALPHA_MODE_BLEND})
    {
        pCtx->SetPipelineState(m_PBRRenderer->GetPSO({AlphaMode, /*DoubleSided = */ false}));

        for (auto mesh_it : Meshes)
        {
            if (!mesh_it.second)
                continue;

            auto& Mesh = *mesh_it.second;

            const auto& MaterialId = Mesh.GetMaterialId();
            const auto* pMaterial  = m_RenderDelegate->GetMaterial(MaterialId.GetText());
            if (pMaterial == nullptr)
                return;

            const auto& ShaderAttribs = pMaterial->GetShaderAttribs();
            if (ShaderAttribs.AlphaMode != AlphaMode)
                continue;

            auto* pSRB = pMaterial->GetSRB();
            auto* pVB0 = Mesh.GetVertexBuffer(HnMesh::VERTEX_BUFFER_ID_POSITION);
            auto* pVB1 = Mesh.GetVertexBuffer(HnMesh::VERTEX_BUFFER_ID_NORMAL);
            auto* pVB2 = Mesh.GetVertexBuffer(HnMesh::VERTEX_BUFFER_ID_TEXCOORD);
            auto* pIB  = Mesh.GetTriangleIndexBuffer();

            if (pVB0 == nullptr || pVB1 == nullptr || pVB2 == nullptr || pIB == nullptr || pSRB == nullptr)
                continue;

            // Bind vertex and index buffers
            IBuffer* pBuffs[] = {pVB0, pVB1, pVB2, pVB2};
            pCtx->SetVertexBuffers(0, _countof(pBuffs), pBuffs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            pCtx->SetIndexBuffer(pIB, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            {
                MapHelper<HLSL::PBRShaderAttribs> pAttribs{pCtx, m_PBRRenderer->GetPBRAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD};

                pAttribs->Transforms.NodeMatrix = Mesh.GetTransform();
                pAttribs->Transforms.JointCount = 0;

                static_assert(sizeof(pAttribs->Material) == sizeof(ShaderAttribs), "The sizeof(PBRMaterialShaderInfo) is inconsistent with sizeof(ShaderAttribs)");
                memcpy(&pAttribs->Material, &ShaderAttribs, sizeof(ShaderAttribs));

                auto& RendererParams = pAttribs->Renderer;

                RendererParams.DebugViewType            = Attribs.DebugView;
                RendererParams.OcclusionStrength        = Attribs.OcclusionStrength;
                RendererParams.EmissionScale            = Attribs.EmissionScale;
                RendererParams.AverageLogLum            = Attribs.AverageLogLum;
                RendererParams.MiddleGray               = Attribs.MiddleGray;
                RendererParams.WhitePoint               = Attribs.WhitePoint;
                RendererParams.IBLScale                 = Attribs.IBLScale;
                RendererParams.PrefilteredCubeMipLevels = 5; //m_Settings.UseIBL ? static_cast<float>(m_pPrefilteredEnvMapSRV->GetTexture()->GetDesc().MipLevels) : 0.f;
            }

            pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            DrawIndexedAttribs DrawAttrs{Mesh.GetNumTriangles() * 3, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
            pCtx->DrawIndexed(DrawAttrs);
        }
    }
}

void HnRendererImpl::SetEnvironmentMap(IDeviceContext* pCtx, ITextureView* pEnvironmentMapSRV)
{
    m_PBRRenderer->PrecomputeCubemaps(m_Device, m_Device, pCtx, pEnvironmentMapSRV);
}

} // namespace USD

} // namespace Diligent
