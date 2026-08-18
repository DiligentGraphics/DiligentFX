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

#include "DebugUtilities.hpp"
#include "RefCntAutoPtr.hpp"

#include <array>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace Diligent
{

namespace
{

bool HasStandardMaterialFeature(RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS Features,
                                RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS Feature) noexcept
{
    return (static_cast<Uint32>(Features) & static_cast<Uint32>(Feature)) != 0;
}

bool HasStandardMaterialTexture(RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS Textures,
                                RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS Texture) noexcept
{
    return (static_cast<Uint32>(Textures) & static_cast<Uint32>(Texture)) != 0;
}

RADIENT_STATUS ValidateStandardMaterialDefinitionCreateInfo(const RadientStandardMaterialDefinitionCreateInfo& CreateInfo)
{
    const Uint32 Features = static_cast<Uint32>(CreateInfo.Features);
    const Uint32 Textures = static_cast<Uint32>(CreateInfo.Textures);
    if (CreateInfo.Model >= RADIENT_STANDARD_MATERIAL_MODEL_COUNT)
    {
        LOG_ERROR_MESSAGE("Invalid standard material model ", Uint32{CreateInfo.Model});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const Uint32 UnsupportedFeatures = Features & ~static_cast<Uint32>(RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS_ALL);
    if (UnsupportedFeatures != 0)
    {
        LOG_ERROR_MESSAGE("Standard material feature flags contain unsupported bits ", UnsupportedFeatures);
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const Uint32 UnsupportedTextures = Textures & ~static_cast<Uint32>(RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS_ALL);
    if (UnsupportedTextures != 0)
    {
        LOG_ERROR_MESSAGE("Standard material texture flags contain unsupported bits ", UnsupportedTextures);
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (CreateInfo.Model == RADIENT_STANDARD_MATERIAL_MODEL_UNLIT)
    {
        if (Features != RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_NONE)
        {
            LOG_ERROR_MESSAGE("Unlit standard materials do not support optional material features");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const Uint32 UnsupportedUnlitTextures = Textures & ~static_cast<Uint32>(RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_BASE_COLOR);
        if (UnsupportedUnlitTextures != 0)
        {
            LOG_ERROR_MESSAGE("Unlit standard materials only support the base-color texture semantic");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        return RADIENT_STATUS_OK;
    }

    struct FeatureTextureSet
    {
        RADIENT_STANDARD_MATERIAL_FEATURE_FLAGS Feature;
        Uint32                                  Textures;
        const Char*                             Description;
    };

    static constexpr std::array<FeatureTextureSet, 6> FeatureTextures{{
        {RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_CLEAR_COAT,
         RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_ALL,
         "Clear-coat texture semantics require the clear-coat material feature"},
        {RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_SHEEN,
         RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_SHEEN_ALL,
         "Sheen texture semantics require the sheen material feature"},
        {RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_ANISOTROPY,
         RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_ANISOTROPY,
         "The anisotropy texture semantic requires the anisotropy material feature"},
        {RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_IRIDESCENCE,
         RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_IRIDESCENCE_ALL,
         "Iridescence texture semantics require the iridescence material feature"},
        {RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_TRANSMISSION,
         RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_TRANSMISSION,
         "The transmission texture semantic requires the transmission material feature"},
        {RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_VOLUME,
         RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_THICKNESS,
         "The thickness texture semantic requires the volume material feature"},
    }};

    for (const FeatureTextureSet& Set : FeatureTextures)
    {
        if ((Textures & Set.Textures) != 0 &&
            !HasStandardMaterialFeature(CreateInfo.Features, Set.Feature))
        {
            LOG_ERROR_MESSAGE(Set.Description);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_VOLUME) &&
        !HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_TRANSMISSION))
    {
        LOG_ERROR_MESSAGE("The volume material feature requires the transmission material feature");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

std::string GetStandardMaterialDefinitionKey(const RadientStandardMaterialDefinitionCreateInfo& CreateInfo)
{
    return std::string{"standard-material:"} +
        std::to_string(static_cast<Uint32>(CreateInfo.Model)) + ':' +
        std::to_string(static_cast<Uint32>(CreateInfo.Features)) + ':' +
        std::to_string(static_cast<Uint32>(CreateInfo.Textures));
}

void AddStandardMaterialValueParameter(std::vector<RadientMaterialParameterDesc>& Parameters,
                                       const Char*                                Name,
                                       RADIENT_MATERIAL_PARAMETER_TYPE            Type,
                                       const void*                                pDefaultValue)
{
    RadientMaterialParameterDesc& Desc = Parameters.emplace_back();
    Desc.Name                          = Name;
    Desc.Type                          = Type;
    Desc.pDefaultValue                 = pDefaultValue;
}

void AddStandardMaterialTextureParameters(std::vector<RadientMaterialParameterDesc>& Parameters,
                                          RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS    Textures,
                                          RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS    Texture,
                                          const Char*                                TextureName,
                                          const Char*                                UVSelectorName,
                                          const Char*                                UVScaleAndRotationName,
                                          const Char*                                UVBiasName,
                                          const Char*                                WrapUName,
                                          const Char*                                WrapVName)
{
    if (!HasStandardMaterialTexture(Textures, Texture))
        return;

    RadientMaterialParameterDesc& Desc = Parameters.emplace_back();
    Desc.Name                          = TextureName;
    Desc.Type                          = RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;

    static constexpr Int32         DefaultUVSelector           = 0;
    static constexpr Float32       DefaultUVScaleAndRotation[] = {1.f, 0.f, 0.f, 1.f};
    static constexpr RadientFloat2 DefaultUVBias{0.f, 0.f};
    static constexpr Uint32        DefaultWrapMode = TEXTURE_ADDRESS_WRAP;

    AddStandardMaterialValueParameter(Parameters, UVSelectorName, RADIENT_MATERIAL_PARAMETER_TYPE_INT, &DefaultUVSelector);
    AddStandardMaterialValueParameter(Parameters, UVScaleAndRotationName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2, &DefaultUVScaleAndRotation);
    AddStandardMaterialValueParameter(Parameters, UVBiasName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2, &DefaultUVBias);
    AddStandardMaterialValueParameter(Parameters, WrapUName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT, &DefaultWrapMode);
    AddStandardMaterialValueParameter(Parameters, WrapVName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT, &DefaultWrapMode);
}

std::vector<RadientMaterialParameterDesc> BuildStandardMaterialParameters(const RadientStandardMaterialDefinitionCreateInfo& CreateInfo)
{
    static constexpr RadientFloat4 DefaultBaseColor{1.f, 1.f, 1.f, 1.f};
    static constexpr RadientFloat3 DefaultEmissive{0.f, 0.f, 0.f};
    static constexpr RadientFloat3 DefaultSheenColor{0.f, 0.f, 0.f};
    static constexpr RadientFloat3 DefaultAttenuationColor{1.f, 1.f, 1.f};
    static constexpr Float32       DefaultZero                = 0.f;
    static constexpr Float32       DefaultOne                 = 1.f;
    static constexpr Float32       DefaultAlphaCutoff         = 0.5f;
    static constexpr Float32       DefaultIridescenceIOR      = 1.3f;
    static constexpr Float32       DefaultIOR                 = 1.5f;
    static constexpr Float32       DefaultThicknessMinimum    = 100.f;
    static constexpr Float32       DefaultThicknessMaximum    = 400.f;
    static constexpr Float32       DefaultAttenuationDistance = std::numeric_limits<Float32>::max();
    static constexpr Uint32        DefaultAlphaMode           = RADIENT_STANDARD_MATERIAL_ALPHA_MODE_OPAQUE;
    static constexpr Bool          DefaultDoubleSided         = False;

    std::vector<RadientMaterialParameterDesc> Parameters;
    Parameters.reserve(32 + 6 * 15);

    AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialBaseColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, &DefaultBaseColor);

    if (CreateInfo.Model == RADIENT_STANDARD_MATERIAL_MODEL_METALLIC_ROUGHNESS)
    {
        AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialMetallicFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialEmissiveFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultEmissive);

        if (HasStandardMaterialTexture(CreateInfo.Textures, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_NORMAL))
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialNormalScaleName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        if (HasStandardMaterialTexture(CreateInfo.Textures, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_OCCLUSION))
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialOcclusionStrengthName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);

        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_CLEAR_COAT))
        {
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialClearCoatFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialClearCoatRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            if (HasStandardMaterialTexture(CreateInfo.Textures, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_NORMAL))
                AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialClearCoatNormalScaleName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_SHEEN))
        {
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialSheenColorFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultSheenColor);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialSheenRoughnessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_ANISOTROPY))
        {
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAnisotropyStrengthName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAnisotropyRotationName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_IRIDESCENCE))
        {
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceIORName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultIridescenceIOR);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceThicknessMinimumName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultThicknessMinimum);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIridescenceThicknessMaximumName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultThicknessMaximum);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_TRANSMISSION))
        {
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialTransmissionFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialIORName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultIOR);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_VOLUME))
        {
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialThicknessFactorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAttenuationColorName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultAttenuationColor);
            AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAttenuationDistanceName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultAttenuationDistance);
        }
    }

    AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAlphaModeName, RADIENT_MATERIAL_PARAMETER_TYPE_UINT, &DefaultAlphaMode);
    AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialAlphaCutoffName, RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultAlphaCutoff);
    AddStandardMaterialValueParameter(Parameters, RadientStandardMaterialDoubleSidedName, RADIENT_MATERIAL_PARAMETER_TYPE_BOOL, &DefaultDoubleSided);

    const RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS Textures = CreateInfo.Textures;

#define ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Name, TextureFlag)                                    \
    AddStandardMaterialTextureParameters(Parameters, Textures, TextureFlag,                            \
                                         RadientStandardMaterial##Name##TextureName,                   \
                                         RadientStandardMaterial##Name##TextureUVSelectorName,         \
                                         RadientStandardMaterial##Name##TextureUVScaleAndRotationName, \
                                         RadientStandardMaterial##Name##TextureUVBiasName,             \
                                         RadientStandardMaterial##Name##TextureWrapUName,              \
                                         RadientStandardMaterial##Name##TextureWrapVName)

    ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(BaseColor, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_BASE_COLOR);
    if (CreateInfo.Model == RADIENT_STANDARD_MATERIAL_MODEL_METALLIC_ROUGHNESS)
    {
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(MetallicRoughness, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_METALLIC_ROUGHNESS);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Normal, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_NORMAL);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Occlusion, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_OCCLUSION);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Emissive, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_EMISSIVE);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(ClearCoat, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(ClearCoatRoughness, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_ROUGHNESS);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(ClearCoatNormal, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_NORMAL);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(SheenColor, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_SHEEN_COLOR);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(SheenRoughness, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_SHEEN_ROUGHNESS);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Anisotropy, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_ANISOTROPY);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Iridescence, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_IRIDESCENCE);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(IridescenceThickness, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_IRIDESCENCE_THICKNESS);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Transmission, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_TRANSMISSION);
        ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Thickness, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_THICKNESS);
    }

#undef ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS

    return Parameters;
}

} // namespace

RADIENT_STATUS RadientMaterialAssetManager::CreateStandardMaterialDefinition(const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
                                                                             IRadientMaterialDefinition**                       ppDefinition)
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

        RefCntAutoPtr<IRadientMaterialDefinition> pDefinition =
            m_StandardMaterialDefinitions.GetOrCreate(
                                             CacheKey.c_str(),
                                             [&]() -> RefCntAutoPtr<IRadientMaterialDefinition> {
                                                 const std::vector<RadientMaterialParameterDesc> Parameters =
                                                     BuildStandardMaterialParameters(DefinitionCI);
                                                 const std::string DefinitionName = "Radient " + CacheKey;

                                                 RadientMaterialDefinitionDesc DefinitionDesc{};
                                                 DefinitionDesc.Name           = DefinitionName.c_str();
                                                 DefinitionDesc.Domain         = RADIENT_MATERIAL_DOMAIN_SURFACE;
                                                 DefinitionDesc.Reference      = {CacheKey.c_str(), RadientStandardMaterialSchemaVersion};
                                                 DefinitionDesc.pParameters    = Parameters.data();
                                                 DefinitionDesc.ParameterCount = static_cast<Uint32>(Parameters.size());

                                                 RefCntAutoPtr<IRadientMaterialDefinition> pNewDefinition;
                                                 DefinitionStatus = CreateDefinition(DefinitionDesc, pNewDefinition.GetAddressOfEmpty());
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
