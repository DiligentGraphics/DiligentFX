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
#include "DynamicBitSet.hpp"
#include "EngineMemory.h"
#include "Errors.hpp"
#include "FixedLinearAllocator.hpp"
#include "HashUtils.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127) // conditional expression is constant
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/flat_hash_map.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <atomic>
#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
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

RADIENT_STATUS ValidateMaterialDefinitionDesc(const RadientMaterialDefinitionDesc& Desc)
{
    if (Desc.Domain >= RADIENT_MATERIAL_DOMAIN_COUNT)
    {
        LOG_ERROR_MESSAGE("Invalid material definition domain ", Uint32{Desc.Domain});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.ParameterCount != 0 && Desc.pParameters == nullptr)
    {
        LOG_ERROR_MESSAGE("Material definition declares ", Desc.ParameterCount,
                          " parameters, but pParameters is null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    std::unordered_map<std::string, Uint32> Names;
    Names.reserve(Desc.ParameterCount);

    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Parameter = Desc.pParameters[Index];
        if (Parameter.Name == nullptr)
        {
            LOG_ERROR_MESSAGE("Material parameter ", Index, " name must not be null");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Parameter.Name[0] == '\0')
        {
            LOG_ERROR_MESSAGE("Material parameter ", Index, " name must not be empty");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Parameter.Type <= RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN ||
            Parameter.Type >= RADIENT_MATERIAL_PARAMETER_TYPE_COUNT)
        {
            LOG_ERROR_MESSAGE("Material parameter '", Parameter.Name, "' has invalid type ", Uint32{Parameter.Type});
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Parameter.ArraySize == 0)
        {
            LOG_ERROR_MESSAGE("Material parameter '", Parameter.Name, "' array size must not be zero");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        Uint32 DataSize;
        if (!GetMaterialParameterDataSize(Parameter, DataSize))
        {
            LOG_ERROR_MESSAGE("Material parameter '", Parameter.Name, "' data size exceeds the supported limit");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (IsTextureParameter(Parameter.Type))
        {
            if (Parameter.pDefaultValue != nullptr)
            {
                LOG_ERROR_MESSAGE("Texture material parameter '", Parameter.Name,
                                  "' must use pDefaultTexture instead of pDefaultValue");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            if (Parameter.ArraySize != 1 && Parameter.pDefaultTexture != nullptr)
            {
                LOG_ERROR_MESSAGE("Texture array material parameter '", Parameter.Name,
                                  "' must not specify pDefaultTexture");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }
        else if (Parameter.pDefaultTexture != nullptr)
        {
            LOG_ERROR_MESSAGE("Non-texture material parameter '", Parameter.Name,
                              "' must not specify pDefaultTexture");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const auto InsertResult = Names.emplace(Parameter.Name, Index);
        if (!InsertResult.second)
        {
            LOG_ERROR_MESSAGE("Material parameter ", Index, " name '", Parameter.Name,
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

// Parameter descriptors, strings, and default values reside in Memory.
// Default texture pointers are retained directly by their descriptors.
struct PackedMaterialDefinitionData
{
    PackedMaterialDefinitionData(void* pData, IMemoryAllocator& Allocator) :
        Memory{pData, STDDeleterRawMem<void>{Allocator}}
    {}

    PackedMaterialDefinitionData(PackedMaterialDefinitionData&& Other) noexcept :
        Memory{std::move(Other.Memory)},
        Desc{Other.Desc}
    {
        Other.Desc = {};
    }

    PackedMaterialDefinitionData(const PackedMaterialDefinitionData&)            = delete;
    PackedMaterialDefinitionData& operator=(const PackedMaterialDefinitionData&) = delete;
    PackedMaterialDefinitionData& operator=(PackedMaterialDefinitionData&&)      = delete;

    ~PackedMaterialDefinitionData()
    {
        for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
        {
            if (Desc.pParameters[Index].pDefaultTexture != nullptr)
                Desc.pParameters[Index].pDefaultTexture->Release();
        }
    }

    std::unique_ptr<void, STDDeleterRawMem<void>> Memory;
    RadientMaterialDefinitionDesc                 Desc;
};

PackedMaterialDefinitionData PackMaterialDefinitionData(const RadientMaterialDefinitionDesc& Desc)
{
    const Char* const DefinitionName = Desc.Name != nullptr ? Desc.Name : "";

    FixedLinearAllocator Allocator{GetRawAllocator()};
    Allocator.AddSpace<RadientMaterialParameterDesc>(Desc.ParameterCount);
    Allocator.AddSpaceForString(DefinitionName);
    Allocator.AddSpaceForString(Desc.Reference.URI);

    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Parameter = Desc.pParameters[Index];
        Allocator.AddSpaceForString(Parameter.Name);

        Uint32     DataSize        = 0;
        const bool IsValidDataSize = GetMaterialParameterDataSize(Parameter, DataSize);
        VERIFY_EXPR(IsValidDataSize);
        (void)IsValidDataSize;
        Allocator.AddSpace(DataSize, alignof(Uint32));
    }

    // Reserve one block, then use a second allocator as a cursor over it.
    Allocator.Reserve();
    const size_t MemorySize = Allocator.GetReservedSize();
    void* const  pMemory    = Allocator.ReleaseOwnership();

    PackedMaterialDefinitionData Data{pMemory, GetRawAllocator()};
    FixedLinearAllocator         Writer{pMemory, MemorySize};

    RadientMaterialParameterDesc* const pParameters =
        Writer.ConstructArray<RadientMaterialParameterDesc>(Desc.ParameterCount);

    Data.Desc               = Desc;
    Data.Desc.Name          = Writer.CopyString(DefinitionName);
    Data.Desc.Reference     = Desc.Reference;
    Data.Desc.Reference.URI = Writer.CopyString(Desc.Reference.URI);
    Data.Desc.pParameters   = pParameters;

    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Src = Desc.pParameters[Index];
        RadientMaterialParameterDesc&       Dst = pParameters[Index];
        Dst                                     = Src;
        Dst.Name                                = Writer.CopyString(Src.Name);

        Uint32     DataSize        = 0;
        const bool IsValidDataSize = GetMaterialParameterDataSize(Src, DataSize);
        VERIFY_EXPR(IsValidDataSize);
        (void)IsValidDataSize;
        if (DataSize != 0)
        {
            void* const pDefaultValue = Writer.Allocate(DataSize, alignof(Uint32));
            if (Src.pDefaultValue != nullptr)
                std::memcpy(pDefaultValue, Src.pDefaultValue, DataSize);
            else
                std::memset(pDefaultValue, 0, DataSize);
            Dst.pDefaultValue = pDefaultValue;
        }

        if (Dst.pDefaultTexture != nullptr)
            Dst.pDefaultTexture->AddRef();
    }

    VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());

    return Data;
}

struct MaterialParameterValue
{
    // Size is the byte size for value parameters and the element count for textures.
    RADIENT_MATERIAL_PARAMETER_TYPE Type  = RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN;
    Uint32                          Size  = 0;
    void*                           pData = nullptr;
};

// Value records, raw parameter data, and retained texture arrays share one allocation.
class PackedMaterialInstanceData
{
public:
    using TexturePtr = RefCntAutoPtr<IRadientTextureAsset>;

    explicit PackedMaterialInstanceData(const RadientMaterialDefinitionDesc& Desc,
                                        const PackedMaterialInstanceData*    pSource = nullptr);

    PackedMaterialInstanceData(const PackedMaterialInstanceData&)            = delete;
    PackedMaterialInstanceData& operator=(const PackedMaterialInstanceData&) = delete;
    PackedMaterialInstanceData(PackedMaterialInstanceData&&)                 = delete;
    PackedMaterialInstanceData& operator=(PackedMaterialInstanceData&&)      = delete;

    ~PackedMaterialInstanceData()
    {
        for (Uint32 ValueIndex = 0; ValueIndex < m_ValueCount; ++ValueIndex)
        {
            const MaterialParameterValue& Value = m_pValues[ValueIndex];
            if (!IsTextureParameter(Value.Type))
                continue;

            TexturePtr* const pTextures = static_cast<TexturePtr*>(Value.pData);
            for (Uint32 TextureIndex = 0; TextureIndex < Value.Size; ++TextureIndex)
                pTextures[TextureIndex].~TexturePtr();
        }
    }

    Uint32 GetValueCount() const noexcept
    {
        return m_ValueCount;
    }

    MaterialParameterValue& GetValue(Uint32 Index) noexcept
    {
        VERIFY_EXPR(Index < m_ValueCount);
        return m_pValues[Index];
    }

    const MaterialParameterValue& GetValue(Uint32 Index) const noexcept
    {
        VERIFY_EXPR(Index < m_ValueCount);
        return m_pValues[Index];
    }

    bool HasSameValue(Uint32 Index, const PackedMaterialInstanceData& Other) const noexcept
    {
        const MaterialParameterValue& Lhs = GetValue(Index);
        const MaterialParameterValue& Rhs = Other.GetValue(Index);
        VERIFY_EXPR(Lhs.Type == Rhs.Type && Lhs.Size == Rhs.Size);

        if (IsTextureParameter(Lhs.Type))
        {
            const auto* const pLhsTextures = static_cast<const TexturePtr*>(Lhs.pData);
            const auto* const pRhsTextures = static_cast<const TexturePtr*>(Rhs.pData);
            for (Uint32 TextureIndex = 0; TextureIndex < Lhs.Size; ++TextureIndex)
            {
                if (pLhsTextures[TextureIndex] != pRhsTextures[TextureIndex])
                    return false;
            }
            return true;
        }

        return std::memcmp(Lhs.pData, Rhs.pData, Lhs.Size) == 0;
    }

    void CopyValueFrom(Uint32 Index, const PackedMaterialInstanceData& Source) noexcept
    {
        MaterialParameterValue&       Dst = GetValue(Index);
        const MaterialParameterValue& Src = Source.GetValue(Index);
        VERIFY_EXPR(Dst.Type == Src.Type && Dst.Size == Src.Size);

        if (IsTextureParameter(Dst.Type))
        {
            auto* const       pDstTextures = static_cast<TexturePtr*>(Dst.pData);
            const auto* const pSrcTextures = static_cast<const TexturePtr*>(Src.pData);
            for (Uint32 TextureIndex = 0; TextureIndex < Dst.Size; ++TextureIndex)
                pDstTextures[TextureIndex] = pSrcTextures[TextureIndex];
        }
        else
        {
            std::memcpy(Dst.pData, Src.pData, Dst.Size);
        }
    }

private:
    std::unique_ptr<void, STDDeleterRawMem<void>> m_Memory;
    MaterialParameterValue*                       m_pValues    = nullptr;
    Uint32                                        m_ValueCount = 0;
};

PackedMaterialInstanceData::PackedMaterialInstanceData(const RadientMaterialDefinitionDesc& Desc,
                                                       const PackedMaterialInstanceData*    pSource)
{
    VERIFY_EXPR(pSource == nullptr || pSource->GetValueCount() == Desc.ParameterCount);

    FixedLinearAllocator Allocator{GetRawAllocator()};
    Allocator.AddSpace<MaterialParameterValue>(Desc.ParameterCount);
    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Parameter = Desc.pParameters[Index];
        Uint32                              DataSize;
        const bool                          IsValidDataSize = GetMaterialParameterDataSize(Parameter, DataSize);
        VERIFY_EXPR(IsValidDataSize);
        (void)IsValidDataSize;

        if (IsTextureParameter(Parameter.Type))
            Allocator.AddSpace<PackedMaterialInstanceData::TexturePtr>(Parameter.ArraySize);
        else
            Allocator.AddSpace(DataSize, alignof(Uint32));
    }

    Allocator.Reserve();
    const size_t MemorySize = Allocator.GetReservedSize();
    m_Memory                = decltype(m_Memory){Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

    FixedLinearAllocator Writer{m_Memory.get(), MemorySize};
    m_pValues = Writer.ConstructArray<MaterialParameterValue>(Desc.ParameterCount);

    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Parameter = Desc.pParameters[Index];
        MaterialParameterValue&             Value     = m_pValues[Index];
        Value.Type                                    = Parameter.Type;

        Uint32     DataSize;
        const bool IsValidDataSize = GetMaterialParameterDataSize(Parameter, DataSize);
        VERIFY_EXPR(IsValidDataSize);
        (void)IsValidDataSize;

        if (IsTextureParameter(Parameter.Type))
        {
            auto* const pTextures =
                Writer.ConstructArray<PackedMaterialInstanceData::TexturePtr>(Parameter.ArraySize);
            Value.pData = pTextures;
            Value.Size  = Parameter.ArraySize;
            ++m_ValueCount;

            if (pSource != nullptr)
            {
                const MaterialParameterValue& SourceValue = pSource->GetValue(Index);
                VERIFY_EXPR(SourceValue.Type == Value.Type && SourceValue.Size == Value.Size);
                const auto* const pSourceTextures =
                    static_cast<const PackedMaterialInstanceData::TexturePtr*>(SourceValue.pData);
                for (Uint32 TextureIndex = 0; TextureIndex < Value.Size; ++TextureIndex)
                    pTextures[TextureIndex] = pSourceTextures[TextureIndex];
            }
            else if (Parameter.ArraySize == 1)
            {
                pTextures[0] = Parameter.pDefaultTexture;
            }
        }
        else
        {
            Value.pData = Writer.Allocate(DataSize, alignof(Uint32));
            Value.Size  = DataSize;
            ++m_ValueCount;

            if (pSource != nullptr)
            {
                const MaterialParameterValue& SourceValue = pSource->GetValue(Index);
                VERIFY_EXPR(SourceValue.Type == Value.Type && SourceValue.Size == Value.Size);
                std::memcpy(Value.pData, SourceValue.pData, Value.Size);
            }
            else if (Parameter.pDefaultValue != nullptr)
            {
                std::memcpy(Value.pData, Parameter.pDefaultValue, Value.Size);
            }
            else
            {
                std::memset(Value.pData, 0, Value.Size);
            }
        }
    }

    VERIFY_EXPR(m_ValueCount == Desc.ParameterCount);
    VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
}

class RadientMaterialDefinitionImpl final : public ObjectBase<IRadientMaterialDefinition>
{
public:
    using TBase = ObjectBase<IRadientMaterialDefinition>;

    RadientMaterialDefinitionImpl(IReferenceCounters*                  pRefCounters,
                                  const RadientMaterialDefinitionDesc& Desc) :
        TBase{pRefCounters},
        m_Data{PackMaterialDefinitionData(Desc)},
        m_ParameterIndices{},
        m_DefinitionHandle{s_NextMaterialDefinitionHandle.fetch_add(1, std::memory_order_relaxed)}
    {
        m_ParameterIndices.reserve(m_Data.Desc.ParameterCount);
        for (Uint32 Index = 0; Index < m_Data.Desc.ParameterCount; ++Index)
        {
            const bool Inserted =
                m_ParameterIndices.emplace(m_Data.Desc.pParameters[Index].Name, Index).second;
            VERIFY_EXPR(Inserted);
        }
    }

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientMaterialDefinition, IID_RadientAsset, TBase)

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Data.Desc.Reference;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_MATERIAL;
    }

    virtual const RadientMaterialDefinitionDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Data.Desc;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetStatus() const override final
    {
        return RADIENT_STATUS_OK;
    }

    virtual Uint32 DILIGENT_CALL_TYPE GetParameterCount() const override final
    {
        return m_Data.Desc.ParameterCount;
    }

    virtual const RadientMaterialParameterDesc& DILIGENT_CALL_TYPE GetParameterDesc(Uint32 Index) const override final
    {
        if (Index >= m_Data.Desc.ParameterCount)
        {
            UNEXPECTED("Material parameter index ", Index, " is out of range");
            static constexpr RadientMaterialParameterDesc InvalidDesc{};
            return InvalidDesc;
        }
        return m_Data.Desc.pParameters[Index];
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameterHandle(Uint32                          Index,
                                                                 RadientMaterialParameterHandle* pHandle) const override final
    {
        if (pHandle == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *pHandle = {};

        if (Index >= m_Data.Desc.ParameterCount)
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

        const auto It = m_ParameterIndices.find(Name);
        if (It == m_ParameterIndices.end())
            return RADIENT_STATUS_NOT_FOUND;

        return GetParameterHandle(It->second, pHandle);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateInstance(IRadientMaterialInstance** ppInstance) const override final;

private:
    using ParameterIndexMap = absl::flat_hash_map<const Char*, Uint32, CStringHash<Char>, CStringCompare<Char>>;

    const PackedMaterialDefinitionData m_Data;
    // Map keys reference parameter names packed in m_Data. Declaring the map
    // after m_Data ensures that the keys are destroyed before their strings.
    ParameterIndexMap   m_ParameterIndices;
    const RadientHandle m_DefinitionHandle;
};

class RadientMaterialInstanceWriterImpl;

class RadientMaterialInstanceImpl final : public ObjectBase<IRadientMaterialInstance>
{
public:
    using TBase = ObjectBase<IRadientMaterialInstance>;

    RadientMaterialInstanceImpl(IReferenceCounters*                     pRefCounters,
                                IRadientMaterialDefinition*             pDefinition,
                                RadientHandle                           DefinitionHandle,
                                const PackedMaterialInstanceData* const pSource = nullptr) :
        TBase{pRefCounters},
        m_pDefinition{pDefinition},
        m_DefinitionHandle{DefinitionHandle},
        m_Data{pDefinition->GetDesc(), pSource}
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
        const MaterialParameterValue& Value = m_Data.GetValue(Handle.Index);
        if (IsTextureParameter(Value.Type))
            return RADIENT_STATUS_INVALID_OPERATION;

        if (pData == nullptr || DataSize != Value.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        std::memcpy(pData, Value.pData, Value.Size);
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
        const MaterialParameterValue& Value = m_Data.GetValue(Handle.Index);
        if (!IsTextureParameter(Value.Type))
            return RADIENT_STATUS_INVALID_OPERATION;

        if (ArrayIndex >= Value.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const auto* const pTextures =
            static_cast<const PackedMaterialInstanceData::TexturePtr*>(Value.pData);
        *ppTexture = pTextures[ArrayIndex];
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
        return Handle.Definition == m_DefinitionHandle && Handle.Index < m_Data.GetValueCount();
    }

    RADIENT_STATUS Commit(const PackedMaterialInstanceData& ScratchData,
                          const DynamicBitSet&              ChangedParameters) noexcept
    {
        VERIFY_EXPR(m_Data.GetValueCount() == ScratchData.GetValueCount());
        bool StateChanged = false;
        ChangedParameters.ForEachSetBit(
            [&](size_t ParameterIndex) {
                const Uint32 Index = static_cast<Uint32>(ParameterIndex);
                if (!m_Data.HasSameValue(Index, ScratchData))
                {
                    m_Data.CopyValueFrom(Index, ScratchData);
                    StateChanged = true;
                }
            });

        if (!StateChanged)
            return RADIENT_STATUS_NO_CHANGE;

        ++m_Version;
        return RADIENT_STATUS_OK;
    }

private:
    RefCntAutoPtr<IRadientMaterialDefinition> m_pDefinition;
    const RadientHandle                       m_DefinitionHandle;
    PackedMaterialInstanceData                m_Data;
    Uint64                                    m_Version = 1;
};

class RadientMaterialInstanceWriterImpl final : public ObjectBase<IRadientMaterialInstanceWriter>
{
public:
    using TBase = ObjectBase<IRadientMaterialInstanceWriter>;

    RadientMaterialInstanceWriterImpl(IReferenceCounters*          pRefCounters,
                                      RadientMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters},
        m_pInstance{pInstance},
        m_ScratchData{m_pInstance->m_pDefinition->GetDesc(), &m_pInstance->m_Data},
        m_ChangedParameters{m_ScratchData.GetValueCount()}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstanceWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetParameter(RadientMaterialParameterHandle Handle,
                                                           const void*                    pData,
                                                           Uint32                         DataSize) override final
    {
        if (!m_pInstance->IsValidHandle(Handle) ||
            IsTextureParameter(m_pInstance->m_Data.GetValue(Handle.Index).Type))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const MaterialParameterValue& InstanceValue = m_pInstance->m_Data.GetValue(Handle.Index);
        MaterialParameterValue&       ScratchValue  = m_ScratchData.GetValue(Handle.Index);
        const MaterialParameterValue& CurrentValue  = m_ChangedParameters.Test(Handle.Index) ?
            ScratchValue :
            InstanceValue;
        if (pData == nullptr || DataSize != InstanceValue.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (std::memcmp(CurrentValue.pData, pData, InstanceValue.Size) == 0)
            return RADIENT_STATUS_OK;

        std::memcpy(ScratchValue.pData, pData, ScratchValue.Size);
        UpdateChangedParameter(Handle.Index);
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset*          pTexture) override final
    {
        if (!m_pInstance->IsValidHandle(Handle) ||
            !IsTextureParameter(m_pInstance->m_Data.GetValue(Handle.Index).Type))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const MaterialParameterValue& InstanceValue = m_pInstance->m_Data.GetValue(Handle.Index);
        MaterialParameterValue&       ScratchValue  = m_ScratchData.GetValue(Handle.Index);
        if (ArrayIndex >= InstanceValue.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const bool        ParameterChanged = m_ChangedParameters.Test(Handle.Index);
        const auto* const pInstanceTextures =
            static_cast<const PackedMaterialInstanceData::TexturePtr*>(InstanceValue.pData);
        auto* const pScratchTextures =
            static_cast<PackedMaterialInstanceData::TexturePtr*>(ScratchValue.pData);
        IRadientTextureAsset* const pCurrentTexture = ParameterChanged ?
            pScratchTextures[ArrayIndex].RawPtr() :
            pInstanceTextures[ArrayIndex].RawPtr();
        if (pCurrentTexture == pTexture)
            return RADIENT_STATUS_OK;

        pScratchTextures[ArrayIndex] = pTexture;
        UpdateChangedParameter(Handle.Index);
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        const RADIENT_STATUS Status = m_pInstance->Commit(m_ScratchData, m_ChangedParameters);
        if (Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_NO_CHANGE)
            m_ChangedParameters.ResetAll();
        return Status;
    }

private:
    void UpdateChangedParameter(Uint32 ParameterIndex)
    {
        m_ChangedParameters.Set(
            ParameterIndex,
            !m_pInstance->m_Data.HasSameValue(ParameterIndex, m_ScratchData));
    }

private:
    RefCntAutoPtr<RadientMaterialInstanceImpl> m_pInstance;
    PackedMaterialInstanceData                 m_ScratchData;
    DynamicBitSet                              m_ChangedParameters;
};

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
        RefCntAutoPtr<RadientMaterialInstanceImpl> pInstance{
            MakeNewRCObj<RadientMaterialInstanceImpl>()(
                const_cast<RadientMaterialDefinitionImpl*>(this),
                m_DefinitionHandle)};
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
                m_DefinitionHandle,
                &m_Data)};
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

RADIENT_STATUS RadientMaterialAssetManager::CreateDefinition(const RadientMaterialDefinitionDesc& DefinitionDesc,
                                                             IRadientMaterialDefinition**         ppDefinition)
{
    if (ppDefinition == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppDefinition = nullptr;

    try
    {
        const RADIENT_STATUS ValidationStatus = ValidateMaterialDefinitionDesc(DefinitionDesc);
        if (ValidationStatus != RADIENT_STATUS_OK)
            return ValidationStatus;

        RefCntAutoPtr<RadientMaterialDefinitionImpl> pDefinition{
            MakeNewRCObj<RadientMaterialDefinitionImpl>()(DefinitionDesc)};
        *ppDefinition = pDefinition.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material definition: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material definition: unknown exception");
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

                                                 RadientMaterialDefinitionDesc DefinitionDesc{};
                                                 DefinitionDesc.Name           = DefinitionName.c_str();
                                                 DefinitionDesc.Domain         = RADIENT_MATERIAL_DOMAIN_SURFACE;
                                                 DefinitionDesc.Reference      = {CacheKey.c_str(), 1};
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
