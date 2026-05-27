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

#include "GraphicsAccessories.hpp"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
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

constexpr float  RadientDefaultSceneScale = 1.f;
constexpr Uint32 RadientMaxLightCount     = 16;

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

void WriteSceneLights(PBR_Renderer&           Renderer,
                      const RadientLightList& LightList,
                      HLSL::PBRFrameAttribs&  FrameAttribs)
{
    HLSL::PBRLightAttribs* Lights = reinterpret_cast<HLSL::PBRLightAttribs*>(&FrameAttribs + 1);

    Uint32 LightCount = 0;
    for (const RadientLightItem& LightItem : LightList.GetItems())
    {
        if (LightCount >= RadientMaxLightCount)
            break;

        const float3 Position  = GetLightPosition(LightItem.WorldMatrix);
        const float3 Direction = GetLightDirection(LightItem.WorldMatrix);
        const bool   HasPosition =
            LightItem.Light.Type == RADIENT_LIGHT_TYPE_POINT ||
            LightItem.Light.Type == RADIENT_LIGHT_TYPE_SPOT;
        const bool HasDirection =
            LightItem.Light.Type == RADIENT_LIGHT_TYPE_DIRECTIONAL ||
            LightItem.Light.Type == RADIENT_LIGHT_TYPE_SPOT;

        WritePBRLightShaderAttribs(LightItem.Light,
                                   HasPosition ? &Position : nullptr,
                                   HasDirection ? &Direction : nullptr,
                                   Lights[LightCount]);
        ++LightCount;
    }

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
    RendererAttribs.LightCount        = static_cast<int>(LightCount);
    RendererAttribs.DebugView         = 0;
}

PBR_Renderer::ALPHA_MODE ToPBRAlphaMode(GLTF::Material::ALPHA_MODE AlphaMode)
{
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(GLTF::Material::ALPHA_MODE_OPAQUE) == PBR_Renderer::ALPHA_MODE_OPAQUE, "GLTF opaque alpha mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(GLTF::Material::ALPHA_MODE_MASK) == PBR_Renderer::ALPHA_MODE_MASK, "GLTF mask alpha mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(GLTF::Material::ALPHA_MODE_BLEND) == PBR_Renderer::ALPHA_MODE_BLEND, "GLTF blend alpha mode must match PBR alpha mode");
    return static_cast<PBR_Renderer::ALPHA_MODE>(AlphaMode);
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

PBR_Renderer::PSO_FLAGS GetVertexAttribFlags(const GLTF::Model& Model)
{
    PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::PSO_FLAG_NONE;
    for (Uint32 AttribIndex = 0; AttribIndex < Model.GetNumVertexAttributes(); ++AttribIndex)
    {
        if (!Model.IsVertexAttributeEnabled(AttribIndex))
            continue;

        const GLTF::VertexAttributeDesc& Attrib = Model.GetVertexAttribute(AttribIndex);
        if (std::strcmp(Attrib.Name, GLTF::NormalAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
        else if (std::strcmp(Attrib.Name, GLTF::Texcoord0AttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
        else if (std::strcmp(Attrib.Name, GLTF::Texcoord1AttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;
        else if (std::strcmp(Attrib.Name, GLTF::JointsAttributeName) == 0)
        {
            // Radient skinning is not wired yet; keep the pass on the rigid path.
        }
        else if (std::strcmp(Attrib.Name, GLTF::VertexColorAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
        else if (std::strcmp(Attrib.Name, GLTF::TangentAttributeName) == 0)
            Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
    }

    return Flags;
}

PBR_Renderer::PSO_FLAGS GetMaterialPSOFlags(const PBR_Renderer&   Renderer,
                                            const GLTF::Material& Material)
{
    const PBR_Renderer::CreateInfo& Settings = Renderer.GetSettings();

    PBR_Renderer::PSO_FLAGS Flags =
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP;

    if (Settings.EnableAO)
        Flags |= PBR_Renderer::PSO_FLAG_USE_AO_MAP;

    if (Settings.EnableEmissive)
        Flags |= PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP;

    if (Settings.EnableClearCoat && Material.HasClearcoat)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_CLEAR_COAT |
            PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_MAP |
            PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP |
            PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP;
    }

    if (Settings.EnableSheen && Material.Sheen)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_SHEEN |
            PBR_Renderer::PSO_FLAG_USE_SHEEN_COLOR_MAP |
            PBR_Renderer::PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP;
    }

    if (Settings.EnableAnisotropy && Material.Anisotropy)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_ANISOTROPY |
            PBR_Renderer::PSO_FLAG_USE_ANISOTROPY_MAP;
    }

    if (Settings.EnableIridescence && Material.Iridescence)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_IRIDESCENCE |
            PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_MAP |
            PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP;
    }

    if (Settings.EnableTransmission && Material.Transmission)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_TRANSMISSION |
            PBR_Renderer::PSO_FLAG_USE_TRANSMISSION_MAP;
    }

    if (Settings.EnableVolume && Material.Volume)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_VOLUME |
            PBR_Renderer::PSO_FLAG_USE_THICKNESS_MAP;
    }

    return Flags;
}

template <typename ShaderStructType, typename HostStructType>
Uint8* WriteShaderAttribs(Uint8* pDstPtr, HostStructType* pSrc, const char* DebugName)
{
    static_assert(sizeof(ShaderStructType) == sizeof(HostStructType), "Size of HLSL and C++ structures must be the same");
    if (pSrc != nullptr)
    {
        std::memcpy(pDstPtr, pSrc, sizeof(ShaderStructType));
    }
    else
    {
        UNEXPECTED("Shader attribute ", DebugName, " is not initialized in the material");
        std::memset(pDstPtr, 0, sizeof(ShaderStructType));
    }
    static_assert(sizeof(ShaderStructType) % 16 == 0, "Size structure must be a multiple of 16");
    return pDstPtr + sizeof(ShaderStructType);
}

struct PBRPrimitiveShaderAttribsData
{
    PBR_Renderer::PSO_FLAGS PSOFlags       = PBR_Renderer::PSO_FLAG_NONE;
    const float4x4*         NodeMatrix     = nullptr;
    const float4x4*         PrevNodeMatrix = nullptr;
    Uint32                  JointCount     = 0;
    Uint32                  FirstJoint     = 0;
};

void* WritePBRPrimitiveShaderAttribs(void*                                pDstShaderAttribs,
                                     const PBRPrimitiveShaderAttribsData& AttribsData,
                                     bool                                 TransposeMatrices)
{
    Uint8* pDstPtr    = reinterpret_cast<Uint8*>(pDstShaderAttribs);
    auto   WriteValue = [&pDstPtr](auto Val) {
        *reinterpret_cast<decltype(Val)*>(pDstPtr) = Val;
        pDstPtr += sizeof(Val);
    };

    if (AttribsData.NodeMatrix != nullptr)
        WriteShaderMatrix(pDstPtr, *AttribsData.NodeMatrix, TransposeMatrices);
    else
        UNEXPECTED("Node matrix must not be null");
    pDstPtr += sizeof(float4x4);

    if (AttribsData.PSOFlags & PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS)
    {
        if (AttribsData.PrevNodeMatrix != nullptr)
            WriteShaderMatrix(pDstPtr, *AttribsData.PrevNodeMatrix, TransposeMatrices);
        else
            UNEXPECTED("Prev node matrix must not be null when motion vectors are enabled");
        pDstPtr += sizeof(float4x4);
    }

    WriteValue(static_cast<int>(AttribsData.JointCount));
    WriteValue(static_cast<int>(AttribsData.FirstJoint));

    WriteValue(0.f);
    WriteValue(0.f);
    WriteValue(0.f);

    WriteValue(1.f);
    WriteValue(1.f);
    WriteValue(1.f);

    const float4 FallbackColor{1.f, 1.f, 1.f, 1.f};
    std::memcpy(pDstPtr, &FallbackColor, sizeof(FallbackColor));
    pDstPtr += sizeof(FallbackColor);

    return pDstPtr;
}

void* WritePBRMaterialShaderAttribs(void*                           pDstShaderAttribs,
                                    const PBR_Renderer::CreateInfo& Settings,
                                    PBR_Renderer::PSO_FLAGS         PSOFlags,
                                    const GLTF::Material&           Material)
{
    static_assert(static_cast<PBR_Renderer::PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_METALL_ROUGH) == PBR_Renderer::PBR_WORKFLOW_METALL_ROUGH, "GLTF metallic-roughness workflow must match PBR workflow");
    static_assert(static_cast<PBR_Renderer::PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS) == PBR_Renderer::PBR_WORKFLOW_SPEC_GLOSS, "GLTF specular-glossiness workflow must match PBR workflow");
    static_assert(static_cast<PBR_Renderer::PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_UNLIT) == PBR_Renderer::PBR_WORKFLOW_UNLIT, "GLTF unlit workflow must match PBR workflow");

    Uint8* pDstPtr = reinterpret_cast<Uint8*>(pDstShaderAttribs);

    pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialBasicAttribs>(pDstPtr, &Material.Attribs, "Basic Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_SHEEN)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialSheenAttribs>(pDstPtr, Material.Sheen.get(), "Sheen Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_ANISOTROPY)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialAnisotropyAttribs>(pDstPtr, Material.Anisotropy.get(), "Anisotropy Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_IRIDESCENCE)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialIridescenceAttribs>(pDstPtr, Material.Iridescence.get(), "Iridescence Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_TRANSMISSION)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialTransmissionAttribs>(pDstPtr, Material.Transmission.get(), "Transmission Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_VOLUME)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialVolumeAttribs>(pDstPtr, Material.Volume.get(), "Volume Attribs");

    HLSL::PBRMaterialTextureAttribs* pDstTextures      = reinterpret_cast<HLSL::PBRMaterialTextureAttribs*>(pDstPtr);
    Uint32                           NumTextureAttribs = 0;
    PBR_Renderer::ProcessTexturAttribs(PSOFlags, [&](int CurrIndex, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
        const int SrcAttribIndex = Settings.TextureAttribIndices[AttribId];
        if (SrcAttribIndex < 0)
        {
            UNEXPECTED("Shader texture attribute ", Uint32{AttribId}, " is not initialized");
            return;
        }

        static_assert(sizeof(HLSL::PBRMaterialTextureAttribs) == sizeof(GLTF::Material::TextureShaderAttribs),
                      "The sizeof(HLSL::PBRMaterialTextureAttribs) is inconsistent with sizeof(GLTF::Material::TextureShaderAttribs)");
        std::memcpy(pDstTextures + CurrIndex, &Material.GetTextureAttrib(SrcAttribIndex), sizeof(HLSL::PBRMaterialTextureAttribs));
        ++NumTextureAttribs;
    });

    return pDstTextures + NumTextureAttribs;
}

RefCntAutoPtr<ITextureView> GetPBRTextureSRV(ITexture*                                           pTexture,
                                             PBR_Renderer::TEXTURE_ATTRIB_ID                     ID,
                                             PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE ConversionMode)
{
    if (pTexture == nullptr)
    {
        UNEXPECTED("Texture is null");
        return {};
    }

    const TextureDesc& TexDesc = pTexture->GetDesc();
    TEXTURE_FORMAT     ViewFmt = TexDesc.Format;
    static_assert(PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT == 17, "Did you add a new texture attribute? It may need to be handled here.");
    if ((ConversionMode == PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE) &&
        (ID == PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR ||
         ID == PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE ||
         ID == PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_COLOR))
    {
        const TextureFormatAttribs& FmtInfo = GetTextureFormatAttribs(ViewFmt);
        if (FmtInfo.ComponentType != COMPONENT_TYPE_UNORM_SRGB)
        {
            if (FmtInfo.IsTypeless)
            {
                ViewFmt = TypelessFormatToSRGB(ViewFmt);
            }
            else
            {
                LOG_WARNING_MESSAGE("Unable to create sRGB view for texture '", pTexture->GetDesc().Name,
                                    "' as its format (", FmtInfo.Name,
                                    ") is not typeless. Expect images to be too bright.");
            }
        }
    }

    RefCntAutoPtr<ITextureView> pTexSRV;

    if (TexDesc.Type == RESOURCE_DIM_TEX_2D_ARRAY && TexDesc.Format == ViewFmt)
    {
        pTexSRV = pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }
    else
    {
        TextureViewDesc SRVDesc;
        SRVDesc.ViewType   = TEXTURE_VIEW_SHADER_RESOURCE;
        SRVDesc.TextureDim = RESOURCE_DIM_TEX_2D_ARRAY;
        SRVDesc.Format     = ViewFmt;
        pTexture->CreateView(SRVDesc, &pTexSRV);
    }

    return pTexSRV;
}

void CreateResourceCacheSRB(PBR_Renderer&                        Renderer,
                            IRenderDevice*                       pDevice,
                            IDeviceContext*                      pContext,
                            RadientGeometryResourceCacheUseInfo& CacheUseInfo,
                            IBuffer*                             pFrameAttribs,
                            IShaderResourceBinding**             ppCacheSRB)
{
    DEV_CHECK_ERR(CacheUseInfo.pResourceMgr != nullptr, "Resource manager must not be null");

    Renderer.CreateResourceBinding(ppCacheSRB);
    IShaderResourceBinding* const pSRB = *ppCacheSRB;
    if (pSRB == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient PBR resource cache SRB");
        return;
    }

    Renderer.InitCommonSRBVars(pSRB, pFrameAttribs);

    const PBR_Renderer::CreateInfo& Settings   = Renderer.GetSettings();
    auto                            SetTexture = [&](PBR_Renderer::TEXTURE_ATTRIB_ID ID) {
        const TEXTURE_FORMAT Fmt = CacheUseInfo.AtlasFormats[ID];
        if (ITexture* pTexture = CacheUseInfo.pResourceMgr->UpdateTexture(Fmt, pDevice, pContext))
        {
            if (RefCntAutoPtr<ITextureView> pTexSRV = GetPBRTextureSRV(pTexture, ID, Settings.TexColorConversionMode))
            {
                Renderer.SetMaterialTexture(pSRB, pTexSRV, ID);
            }
            else
            {
                UNEXPECTED("Failed to get SRV for atlas '", pTexture->GetDesc().Name, "'");
            }
        }
    };

    SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);
    SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC);
    SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);
    if (Settings.EnableAO)
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION);
    if (Settings.EnableEmissive)
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE);
    if (Settings.EnableClearCoat)
    {
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT);
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS);
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL);
    }
    if (Settings.EnableSheen)
    {
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_COLOR);
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS);
    }
    if (Settings.EnableAnisotropy)
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_ANISOTROPY);
    if (Settings.EnableIridescence)
    {
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE);
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS);
    }
    if (Settings.EnableTransmission)
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_TRANSMISSION);
    if (Settings.EnableVolume)
        SetTexture(PBR_Renderer::TEXTURE_ATTRIB_ID_THICKNESS);
}

void BeginResourceCache(PBR_Renderer&                         Renderer,
                        IRenderDevice*                        pDevice,
                        IDeviceContext*                       pContext,
                        RadientGeometryResourceCacheUseInfo&  CacheUseInfo,
                        RadientGeometryResourceCacheBindings& Bindings,
                        IBuffer*                              pFrameAttribs)
{
    VERIFY(CacheUseInfo.pResourceMgr != nullptr, "Resource manager must not be null.");
    VERIFY(CacheUseInfo.VtxLayoutKey != GLTF::ResourceManager::VertexLayoutKey{}, "Vertex layout key must not be empty.");

    if (Renderer.GetJointsBuffer() != nullptr)
    {
        MapHelper<float4x4> pJoints{pContext, Renderer.GetJointsBuffer(), MAP_WRITE, MAP_FLAG_DISCARD};
    }

    const Uint32 TextureVersion = CacheUseInfo.pResourceMgr->GetTextureVersion();
    if (!Bindings.pSRB || Bindings.Version != TextureVersion)
    {
        Bindings.pSRB.Release();
        CreateResourceCacheSRB(Renderer, pDevice, pContext, CacheUseInfo, pFrameAttribs, &Bindings.pSRB);
        if (!Bindings.pSRB)
        {
            LOG_ERROR_MESSAGE("Failed to create an SRB for Radient resource cache");
            return;
        }
        Bindings.Version = TextureVersion;
    }

    pContext->TransitionShaderResources(Bindings.pSRB);

    if (IVertexPool* pVertexPool = CacheUseInfo.pResourceMgr->GetVertexPool(CacheUseInfo.VtxLayoutKey))
    {
        const VertexPoolDesc& PoolDesc = pVertexPool->GetDesc();

        std::array<IBuffer*, 8> pVBs;
        VERIFY(PoolDesc.NumElements <= pVBs.size(), "Too many vertex buffers in Radient GLTF vertex pool");
        for (Uint32 Index = 0; Index < PoolDesc.NumElements; ++Index)
        {
            pVBs[Index] = pVertexPool->Update(Index, pDevice, pContext);
            if (pVBs[Index] != nullptr && (pVBs[Index]->GetDesc().BindFlags & BIND_VERTEX_BUFFER) == 0)
                pVBs[Index] = nullptr;
        }

        pContext->SetVertexBuffers(0, PoolDesc.NumElements, pVBs.data(), nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
    }

    if (IBuffer* pIndexBuffer = CacheUseInfo.pResourceMgr->UpdateIndexBuffer(pDevice, pContext))
        pContext->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void WritePrimitiveAttribs(PBR_Renderer&           Renderer,
                           IDeviceContext*         pContext,
                           PBR_Renderer::PSO_FLAGS PSOFlags,
                           const RadientMatrix4x4& WorldMatrix)
{
    void* pAttribsData = nullptr;
    pContext->MapBuffer(Renderer.GetPBRPrimitiveAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD, pAttribsData);
    if (pAttribsData == nullptr)
    {
        UNEXPECTED("Unable to map PBR primitive attribs buffer");
        return;
    }

    const float4x4                NodeTransform = RadientMath::ToFloat4x4(WorldMatrix);
    PBRPrimitiveShaderAttribsData AttribsData;
    AttribsData.PSOFlags       = PSOFlags;
    AttribsData.NodeMatrix     = &NodeTransform;
    AttribsData.PrevNodeMatrix = &NodeTransform;

    void* pEndPtr = WritePBRPrimitiveShaderAttribs(pAttribsData,
                                                   AttribsData,
                                                   !Renderer.GetSettings().PackMatrixRowMajor);

    VERIFY(reinterpret_cast<uint8_t*>(pEndPtr) <= static_cast<uint8_t*>(pAttribsData) + Renderer.GetPBRPrimitiveAttribsCB()->GetDesc().Size,
           "Not enough space in the buffer to store primitive attributes");

    pContext->UnmapBuffer(Renderer.GetPBRPrimitiveAttribsCB(), MAP_WRITE);
}

void WriteMaterialAttribs(PBR_Renderer&           Renderer,
                          IDeviceContext*         pContext,
                          PBR_Renderer::PSO_FLAGS PSOFlags,
                          const GLTF::Material&   Material)
{
    void* pAttribsData = nullptr;
    pContext->MapBuffer(Renderer.GetPBRMaterialAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD, pAttribsData);
    if (pAttribsData == nullptr)
    {
        UNEXPECTED("Unable to map PBR material attribs buffer");
        return;
    }

    void* pEndPtr = WritePBRMaterialShaderAttribs(pAttribsData,
                                                  Renderer.GetSettings(),
                                                  PSOFlags,
                                                  Material);

    VERIFY(reinterpret_cast<uint8_t*>(pEndPtr) <= static_cast<uint8_t*>(pAttribsData) + Renderer.GetPBRMaterialAttribsCB()->GetDesc().Size,
           "Not enough space in the buffer to store material attributes");

    pContext->UnmapBuffer(Renderer.GetPBRMaterialAttribsCB(), MAP_WRITE);
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
    if (m_pRenderer == nullptr ||
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
                                            const RadientLightList&          LightList,
                                            RadientRenderResourceCache&      ResourceCache,
                                            const RadientRenderAttribs&      Attribs,
                                            const RadientFrameRenderTargets& Targets)
{
    if (pDevice == nullptr || pContext == nullptr || DrawList.IsEmpty())
        return RADIENT_STATUS_OK;

    if (m_pRenderer == nullptr)
    {
        const RADIENT_STATUS PrepareStatus = Prepare(pDevice, pContext, Targets);
        if (RADIENT_FAILED(PrepareStatus))
            return PrepareStatus;
    }
    if (m_pRenderer == nullptr || m_pFrameAttribsCB == nullptr)
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
        WriteSceneLights(*m_pRenderer, LightList, *pFrameAttribs);
    }

    GLTF::ResourceManager* pResourceManager = ResourceCache.GetResourceManager();
    if (pResourceManager == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    struct ReadyDrawItem
    {
        const RadientDrawItem* pItem = nullptr;
        RadientAssetReference  Model;
        Uint32                 MeshIndex = ~0u;
    };

    std::vector<ReadyDrawItem> ReadyItems;
    ReadyItems.reserve(DrawList.GetItemCount());
    for (const RadientDrawItem& Item : DrawList.GetItems())
    {
        RadientAssetReference Model{};
        Uint32                MeshIndex    = ~0u;
        const RADIENT_STATUS  SourceStatus = ResourceCache.GetMeshGLTFSource(Item.Mesh.Mesh, Model, MeshIndex);
        if (SourceStatus == RADIENT_STATUS_INVALID_ARGUMENT)
            continue; // Native Radient mesh upload is not implemented yet.
        if (RADIENT_FAILED(SourceStatus))
            return SourceStatus;

        const RADIENT_STATUS LoadStatus = ResourceCache.EnsureGLTFLoaded(Model, pDevice, pContext);
        if (RADIENT_FAILED(LoadStatus))
            return LoadStatus;
        if (LoadStatus == RADIENT_STATUS_OK)
            ReadyItems.push_back({&Item, Model, MeshIndex});
    }

    if (ReadyItems.empty())
        return RADIENT_STATUS_OK;

    m_CacheUseInfo.pResourceMgr = pResourceManager;
    BeginResourceCache(*m_pRenderer, pDevice, pContext, m_CacheUseInfo, m_CacheBindings, m_pFrameAttribsCB);
    if (!m_CacheBindings.pSRB)
        return RADIENT_STATUS_OUT_OF_DATE;

    const std::array<GLTF::Material::ALPHA_MODE, 3> AlphaModes =
        {
            GLTF::Material::ALPHA_MODE_OPAQUE,
            GLTF::Material::ALPHA_MODE_MASK,
            GLTF::Material::ALPHA_MODE_BLEND,
        };

    IShaderResourceBinding* pCurrSRB = nullptr;

    for (const ReadyDrawItem& ReadyItem : ReadyItems)
    {
        VERIFY_EXPR(ReadyItem.pItem != nullptr);
        const RadientDrawItem& Item = *ReadyItem.pItem;

        const GLTF::Model* pModel = ResourceCache.GetGLTFModel(ReadyItem.Model);
        if (pModel == nullptr)
            continue;

        if (ReadyItem.MeshIndex >= pModel->Meshes.size())
            continue;

        const GLTF::Mesh& Mesh = pModel->Meshes[ReadyItem.MeshIndex];
        if (Mesh.Primitives.empty())
            continue;

        const PBR_Renderer::PSO_FLAGS VertexAttribFlags  = GetVertexAttribFlags(*pModel);
        const Uint32                  FirstIndexLocation = pModel->GetFirstIndexLocation();
        const Uint32                  BaseVertex         = pModel->GetBaseVertex();

        PBR_Renderer::PSOKey  CurrPsoKey;
        IPipelineState*       pCurrPSO      = nullptr;
        const GLTF::Material* pCurrMaterial = nullptr;

        for (GLTF::Material::ALPHA_MODE AlphaMode : AlphaModes)
        {
            for (const GLTF::Primitive& Primitive : Mesh.Primitives)
            {
                if (Primitive.VertexCount == 0 && Primitive.IndexCount == 0)
                    continue;

                if (Primitive.MaterialId >= pModel->Materials.size())
                    continue;

                const GLTF::Material& Material = pModel->Materials[Primitive.MaterialId];
                if (Material.Attribs.AlphaMode != AlphaMode)
                    continue;

                PBR_Renderer::PSO_FLAGS PSOFlags = VertexAttribFlags | GetMaterialPSOFlags(*m_pRenderer, Material);
                PSOFlags |=
                    PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS |
                    PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
                    PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB |
                    PBR_Renderer::PSO_FLAG_USE_LIGHTS;
                PSOFlags &= m_RenderFlags;

                const PBR_Renderer::PSOKey NewKey{
                    PBR_Renderer::RenderPassType::Main,
                    PSOFlags,
                    ToPBRAlphaMode(AlphaMode),
                    Material.DoubleSided ? CULL_MODE_NONE : CULL_MODE_BACK,
                    PBR_Renderer::DebugViewType::None,
                };

                if (NewKey != CurrPsoKey)
                {
                    CurrPsoKey    = NewKey;
                    pCurrPSO      = nullptr;
                    pCurrMaterial = nullptr;
                }

                if (pCurrPSO == nullptr)
                {
                    pCurrPSO = m_PbrPSOCache.Get(NewKey, PBR_Renderer::PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL);
                    VERIFY_EXPR(pCurrPSO != nullptr);
                    if (pCurrPSO != nullptr)
                        pContext->SetPipelineState(pCurrPSO);
                }

                if (pCurrSRB != m_CacheBindings.pSRB)
                {
                    pCurrSRB = m_CacheBindings.pSRB;
                    pContext->CommitShaderResources(pCurrSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
                }

                WritePrimitiveAttribs(*m_pRenderer, pContext, PSOFlags, Item.WorldMatrix);

                if (pCurrMaterial != &Material)
                {
                    WriteMaterialAttribs(*m_pRenderer, pContext, PSOFlags, Material);
                    pCurrMaterial = &Material;
                }

                if (Primitive.HasIndices())
                {
                    DrawIndexedAttribs DrawAttrs{Primitive.IndexCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
                    DrawAttrs.FirstIndexLocation = FirstIndexLocation + Primitive.FirstIndex;
                    DrawAttrs.BaseVertex         = BaseVertex + Primitive.FirstVertex;
                    pContext->DrawIndexed(DrawAttrs);
                }
                else
                {
                    DrawAttribs DrawAttrs{Primitive.VertexCount, DRAW_FLAG_VERIFY_ALL};
                    DrawAttrs.StartVertexLocation = BaseVertex + Primitive.FirstVertex;
                    pContext->Draw(DrawAttrs);
                }
            }
        }
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

    PBR_Renderer::CreateInfo RendererCI;
    RendererCI.EnableIBL               = false;
    RendererCI.EnableAO                = true;
    RendererCI.EnableEmissive          = true;
    RendererCI.EnableShadows           = false;
    RendererCI.MaxLightCount           = RadientMaxLightCount;
    RendererCI.MaxJointCount           = 0;
    RendererCI.PackMatrixRowMajor      = true;
    RendererCI.ShaderTexturesArrayMode = PBR_Renderer::SHADER_TEXTURE_ARRAY_MODE_NONE;
    InputLayoutDescX InputLayout       = GLTF::VertexAttributesToInputLayout(GLTF::DefaultVertexAttributes.data(), GLTF::DefaultVertexAttributes.size());
    RendererCI.InputLayout             = InputLayout;
    RendererCI.TexColorConversionMode  = pDevice->GetDeviceInfo().Features.TextureSubresourceViews ?
         PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE :
         PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR;
    SetGLTFTextureAttribIndices(RendererCI);

    m_pRenderer = std::make_unique<PBR_Renderer>(pDevice, nullptr, pContext, RendererCI);

    GraphicsPipelineDesc GraphicsDesc;
    GraphicsDesc.NumRenderTargets = 1;
    GraphicsDesc.RTVFormats[0]    = RTVFormat;
    GraphicsDesc.DSVFormat        = DSVFormat;

    GraphicsDesc.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsDesc.RasterizerDesc.FrontCounterClockwise = true;

    m_PbrPSOCache = m_pRenderer->GetPsoCacheAccessor(GraphicsDesc);

    GraphicsDesc.RasterizerDesc.FillMode = FILL_MODE_WIREFRAME;
    m_WireframePSOCache                  = m_pRenderer->GetPsoCacheAccessor(GraphicsDesc);

    m_pFrameAttribsCB.Release();
    CreateUniformBuffer(pDevice,
                        m_pRenderer->GetPRBFrameAttribsSize(),
                        "Radient PBR frame attribs buffer",
                        &m_pFrameAttribsCB);
    if (m_pFrameAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    InitializeResourceCacheUseInfo();

    m_RenderFlags =
        PBR_Renderer::PSO_FLAG_DEFAULT |
        PBR_Renderer::PSO_FLAG_ALL_TEXTURES |
        PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
        PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS;
    m_RenderFlags &= ~PBR_Renderer::PSO_FLAG_USE_IBL;
    m_RenderFlags &= ~PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING;
    m_RenderFlags &= ~PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS;
    if (RequiresOutputSRGBConversion(RTVFormat))
        m_RenderFlags |= PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB;

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

    m_CacheUseInfo.AtlasFormats.fill(TEX_FORMAT_RGBA8_TYPELESS);
}

} // namespace Diligent
