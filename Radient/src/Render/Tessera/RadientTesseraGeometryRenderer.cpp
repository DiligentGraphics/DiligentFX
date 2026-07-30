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

#include "Math/RadientMath.hpp"

#include "MapHelper.hpp"

#include <algorithm>
#include <cmath>

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

constexpr float  RadientDefaultSceneScale = 1.f;
constexpr Uint32 RadientMaxLightCount     = 16;

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

void WriteCameraShaderAttribs(IRenderDevice*                   pDevice,
                              IRadientScene*                   pScene,
                              RadientEntityID                  CameraEntity,
                              const RadientFrameRenderTargets& Targets,
                              Uint32                           FrameIndex,
                              HLSL::CameraAttribs&             CameraAttribs)
{
    const RadientCameraComponent Camera     = GetCameraComponent(pScene, CameraEntity);
    const RadientExtent2D&       TargetSize = Targets.GetSize();

    const float Width            = static_cast<float>(TargetSize.Width);
    const float Height           = static_cast<float>(TargetSize.Height);
    const float Aspect           = Height > 0.f ? Width / Height : 1.f;
    const bool  NDCMinusOneToOne = pDevice != nullptr && pDevice->GetDeviceInfo().NDC.MinZ < 0.f;

    float4x4 CameraWorld = float4x4::Identity();
    if (pScene != nullptr && CameraEntity != InvalidRadientEntityID)
    {
        RadientMatrix4x4 CameraWorldMatrix{};
        if (RADIENT_SUCCEEDED(pScene->GetCachedWorldMatrix(CameraEntity, CameraWorldMatrix)))
            CameraWorld = RadientMath::ToFloat4x4(CameraWorldMatrix);
    }
    const RadientMath::CameraProjection CameraProj     = RadientMath::GetCameraProjection(Camera, Aspect, NDCMinusOneToOne);
    const float4x4                      CameraView     = CameraWorld.Inverse();
    const float4x4                      CameraViewProj = CameraView * CameraProj.Matrix;
    const float4x4                      CameraViewInv  = CameraWorld;

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
    RendererAttribs.DebugView         = 0;
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

} // namespace

RADIENT_STATUS RadientTesseraGeometryRenderer::Prepare(IRenderDevice* pDevice, IDeviceContext* pContext)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (m_pRenderer == nullptr)
        return CreateRenderer(pDevice, pContext);

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraGeometryRenderer::BeginFrame(IRenderDevice*                            pDevice,
                                                          IDeviceContext*                           pContext,
                                                          const RadientLightLists&                  LightList,
                                                          GLTF::ResourceManager*                    pResourceManager,
                                                          const RadientTesseraGeometryFrameAttribs& FrameAttribs,
                                                          const RadientFrameRenderTargets&          Targets)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (m_pRenderer == nullptr)
    {
        const RADIENT_STATUS PrepareStatus = Prepare(pDevice, pContext);
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

        WriteCameraShaderAttribs(pDevice, FrameAttribs.pScene, FrameAttribs.Camera, Targets, m_FrameIndex, pFrameAttribs->Camera);
        WriteCameraShaderAttribs(pDevice, FrameAttribs.pScene, FrameAttribs.Camera, Targets, m_FrameIndex, pFrameAttribs->PrevCamera);
        WriteSceneLights(*m_pRenderer,
                         LightList,
                         FrameAttribs.Environment,
                         FrameAttribs.pPrefilteredEnvMapSRV,
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

void RadientTesseraGeometryRenderer::EndFrame()
{
    ++m_FrameIndex;
}

RADIENT_STATUS RadientTesseraGeometryRenderer::CreateRenderer(IRenderDevice* pDevice, IDeviceContext* pContext)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (pDevice->GetDeviceInfo().Features.TextureSubresourceViews != DEVICE_FEATURE_STATE_ENABLED)
    {
        LOG_ERROR_MESSAGE("Radient geometry rendering requires texture subresource views");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    PBR_Renderer::CreateInfo RendererCI;
    RendererCI.EnableIBL               = true;
    RendererCI.EnableAO                = true;
    RendererCI.EnableEmissive          = true;
    RendererCI.EnableShadows           = false;
    RendererCI.MaxLightCount           = RadientMaxLightCount;
    RendererCI.MaxJointCount           = 0;
    RendererCI.PackMatrixRowMajor      = true;
    RendererCI.ShaderTexturesArrayMode = PBR_Renderer::SHADER_TEXTURE_ARRAY_MODE_NONE;
    InputLayoutDescX InputLayout       = GLTF::VertexAttributesToInputLayout(GLTF::DefaultVertexAttributes.data(), GLTF::DefaultVertexAttributes.size());
    RendererCI.InputLayout             = InputLayout;
    RendererCI.TexColorConversionMode  = PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE;
    SetGLTFTextureAttribIndices(RendererCI);

    m_pRenderer = std::make_unique<RadientPBRRenderer>(pDevice, nullptr, pContext, RendererCI);
    if (m_pRenderer->GetFrameAttribsCB() == nullptr)
    {
        m_pRenderer.reset();
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    m_BaseRenderFlags =
        PBR_Renderer::PSO_FLAG_DEFAULT |
        PBR_Renderer::PSO_FLAG_ALL_TEXTURES |
        PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
        PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS;
    m_BaseRenderFlags &= ~PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING;
    m_BaseRenderFlags &= ~PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS;

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
