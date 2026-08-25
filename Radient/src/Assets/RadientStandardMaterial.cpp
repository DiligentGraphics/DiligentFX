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

#include "RadientStandardMaterialParameters.h"

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMaterialDefinitionImpl.hpp"

#include "DebugUtilities.hpp"
#include "GLTFLoader.hpp"
#include "PBR_Renderer.hpp"
#include "RefCntAutoPtr.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace Diligent
{

namespace
{

bool HasStandardMaterialFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Features,
                                RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Feature) noexcept
{
    return (Features & Feature) != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE;
}

RADIENT_STATUS ValidateStandardMaterialDefinitionCreateInfo(const RadientStandardMaterialDefinitionCreateInfo& CreateInfo)
{
    if (CreateInfo.ShadingModel >= RADIENT_SURFACE_SHADING_MODEL_COUNT)
    {
        LOG_ERROR_MESSAGE("Invalid surface shading model ", Uint32{CreateInfo.ShadingModel});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS UnsupportedFeatures =
        CreateInfo.Features & ~RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL;
    if (UnsupportedFeatures != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE)
    {
        LOG_ERROR_MESSAGE("Standard material feature flags contain unsupported bits ", Uint32{UnsupportedFeatures});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_UNLIT &&
        CreateInfo.Features != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE)
    {
        LOG_ERROR_MESSAGE("Unlit standard materials do not support optional material features");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS &&
        CreateInfo.Features != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE)
    {
        LOG_ERROR_MESSAGE("Specular-glossiness standard materials do not support optional material features");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME) &&
        !HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION))
    {
        LOG_ERROR_MESSAGE("The volume material feature requires the transmission material feature");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

std::string GetStandardMaterialDefinitionKey(const RadientStandardMaterialDefinitionCreateInfo& CreateInfo)
{
    return std::string{"standard-material:"} +
        std::to_string(static_cast<Uint32>(CreateInfo.ShadingModel)) + ':' +
        std::to_string(static_cast<Uint32>(CreateInfo.Features));
}

static constexpr Uint32 InvalidParameterIndex = ~Uint32{0};

struct StandardMaterialTextureParameterIndices
{
    Uint32 Texture            = InvalidParameterIndex;
    Uint32 UVSelector         = InvalidParameterIndex;
    Uint32 UVScaleAndRotation = InvalidParameterIndex;
    Uint32 UVBias             = InvalidParameterIndex;
    Uint32 WrapU              = InvalidParameterIndex;
    Uint32 WrapV              = InvalidParameterIndex;
};

struct StandardMaterialParameters
{
    struct ShaderParameterIndices
    {
        Uint32 BaseColorFactor             = InvalidParameterIndex;
        Uint32 DiffuseFactor               = InvalidParameterIndex;
        Uint32 MetallicFactor              = InvalidParameterIndex;
        Uint32 RoughnessFactor             = InvalidParameterIndex;
        Uint32 SpecularFactor              = InvalidParameterIndex;
        Uint32 GlossinessFactor            = InvalidParameterIndex;
        Uint32 EmissiveFactor              = InvalidParameterIndex;
        Uint32 NormalScale                 = InvalidParameterIndex;
        Uint32 OcclusionStrength           = InvalidParameterIndex;
        Uint32 ClearCoatFactor             = InvalidParameterIndex;
        Uint32 ClearCoatRoughnessFactor    = InvalidParameterIndex;
        Uint32 ClearCoatNormalScale        = InvalidParameterIndex;
        Uint32 SheenColorFactor            = InvalidParameterIndex;
        Uint32 SheenRoughnessFactor        = InvalidParameterIndex;
        Uint32 SpecularWeight              = InvalidParameterIndex;
        Uint32 SpecularColorFactor         = InvalidParameterIndex;
        Uint32 AnisotropyStrength          = InvalidParameterIndex;
        Uint32 AnisotropyRotation          = InvalidParameterIndex;
        Uint32 IridescenceFactor           = InvalidParameterIndex;
        Uint32 IridescenceIOR              = InvalidParameterIndex;
        Uint32 IridescenceThicknessMinimum = InvalidParameterIndex;
        Uint32 IridescenceThicknessMaximum = InvalidParameterIndex;
        Uint32 TransmissionFactor          = InvalidParameterIndex;
        Uint32 IOR                         = InvalidParameterIndex;
        Uint32 ThicknessFactor             = InvalidParameterIndex;
        Uint32 AttenuationColor            = InvalidParameterIndex;
        Uint32 AttenuationDistance         = InvalidParameterIndex;
    };

    std::vector<RadientMaterialParameterDesc>                                                  Parameters;
    ShaderParameterIndices                                                                     ShaderIndices;
    std::array<StandardMaterialTextureParameterIndices, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> TextureIndices{};
};

Uint32 AddStandardMaterialValueParameter(std::vector<RadientMaterialParameterDesc>& Parameters,
                                         const Char*                                Name,
                                         RADIENT_MATERIAL_PARAMETER_TYPE            Type,
                                         const void*                                pDefaultValue)
{
    const Uint32                  Index = static_cast<Uint32>(Parameters.size());
    RadientMaterialParameterDesc& Desc  = Parameters.emplace_back();
    Desc.Name                           = Name;
    Desc.Type                           = Type;
    Desc.pDefaultValue                  = pDefaultValue;
    return Index;
}

StandardMaterialTextureParameterIndices AddStandardMaterialTextureParameters(
    std::vector<RadientMaterialParameterDesc>& Parameters,
    IRadientTextureAsset*                      pDefaultTexture,
    const Char*                                TextureName,
    const Char*                                UVSelectorName,
    const Char*                                UVScaleAndRotationName,
    const Char*                                UVBiasName,
    const Char*                                WrapUName,
    const Char*                                WrapVName)
{
    StandardMaterialTextureParameterIndices Indices;
    Indices.Texture                    = static_cast<Uint32>(Parameters.size());
    RadientMaterialParameterDesc& Desc = Parameters.emplace_back();
    Desc.Name                          = TextureName;
    Desc.Type                          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
    Desc.pDefaultTexture               = pDefaultTexture;

    static constexpr Int32                                 DefaultUVSelector           = -1;
    static constexpr Float32                               DefaultUVScaleAndRotation[] = {1.f, 0.f, 0.f, 1.f};
    static constexpr RadientFloat2                         DefaultUVBias{0.f, 0.f};
    static constexpr RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE DefaultWrapMode = RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP;

    Indices.UVSelector = AddStandardMaterialValueParameter(
        Parameters, UVSelectorName, RADIENT_MATERIAL_PARAMETER_TYPE_INT, &DefaultUVSelector);
    Indices.UVScaleAndRotation = AddStandardMaterialValueParameter(
        Parameters, UVScaleAndRotationName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2, &DefaultUVScaleAndRotation);
    Indices.UVBias = AddStandardMaterialValueParameter(
        Parameters, UVBiasName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2, &DefaultUVBias);
    Indices.WrapU = AddStandardMaterialValueParameter(
        Parameters, WrapUName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT, &DefaultWrapMode);
    Indices.WrapV = AddStandardMaterialValueParameter(
        Parameters, WrapVName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT, &DefaultWrapMode);
    return Indices;
}

StandardMaterialParameters BuildStandardMaterialParameters(const RadientStandardMaterialDefinitionCreateInfo& CreateInfo,
                                                           const RadientMaterialDefaultTextures&              DefaultTextures)
{
    static constexpr RadientFloat4 DefaultBaseColor{1.f, 1.f, 1.f, 1.f};
    static constexpr RadientFloat4 DefaultDiffuse{1.f, 1.f, 1.f, 1.f};
    static constexpr RadientFloat3 DefaultSpecular{1.f, 1.f, 1.f};
    static constexpr RadientFloat3 DefaultEmissive{0.f, 0.f, 0.f};
    static constexpr RadientFloat3 DefaultSheenColor{0.f, 0.f, 0.f};
    static constexpr RadientFloat3 DefaultAttenuationColor{1.f, 1.f, 1.f};
    static constexpr Float32       DefaultZero                = 0.f;
    static constexpr Float32       DefaultOne                 = 1.f;
    static constexpr Float32       DefaultIridescenceIOR      = 1.3f;
    static constexpr Float32       DefaultIOR                 = 1.5f;
    static constexpr Float32       DefaultThicknessMinimum    = 100.f;
    static constexpr Float32       DefaultThicknessMaximum    = 400.f;
    static constexpr Float32       DefaultAttenuationDistance = std::numeric_limits<Float32>::max();

    StandardMaterialParameters                 Result;
    std::vector<RadientMaterialParameterDesc>& Parameters = Result.Parameters;
    Parameters.reserve(32 + 6 * PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT);

    switch (CreateInfo.ShadingModel)
    {
        case RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS:
            Result.ShaderIndices.BaseColorFactor =
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialBaseColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, &DefaultBaseColor);
            Result.ShaderIndices.MetallicFactor =
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialMetallicFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
            Result.ShaderIndices.RoughnessFactor =
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
            break;

        case RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS:
            Result.ShaderIndices.DiffuseFactor =
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialDiffuseFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, &DefaultDiffuse);
            Result.ShaderIndices.SpecularFactor =
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialSpecularFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultSpecular);
            Result.ShaderIndices.GlossinessFactor =
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialGlossinessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
            break;

        case RADIENT_SURFACE_SHADING_MODEL_UNLIT:
            Result.ShaderIndices.BaseColorFactor =
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialBaseColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, &DefaultBaseColor);
            break;

        default:
            UNEXPECTED("Unexpected standard material shading model");
            break;
    }

    if (CreateInfo.ShadingModel != RADIENT_SURFACE_SHADING_MODEL_UNLIT)
    {
        Result.ShaderIndices.EmissiveFactor =
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialEmissiveFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultEmissive);

        Result.ShaderIndices.NormalScale       = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialNormalScaleName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        Result.ShaderIndices.OcclusionStrength = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialOcclusionStrengthName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
    }

    if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS)
    {
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT))
        {
            Result.ShaderIndices.ClearCoatFactor          = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialClearCoatFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            Result.ShaderIndices.ClearCoatRoughnessFactor = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialClearCoatRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            Result.ShaderIndices.ClearCoatNormalScale     = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialClearCoatNormalScaleName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN))
        {
            Result.ShaderIndices.SheenColorFactor     = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialSheenColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultSheenColor);
            Result.ShaderIndices.SheenRoughnessFactor = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialSheenRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SPECULAR))
        {
            Result.ShaderIndices.SpecularWeight      = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialSpecularWeightName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
            Result.ShaderIndices.SpecularColorFactor = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialSpecularColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultSpecular);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY))
        {
            Result.ShaderIndices.AnisotropyStrength = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAnisotropyStrengthName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            Result.ShaderIndices.AnisotropyRotation = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAnisotropyRotationName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE))
        {
            Result.ShaderIndices.IridescenceFactor           = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            Result.ShaderIndices.IridescenceIOR              = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceIORName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultIridescenceIOR);
            Result.ShaderIndices.IridescenceThicknessMinimum = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceThicknessMinimumName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultThicknessMinimum);
            Result.ShaderIndices.IridescenceThicknessMaximum = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceThicknessMaximumName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultThicknessMaximum);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION))
        {
            Result.ShaderIndices.TransmissionFactor = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialTransmissionFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            Result.ShaderIndices.IOR                = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIORName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultIOR);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME))
        {
            Result.ShaderIndices.ThicknessFactor     = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialThicknessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            Result.ShaderIndices.AttenuationColor    = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAttenuationColorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultAttenuationColor);
            Result.ShaderIndices.AttenuationDistance = AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAttenuationDistanceName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultAttenuationDistance);
        }
    }

#define ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(PBRName, Name, DefaultTexture)                            \
    Result.TextureIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_##PBRName] =                                     \
        AddStandardMaterialTextureParameters(Parameters,                                                   \
                                             DefaultTextures.DefaultTexture,                               \
                                             RadientStandardMaterial##Name##TextureName,                   \
                                             RadientStandardMaterial##Name##TextureUVSelectorName,         \
                                             RadientStandardMaterial##Name##TextureUVScaleAndRotationName, \
                                             RadientStandardMaterial##Name##TextureUVBiasName,             \
                                             RadientStandardMaterial##Name##TextureWrapUName,              \
                                             RadientStandardMaterial##Name##TextureWrapVName)

    switch (CreateInfo.ShadingModel)
    {
        case RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS:
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(BASE_COLOR, BaseColor, pWhite);
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(PHYS_DESC, MetallicRoughness, pPhysicalDesc);
            break;

        case RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS:
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(BASE_COLOR, Diffuse, pWhite);
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(PHYS_DESC, SpecularGlossiness, pWhite);
            break;

        case RADIENT_SURFACE_SHADING_MODEL_UNLIT:
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(BASE_COLOR, BaseColor, pWhite);
            break;

        default:
            UNEXPECTED("Unexpected standard material shading model");
            break;
    }

    if (CreateInfo.ShadingModel != RADIENT_SURFACE_SHADING_MODEL_UNLIT)
    {
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(NORMAL, Normal, pNormal);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(OCCLUSION, Occlusion, pWhite);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(EMISSIVE, Emissive, pBlack);
    }

    if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS)
    {
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT))
        {
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(CLEAR_COAT, ClearCoat, pWhite);
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(CLEAR_COAT_ROUGHNESS, ClearCoatRoughness, pWhite);
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(CLEAR_COAT_NORMAL, ClearCoatNormal, pNormal);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN))
        {
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(SHEEN_COLOR, SheenColor, pWhite);
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(SHEEN_ROUGHNESS, SheenRoughness, pWhite);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SPECULAR))
        {
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(SPECULAR, Specular, pWhite);
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(SPECULAR_COLOR, SpecularColor, pWhite);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY))
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(ANISOTROPY, Anisotropy, pWhite);
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE))
        {
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(IRIDESCENCE, Iridescence, pWhite);
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(IRIDESCENCE_THICKNESS, IridescenceThickness, pWhite);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION))
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(TRANSMISSION, Transmission, pWhite);
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME))
            ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(THICKNESS, Thickness, pWhite);
    }

#undef ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS

    return Result;
}

struct StandardMaterialShaderDataLayout
{
    RadientMaterialShaderDataLayoutDesc GetDesc() const noexcept
    {
        RadientMaterialShaderDataLayoutDesc Desc{};
        Desc.Size                = Size;
        Desc.pMappings           = ParameterPackings.data();
        Desc.MappingCount        = static_cast<Uint32>(ParameterPackings.size());
        Desc.pTexturePackings    = TexturePackings.data();
        Desc.TexturePackingCount = static_cast<Uint32>(TexturePackings.size());
        Desc.pInitializations    = Initializations.data();
        Desc.InitializationCount = static_cast<Uint32>(Initializations.size());
        Desc.pSurfacePacking     = &SurfacePacking;
        return Desc;
    }

    Uint32                                               Size = 0;
    RadientSurfaceMaterialShaderParameterPacking         SurfacePacking;
    std::vector<RadientMaterialShaderParameterPacking>   ParameterPackings;
    std::vector<RadientMaterialShaderTexturePacking>     TexturePackings;
    std::vector<RadientMaterialShaderDataInitialization> Initializations;
};

void AddParameterPacking(StandardMaterialShaderDataLayout& Layout,
                         Uint32                            ParameterIndex,
                         Uint32                            Offset)
{
    VERIFY_EXPR(ParameterIndex != InvalidParameterIndex);
    if (ParameterIndex != InvalidParameterIndex)
        Layout.ParameterPackings.push_back({ParameterIndex, Offset});
}

template <typename ValueType>
void AddInitialization(StandardMaterialShaderDataLayout& Layout,
                       const ValueType&                  Value,
                       Uint32                            Offset)
{
    Layout.Initializations.push_back({&Value, static_cast<Uint32>(sizeof(Value)), Offset});
}

PBR_Renderer::PSO_FLAGS GetStandardMaterialPSOFlags(const RadientStandardMaterialDefinitionCreateInfo& CreateInfo) noexcept
{
    if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_UNLIT)
        return PBR_Renderer::PSO_FLAG_USE_COLOR_MAP;

    PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES;
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_CLEAR_COAT;
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_SHEEN;
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SPECULAR))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_SPECULAR;
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_ANISOTROPY;
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_IRIDESCENCE;
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_TRANSMISSION;
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_VOLUME;
    return Flags;
}

const Int32& GetStandardMaterialWorkflow(RADIENT_SURFACE_SHADING_MODEL ShadingModel) noexcept
{
    static const Int32 MetallicRoughnessWorkflow  = GLTF::Material::PBR_WORKFLOW_METALL_ROUGH;
    static const Int32 UnlitWorkflow              = GLTF::Material::PBR_WORKFLOW_UNLIT;
    static const Int32 SpecularGlossinessWorkflow = GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS;

    switch (ShadingModel)
    {
        case RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS:
            return MetallicRoughnessWorkflow;

        case RADIENT_SURFACE_SHADING_MODEL_UNLIT:
            return UnlitWorkflow;

        case RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS:
            return SpecularGlossinessWorkflow;

        default:
            UNEXPECTED("Unexpected standard material shading model");
            return MetallicRoughnessWorkflow;
    }
}

StandardMaterialShaderDataLayout BuildStandardMaterialShaderDataLayout(
    const RadientStandardMaterialDefinitionCreateInfo& CreateInfo,
    const StandardMaterialParameters&                  MaterialParameters)
{
    using Material = GLTF::Material;

    const StandardMaterialParameters::ShaderParameterIndices& Indices = MaterialParameters.ShaderIndices;

    static const Material::ShaderAttribs        DefaultBasicAttribs{};
    static const Material::TextureShaderAttribs DefaultTextureAttribs{};

    StandardMaterialShaderDataLayout Layout;
    Layout.ParameterPackings.reserve(32 + PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT * 2);
    Layout.TexturePackings.reserve(PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT);
    Layout.Initializations.reserve(16 + PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT * 2);

    AddInitialization(Layout, DefaultBasicAttribs.NormalScale, offsetof(Material::ShaderAttribs, NormalScale));
    AddInitialization(Layout, DefaultBasicAttribs.SpecularFactor, offsetof(Material::ShaderAttribs, SpecularFactor));
    AddInitialization(Layout, DefaultBasicAttribs.ClearcoatNormalScale, offsetof(Material::ShaderAttribs, ClearcoatNormalScale));
    AddInitialization(Layout, GetStandardMaterialWorkflow(CreateInfo.ShadingModel),
                      offsetof(Material::ShaderAttribs, Workflow));
    AddInitialization(Layout, DefaultBasicAttribs.MetallicFactor, offsetof(Material::ShaderAttribs, MetallicFactor));
    AddInitialization(Layout, DefaultBasicAttribs.RoughnessFactor, offsetof(Material::ShaderAttribs, RoughnessFactor));
    AddInitialization(Layout, DefaultBasicAttribs.OcclusionFactor, offsetof(Material::ShaderAttribs, OcclusionFactor));

    if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS)
    {
        AddParameterPacking(Layout, Indices.DiffuseFactor,
                            offsetof(Material::ShaderAttribs, BaseColorFactor));
    }
    else
    {
        AddParameterPacking(Layout, Indices.BaseColorFactor,
                            offsetof(Material::ShaderAttribs, BaseColorFactor));
    }
    Layout.SurfacePacking.SurfaceModeOffset = offsetof(Material::ShaderAttribs, AlphaMode);
    Layout.SurfacePacking.AlphaCutoffOffset = offsetof(Material::ShaderAttribs, AlphaCutoff);

    if (CreateInfo.ShadingModel != RADIENT_SURFACE_SHADING_MODEL_UNLIT)
    {
        AddParameterPacking(Layout, Indices.EmissiveFactor,
                            offsetof(Material::ShaderAttribs, EmissiveFactor));
        AddParameterPacking(Layout, Indices.NormalScale,
                            offsetof(Material::ShaderAttribs, NormalScale));
        AddParameterPacking(Layout, Indices.OcclusionStrength,
                            offsetof(Material::ShaderAttribs, OcclusionFactor));
    }

    if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS)
    {
        AddParameterPacking(Layout, Indices.MetallicFactor,
                            offsetof(Material::ShaderAttribs, MetallicFactor));
        AddParameterPacking(Layout, Indices.RoughnessFactor,
                            offsetof(Material::ShaderAttribs, RoughnessFactor));
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT))
        {
            AddParameterPacking(Layout, Indices.ClearCoatFactor,
                                offsetof(Material::ShaderAttribs, ClearcoatFactor));
            AddParameterPacking(Layout, Indices.ClearCoatRoughnessFactor,
                                offsetof(Material::ShaderAttribs, ClearcoatRoughnessFactor));
            AddParameterPacking(Layout, Indices.ClearCoatNormalScale,
                                offsetof(Material::ShaderAttribs, ClearcoatNormalScale));
        }
    }
    else if (CreateInfo.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS)
    {
        AddParameterPacking(Layout, Indices.SpecularFactor,
                            offsetof(Material::ShaderAttribs, SpecularFactor));
        AddParameterPacking(Layout, Indices.GlossinessFactor,
                            offsetof(Material::ShaderAttribs, RoughnessFactor));
    }

    Uint32 Offset = sizeof(Material::ShaderAttribs);
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN))
    {
        AddParameterPacking(Layout, Indices.SheenColorFactor,
                            Offset + offsetof(Material::SheenShaderAttribs, ColorFactor));
        AddParameterPacking(Layout, Indices.SheenRoughnessFactor,
                            Offset + offsetof(Material::SheenShaderAttribs, RoughnessFactor));
        Offset += sizeof(Material::SheenShaderAttribs);
    }
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SPECULAR))
    {
        AddParameterPacking(Layout, Indices.SpecularColorFactor,
                            Offset + offsetof(Material::SpecularShaderAttribs, ColorFactor));
        AddParameterPacking(Layout, Indices.SpecularWeight,
                            Offset + offsetof(Material::SpecularShaderAttribs, Factor));
        Offset += sizeof(Material::SpecularShaderAttribs);
    }
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY))
    {
        AddParameterPacking(Layout, Indices.AnisotropyStrength,
                            Offset + offsetof(Material::AnisotropyShaderAttribs, Strength));
        AddParameterPacking(Layout, Indices.AnisotropyRotation,
                            Offset + offsetof(Material::AnisotropyShaderAttribs, Rotation));
        Offset += sizeof(Material::AnisotropyShaderAttribs);
    }
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE))
    {
        AddParameterPacking(Layout, Indices.IridescenceFactor,
                            Offset + offsetof(Material::IridescenceShaderAttribs, Factor));
        AddParameterPacking(Layout, Indices.IridescenceIOR,
                            Offset + offsetof(Material::IridescenceShaderAttribs, IOR));
        AddParameterPacking(Layout, Indices.IridescenceThicknessMinimum,
                            Offset + offsetof(Material::IridescenceShaderAttribs, ThicknessMinimum));
        AddParameterPacking(Layout, Indices.IridescenceThicknessMaximum,
                            Offset + offsetof(Material::IridescenceShaderAttribs, ThicknessMaximum));
        Offset += sizeof(Material::IridescenceShaderAttribs);
    }
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION))
    {
        AddParameterPacking(Layout, Indices.TransmissionFactor,
                            Offset + offsetof(Material::TransmissionShaderAttribs, Factor));
        AddParameterPacking(Layout, Indices.IOR,
                            Offset + offsetof(Material::TransmissionShaderAttribs, IOR));
        Offset += sizeof(Material::TransmissionShaderAttribs);
    }
    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME))
    {
        AddParameterPacking(Layout, Indices.AttenuationColor,
                            Offset + offsetof(Material::VolumeShaderAttribs, AttenuationColor));
        AddParameterPacking(Layout, Indices.ThicknessFactor,
                            Offset + offsetof(Material::VolumeShaderAttribs, ThicknessFactor));
        AddParameterPacking(Layout, Indices.AttenuationDistance,
                            Offset + offsetof(Material::VolumeShaderAttribs, AttenuationDistance));
        Offset += sizeof(Material::VolumeShaderAttribs);
    }

    PBR_Renderer::ProcessTexturAttribs(
        GetStandardMaterialPSOFlags(CreateInfo),
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            AddInitialization(Layout, DefaultTextureAttribs.UVScaleAndRotation,
                              Offset + offsetof(Material::TextureShaderAttribs, UVScaleAndRotation));
            AddInitialization(Layout, DefaultTextureAttribs.AtlasUVScaleAndBias,
                              Offset + offsetof(Material::TextureShaderAttribs, AtlasUVScaleAndBias));

            const StandardMaterialTextureParameterIndices& TextureIndices =
                MaterialParameters.TextureIndices[AttribId];
            if (TextureIndices.Texture != InvalidParameterIndex)
            {
                AddParameterPacking(Layout, TextureIndices.UVScaleAndRotation,
                                    Offset + offsetof(Material::TextureShaderAttribs, UVScaleAndRotation));
                AddParameterPacking(Layout, TextureIndices.UVBias,
                                    Offset + offsetof(Material::TextureShaderAttribs, UBias));
                Layout.TexturePackings.push_back(
                    {TextureIndices.Texture,
                     TextureIndices.UVSelector,
                     TextureIndices.WrapU,
                     TextureIndices.WrapV,
                     Offset});
            }

            Offset += sizeof(Material::TextureShaderAttribs);
        });

    Layout.Size = Offset;
    return Layout;
}

} // namespace

RADIENT_STATUS RadientMaterialAssetManager::CreateStandardMaterialDefinition(const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
                                                                             IRadientMaterialDefinitionAsset**                  ppDefinition)
{
    if (ppDefinition == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppDefinition = nullptr;

    const RADIENT_STATUS ValidationStatus = ValidateStandardMaterialDefinitionCreateInfo(DefinitionCI);
    if (ValidationStatus != RADIENT_STATUS_OK)
        return ValidationStatus;

    try
    {
        const std::string CacheKey         = GetStandardMaterialDefinitionKey(DefinitionCI);
        RADIENT_STATUS    DefinitionStatus = RADIENT_STATUS_OK;

        RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition =
            m_StandardMaterialDefinitions.GetOrCreate(
                                             CacheKey.c_str(),
                                             [&]() -> RefCntAutoPtr<IRadientMaterialDefinitionAsset> {
                                                 const StandardMaterialParameters ParameterData =
                                                     BuildStandardMaterialParameters(DefinitionCI, m_DefaultTextures);
                                                 const StandardMaterialShaderDataLayout ShaderDataLayout =
                                                     BuildStandardMaterialShaderDataLayout(DefinitionCI, ParameterData);
                                                 const std::string DefinitionName = "Radient " + CacheKey;

                                                 RadientSurfaceMaterialDefinitionDesc DefinitionDesc{};
                                                 DefinitionDesc.Name           = DefinitionName.c_str();
                                                 DefinitionDesc.Reference      = {CacheKey.c_str(), RadientStandardMaterialSchemaVersion};
                                                 DefinitionDesc.pParameters    = ParameterData.Parameters.data();
                                                 DefinitionDesc.ParameterCount = static_cast<Uint32>(ParameterData.Parameters.size());
                                                 DefinitionDesc.ShadingModel   = DefinitionCI.ShadingModel;
                                                 DefinitionDesc.Features       = DefinitionCI.Features;

                                                 RefCntAutoPtr<IRadientMaterialDefinitionAsset> pNewDefinition;
                                                 DefinitionStatus = CreateDefinition(DefinitionDesc, ShaderDataLayout.GetDesc(),
                                                                                     pNewDefinition.GetAddressOfEmpty());
                                                 return pNewDefinition;
                                             })
                .first;

        if (!pDefinition)
            return DefinitionStatus == RADIENT_STATUS_OK ? RADIENT_STATUS_FAILED : DefinitionStatus;

        *ppDefinition = pDefinition.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient standard material definition: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
