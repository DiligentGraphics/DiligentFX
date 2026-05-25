/*
 *  Copyright 2026 Diligent Graphics LLC
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

#include "RadientGeometryPass.hpp"

#include "RadientMath.hpp"

#include "GraphicsUtilities.h"
#include "MapHelper.hpp"

#include <vector>

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
} // namespace HLSL

namespace
{

constexpr float RadientDefaultSceneScale = 1.f;

TEXTURE_FORMAT GetTextureViewFormat(ITextureView* pView)
{
    if (pView == nullptr)
        return TEX_FORMAT_UNKNOWN;

    const TextureViewDesc& ViewDesc = pView->GetDesc();
    if (ViewDesc.Format != TEX_FORMAT_UNKNOWN)
        return ViewDesc.Format;

    ITexture* pTexture = pView->GetTexture();
    return pTexture != nullptr ? pTexture->GetDesc().Format : TEX_FORMAT_UNKNOWN;
}

bool RequiresOutputSRGBConversion(TEXTURE_FORMAT Format)
{
    return Format == TEX_FORMAT_RGBA8_UNORM ||
        Format == TEX_FORMAT_BGRA8_UNORM;
}

RadientCameraComponent GetCameraComponent(const RadientRenderAttribs& Attribs)
{
    RadientCameraComponent Camera{};
    if (Attribs.pScene != nullptr && Attribs.Camera != InvalidRadientEntityID)
        (void)Attribs.pScene->GetCamera(Attribs.Camera, Camera);

    return Camera;
}

HLSL::CameraAttribs GetCameraAttribs(IRenderDevice*                   pDevice,
                                     const RadientRenderAttribs&      Attribs,
                                     const RadientFrameRenderTargets& Targets,
                                     Uint32                           FrameIndex)
{
    const RadientCameraComponent Camera = GetCameraComponent(Attribs);

    const RadientExtent2D& TargetSize       = Targets.GetSize();
    const float            Width            = static_cast<float>(TargetSize.Width);
    const float            Height           = static_cast<float>(TargetSize.Height);
    const float            Aspect           = Height > 0.f ? Width / Height : 1.f;
    const bool             NDCMinusOneToOne = pDevice != nullptr && pDevice->GetDeviceInfo().NDC.MinZ < 0.f;

    float4x4 CameraWorld = float4x4::Identity();
    if (Attribs.pScene != nullptr && Attribs.Camera != InvalidRadientEntityID)
    {
        RadientMatrix4x4 CameraWorldMatrix{};
        if (RADIENT_SUCCEEDED(Attribs.pScene->GetCachedWorldMatrix(Attribs.Camera, CameraWorldMatrix)))
            CameraWorld = RadientMath::ToFloat4x4(CameraWorldMatrix);
    }
    const RadientMath::CameraProjection CameraProj     = RadientMath::GetCameraProjection(Camera, Aspect, NDCMinusOneToOne);
    const float4x4                      CameraView     = CameraWorld.Inverse();
    const float4x4                      CameraViewProj = CameraView * CameraProj.Matrix;
    const float4x4                      CameraViewInv  = CameraWorld;

    HLSL::CameraAttribs CameraAttribs{};
    CameraAttribs.f4ViewportSize = float4{Width, Height, Width > 0.f ? 1.f / Width : 0.f, Height > 0.f ? 1.f / Height : 0.f};
    CameraAttribs.SetClipPlanes(CameraProj.NearPlaneZ, CameraProj.FarPlaneZ);
    CameraAttribs.fSceneNearZ     = CameraAttribs.fNearPlaneZ;
    CameraAttribs.fSceneFarZ      = CameraAttribs.fFarPlaneZ;
    CameraAttribs.fSceneNearDepth = CameraAttribs.fNearPlaneDepth;
    CameraAttribs.fSceneFarDepth  = CameraAttribs.fFarPlaneDepth;
    CameraAttribs.fHandness       = CameraView.Determinant() > 0.f ? 1.f : -1.f;
    CameraAttribs.uiFrameIndex    = FrameIndex;
    CameraAttribs.fFocusDistance  = Camera.FocusDistance;
    CameraAttribs.fFStop          = Camera.FStop;
    CameraAttribs.fFocalLength    = CameraProj.FocalLength;
    CameraAttribs.fSensorWidth    = CameraProj.HorizontalAperture;
    CameraAttribs.fSensorHeight   = CameraProj.VerticalAperture;
    CameraAttribs.mView           = CameraView;
    CameraAttribs.mProj           = CameraProj.Matrix;
    CameraAttribs.mViewProj       = CameraViewProj;
    CameraAttribs.mViewInv        = CameraViewInv;
    CameraAttribs.mProjInv        = CameraProj.Matrix.Inverse();
    CameraAttribs.mViewProjInv    = CameraViewProj.Inverse();
    CameraAttribs.f4Position      = float4{float3::MakeVector(CameraWorld[3]), 1.f};

    return CameraAttribs;
}

void WriteAdHocLight(GLTF_PBR_Renderer&     Renderer,
                     HLSL::PBRFrameAttribs& FrameAttribs)
{
    GLTF::Light Light;
    Light.Type      = GLTF::Light::TYPE::DIRECTIONAL;
    Light.Color     = float3{1.f, 1.f, 1.f};
    Light.Intensity = 3.f;

    const float3 Direction = normalize(float3{-0.45f, -0.75f, 0.5f});

    HLSL::PBRLightAttribs* Lights = reinterpret_cast<HLSL::PBRLightAttribs*>(&FrameAttribs + 1);
    GLTF_PBR_Renderer::WritePBRLightShaderAttribs({&Light, nullptr, &Direction, RadientDefaultSceneScale}, Lights);

    HLSL::PBRRendererShaderParameters& RendererAttribs = FrameAttribs.Renderer;
    Renderer.SetInternalShaderParameters(RendererAttribs);
    RendererAttribs.OcclusionStrength = 1.f;
    RendererAttribs.EmissionScale     = 1.f;
    RendererAttribs.AverageLogLum     = 0.f;
    RendererAttribs.MiddleGray        = 0.18f;
    RendererAttribs.WhitePoint        = 3.f;
    RendererAttribs.IBLScale          = float4{0.f, 0.f, 0.f, 0.f};
    RendererAttribs.HighlightColor    = float4{0.f, 0.f, 0.f, 0.f};
    RendererAttribs.UnshadedColor     = float4{1.f, 1.f, 1.f, 1.f};
    RendererAttribs.PointSize         = 1.f;
    RendererAttribs.MipBias           = 0.f;
    RendererAttribs.LightCount        = 1;
    RendererAttribs.DebugView         = 0;
}

} // namespace

RADIENT_STATUS RadientGeometryPass::Prepare(IRenderDevice*                   pDevice,
                                            IDeviceContext*                  pContext,
                                            const RadientFrameRenderTargets& Targets)
{
    ITextureView* pColorRTV = Targets.GetColorRTV();
    if (pDevice == nullptr || pContext == nullptr || pColorRTV == nullptr)
        return RADIENT_STATUS_OK;

    const TEXTURE_FORMAT RTVFormat = GetTextureViewFormat(pColorRTV);
    const TEXTURE_FORMAT DSVFormat = GetTextureViewFormat(Targets.GetDepthDSV());
    if (m_pGLTFRenderer == nullptr ||
        m_RTVFormat != RTVFormat ||
        m_DSVFormat != DSVFormat)
    {
        return CreateRenderer(pDevice, pContext, RTVFormat, DSVFormat);
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientGeometryPass::Execute(IRenderDevice*                   pDevice,
                                            IDeviceContext*                  pContext,
                                            const RadientDrawList&           DrawList,
                                            RadientRenderResourceCache&      ResourceCache,
                                            const RadientRenderAttribs&      Attribs,
                                            const RadientFrameRenderTargets& Targets)
{
    if (pDevice == nullptr || pContext == nullptr || DrawList.IsEmpty())
        return RADIENT_STATUS_OK;

    if (m_pGLTFRenderer == nullptr)
    {
        const RADIENT_STATUS PrepareStatus = Prepare(pDevice, pContext, Targets);
        if (RADIENT_FAILED(PrepareStatus))
            return PrepareStatus;
    }
    if (m_pGLTFRenderer == nullptr || m_pFrameAttribsCB == nullptr)
        return RADIENT_STATUS_OK;

    ITextureView* pColorRTV = Targets.GetColorRTV();
    ITextureView* pDepthDSV = Targets.GetDepthDSV();
    pContext->SetRenderTargets(1, &pColorRTV, pDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    {
        MapHelper<HLSL::PBRFrameAttribs> FrameAttribs{pContext, m_pFrameAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        HLSL::PBRFrameAttribs*           pFrameAttribs = FrameAttribs;
        if (pFrameAttribs == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        pFrameAttribs->Camera     = GetCameraAttribs(pDevice, Attribs, Targets, m_FrameIndex);
        pFrameAttribs->PrevCamera = pFrameAttribs->Camera;
        WriteAdHocLight(*m_pGLTFRenderer, *pFrameAttribs);
    }

    GLTF::ResourceManager* pResourceManager = ResourceCache.GetResourceManager();
    if (pResourceManager == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    std::vector<const RadientDrawItem*> ReadyItems;
    ReadyItems.reserve(DrawList.GetItemCount());
    for (const RadientDrawItem& Item : DrawList.GetItems())
    {
        const RADIENT_STATUS LoadStatus = ResourceCache.EnsureGLTFLoaded(Item.Mesh, pDevice, pContext);
        if (RADIENT_FAILED(LoadStatus))
            return LoadStatus;
        if (LoadStatus == RADIENT_STATUS_OK)
            ReadyItems.emplace_back(&Item);
    }

    if (ReadyItems.empty())
        return RADIENT_STATUS_OK;

    m_CacheUseInfo.pResourceMgr = pResourceManager;
    m_pGLTFRenderer->Begin(pDevice, pContext, m_CacheUseInfo, m_CacheBindings, m_pFrameAttribsCB);

    for (const RadientDrawItem* pItem : ReadyItems)
    {
        VERIFY_EXPR(pItem != nullptr);
        const RadientDrawItem& Item = *pItem;

        const GLTF::Model* pModel = ResourceCache.GetGLTFModel(Item.Mesh);
        if (pModel == nullptr)
            continue;

        const Uint32 SceneIndex = static_cast<Uint32>(pModel->DefaultSceneId);
        pModel->ComputeTransforms(SceneIndex, m_Transforms);
        m_PrevTransforms = m_Transforms;

        m_RenderInfo.SceneIndex     = SceneIndex;
        m_RenderInfo.ModelTransform = RadientMath::ToFloat4x4(Item.WorldMatrix);
        m_pGLTFRenderer->Render(pContext, *pModel, m_Transforms, &m_PrevTransforms, m_RenderInfo, nullptr, &m_CacheBindings);
    }

    ++m_FrameIndex;

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientGeometryPass::CreateRenderer(IRenderDevice*  pDevice,
                                                   IDeviceContext* pContext,
                                                   TEXTURE_FORMAT  RTVFormat,
                                                   TEXTURE_FORMAT  DSVFormat)
{
    if (pDevice == nullptr || pContext == nullptr || RTVFormat == TEX_FORMAT_UNKNOWN)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    GLTF_PBR_Renderer::CreateInfo RendererCI;
    RendererCI.EnableIBL               = false;
    RendererCI.EnableAO                = true;
    RendererCI.EnableEmissive          = true;
    RendererCI.EnableShadows           = false;
    RendererCI.FrontCounterClockwise   = true;
    RendererCI.PackMatrixRowMajor      = true;
    RendererCI.ShaderTexturesArrayMode = PBR_Renderer::SHADER_TEXTURE_ARRAY_MODE_NONE;
    RendererCI.NumRenderTargets        = 1;
    RendererCI.RTVFormats[0]           = RTVFormat;
    RendererCI.DSVFormat               = DSVFormat;
    RendererCI.TexColorConversionMode  = pDevice->GetDeviceInfo().Features.TextureSubresourceViews ?
         GLTF_PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE :
         GLTF_PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR;

    m_pGLTFRenderer = std::make_unique<GLTF_PBR_Renderer>(pDevice, nullptr, pContext, RendererCI);

    m_pFrameAttribsCB.Release();
    CreateUniformBuffer(pDevice,
                        m_pGLTFRenderer->GetPRBFrameAttribsSize(),
                        "Radient PBR frame attribs buffer",
                        &m_pFrameAttribsCB);
    if (m_pFrameAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    InitializeResourceCacheUseInfo();

    m_RenderInfo.Flags =
        GLTF_PBR_Renderer::PSO_FLAG_DEFAULT |
        GLTF_PBR_Renderer::PSO_FLAG_ALL_TEXTURES |
        GLTF_PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
        GLTF_PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS;
    m_RenderInfo.Flags &= ~GLTF_PBR_Renderer::PSO_FLAG_USE_IBL;
    m_RenderInfo.Flags &= ~GLTF_PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING;
    if (RequiresOutputSRGBConversion(RTVFormat))
        m_RenderInfo.Flags |= GLTF_PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB;

    m_CacheBindings = {};
    m_RTVFormat     = RTVFormat;
    m_DSVFormat     = DSVFormat;

    return RADIENT_STATUS_OK;
}

void RadientGeometryPass::InitializeResourceCacheUseInfo()
{
    if (!m_CacheUseInfo.VtxLayoutKey.Elements.empty())
        return;

    InputLayoutDescX    InputLayout = GLTF::VertexAttributesToInputLayout(GLTF::DefaultVertexAttributes.data(), GLTF::DefaultVertexAttributes.size());
    std::vector<Uint32> Strides     = InputLayout.ResolveAutoOffsetsAndStrides();

    m_CacheUseInfo.VtxLayoutKey.Elements.reserve(Strides.size());
    for (const Uint32 Stride : Strides)
    {
        m_CacheUseInfo.VtxLayoutKey.Elements.emplace_back(Stride, BIND_VERTEX_BUFFER);
    }

    m_CacheUseInfo.SetAtlasFormats(TEX_FORMAT_RGBA8_TYPELESS);
}

} // namespace Diligent
