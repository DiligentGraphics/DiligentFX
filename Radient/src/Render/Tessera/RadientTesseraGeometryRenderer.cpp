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

#include "Render/Tessera/RadientTesseraGeometryRenderer.hpp"

#include "Assets/RadientTextureAssetManager.hpp"
#include "Math/RadientMath.hpp"
#include "PostProcess/TemporalAntiAliasing/interface/TemporalAntiAliasing.hpp"

#include "GraphicsUtilities.h"
#include "MapHelper.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

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

constexpr float  RadientDefaultSceneScale          = 1.f;
constexpr Uint32 RadientMaxLightCount              = 16;
constexpr Uint64 RadientPrimitiveAttribsBufferSize = Uint64{64} << 10u;

float3 GetLightingScale(const RadientFloat3& Color, Float32 Intensity, Float32 Exposure)
{
    const float Scale = Intensity * std::exp2(Exposure);
    return float3{Color.x * Scale, Color.y * Scale, Color.z * Scale};
}

RadientCameraComponent GetCameraComponent(IRadientScene* pScene, RadientEntityID CameraEntity)
{
    RadientCameraComponent Camera{};
    if (pScene != nullptr && CameraEntity != InvalidRadientEntityID)
        (void)pScene->GetCamera(CameraEntity, Camera);

    return Camera;
}

RadientTesseraCameraState CaptureCameraState(IRadientScene*                   pScene,
                                             RadientEntityID                  CameraEntity,
                                             const RadientFrameRenderTargets& Targets,
                                             const float2&                    Jitter)
{
    RadientTesseraCameraState State;
    State.Scene        = pScene;
    State.Camera       = CameraEntity;
    State.Attribs      = GetCameraComponent(pScene, CameraEntity);
    State.ViewportSize = Targets.GetSize();
    State.Jitter       = Jitter;
    State.IsValid      = true;

    if (pScene != nullptr && CameraEntity != InvalidRadientEntityID)
        (void)pScene->GetCachedWorldMatrix(CameraEntity, State.World);

    return State;
}

void WriteCameraShaderAttribs(IRenderDevice*                   pDevice,
                              const RadientTesseraCameraState& CameraState,
                              bool                             UseReverseDepth,
                              Uint32                           FrameIndex,
                              HLSL::CameraAttribs&             CameraAttribs)
{
    const RadientCameraComponent& Camera     = CameraState.Attribs;
    const RadientExtent2D&        TargetSize = CameraState.ViewportSize;

    const float Width            = static_cast<float>(TargetSize.Width);
    const float Height           = static_cast<float>(TargetSize.Height);
    const float Aspect           = Height > 0.f ? Width / Height : 1.f;
    const bool  NDCMinusOneToOne = pDevice != nullptr && pDevice->GetDeviceInfo().NDC.MinZ < 0.f;

    // Radient cameras follow the OpenUSD/glTF convention and look along local
    // -Z, while Diligent shaders and post-processing effects use +Z camera
    // space. Convert the camera transform instead of baking the adapter into
    // the projection so effects that consume mView and mProj separately see
    // the expected coordinate system.
    const float4x4 CameraWorld =
        float4x4::Scale(1.f, 1.f, -1.f) * RadientMath::ToFloat4x4(CameraState.World);
    const RadientMath::CameraProjection CameraProj = RadientMath::GetCameraProjection(Camera, Aspect, NDCMinusOneToOne, UseReverseDepth);
    const float4x4                      JitteredProjection =
        TemporalAntiAliasing::GetJitteredProjMatrix(CameraProj.Matrix, CameraState.Jitter);
    const float4x4 CameraView     = CameraWorld.Inverse();
    const float4x4 CameraViewProj = CameraView * JitteredProjection;
    const float4x4 CameraViewInv  = CameraWorld;

    CameraAttribs.f4ViewportSize = float4{Width, Height, Width > 0.f ? 1.f / Width : 0.f, Height > 0.f ? 1.f / Height : 0.f};
    CameraAttribs.SetClipPlanes(UseReverseDepth ? CameraProj.FarPlaneZ : CameraProj.NearPlaneZ,
                                UseReverseDepth ? CameraProj.NearPlaneZ : CameraProj.FarPlaneZ);
    CameraAttribs.fSceneNearZ     = CameraAttribs.fNearPlaneZ;
    CameraAttribs.fSceneFarZ      = CameraAttribs.fFarPlaneZ;
    CameraAttribs.fSceneNearDepth = CameraAttribs.fNearPlaneDepth;
    CameraAttribs.fSceneFarDepth  = CameraAttribs.fFarPlaneDepth;
    CameraAttribs.fHandness       = CameraView.Determinant() > 0.f ? 1.f : -1.f;
    CameraAttribs.uiFrameIndex    = FrameIndex;
    CameraAttribs.fExposure       = 0.f;
    CameraAttribs.fFocusDistance  = Camera.FocusDistance;
    CameraAttribs.fFStop          = Camera.FStop;
    CameraAttribs.fFocalLength    = CameraProj.FocalLength;
    CameraAttribs.fSensorWidth    = CameraProj.HorizontalAperture;
    CameraAttribs.fSensorHeight   = CameraProj.VerticalAperture;
    CameraAttribs.mView           = CameraView;
    CameraAttribs.mProj           = JitteredProjection;
    CameraAttribs.mViewProj       = CameraViewProj;
    CameraAttribs.mViewInv        = CameraViewInv;
    CameraAttribs.mProjInv        = JitteredProjection.Inverse();
    CameraAttribs.mViewProjInv    = CameraViewProj.Inverse();
    CameraAttribs.f4Position      = float4{float3::MakeVector(CameraWorld[3]), 1.f};
    CameraAttribs.f2Jitter        = CameraState.Jitter;
}

PBR_Renderer::CreateInfo::PSMainSourceInfo GetTesseraPBRPSMainSource(PBR_Renderer::PSO_FLAGS)
{
    static_assert(RadientFrameRenderTargets::GBUFFER_TARGET_COUNT == 6,
                  "Update the Tessera PBR pixel shader outputs when the G-buffer layout changes");

    PBR_Renderer::CreateInfo::PSMainSourceInfo Source;
    std::stringstream                          Output;
    Output << "struct PSOutput\n"
           << "{\n"
           << "    float4 Color      : SV_Target" << RadientFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR << ";\n"
           << "    float4 MotionVec  : SV_Target" << RadientFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR << ";\n"
           << "    float4 Normal     : SV_Target" << RadientFrameRenderTargets::GBUFFER_TARGET_NORMAL << ";\n"
           << "    float4 BaseColor  : SV_Target" << RadientFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR << ";\n"
           << "    float4 Material   : SV_Target" << RadientFrameRenderTargets::GBUFFER_TARGET_MATERIAL << ";\n"
           << "    float4 IBL        : SV_Target" << RadientFrameRenderTargets::GBUFFER_TARGET_IBL << ";\n"
           << "};\n";
    Source.OutputStruct = Output.str();

    Source.Footer = R"(
    PSOutput PSOut;

    float3 Normal       = float3(0.0, 0.0, 0.0);
    float2 MaterialData = float2(0.0, 0.0);
    float3 IBL          = float3(0.0, 0.0, 0.0);

#if UNSHADED
    float4 OutColor     = g_Frame.Renderer.UnshadedColor + g_Frame.Renderer.HighlightColor;
    float4 BaseColor    = float4(0.0, 0.0, 0.0, 0.0);
    float2 MotionVector = float2(0.0, 0.0);
#else
    Normal       = Shading.BaseLayer.Normal.xyz;
    MaterialData = float2(Shading.BaseLayer.Srf.PerceptualRoughness, Shading.BaseLayer.Metallic);
    IBL          = GetBaseLayerSpecularIBL(Shading, SrfLighting);

#   if ENABLE_CLEAR_COAT
    {
        Normal       = normalize(lerp(Normal, Shading.Clearcoat.Normal, Shading.Clearcoat.Factor));
        MaterialData = lerp(MaterialData,
                            float2(Shading.Clearcoat.Srf.PerceptualRoughness, 0.0),
                            Shading.Clearcoat.Factor);
        BaseColor.rgb = lerp(BaseColor.rgb, float3(1.0, 1.0, 1.0), Shading.Clearcoat.Factor);
        IBL = lerp(IBL, GetClearcoatIBL(Shading, SrfLighting), Shading.Clearcoat.Factor);
    }
#   endif

    MaterialData  *= Transmittance;
    IBL           *= Transmittance;
    BaseColor.rgb *= Transmittance;
#endif

    PSOut.Color     = OutColor;
    PSOut.MotionVec = float4(MotionVector, 0.0, 1.0);
    PSOut.Normal    = float4(Normal, 1.0);
    PSOut.BaseColor = float4(BaseColor.rgb * BaseColor.a, BaseColor.a);
    PSOut.Material  = float4(MaterialData * BaseColor.a, 0.0, BaseColor.a);
    PSOut.IBL       = float4(IBL * BaseColor.a, BaseColor.a);
    return PSOut;
)";
    return Source;
}

PBR_Renderer::LIGHT_TYPE GetPBRLightType(RADIENT_LIGHT_TYPE Type)
{
    switch (Type)
    {
        case RADIENT_LIGHT_TYPE_DIRECTIONAL:
            return PBR_Renderer::LIGHT_TYPE_DIRECTIONAL;

        case RADIENT_LIGHT_TYPE_POINT:
            return PBR_Renderer::LIGHT_TYPE_POINT;

        case RADIENT_LIGHT_TYPE_SPOT:
            return PBR_Renderer::LIGHT_TYPE_SPOT;

        default:
            UNEXPECTED("Unexpected Radient light type");
            return PBR_Renderer::LIGHT_TYPE_UNKNOWN;
    }
}

float3 GetLightPosition(const RadientMatrix4x4& WorldMatrix)
{
    const RadientFloat4 Position = WorldMatrix.GetRow(3);
    return float3{Position.x, Position.y, Position.z};
}

float3 GetLightDirection(const RadientMatrix4x4& WorldMatrix)
{
    const RadientFloat4 LocalZ    = WorldMatrix.GetRow(2);
    const float3        Direction = {-LocalZ.x, -LocalZ.y, -LocalZ.z};
    const float         Length    = length(Direction);
    return Length > 0.f ? Direction / Length : float3{0.f, 0.f, -1.f};
}

void WritePBRLightShaderAttribs(const RadientLightComponent& Light,
                                const float3*                Position,
                                const float3*                Direction,
                                HLSL::PBRLightAttribs&       ShaderAttribs)
{
    ShaderAttribs.Type = static_cast<int>(GetPBRLightType(Light.Type));

    if (Position != nullptr)
    {
        ShaderAttribs.PosX = Position->x;
        ShaderAttribs.PosY = Position->y;
        ShaderAttribs.PosZ = Position->z;
    }

    if (Direction != nullptr)
    {
        ShaderAttribs.DirectionX = Direction->x;
        ShaderAttribs.DirectionY = Direction->y;
        ShaderAttribs.DirectionZ = Direction->z;
    }

    ShaderAttribs.ShadowMapIndex = -1;

    const float Intensity    = Light.Intensity * std::exp2(Light.Exposure);
    ShaderAttribs.IntensityR = Light.Color.x * Intensity;
    ShaderAttribs.IntensityG = Light.Color.y * Intensity;
    ShaderAttribs.IntensityB = Light.Color.z * Intensity;

    const float Range    = Light.Range * RadientDefaultSceneScale;
    const float Range2   = Range * Range;
    ShaderAttribs.Range4 = Range2 * Range2;

    const float OuterConeAngle = clamp(Light.OuterConeAngle, 0.f, PI_F * 0.5f);
    const float InnerConeAngle = clamp(Light.InnerConeAngle, 0.f, OuterConeAngle);

    const float SpotAngleScale  = 1.f / std::max(0.001f, std::cos(InnerConeAngle) - std::cos(OuterConeAngle));
    const float SpotAngleOffset = -std::cos(OuterConeAngle) * SpotAngleScale;

    ShaderAttribs.SpotAngleOffset = SpotAngleOffset;
    ShaderAttribs.SpotAngleScale  = SpotAngleScale;
}

void WriteSceneLights(PBR_Renderer&                 Renderer,
                      const RadientLightLists&      LightList,
                      const RadientEnvironmentDesc& Environment,
                      ITextureView*                 pPrefilteredEnvMapSRV,
                      PBR_Renderer::DebugViewType   DebugView,
                      HLSL::PBRFrameAttribs&        FrameAttribs)
{
    HLSL::PBRLightAttribs* Lights = reinterpret_cast<HLSL::PBRLightAttribs*>(&FrameAttribs + 1);

    Uint32 LightCount = 0;
    LightList.Enumerate([&](const RadientLightItem& LightItem) {
        if (LightCount >= RadientMaxLightCount)
            return;

        VERIFY(LightItem.pLight != nullptr, "Light list item has null light pointer");
        VERIFY(LightItem.pWorldMatrix != nullptr, "Light list item has null world matrix pointer");
        VERIFY(LightItem.pEffectiveVisible != nullptr, "Light list item has null visibility pointer");
        if (LightItem.pLight == nullptr ||
            LightItem.pWorldMatrix == nullptr ||
            LightItem.pEffectiveVisible == nullptr ||
            !*LightItem.pEffectiveVisible)
            return;

        const RadientLightComponent& Light = *LightItem.pLight;

        const float3 Position  = GetLightPosition(*LightItem.pWorldMatrix);
        const float3 Direction = GetLightDirection(*LightItem.pWorldMatrix);
        const bool   HasPosition =
            Light.Type == RADIENT_LIGHT_TYPE_POINT ||
            Light.Type == RADIENT_LIGHT_TYPE_SPOT;
        const bool HasDirection =
            Light.Type == RADIENT_LIGHT_TYPE_DIRECTIONAL ||
            Light.Type == RADIENT_LIGHT_TYPE_SPOT;

        WritePBRLightShaderAttribs(Light,
                                   HasPosition ? &Position : nullptr,
                                   HasDirection ? &Direction : nullptr,
                                   Lights[LightCount]);
        ++LightCount;
    });

    HLSL::PBRRendererShaderParameters& RendererAttribs = FrameAttribs.Renderer;
    Renderer.SetInternalShaderParameters(RendererAttribs, pPrefilteredEnvMapSRV);
    RendererAttribs.OcclusionStrength = 1.f;
    RendererAttribs.EmissionScale     = 1.f;
    RendererAttribs.AverageLogLum     = 0.f;
    RendererAttribs.MiddleGray        = 0.18f;
    RendererAttribs.WhitePoint        = 3.f;
    RendererAttribs.IBLScale          = float4{GetLightingScale(Environment.Color, Environment.Intensity, Environment.Exposure), 1.f};
    RendererAttribs.HighlightColor    = float4{0.f, 0.f, 0.f, 0.f};
    RendererAttribs.UnshadedColor     = float4{1.f, 1.f, 1.f, 1.f};
    RendererAttribs.PointSize         = 1.f;
    RendererAttribs.MipBias           = 0.f;
    RendererAttribs.LightCount        = static_cast<int>(LightCount);
    RendererAttribs.DebugView         = static_cast<int>(DebugView);
}

void SetGLTFTextureAttribIndices(PBR_Renderer::CreateInfo& CI)
{
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR]            = GLTF::DefaultBaseColorTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]             = GLTF::DefaultMetallicRoughnessTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]                = GLTF::DefaultNormalTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION]             = GLTF::DefaultOcclusionTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]              = GLTF::DefaultEmissiveTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT]            = GLTF::DefaultClearcoatTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS]  = GLTF::DefaultClearcoatRoughnessTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL]     = GLTF::DefaultClearcoatNormalTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_COLOR]           = GLTF::DefaultSheenColorTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS]       = GLTF::DefaultSheenRoughnessTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_ANISOTROPY]            = GLTF::DefaultAnisotropyTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE]           = GLTF::DefaultIridescenceTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS] = GLTF::DefaultIridescenceThicknessTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_TRANSMISSION]          = GLTF::DefaultTransmissionTextureAttribId;
    CI.TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_THICKNESS]             = GLTF::DefaultThicknessTextureAttribId;
    static_assert(PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT == 17, "Please update the GLTF texture attribute mapping");
}

bool InitializeMaterialTextureBinding(IRadientTextureAsset*             pTexture,
                                      RadientTextureViewType            ViewType,
                                      RadientMaterialTextureRenderData& TextureData)
{
    if (pTexture == nullptr)
        return false;

    const RadientTextureBindingIdentity BindingIdentity =
        RadientTextureAssetManager::GetTextureBindingIdentity(pTexture, ViewType);
    if (!BindingIdentity)
        return false;

    TextureData.pTexture        = pTexture;
    TextureData.ViewType        = ViewType;
    TextureData.BindingIdentity = BindingIdentity;
    return true;
}

} // namespace

RadientTesseraGeometryRenderer::RadientTesseraGeometryRenderer(
    Uint32                                MaterialTextureSlotCount,
    const RadientMaterialDefaultTextures& DefaultTextures,
    Uint32                                MultiDrawBatchSize) noexcept :
    m_DefaultMaterialTextures{DefaultTextures},
    m_MaterialTextureSlotCount{MaterialTextureSlotCount != 0 ? MaterialTextureSlotCount : 8},
    m_MultiDrawBatchSize{MultiDrawBatchSize != 0 ? MultiDrawBatchSize : 16}
{}

void RadientTesseraGeometryRenderer::PrepareDefaultMaterialTextureBindings()
{
    // Default texture assets load asynchronously. Keep each resolved binding so
    // subsequent frames only retry the entries that are not ready yet.
    auto Initialize = [](IRadientTextureAsset*             pTexture,
                         RadientTextureViewType            ViewType,
                         RadientMaterialTextureRenderData& TextureData) {
        return TextureData || InitializeMaterialTextureBinding(pTexture, ViewType, TextureData);
    };

    if (!Initialize(m_DefaultMaterialTextures.pWhite, RadientTextureViewType::Linear, m_DefaultMaterialTextureBindings.WhiteLinear) ||
        !Initialize(m_DefaultMaterialTextures.pWhite, RadientTextureViewType::SRGB, m_DefaultMaterialTextureBindings.WhiteSRGB) ||
        !Initialize(m_DefaultMaterialTextures.pBlack, RadientTextureViewType::SRGB, m_DefaultMaterialTextureBindings.BlackSRGB) ||
        !Initialize(m_DefaultMaterialTextures.pNormal, RadientTextureViewType::Linear, m_DefaultMaterialTextureBindings.Normal) ||
        !Initialize(m_DefaultMaterialTextures.pPhysicalDesc, RadientTextureViewType::Linear, m_DefaultMaterialTextureBindings.PhysicalDesc))
    {
        return;
    }

    m_DefaultMaterialTextureBindingsReady = true;
}

void RadientTesseraGeometryRenderer::CreateMaterialCache(IRenderDevice* pDevice)
{
    VERIFY_EXPR(pDevice != nullptr);
    VERIFY_EXPR(m_pRenderer != nullptr);
    VERIFY_EXPR(m_DefaultMaterialTextureBindingsReady);

    const PBR_Renderer::CreateInfo& Settings = m_pRenderer->GetSettings();

    RadientTesseraMaterialCache::CreateInfo CacheCI;
    CacheCI.TextureAttribIndices          = Settings.TextureAttribIndices;
    CacheCI.MaterialTextureSlotCount      = Settings.MaterialTexturesArraySize;
    CacheCI.EnabledMaterialPSOFlags       = PBR_Renderer::GetEnabledPSOFlags(Settings);
    CacheCI.DefaultTextures               = m_DefaultMaterialTextureBindings;
    CacheCI.ConstantBufferOffsetAlignment = pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment;
    CacheCI.MaxMaterialAttribsSize        = m_pRenderer->GetPBRMaterialAttribsSize(PBR_Renderer::PSO_FLAG_ALL);
    m_pMaterialCache                      = std::make_unique<RadientTesseraMaterialCache>(CacheCI);
}

RADIENT_STATUS RadientTesseraGeometryRenderer::PrepareMaterialSRBs(IRenderDevice*  pDevice,
                                                                   IDeviceContext* pContext,
                                                                   Uint32          TextureVersion)
{
    VERIFY_EXPR(m_pRenderer != nullptr);
    VERIFY_EXPR(m_pMaterialCache != nullptr);

    const RADIENT_STATUS MaterialBufferStatus = m_pMaterialCache->PrepareMaterialBuffer(pDevice, pContext);
    if (MaterialBufferStatus != RADIENT_STATUS_OK)
        return MaterialBufferStatus;

    // The renderer may be initialized before any material records exist. A
    // worker that allocates the first record after PrepareMaterialBuffer()
    // will leave its generation pending for the next frame.
    if (m_pMaterialCache->GetMaterialBuffer() == nullptr)
        return RADIENT_STATUS_OK;

    return m_pMaterialCache->Prepare(
        TextureVersion,
        [](const RadientMaterialTextureRenderData& Binding) {
            const RADIENT_STATUS TextureStatus = RadientTextureAssetManager::GetGPUResourceStatus(Binding.pTexture);
            if (TextureStatus != RADIENT_STATUS_OK)
                return RadientMaterialTextureSRVResolveResult{TextureStatus, nullptr};

            ITextureView* const pTextureSRV = RadientTextureAssetManager::GetTextureSRV(Binding.pTexture, Binding.ViewType);
            if (pTextureSRV == nullptr)
                return RadientMaterialTextureSRVResolveResult{RADIENT_STATUS_INVALID_OPERATION, nullptr};

            if (pTextureSRV->GetDesc().TextureDim != RESOURCE_DIM_TEX_2D_ARRAY)
            {
                UNEXPECTED("Material texture SRV is not a 2D array");
                return RadientMaterialTextureSRVResolveResult{RADIENT_STATUS_INVALID_OPERATION, nullptr};
            }
            return RadientMaterialTextureSRVResolveResult{RADIENT_STATUS_OK, pTextureSRV};
        },
        [this](ITextureView* const* ppTextureSRVs, Uint32 TextureCount) {
            RefCntAutoPtr<IShaderResourceBinding> pSRB;
            m_pRenderer->CreateResourceBinding(pSRB.GetAddressOfEmpty(), 1);
            if (pSRB == nullptr)
                return pSRB;

            m_pRenderer->InitMaterialSRBVars(
                pSRB,
                m_pMaterialCache->GetMaterialBuffer(),
                m_pMaterialCache->GetMaxMaterialAttribsSize());
            if (!m_pRenderer->SetMaterialTextures(pSRB, ppTextureSRVs, 0, TextureCount))
                pSRB.Release();

            return pSRB;
        });
}

RADIENT_STATUS RadientTesseraGeometryRenderer::Prepare(IRenderDevice*         pDevice,
                                                       IDeviceContext*        pContext,
                                                       GLTF::ResourceManager* pResourceManager)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (m_pRenderer == nullptr)
    {
        const RADIENT_STATUS Status = CreateRenderer(pDevice, pContext);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (!m_DefaultMaterialTextureBindingsReady)
        PrepareDefaultMaterialTextureBindings();

    if (m_DefaultMaterialTextureBindingsReady && m_pMaterialCache == nullptr)
        CreateMaterialCache(pDevice);

    if (m_pMaterialCache != nullptr && pResourceManager != nullptr)
        return PrepareMaterialSRBs(pDevice, pContext, pResourceManager->GetTextureVersion());

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraGeometryRenderer::BeginFrame(IRenderDevice*                            pDevice,
                                                          IDeviceContext*                           pContext,
                                                          const RadientLightLists&                  LightList,
                                                          GLTF::ResourceManager*                    pResourceManager,
                                                          const RadientTesseraGeometryFrameAttribs& FrameAttribs,
                                                          const RadientFrameRenderTargets&          Targets,
                                                          RadientTesseraFrameHistory&               FrameHistory)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (m_pRenderer == nullptr)
    {
        const RADIENT_STATUS PrepareStatus = Prepare(pDevice, pContext, pResourceManager);
        if (RADIENT_FAILED(PrepareStatus))
            return PrepareStatus;
    }
    if (m_pRenderer == nullptr || m_pRenderer->GetFrameAttribsCB() == nullptr)
        return RADIENT_STATUS_OK;

    {
        MapHelper<HLSL::PBRFrameAttribs> MappedFrameAttribs{pContext, m_pRenderer->GetFrameAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD};
        HLSL::PBRFrameAttribs*           pFrameAttribs = MappedFrameAttribs;
        if (pFrameAttribs == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        RadientTesseraCameraState        CurrentCamera   = CaptureCameraState(FrameAttribs.pScene, FrameAttribs.Camera, Targets, FrameAttribs.CameraJitter);
        const RadientTesseraCameraState* pPreviousCamera = FrameHistory.GetPreviousCamera(CurrentCamera);
        if (pPreviousCamera == nullptr)
        {
            // A frame without compatible history establishes an unjittered
            // baseline for both camera attributes and TAA accumulation.
            CurrentCamera.Jitter = {};
            pPreviousCamera      = &CurrentCamera;
        }

        const Uint32 FrameIndex         = FrameHistory.GetFrameIndex();
        const Uint32 PreviousFrameIndex = pPreviousCamera != &CurrentCamera ? FrameIndex - 1u : FrameIndex;
        WriteCameraShaderAttribs(pDevice, CurrentCamera, Targets.GetUseReverseDepth(), FrameIndex, pFrameAttribs->Camera);
        WriteCameraShaderAttribs(pDevice, *pPreviousCamera, Targets.GetUseReverseDepth(), PreviousFrameIndex, pFrameAttribs->PrevCamera);
        FrameHistory.SetCurrentCamera(CurrentCamera);
        WriteSceneLights(*m_pRenderer,
                         LightList,
                         FrameAttribs.Environment,
                         FrameAttribs.pPrefilteredEnvMapSRV,
                         FrameAttribs.DebugView,
                         *pFrameAttribs);
    }

    if (pResourceManager == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    if (m_pRenderer->GetJointsBuffer() != nullptr)
    {
        MapHelper<float4x4> pJoints{pContext, m_pRenderer->GetJointsBuffer(), MAP_WRITE, MAP_FLAG_DISCARD};
    }

    if (IBuffer* pIndexBuffer = pResourceManager->GetIndexBuffer())
        pContext->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    return RADIENT_STATUS_OK;
}

void RadientTesseraGeometryRenderer::EndFrame(RadientTesseraFrameHistory& FrameHistory)
{
    FrameHistory.CommitFrame();
}

RADIENT_STATUS RadientTesseraGeometryRenderer::CreateRenderer(IRenderDevice* pDevice, IDeviceContext* pContext)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_MaterialTextureSlotCount == 0 ||
        m_MaterialTextureSlotCount > PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT)
    {
        LOG_ERROR_MESSAGE("Radient material texture slot count must be between 1 and ",
                          Uint32{PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (pDevice->GetDeviceInfo().Features.TextureSubresourceViews != DEVICE_FEATURE_STATE_ENABLED)
    {
        LOG_ERROR_MESSAGE("Radient geometry rendering requires texture subresource views");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    PBR_Renderer::CreateInfo RendererCI;
    RendererCI.EnableIBL                 = true;
    RendererCI.EnableAO                  = true;
    RendererCI.EnableEmissive            = true;
    RendererCI.EnableClearCoat           = true;
    RendererCI.EnableSheen               = false;
    RendererCI.EnableAnisotropy          = true;
    RendererCI.EnableIridescence         = true;
    RendererCI.EnableShadows             = false;
    RendererCI.MaxLightCount             = RadientMaxLightCount;
    RendererCI.MaxJointCount             = 0;
    RendererCI.PackMatrixRowMajor        = true;
    RendererCI.ShaderTexturesArrayMode   = PBR_Renderer::SHADER_TEXTURE_ARRAY_MODE_STATIC;
    RendererCI.MaterialTexturesArraySize = m_MaterialTextureSlotCount;
    RendererCI.GetPSMainSource           = GetTesseraPBRPSMainSource;

    const RenderDeviceInfo& DeviceInfo = pDevice->GetDeviceInfo();
    if (m_MultiDrawBatchSize > 1 &&
        (DeviceInfo.IsVulkanDevice() ||
         DeviceInfo.IsWebGPUDevice() ||
         (DeviceInfo.IsGLDevice() && DeviceInfo.Features.NativeMultiDraw)))
    {
        // Without native multi-draw, the PBR shader uses base instance as the
        // primitive ID. Direct3D does not offset SV_InstanceID by base instance.
        RendererCI.PrimitiveArraySize = m_MultiDrawBatchSize;
    }

    InputLayoutDescX InputLayout      = GLTF::VertexAttributesToInputLayout(GLTF::DefaultVertexAttributes.data(), GLTF::DefaultVertexAttributes.size());
    RendererCI.InputLayout            = InputLayout;
    RendererCI.TexColorConversionMode = PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE;
    SetGLTFTextureAttribIndices(RendererCI);

    RefCntAutoPtr<IBuffer> pPrimitiveAttribsCB;
    CreateUniformBuffer(pDevice,
                        RadientPrimitiveAttribsBufferSize,
                        "Radient PBR primitive attribs buffer",
                        pPrimitiveAttribsCB.GetAddressOfEmpty(),
                        USAGE_DYNAMIC);
    if (pPrimitiveAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;
    RendererCI.pPrimitiveAttribsCB = pPrimitiveAttribsCB;

    m_pRenderer                     = std::make_unique<RadientPBRRenderer>(pDevice, nullptr, pContext, RendererCI);
    const Uint32 PrimitiveArraySize = std::max(RendererCI.PrimitiveArraySize, 1u);
    const Uint64 PrimitiveAttribsRange =
        Uint64{m_pRenderer->GetPBRPrimitiveAttribsSize(PBR_Renderer::PSO_FLAG_ALL)} * PrimitiveArraySize;
    if (PrimitiveAttribsRange > pPrimitiveAttribsCB->GetDesc().Size)
    {
        LOG_ERROR_MESSAGE("Radient multi-draw batch size ", PrimitiveArraySize,
                          " exceeds the primitive attributes buffer capacity");
        m_pRenderer.reset();
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (m_pRenderer->GetFrameAttribsCB() == nullptr)
    {
        m_pRenderer.reset();
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    // Tessera enables material and geometry features per drawable. Only flags
    // controlled by the pass itself are excluded from this capability mask.
    m_BaseRenderFlags =
        PBR_Renderer::PSO_FLAG_ALL &
        ~(PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING |
          PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB);

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
