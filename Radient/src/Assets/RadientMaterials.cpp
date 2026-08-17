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

#include "RadientMaterials.h"

#include "Assets/RadientMaterialAssetManager.hpp"

#include "DebugUtilities.hpp"
#include "Errors.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <atomic>
#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

std::atomic<RadientHandle> s_NextMaterialDefinitionHandle{1};

Uint32 GetMaterialParameterElementSize(RADIENT_MATERIAL_PARAMETER_TYPE Type) noexcept
{
    switch (Type)
    {
        case RADIENT_MATERIAL_PARAMETER_TYPE_BOOL:
            return sizeof(Bool);

        case RADIENT_MATERIAL_PARAMETER_TYPE_INT:
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT:
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT:
            return sizeof(Uint32);

        case RADIENT_MATERIAL_PARAMETER_TYPE_INT2:
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT2:
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2:
            return sizeof(Uint32) * 2;

        case RADIENT_MATERIAL_PARAMETER_TYPE_INT3:
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT3:
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3:
            return sizeof(Uint32) * 3;

        case RADIENT_MATERIAL_PARAMETER_TYPE_INT4:
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT4:
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4:
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2:
            return sizeof(Uint32) * 4;

        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3:
            return sizeof(Float32) * 9;

        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4:
            return sizeof(Float32) * 16;

        default:
            return 0;
    }
}

bool IsTextureParameter(RADIENT_MATERIAL_PARAMETER_TYPE Type) noexcept
{
    return Type == RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE;
}

bool GetMaterialParameterDataSize(const RadientMaterialParameterDesc& Desc, Uint32& DataSize) noexcept
{
    DataSize = 0;
    if (Desc.ArraySize == 0)
        return false;

    if (IsTextureParameter(Desc.Type))
        return true;

    const Uint32 ElementSize = GetMaterialParameterElementSize(Desc.Type);
    if (ElementSize == 0 || Desc.ArraySize > std::numeric_limits<Uint32>::max() / ElementSize)
        return false;

    DataSize = ElementSize * Desc.ArraySize;
    return true;
}

RADIENT_STATUS ValidateMaterialDefinitionCreateInfo(const RadientMaterialDefinitionCreateInfo& CreateInfo)
{
    if (CreateInfo.Desc.Domain >= RADIENT_MATERIAL_DOMAIN_COUNT)
    {
        LOG_ERROR_MESSAGE("Invalid material definition domain ", Uint32{CreateInfo.Desc.Domain});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (CreateInfo.ParameterCount != 0 && CreateInfo.pParameters == nullptr)
    {
        LOG_ERROR_MESSAGE("Material definition declares ", CreateInfo.ParameterCount,
                          " parameters, but pParameters is null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    std::unordered_map<std::string, Uint32> Names;
    Names.reserve(CreateInfo.ParameterCount);

    for (Uint32 Index = 0; Index < CreateInfo.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Desc = CreateInfo.pParameters[Index];
        if (Desc.Name == nullptr)
        {
            LOG_ERROR_MESSAGE("Material parameter ", Index, " name must not be null");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Desc.Name[0] == '\0')
        {
            LOG_ERROR_MESSAGE("Material parameter ", Index, " name must not be empty");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Desc.Type <= RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN ||
            Desc.Type >= RADIENT_MATERIAL_PARAMETER_TYPE_COUNT)
        {
            LOG_ERROR_MESSAGE("Material parameter '", Desc.Name, "' has invalid type ", Uint32{Desc.Type});
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Desc.ArraySize == 0)
        {
            LOG_ERROR_MESSAGE("Material parameter '", Desc.Name, "' array size must not be zero");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        Uint32 DataSize;
        if (!GetMaterialParameterDataSize(Desc, DataSize))
        {
            LOG_ERROR_MESSAGE("Material parameter '", Desc.Name, "' data size exceeds the supported limit");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (IsTextureParameter(Desc.Type))
        {
            if (Desc.pDefaultValue != nullptr)
            {
                LOG_ERROR_MESSAGE("Texture material parameter '", Desc.Name,
                                  "' must use pDefaultTexture instead of pDefaultValue");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            if (Desc.ArraySize != 1 && Desc.pDefaultTexture != nullptr)
            {
                LOG_ERROR_MESSAGE("Texture array material parameter '", Desc.Name,
                                  "' must not specify pDefaultTexture");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }
        else if (Desc.pDefaultTexture != nullptr)
        {
            LOG_ERROR_MESSAGE("Non-texture material parameter '", Desc.Name,
                              "' must not specify pDefaultTexture");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const auto InsertResult = Names.emplace(Desc.Name, Index);
        if (!InsertResult.second)
        {
            LOG_ERROR_MESSAGE("Material parameter ", Index, " name '", Desc.Name,
                              "' duplicates parameter ", InsertResult.first->second);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
}

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

    AddStandardMaterialValueParameter(Parameters, "BaseColorFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4, &DefaultBaseColor);

    if (CreateInfo.Model == RADIENT_STANDARD_MATERIAL_MODEL_METALLIC_ROUGHNESS)
    {
        AddStandardMaterialValueParameter(Parameters, "MetallicFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        AddStandardMaterialValueParameter(Parameters, "RoughnessFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        AddStandardMaterialValueParameter(Parameters, "EmissiveFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultEmissive);

        if (HasStandardMaterialTexture(CreateInfo.Textures, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_NORMAL))
            AddStandardMaterialValueParameter(Parameters, "NormalScale", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        if (HasStandardMaterialTexture(CreateInfo.Textures, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_OCCLUSION))
            AddStandardMaterialValueParameter(Parameters, "OcclusionStrength", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);

        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_CLEAR_COAT))
        {
            AddStandardMaterialValueParameter(Parameters, "ClearCoatFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, "ClearCoatRoughnessFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            if (HasStandardMaterialTexture(CreateInfo.Textures, RADIENT_STANDARD_MATERIAL_TEXTURE_FLAG_CLEAR_COAT_NORMAL))
                AddStandardMaterialValueParameter(Parameters, "ClearCoatNormalScale", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultOne);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_SHEEN))
        {
            AddStandardMaterialValueParameter(Parameters, "SheenColorFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultSheenColor);
            AddStandardMaterialValueParameter(Parameters, "SheenRoughnessFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_ANISOTROPY))
        {
            AddStandardMaterialValueParameter(Parameters, "AnisotropyStrength", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, "AnisotropyRotation", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_IRIDESCENCE))
        {
            AddStandardMaterialValueParameter(Parameters, "IridescenceFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, "IridescenceIOR", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultIridescenceIOR);
            AddStandardMaterialValueParameter(Parameters, "IridescenceThicknessMinimum", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultThicknessMinimum);
            AddStandardMaterialValueParameter(Parameters, "IridescenceThicknessMaximum", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultThicknessMaximum);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_TRANSMISSION))
        {
            AddStandardMaterialValueParameter(Parameters, "TransmissionFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, "IOR", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultIOR);
        }
        if (HasStandardMaterialFeature(CreateInfo.Features, RADIENT_STANDARD_MATERIAL_FEATURE_FLAG_VOLUME))
        {
            AddStandardMaterialValueParameter(Parameters, "ThicknessFactor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultZero);
            AddStandardMaterialValueParameter(Parameters, "AttenuationColor", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3, &DefaultAttenuationColor);
            AddStandardMaterialValueParameter(Parameters, "AttenuationDistance", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultAttenuationDistance);
        }
    }

    AddStandardMaterialValueParameter(Parameters, "AlphaMode", RADIENT_MATERIAL_PARAMETER_TYPE_UINT, &DefaultAlphaMode);
    AddStandardMaterialValueParameter(Parameters, "AlphaCutoff", RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT, &DefaultAlphaCutoff);
    AddStandardMaterialValueParameter(Parameters, "DoubleSided", RADIENT_MATERIAL_PARAMETER_TYPE_BOOL, &DefaultDoubleSided);

    const RADIENT_STANDARD_MATERIAL_TEXTURE_FLAGS Textures = CreateInfo.Textures;

#define ADD_STANDARD_MATERIAL_TEXTURE_PARAMETERS(Name, TextureFlag)                                                     \
    AddStandardMaterialTextureParameters(Parameters, Textures, TextureFlag,                                             \
                                         #Name "Texture", #Name "TextureUVSelector", #Name "TextureUVScaleAndRotation", \
                                         #Name "TextureUVBias", #Name "TextureWrapU", #Name "TextureWrapV")

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

struct MaterialParameterValue
{
    std::vector<Uint8>                               Data;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures;
};

bool operator==(const MaterialParameterValue& Lhs, const MaterialParameterValue& Rhs) noexcept
{
    if (Lhs.Data != Rhs.Data || Lhs.Textures.size() != Rhs.Textures.size())
        return false;

    for (size_t Index = 0; Index < Lhs.Textures.size(); ++Index)
    {
        if (Lhs.Textures[Index] != Rhs.Textures[Index])
            return false;
    }
    return true;
}

class RadientMaterialDefinitionImpl final : public ObjectBase<IRadientMaterialDefinition>
{
public:
    using TBase = ObjectBase<IRadientMaterialDefinition>;

    RadientMaterialDefinitionImpl(IReferenceCounters*                        pRefCounters,
                                  const RadientMaterialDefinitionCreateInfo& CreateInfo) :
        TBase{pRefCounters},
        m_Name{CreateInfo.Desc.Name != nullptr ? CreateInfo.Desc.Name : ""},
        m_ReferenceURI{CreateInfo.Reference.URI != nullptr ? CreateInfo.Reference.URI : ""},
        m_HasReferenceURI{CreateInfo.Reference.URI != nullptr},
        m_DefinitionHandle{s_NextMaterialDefinitionHandle.fetch_add(1, std::memory_order_relaxed)}
    {
        m_Desc          = CreateInfo.Desc;
        m_Desc.Name     = m_Name.c_str();
        m_Reference     = CreateInfo.Reference;
        m_Reference.URI = m_HasReferenceURI ? m_ReferenceURI.c_str() : nullptr;

        m_Parameters.resize(CreateInfo.ParameterCount);
        m_ParameterNames.reserve(CreateInfo.ParameterCount);

        for (Uint32 Index = 0; Index < CreateInfo.ParameterCount; ++Index)
        {
            const RadientMaterialParameterDesc& SourceDesc = CreateInfo.pParameters[Index];
            Parameter&                          Dst        = m_Parameters[Index];

            Dst.Name = SourceDesc.Name;
            Dst.Desc = SourceDesc;

            Uint32     DataSize        = 0;
            const bool IsValidDataSize = GetMaterialParameterDataSize(SourceDesc, DataSize);
            VERIFY_EXPR(IsValidDataSize);
            (void)IsValidDataSize;
            if (DataSize != 0)
            {
                Dst.DefaultValue.resize(DataSize, 0);
                if (SourceDesc.pDefaultValue != nullptr)
                    std::memcpy(Dst.DefaultValue.data(), SourceDesc.pDefaultValue, DataSize);
            }
            Dst.pDefaultTexture = SourceDesc.pDefaultTexture;

            m_ParameterNames.emplace(Dst.Name, Index);
        }

        // Set pointers only after all movable storage has reached its final location.
        for (Parameter& Param : m_Parameters)
        {
            Param.Desc.Name            = Param.Name.c_str();
            Param.Desc.pDefaultValue   = Param.DefaultValue.empty() ? nullptr : Param.DefaultValue.data();
            Param.Desc.pDefaultTexture = Param.pDefaultTexture;
        }
    }

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientMaterialDefinition, IID_RadientAsset, TBase)

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Reference;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_MATERIAL;
    }

    virtual const RadientMaterialDefinitionDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Desc;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetStatus() const override final
    {
        return RADIENT_STATUS_OK;
    }

    virtual Uint32 DILIGENT_CALL_TYPE GetParameterCount() const override final
    {
        return static_cast<Uint32>(m_Parameters.size());
    }

    virtual const RadientMaterialParameterDesc& DILIGENT_CALL_TYPE GetParameterDesc(Uint32 Index) const override final
    {
        if (Index >= m_Parameters.size())
        {
            UNEXPECTED("Material parameter index ", Index, " is out of range");
            static constexpr RadientMaterialParameterDesc InvalidDesc{};
            return InvalidDesc;
        }
        return m_Parameters[Index].Desc;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameterHandle(Uint32                          Index,
                                                                 RadientMaterialParameterHandle* pHandle) const override final
    {
        if (pHandle == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *pHandle = {};

        if (Index >= m_Parameters.size())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        pHandle->Definition = m_DefinitionHandle;
        pHandle->Index      = Index;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE FindParameter(const Char*                     Name,
                                                            RadientMaterialParameterHandle* pHandle) const override final
    {
        if (pHandle == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *pHandle = {};

        if (Name == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const auto It = m_ParameterNames.find(Name);
        if (It == m_ParameterNames.end())
            return RADIENT_STATUS_NOT_FOUND;

        return GetParameterHandle(It->second, pHandle);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateInstance(IRadientMaterialInstance** ppInstance) const override final;

private:
    struct Parameter
    {
        std::string                         Name;
        RadientMaterialParameterDesc        Desc;
        std::vector<Uint8>                  DefaultValue;
        RefCntAutoPtr<IRadientTextureAsset> pDefaultTexture;
    };

    std::string m_Name;
    std::string m_ReferenceURI;
    bool        m_HasReferenceURI = false;

    RadientMaterialDefinitionDesc m_Desc;
    RadientAssetReference         m_Reference;
    const RadientHandle           m_DefinitionHandle;

    std::vector<Parameter>                  m_Parameters;
    std::unordered_map<std::string, Uint32> m_ParameterNames;
};

class RadientMaterialInstanceWriterImpl;

class RadientMaterialInstanceImpl final : public ObjectBase<IRadientMaterialInstance>
{
public:
    using TBase = ObjectBase<IRadientMaterialInstance>;

    RadientMaterialInstanceImpl(IReferenceCounters*                          pRefCounters,
                                IRadientMaterialDefinition*                  pDefinition,
                                std::vector<RadientMaterialParameterHandle>  Handles,
                                std::vector<RADIENT_MATERIAL_PARAMETER_TYPE> Types,
                                std::vector<MaterialParameterValue>          Values) :
        TBase{pRefCounters},
        m_pDefinition{pDefinition},
        m_Handles{std::move(Handles)},
        m_Types{std::move(Types)},
        m_Values{std::move(Values)}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstance, TBase)

    virtual IRadientMaterialDefinition* DILIGENT_CALL_TYPE GetDefinition() const override final
    {
        return m_pDefinition;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_Version;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameter(RadientMaterialParameterHandle Handle,
                                                           void*                          pData,
                                                           Uint32                         DataSize) const override final
    {
        if (!IsValidHandle(Handle))
            return RADIENT_STATUS_INVALID_ARGUMENT;
        if (IsTextureParameter(m_Types[Handle.Index]))
            return RADIENT_STATUS_INVALID_OPERATION;

        const std::vector<Uint8>& Data = m_Values[Handle.Index].Data;
        if (pData == nullptr || DataSize != static_cast<Uint32>(Data.size()))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        std::memcpy(pData, Data.data(), Data.size());
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset**         ppTexture) const override final
    {
        if (ppTexture == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppTexture = nullptr;

        if (!IsValidHandle(Handle))
            return RADIENT_STATUS_INVALID_ARGUMENT;
        if (!IsTextureParameter(m_Types[Handle.Index]))
            return RADIENT_STATUS_INVALID_OPERATION;

        const auto& Textures = m_Values[Handle.Index].Textures;
        if (ArrayIndex >= Textures.size())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        *ppTexture = Textures[ArrayIndex];
        if (*ppTexture != nullptr)
            (*ppTexture)->AddRef();
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final;
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final;

private:
    friend class RadientMaterialInstanceWriterImpl;

    bool IsValidHandle(RadientMaterialParameterHandle Handle) const noexcept
    {
        return Handle.Index < m_Handles.size() && m_Handles[Handle.Index] == Handle;
    }

    RADIENT_STATUS Commit(std::vector<std::optional<MaterialParameterValue>>& Changes) noexcept
    {
        bool StateChanged = false;
        for (size_t Index = 0; Index < Changes.size(); ++Index)
        {
            if (Changes[Index].has_value() &&
                !(m_Values[Index] == *Changes[Index]))
            {
                MaterialParameterValue& Change = *Changes[Index];
                m_Values[Index].Data.swap(Change.Data);
                m_Values[Index].Textures.swap(Change.Textures);
                StateChanged = true;
            }
        }

        if (!StateChanged)
            return RADIENT_STATUS_NO_CHANGE;

        ++m_Version;
        return RADIENT_STATUS_OK;
    }

private:
    RefCntAutoPtr<IRadientMaterialDefinition>          m_pDefinition;
    const std::vector<RadientMaterialParameterHandle>  m_Handles;
    const std::vector<RADIENT_MATERIAL_PARAMETER_TYPE> m_Types;
    std::vector<MaterialParameterValue>                m_Values;
    Uint64                                             m_Version = 1;
};

class RadientMaterialInstanceWriterImpl final : public ObjectBase<IRadientMaterialInstanceWriter>
{
public:
    using TBase = ObjectBase<IRadientMaterialInstanceWriter>;

    RadientMaterialInstanceWriterImpl(IReferenceCounters*          pRefCounters,
                                      RadientMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters},
        m_pInstance{pInstance},
        m_Changes(m_pInstance->m_Values.size())
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstanceWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetParameter(RadientMaterialParameterHandle Handle,
                                                           const void*                    pData,
                                                           Uint32                         DataSize) override final
    {
        if (!m_pInstance->IsValidHandle(Handle) ||
            IsTextureParameter(m_pInstance->m_Types[Handle.Index]))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const MaterialParameterValue& CurrentValue = m_Changes[Handle.Index].has_value() ?
            *m_Changes[Handle.Index] :
            m_pInstance->m_Values[Handle.Index];
        const std::vector<Uint8>&     Data         = CurrentValue.Data;
        if (pData == nullptr || DataSize != static_cast<Uint32>(Data.size()))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (std::memcmp(Data.data(), pData, Data.size()) == 0)
            return RADIENT_STATUS_OK;

        MaterialParameterValue NewValue = CurrentValue;
        std::memcpy(NewValue.Data.data(), pData, NewValue.Data.size());
        if (NewValue == m_pInstance->m_Values[Handle.Index])
            m_Changes[Handle.Index].reset();
        else
            m_Changes[Handle.Index] = std::move(NewValue);
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset*          pTexture) override final
    {
        if (!m_pInstance->IsValidHandle(Handle) ||
            !IsTextureParameter(m_pInstance->m_Types[Handle.Index]))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const MaterialParameterValue& CurrentValue = m_Changes[Handle.Index].has_value() ?
            *m_Changes[Handle.Index] :
            m_pInstance->m_Values[Handle.Index];
        const auto&                   Textures     = CurrentValue.Textures;
        if (ArrayIndex >= Textures.size())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (Textures[ArrayIndex] == pTexture)
            return RADIENT_STATUS_OK;

        MaterialParameterValue NewValue = CurrentValue;
        NewValue.Textures[ArrayIndex]   = pTexture;
        if (NewValue == m_pInstance->m_Values[Handle.Index])
            m_Changes[Handle.Index].reset();
        else
            m_Changes[Handle.Index] = std::move(NewValue);
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        const RADIENT_STATUS Status = m_pInstance->Commit(m_Changes);
        if (Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_NO_CHANGE)
        {
            for (auto& Change : m_Changes)
                Change.reset();
        }
        return Status;
    }

private:
    RefCntAutoPtr<RadientMaterialInstanceImpl>         m_pInstance;
    std::vector<std::optional<MaterialParameterValue>> m_Changes;
};

RADIENT_STATUS BuildInitialMaterialValues(IRadientMaterialDefinition*                   pDefinition,
                                          std::vector<RadientMaterialParameterHandle>&  Handles,
                                          std::vector<RADIENT_MATERIAL_PARAMETER_TYPE>& Types,
                                          std::vector<MaterialParameterValue>&          Values)
{
    const Uint32 ParameterCount = pDefinition->GetParameterCount();
    Handles.resize(ParameterCount);
    Types.resize(ParameterCount);
    Values.resize(ParameterCount);

    for (Uint32 Index = 0; Index < ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Desc = pDefinition->GetParameterDesc(Index);
        Uint32                              DataSize;
        if (pDefinition->GetParameterHandle(Index, &Handles[Index]) != RADIENT_STATUS_OK ||
            !Handles[Index] || Handles[Index].Index != Index ||
            Desc.Type <= RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN ||
            Desc.Type >= RADIENT_MATERIAL_PARAMETER_TYPE_COUNT ||
            !GetMaterialParameterDataSize(Desc, DataSize))
        {
            return RADIENT_STATUS_INVALID_DATA;
        }

        Types[Index] = Desc.Type;
        if (IsTextureParameter(Desc.Type))
        {
            Values[Index].Textures.resize(Desc.ArraySize);
            if (Desc.ArraySize == 1)
                Values[Index].Textures[0] = Desc.pDefaultTexture;
        }
        else
        {
            Values[Index].Data.resize(DataSize, 0);
            if (Desc.pDefaultValue != nullptr)
                std::memcpy(Values[Index].Data.data(), Desc.pDefaultValue, DataSize);
        }
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientMaterialDefinitionImpl::CreateInstance(IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    const RADIENT_STATUS DefinitionStatus = GetStatus();
    if (RADIENT_FAILED(DefinitionStatus))
        return DefinitionStatus;

    try
    {
        std::vector<RadientMaterialParameterHandle>  Handles;
        std::vector<RADIENT_MATERIAL_PARAMETER_TYPE> Types;
        std::vector<MaterialParameterValue>          Values;
        const RADIENT_STATUS                         BuildStatus =
            BuildInitialMaterialValues(const_cast<RadientMaterialDefinitionImpl*>(this), Handles, Types, Values);
        if (BuildStatus != RADIENT_STATUS_OK)
            return BuildStatus;

        RefCntAutoPtr<RadientMaterialInstanceImpl> pInstance{
            MakeNewRCObj<RadientMaterialInstanceImpl>()(
                const_cast<RadientMaterialDefinitionImpl*>(this),
                std::move(Handles),
                std::move(Types),
                std::move(Values))};
        *ppInstance = pInstance.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientMaterialInstanceImpl::CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppWriter = nullptr;

    try
    {
        RefCntAutoPtr<RadientMaterialInstanceWriterImpl> pWriter{
            MakeNewRCObj<RadientMaterialInstanceWriterImpl>()(
                const_cast<RadientMaterialInstanceImpl*>(this))};
        *ppWriter = pWriter.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material instance writer: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientMaterialInstanceImpl::Clone(IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    try
    {
        RefCntAutoPtr<RadientMaterialInstanceImpl> pInstance{
            MakeNewRCObj<RadientMaterialInstanceImpl>()(
                m_pDefinition,
                m_Handles,
                m_Types,
                m_Values)};
        *ppInstance = pInstance.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to clone Radient material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace

RADIENT_STATUS RadientMaterialAssetManager::CreateDefinition(const RadientMaterialDefinitionCreateInfo& DefinitionCI,
                                                             IRadientMaterialDefinition**               ppDefinition)
{
    if (ppDefinition == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppDefinition = nullptr;

    const RADIENT_STATUS ValidationStatus = ValidateMaterialDefinitionCreateInfo(DefinitionCI);
    if (ValidationStatus != RADIENT_STATUS_OK)
        return ValidationStatus;

    try
    {
        RefCntAutoPtr<RadientMaterialDefinitionImpl> pDefinition{
            MakeNewRCObj<RadientMaterialDefinitionImpl>()(DefinitionCI)};
        *ppDefinition = pDefinition.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material definition: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

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

                                                 RadientMaterialDefinitionCreateInfo GenericDefinitionCI{};
                                                 GenericDefinitionCI.Desc.Name      = DefinitionName.c_str();
                                                 GenericDefinitionCI.Desc.Domain    = RADIENT_MATERIAL_DOMAIN_SURFACE;
                                                 GenericDefinitionCI.Reference      = {CacheKey.c_str(), 1};
                                                 GenericDefinitionCI.pParameters    = Parameters.data();
                                                 GenericDefinitionCI.ParameterCount = static_cast<Uint32>(Parameters.size());

                                                 RefCntAutoPtr<IRadientMaterialDefinition> pNewDefinition;
                                                 DefinitionStatus = CreateDefinition(GenericDefinitionCI, pNewDefinition.GetAddressOfEmpty());
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
