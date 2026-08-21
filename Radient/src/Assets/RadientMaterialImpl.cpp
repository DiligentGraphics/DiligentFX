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
#include "Assets/RadientMaterialImpl.hpp"
#include "Assets/RadientTextureAssetManager.hpp"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
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

using ShaderTextureAttribs = GLTF::Material::TextureShaderAttribs;

static_assert(static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP) == static_cast<Uint32>(TEXTURE_ADDRESS_WRAP));
static_assert(static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP) == static_cast<Uint32>(TEXTURE_ADDRESS_CLAMP));
static_assert(sizeof(ShaderTextureAttribs) <= std::numeric_limits<Uint32>::max());

static constexpr Uint32 ShaderTexturePackedPropsOffset = static_cast<Uint32>(offsetof(ShaderTextureAttribs, PackedProps));
static constexpr Uint32 ShaderTextureSliceOffset       = static_cast<Uint32>(offsetof(ShaderTextureAttribs, TextureSlice));
static constexpr Uint32 ShaderTextureAtlasUVOffset     = static_cast<Uint32>(offsetof(ShaderTextureAttribs, AtlasUVScaleAndBias));
static constexpr Uint32 ShaderTextureDataSize          = static_cast<Uint32>(sizeof(ShaderTextureAttribs));

struct ShaderDataRange
{
    Uint32      Size = 0;
    std::string Name;
};

class ShaderDataRangeTracker
{
public:
    bool Add(Uint32 Offset, Uint32 Size, std::string Name)
    {
        const Uint64 End  = Uint64{Offset} + Size;
        const auto   Next = m_Ranges.lower_bound(Offset);
        if (Next != m_Ranges.end() && End > Next->first)
            return ReportOverlap(Next->second.Name, Name);

        if (Next != m_Ranges.begin())
        {
            auto Previous = Next;
            --Previous;
            if (Uint64{Previous->first} + Previous->second.Size > Offset)
                return ReportOverlap(Previous->second.Name, Name);
        }

        m_Ranges.emplace_hint(Next, Offset, ShaderDataRange{Size, std::move(Name)});
        return true;
    }

private:
    static bool ReportOverlap(const std::string& FirstName,
                              const std::string& SecondName)
    {
        LOG_ERROR_MESSAGE("Material shader data ranges for ", FirstName, " and ", SecondName, " overlap");
        return false;
    }

private:
    std::map<Uint32, ShaderDataRange> m_Ranges;
};

RADIENT_STATUS ValidateSurfaceMaterialDefinitionDesc(const RadientSurfaceMaterialDefinitionDesc& Desc)
{
    if (Desc.ShadingModel >= RADIENT_SURFACE_SHADING_MODEL_COUNT)
    {
        LOG_ERROR_MESSAGE("Invalid surface shading model ", Uint32{Desc.ShadingModel});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS UnsupportedFeatures =
        Desc.Features & ~RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL;
    if (UnsupportedFeatures != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE)
    {
        LOG_ERROR_MESSAGE("Surface material feature flags contain unsupported bits");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_UNLIT &&
        Desc.Features != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE)
    {
        LOG_ERROR_MESSAGE("Unlit surface materials do not support optional material features");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if ((Desc.Features & RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME) != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE &&
        (Desc.Features & RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION) == RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE)
    {
        LOG_ERROR_MESSAGE("The volume surface feature requires the transmission surface feature");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ValidateMaterialDefinitionDesc(const RadientMaterialDefinitionDesc& Desc)
{
    switch (Desc.Type)
    {
        case RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE:
        {
            const auto&          SurfaceDesc = static_cast<const RadientSurfaceMaterialDefinitionDesc&>(Desc);
            const RADIENT_STATUS Status      = ValidateSurfaceMaterialDefinitionDesc(SurfaceDesc);
            if (Status != RADIENT_STATUS_OK)
                return Status;
            break;
        }

        case RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS:
        case RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE:
            break;

        default:
            LOG_ERROR_MESSAGE("Invalid material definition type ", Uint32{Desc.Type});
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

RADIENT_STATUS ValidateSurfaceMaterialShaderParameterPacking(
    const RadientMaterialDefinitionDesc&       DefinitionDesc,
    const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout,
    ShaderDataRangeTracker&                    RangeTracker)
{
    const RadientSurfaceMaterialShaderParameterPacking* const pSurfacePacking = ShaderDataLayout.pSurfacePacking;
    if (pSurfacePacking == nullptr)
        return RADIENT_STATUS_OK;

    const auto ValidateSurfaceProperty =
        [&](Uint32 Offset, Uint32 Size, const char* Article, const char* Name) -> bool {
        if (Offset == ~Uint32{0})
            return true;

        if (DefinitionDesc.Type != RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE)
        {
            LOG_ERROR_MESSAGE("Only surface material definitions may pack ", Article, ' ', Name);
            return false;
        }
        if (Offset > ShaderDataLayout.Size ||
            Size > ShaderDataLayout.Size - Offset)
        {
            LOG_ERROR_MESSAGE("Material ", Name, " byte range exceeds the shader data size ", ShaderDataLayout.Size);
            return false;
        }
        return true;
    };

    if (!ValidateSurfaceProperty(pSurfacePacking->SurfaceModeOffset, sizeof(Uint32), "a", "surface mode") ||
        !ValidateSurfaceProperty(pSurfacePacking->AlphaCutoffOffset, sizeof(Float32), "an", "alpha cutoff"))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (pSurfacePacking->SurfaceModeOffset != ~Uint32{0} &&
        !RangeTracker.Add(pSurfacePacking->SurfaceModeOffset, sizeof(Uint32), "surface mode"))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (pSurfacePacking->AlphaCutoffOffset != ~Uint32{0} &&
        !RangeTracker.Add(pSurfacePacking->AlphaCutoffOffset, sizeof(Float32), "alpha cutoff"))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ValidateMaterialShaderDataLayout(
    const RadientMaterialDefinitionDesc&       DefinitionDesc,
    const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout)
{
    if (ShaderDataLayout.MappingCount != 0 && ShaderDataLayout.pMappings == nullptr)
    {
        LOG_ERROR_MESSAGE("Material shader data layout declares ", ShaderDataLayout.MappingCount,
                          " mappings, but pMappings is null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (ShaderDataLayout.TexturePackingCount != 0 && ShaderDataLayout.pTexturePackings == nullptr)
    {
        LOG_ERROR_MESSAGE("Material shader data layout declares ", ShaderDataLayout.TexturePackingCount,
                          " texture packings, but pTexturePackings is null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (ShaderDataLayout.InitializationCount != 0 && ShaderDataLayout.pInitializations == nullptr)
    {
        LOG_ERROR_MESSAGE("Material shader data layout declares ", ShaderDataLayout.InitializationCount,
                          " initializations, but pInitializations is null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    ShaderDataRangeTracker RangeTracker;

    const RADIENT_STATUS SurfacePackingStatus =
        ValidateSurfaceMaterialShaderParameterPacking(DefinitionDesc, ShaderDataLayout, RangeTracker);
    if (SurfacePackingStatus != RADIENT_STATUS_OK)
        return SurfacePackingStatus;

    // Initializations establish defaults and may intentionally overlap each
    // other and ranges subsequently written from instance parameters.
    for (Uint32 InitializationIndex = 0; InitializationIndex < ShaderDataLayout.InitializationCount; ++InitializationIndex)
    {
        const RadientMaterialShaderDataInitialization& Initialization =
            ShaderDataLayout.pInitializations[InitializationIndex];
        if (Initialization.pData == nullptr)
        {
            LOG_ERROR_MESSAGE("Material shader data initialization ", InitializationIndex,
                              " has null data");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Initialization.Size == 0)
        {
            LOG_ERROR_MESSAGE("Material shader data initialization ", InitializationIndex,
                              " has zero size");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (Initialization.Offset > ShaderDataLayout.Size ||
            Initialization.Size > ShaderDataLayout.Size - Initialization.Offset)
        {
            LOG_ERROR_MESSAGE("Material shader data initialization ", InitializationIndex,
                              " uses byte range [", Initialization.Offset, ", ",
                              Uint64{Initialization.Offset} + Initialization.Size,
                              "), which exceeds the shader data size ", ShaderDataLayout.Size);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    for (Uint32 MappingIndex = 0; MappingIndex < ShaderDataLayout.MappingCount; ++MappingIndex)
    {
        const RadientMaterialShaderParameterPacking& Mapping = ShaderDataLayout.pMappings[MappingIndex];
        if (Mapping.ParameterIndex >= DefinitionDesc.ParameterCount)
        {
            LOG_ERROR_MESSAGE("Material shader data mapping ", MappingIndex, " references parameter index ",
                              Mapping.ParameterIndex, ", but the definition only has ",
                              DefinitionDesc.ParameterCount, " parameters");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const RadientMaterialParameterDesc& Parameter = DefinitionDesc.pParameters[Mapping.ParameterIndex];
        if (IsTextureParameter(Parameter.Type))
        {
            LOG_ERROR_MESSAGE("Material shader data mapping ", MappingIndex, " references texture parameter '",
                              Parameter.Name, "'; texture parameters cannot be copied into shader data");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        Uint32     ParameterDataSize = 0;
        const bool IsValidDataSize   = GetMaterialParameterDataSize(Parameter, ParameterDataSize);
        VERIFY_EXPR(IsValidDataSize);
        (void)IsValidDataSize;

        if (Mapping.Offset > ShaderDataLayout.Size ||
            ParameterDataSize > ShaderDataLayout.Size - Mapping.Offset)
        {
            LOG_ERROR_MESSAGE("Material shader data mapping ", MappingIndex, " for parameter '", Parameter.Name,
                              "' uses byte range [", Mapping.Offset, ", ",
                              Uint64{Mapping.Offset} + ParameterDataSize,
                              "), which exceeds the shader data size ", ShaderDataLayout.Size);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (!RangeTracker.Add(Mapping.Offset,
                              ParameterDataSize,
                              "parameter '" + std::string{Parameter.Name} + "'"))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    for (Uint32 PackingIndex = 0; PackingIndex < ShaderDataLayout.TexturePackingCount; ++PackingIndex)
    {
        const RadientMaterialShaderTexturePacking& Packing = ShaderDataLayout.pTexturePackings[PackingIndex];

        const auto GetParameter = [&](Uint32 ParameterIndex, const char* Role) -> const RadientMaterialParameterDesc* {
            if (ParameterIndex >= DefinitionDesc.ParameterCount)
            {
                LOG_ERROR_MESSAGE("Material shader texture packing ", PackingIndex, " references ", Role,
                                  " parameter index ", ParameterIndex, ", but the definition only has ",
                                  DefinitionDesc.ParameterCount, " parameters");
                return nullptr;
            }
            return &DefinitionDesc.pParameters[ParameterIndex];
        };

        const RadientMaterialParameterDesc* const pTexture = GetParameter(Packing.TextureParameterIndex, "texture");
        if (pTexture == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        if (!IsTextureParameter(pTexture->Type) || pTexture->ArraySize != 1)
        {
            LOG_ERROR_MESSAGE("Material shader texture packing ", PackingIndex, " texture parameter '",
                              pTexture->Name, "' must be a scalar texture");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const auto ValidateScalarParameter = [&](Uint32                          ParameterIndex,
                                                 const char*                     Role,
                                                 RADIENT_MATERIAL_PARAMETER_TYPE ExpectedType) -> bool {
            const RadientMaterialParameterDesc* const pParameter = GetParameter(ParameterIndex, Role);
            if (pParameter == nullptr)
                return false;
            if (pParameter->Type != ExpectedType || pParameter->ArraySize != 1)
            {
                LOG_ERROR_MESSAGE("Material shader texture packing ", PackingIndex, ' ', Role, " parameter '",
                                  pParameter->Name, "' has an incompatible type or array size");
                return false;
            }
            return true;
        };

        if (!ValidateScalarParameter(Packing.UVSelectorParameterIndex, "UV selector", RADIENT_MATERIAL_PARAMETER_TYPE_INT) ||
            !ValidateScalarParameter(Packing.WrapUParameterIndex, "wrap U", RADIENT_MATERIAL_PARAMETER_TYPE_UINT) ||
            !ValidateScalarParameter(Packing.WrapVParameterIndex, "wrap V", RADIENT_MATERIAL_PARAMETER_TYPE_UINT))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (Packing.Offset > ShaderDataLayout.Size ||
            ShaderTextureDataSize > ShaderDataLayout.Size - Packing.Offset)
        {
            LOG_ERROR_MESSAGE("Material shader texture packing ", PackingIndex, " uses byte range [",
                              Packing.Offset, ", ", Uint64{Packing.Offset} + ShaderTextureDataSize,
                              "), which exceeds the shader data size ", ShaderDataLayout.Size);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        const std::string TextureName = "texture parameter '" + std::string{pTexture->Name} + "'";
        if (!RangeTracker.Add(
                Packing.Offset + ShaderTexturePackedPropsOffset,
                ShaderTextureSliceOffset + sizeof(Float32) - ShaderTexturePackedPropsOffset,
                TextureName + " packed properties") ||
            !RangeTracker.Add(
                Packing.Offset + ShaderTextureAtlasUVOffset,
                sizeof(RadientFloat4),
                TextureName + " atlas data"))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
}

} // namespace

RadientMaterialDefinitionImpl::PackedData RadientMaterialDefinitionImpl::PackData(
    const RadientMaterialDefinitionDesc&       Desc,
    const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout)
{
    const Char* const DefinitionName = Desc.Name != nullptr ? Desc.Name : "";

    FixedLinearAllocator Allocator{GetRawAllocator()};
    switch (Desc.Type)
    {
        case RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE:
            Allocator.AddSpace<RadientSurfaceMaterialDefinitionDesc>();
            break;

        case RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS:
            Allocator.AddSpace<RadientPostProcessMaterialDefinitionDesc>();
            break;

        case RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE:
            Allocator.AddSpace<RadientComputeMaterialDefinitionDesc>();
            break;

        default:
            UNEXPECTED("Unexpected material definition type");
            break;
    }
    Allocator.AddSpace<RadientMaterialParameterDesc>(Desc.ParameterCount);
    Allocator.AddSpace<RadientMaterialShaderDataInitialization>(ShaderDataLayout.InitializationCount);
    Allocator.AddSpace<ShaderDataCopyCommand>(ShaderDataLayout.MappingCount);
    Allocator.AddSpace<RadientMaterialShaderTexturePacking>(ShaderDataLayout.TexturePackingCount);
    if (ShaderDataLayout.pSurfacePacking != nullptr)
        Allocator.AddSpace<RadientSurfaceMaterialShaderParameterPacking>();
    for (Uint32 InitializationIndex = 0; InitializationIndex < ShaderDataLayout.InitializationCount; ++InitializationIndex)
        Allocator.AddSpace(ShaderDataLayout.pInitializations[InitializationIndex].Size, alignof(Uint32));
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

    PackedData           Data{pMemory, GetRawAllocator()};
    FixedLinearAllocator Writer{pMemory, MemorySize};

    RadientMaterialDefinitionDesc* pPackedDesc = nullptr;
    switch (Desc.Type)
    {
        case RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE:
            pPackedDesc = Writer.Construct<RadientSurfaceMaterialDefinitionDesc>(
                static_cast<const RadientSurfaceMaterialDefinitionDesc&>(Desc));
            break;

        case RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS:
            pPackedDesc = Writer.Construct<RadientPostProcessMaterialDefinitionDesc>(
                static_cast<const RadientPostProcessMaterialDefinitionDesc&>(Desc));
            break;

        case RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE:
            pPackedDesc = Writer.Construct<RadientComputeMaterialDefinitionDesc>(
                static_cast<const RadientComputeMaterialDefinitionDesc&>(Desc));
            break;

        default:
            UNEXPECTED("Unexpected material definition type");
            break;
    }
    VERIFY_EXPR(pPackedDesc != nullptr);

    RadientMaterialParameterDesc* const pParameters =
        Writer.ConstructArray<RadientMaterialParameterDesc>(Desc.ParameterCount);
    RadientMaterialShaderDataInitialization* const pShaderDataInitializations =
        Writer.ConstructArray<RadientMaterialShaderDataInitialization>(ShaderDataLayout.InitializationCount);
    ShaderDataCopyCommand* const pShaderDataCopyCommands =
        Writer.ConstructArray<ShaderDataCopyCommand>(ShaderDataLayout.MappingCount);
    RadientMaterialShaderTexturePacking* const pShaderDataTextureCommands =
        Writer.ConstructArray<RadientMaterialShaderTexturePacking>(ShaderDataLayout.TexturePackingCount);
    const RadientSurfaceMaterialShaderParameterPacking* const pSurfacePacking =
        ShaderDataLayout.pSurfacePacking != nullptr ?
        Writer.Construct<RadientSurfaceMaterialShaderParameterPacking>(*ShaderDataLayout.pSurfacePacking) :
        nullptr;

    Data.pDesc                           = pPackedDesc;
    pPackedDesc->Name                    = Writer.CopyString(DefinitionName);
    pPackedDesc->Reference               = Desc.Reference;
    pPackedDesc->Reference.URI           = Writer.CopyString(Desc.Reference.URI);
    pPackedDesc->pParameters             = pParameters;
    Data.PackingPlan.Size                = ShaderDataLayout.Size;
    Data.PackingPlan.InitializationCount = ShaderDataLayout.InitializationCount;
    Data.PackingPlan.pInitializations    = pShaderDataInitializations;
    Data.PackingPlan.CopyCommandCount    = ShaderDataLayout.MappingCount;
    Data.PackingPlan.pCopyCommands       = pShaderDataCopyCommands;
    Data.PackingPlan.TextureCommandCount = ShaderDataLayout.TexturePackingCount;
    Data.PackingPlan.pTextureCommands    = pShaderDataTextureCommands;
    Data.PackingPlan.pSurfacePacking     = pSurfacePacking;

    for (Uint32 InitializationIndex = 0; InitializationIndex < ShaderDataLayout.InitializationCount; ++InitializationIndex)
    {
        const RadientMaterialShaderDataInitialization& Src =
            ShaderDataLayout.pInitializations[InitializationIndex];
        RadientMaterialShaderDataInitialization& Dst =
            pShaderDataInitializations[InitializationIndex];
        void* const pInitializationData = Writer.Allocate(Src.Size, alignof(Uint32));
        std::memcpy(pInitializationData, Src.pData, Src.Size);
        Dst       = Src;
        Dst.pData = pInitializationData;
    }

    for (Uint32 MappingIndex = 0; MappingIndex < ShaderDataLayout.MappingCount; ++MappingIndex)
    {
        const RadientMaterialShaderParameterPacking& Mapping   = ShaderDataLayout.pMappings[MappingIndex];
        const RadientMaterialParameterDesc&          Parameter = Desc.pParameters[Mapping.ParameterIndex];

        Uint32     ParameterDataSize = 0;
        const bool IsValidDataSize   = GetMaterialParameterDataSize(Parameter, ParameterDataSize);
        VERIFY_EXPR(IsValidDataSize && ParameterDataSize != 0);
        (void)IsValidDataSize;

        pShaderDataCopyCommands[MappingIndex] = {
            Mapping.ParameterIndex,
            Mapping.Offset,
            ParameterDataSize,
        };
    }

    for (Uint32 PackingIndex = 0; PackingIndex < ShaderDataLayout.TexturePackingCount; ++PackingIndex)
        pShaderDataTextureCommands[PackingIndex] = ShaderDataLayout.pTexturePackings[PackingIndex];

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

namespace
{

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

    const void* GetValueData(Uint32 Index) const noexcept
    {
        return m_pValues[Index].pData;
    }

    bool HasSameValue(Uint32 Index, const void* pData) const noexcept
    {
        const MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(!IsTextureParameter(Value.Type));
        return std::memcmp(Value.pData, pData, Value.Size) == 0;
    }

    void CopyValue(Uint32 Index, const void* pData) noexcept
    {
        MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(!IsTextureParameter(Value.Type));
        std::memcpy(Value.pData, pData, Value.Size);
    }

    IRadientTextureAsset* GetTexture(Uint32 Index, Uint32 ArrayIndex) const noexcept
    {
        const MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
        return static_cast<const TexturePtr*>(Value.pData)[ArrayIndex];
    }

    void SetTexture(Uint32 Index, Uint32 ArrayIndex, IRadientTextureAsset* pTexture) noexcept
    {
        MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
        static_cast<TexturePtr*>(Value.pData)[ArrayIndex] = pTexture;
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
    if (Writer.GetReservedSize() != 0)
        VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
}

} // namespace

RadientMaterialDefinitionImpl::RadientMaterialDefinitionImpl(
    IReferenceCounters*                        pRefCounters,
    const RadientMaterialDefinitionDesc&       Desc,
    const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout) :
    TBase{pRefCounters},
    m_Data{PackData(Desc, ShaderDataLayout)},
    m_ParameterIndices{},
    m_DefinitionHandle{s_NextMaterialDefinitionHandle.fetch_add(1, std::memory_order_relaxed)}
{
    const RadientMaterialDefinitionDesc& PackedDesc = m_Data.GetDesc();
    m_ParameterIndices.reserve(PackedDesc.ParameterCount);
    for (Uint32 Index = 0; Index < PackedDesc.ParameterCount; ++Index)
    {
        const bool Inserted =
            m_ParameterIndices.emplace(PackedDesc.pParameters[Index].Name, Index).second;
        VERIFY_EXPR(Inserted);
    }
}

const RadientMaterialParameterDesc& DILIGENT_CALL_TYPE RadientMaterialDefinitionImpl::GetParameterDesc(Uint32 Index) const
{
    const RadientMaterialDefinitionDesc& Desc = m_Data.GetDesc();
    if (Index >= Desc.ParameterCount)
    {
        UNEXPECTED("Material parameter index ", Index, " is out of range");
        static constexpr RadientMaterialParameterDesc InvalidDesc{};
        return InvalidDesc;
    }
    return Desc.pParameters[Index];
}

RADIENT_STATUS DILIGENT_CALL_TYPE RadientMaterialDefinitionImpl::GetParameterHandle(
    Uint32                          Index,
    RadientMaterialParameterHandle* pHandle) const
{
    if (pHandle == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *pHandle = {};

    if (Index >= m_Data.GetDesc().ParameterCount)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    pHandle->Definition = m_DefinitionHandle;
    pHandle->Index      = Index;
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS DILIGENT_CALL_TYPE RadientMaterialDefinitionImpl::FindParameter(
    const Char*                     Name,
    RadientMaterialParameterHandle* pHandle) const
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

namespace
{

template <typename InterfaceType, typename InstanceType>
class RadientMaterialInstanceWriterBase;

class RadientMaterialInstanceWriterImpl;
class RadientSurfaceMaterialInstanceWriterImpl;

template <typename InterfaceType>
class RadientMaterialInstanceBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    RadientMaterialInstanceBase(IReferenceCounters*               pRefCounters,
                                IRadientMaterialDefinition*       pDefinition,
                                RadientHandle                     DefinitionHandle,
                                const PackedMaterialInstanceData* pSourceData = nullptr) :
        TBase{pRefCounters},
        m_pDefinition{pDefinition},
        m_DefinitionHandle{DefinitionHandle},
        m_Data{pDefinition->GetDesc(), pSourceData}
    {}

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

    const PackedMaterialInstanceData& GetPackedData() const noexcept
    {
        return m_Data;
    }

protected:
    template <typename, typename>
    friend class RadientMaterialInstanceWriterBase;

    bool IsValidHandle(RadientMaterialParameterHandle Handle) const noexcept
    {
        return Handle.Definition == m_DefinitionHandle &&
            Handle.Index < m_Data.GetValueCount() &&
            Handle.Reserved == 0;
    }

protected:
    RefCntAutoPtr<IRadientMaterialDefinition> m_pDefinition;
    const RadientHandle                       m_DefinitionHandle;
    PackedMaterialInstanceData                m_Data;
    Uint64                                    m_Version = 1;
};

class RadientMaterialInstanceImpl final : public RadientMaterialInstanceBase<IRadientMaterialInstance>
{
public:
    using TBase = RadientMaterialInstanceBase<IRadientMaterialInstance>;

    RadientMaterialInstanceImpl(IReferenceCounters*                pRefCounters,
                                IRadientMaterialDefinition*        pDefinition,
                                RadientHandle                      DefinitionHandle,
                                const RadientMaterialInstanceImpl* pSource = nullptr) :
        TBase{pRefCounters,
              pDefinition,
              DefinitionHandle,
              pSource != nullptr ? &pSource->GetPackedData() : nullptr}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstance, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final;
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final;
};

class RadientSurfaceMaterialInstanceImpl final : public RadientMaterialInstanceBase<IRadientSurfaceMaterialInstance>
{
public:
    using TBase = RadientMaterialInstanceBase<IRadientSurfaceMaterialInstance>;

    RadientSurfaceMaterialInstanceImpl(IReferenceCounters*                       pRefCounters,
                                       IRadientMaterialDefinition*               pDefinition,
                                       RadientHandle                             DefinitionHandle,
                                       const RadientSurfaceMaterialInstanceImpl* pSource = nullptr) :
        TBase{pRefCounters,
              pDefinition,
              DefinitionHandle,
              pSource != nullptr ? &pSource->GetPackedData() : nullptr},
        m_SurfaceMode{pSource != nullptr ? pSource->m_SurfaceMode : RADIENT_MATERIAL_SURFACE_MODE_OPAQUE},
        m_AlphaCutoff{pSource != nullptr ? pSource->m_AlphaCutoff : 0.5f},
        m_IsDoubleSided{pSource != nullptr ? pSource->m_IsDoubleSided : False}
    {}

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientSurfaceMaterialInstance, IID_RadientMaterialInstance, TBase)

    virtual RADIENT_MATERIAL_SURFACE_MODE DILIGENT_CALL_TYPE GetSurfaceMode() const override final
    {
        return m_SurfaceMode;
    }

    virtual Float32 DILIGENT_CALL_TYPE GetAlphaCutoff() const override final
    {
        return m_AlphaCutoff;
    }

    virtual Bool DILIGENT_CALL_TYPE IsDoubleSided() const override final
    {
        return m_IsDoubleSided;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final;
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final;

private:
    friend class RadientSurfaceMaterialInstanceWriterImpl;

    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode   = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff   = 0.5f;
    Bool                          m_IsDoubleSided = False;
};

template <typename InterfaceType, typename InstanceType>
class RadientMaterialInstanceWriterBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    RadientMaterialInstanceWriterBase(IReferenceCounters* pRefCounters,
                                      InstanceType*       pInstance) :
        TBase{pRefCounters},
        m_pInstance{pInstance}
    {}

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetParameter(RadientMaterialParameterHandle Handle,
                                                           const void*                    pData,
                                                           Uint32                         DataSize) override final
    {
        if (!m_pInstance->IsValidHandle(Handle) ||
            IsTextureParameter(m_pInstance->m_Data.GetValue(Handle.Index).Type))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const MaterialParameterValue& InstanceValue = m_pInstance->m_Data.GetValue(Handle.Index);
        if (pData == nullptr || DataSize != InstanceValue.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        ChangeIterator    ChangeIt     = FindChange(Handle.Index, ValueArrayIndex);
        const bool        HasChange    = ChangeIt != m_Changes.end();
        const void* const pCurrentData = HasChange ?
            m_ValueData.data() + ChangeIt->DataOffset :
            InstanceValue.pData;
        if (std::memcmp(pCurrentData, pData, InstanceValue.Size) == 0)
            return RADIENT_STATUS_NO_CHANGE;

        if (HasChange)
        {
            std::memcpy(m_ValueData.data() + ChangeIt->DataOffset, pData, InstanceValue.Size);
            return RADIENT_STATUS_OK;
        }

        if (m_ValueData.size() > std::numeric_limits<Uint32>::max() - InstanceValue.Size)
        {
            LOG_ERROR_MESSAGE("Material instance writer value storage exceeds the supported size");
            return RADIENT_STATUS_FAILED;
        }

        try
        {
            const Uint32 DataOffset = static_cast<Uint32>(m_ValueData.size());
            m_Changes.reserve(m_Changes.size() + 1);
            m_ValueData.resize(m_ValueData.size() + InstanceValue.Size);
            std::memcpy(m_ValueData.data() + DataOffset, pData, InstanceValue.Size);
            m_Changes.push_back(ParameterChange{Handle.Index, ValueArrayIndex, DataOffset});
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to store a material instance parameter change: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }

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
        if (ArrayIndex >= InstanceValue.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        ChangeIterator              ChangeIt        = FindChange(Handle.Index, ArrayIndex);
        const bool                  HasChange       = ChangeIt != m_Changes.end();
        IRadientTextureAsset* const pCurrentTexture = HasChange ?
            m_TextureData[ChangeIt->DataOffset].RawPtr() :
            m_pInstance->m_Data.GetTexture(Handle.Index, ArrayIndex);
        if (pCurrentTexture == pTexture)
            return RADIENT_STATUS_NO_CHANGE;

        if (HasChange)
        {
            m_TextureData[ChangeIt->DataOffset] = pTexture;
            return RADIENT_STATUS_OK;
        }

        if (m_TextureData.size() >= std::numeric_limits<Uint32>::max())
        {
            LOG_ERROR_MESSAGE("Material instance writer texture storage exceeds the supported size");
            return RADIENT_STATUS_FAILED;
        }

        try
        {
            const Uint32 DataOffset = static_cast<Uint32>(m_TextureData.size());
            m_Changes.reserve(m_Changes.size() + 1);
            m_TextureData.emplace_back(pTexture);
            m_Changes.push_back(ParameterChange{Handle.Index, ArrayIndex, DataOffset});
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to store a material instance texture change: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }

        return RADIENT_STATUS_OK;
    }

protected:
    bool ApplyParameterChanges()
    {
        bool StateChanged = false;
        for (const ParameterChange& Change : m_Changes)
        {
            const MaterialParameterValue& InstanceValue = m_pInstance->m_Data.GetValue(Change.ParameterIndex);
            if (IsTextureParameter(InstanceValue.Type))
            {
                VERIFY_EXPR(Change.ArrayIndex != ValueArrayIndex);
                IRadientTextureAsset* const pTexture = m_TextureData[Change.DataOffset];
                if (m_pInstance->m_Data.GetTexture(Change.ParameterIndex, Change.ArrayIndex) != pTexture)
                {
                    m_pInstance->m_Data.SetTexture(Change.ParameterIndex, Change.ArrayIndex, pTexture);
                    StateChanged = true;
                }
            }
            else
            {
                VERIFY_EXPR(Change.ArrayIndex == ValueArrayIndex);
                const void* const pChangedData = m_ValueData.data() + Change.DataOffset;
                if (!m_pInstance->m_Data.HasSameValue(Change.ParameterIndex, pChangedData))
                {
                    m_pInstance->m_Data.CopyValue(Change.ParameterIndex, pChangedData);
                    StateChanged = true;
                }
            }
        }
        return StateChanged;
    }

    RADIENT_STATUS FinishCommit(bool StateChanged)
    {
        m_Changes.clear();
        m_ValueData.clear();
        m_TextureData.clear();

        if (!StateChanged)
            return RADIENT_STATUS_NO_CHANGE;

        ++m_pInstance->m_Version;
        return RADIENT_STATUS_OK;
    }

    InstanceType& GetInstance() noexcept
    {
        return *m_pInstance;
    }

private:
    static constexpr Uint32 ValueArrayIndex = ~Uint32{0};

    // Each pending complete value or texture element has one record. Offsets
    // remain valid when the backing value and texture arrays grow.
    struct ParameterChange
    {
        Uint32 ParameterIndex = 0;
        Uint32 ArrayIndex     = ValueArrayIndex;
        Uint32 DataOffset     = 0;
    };

    using ChangeList     = std::vector<ParameterChange>;
    using ChangeIterator = typename ChangeList::iterator;

    ChangeIterator FindChange(Uint32 ParameterIndex, Uint32 ArrayIndex) noexcept
    {
        return std::find_if(
            m_Changes.begin(), m_Changes.end(),
            [ParameterIndex, ArrayIndex](const ParameterChange& Change) {
                return Change.ParameterIndex == ParameterIndex && Change.ArrayIndex == ArrayIndex;
            });
    }

private:
    RefCntAutoPtr<InstanceType>                         m_pInstance;
    ChangeList                                          m_Changes;
    std::vector<Uint8>                                  m_ValueData;
    std::vector<PackedMaterialInstanceData::TexturePtr> m_TextureData;
};

class RadientMaterialInstanceWriterImpl final :
    public RadientMaterialInstanceWriterBase<IRadientMaterialInstanceWriter, RadientMaterialInstanceImpl>
{
public:
    using TBase = RadientMaterialInstanceWriterBase<IRadientMaterialInstanceWriter, RadientMaterialInstanceImpl>;

    RadientMaterialInstanceWriterImpl(IReferenceCounters*          pRefCounters,
                                      RadientMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters, pInstance}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstanceWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        return FinishCommit(ApplyParameterChanges());
    }
};

class RadientSurfaceMaterialInstanceWriterImpl final :
    public RadientMaterialInstanceWriterBase<IRadientSurfaceMaterialInstanceWriter, RadientSurfaceMaterialInstanceImpl>
{
public:
    using TBase = RadientMaterialInstanceWriterBase<IRadientSurfaceMaterialInstanceWriter, RadientSurfaceMaterialInstanceImpl>;

    RadientSurfaceMaterialInstanceWriterImpl(IReferenceCounters*                 pRefCounters,
                                             RadientSurfaceMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters, pInstance}
    {}

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientSurfaceMaterialInstanceWriter, IID_RadientMaterialInstanceWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) override final
    {
        if (SurfaceMode >= RADIENT_MATERIAL_SURFACE_MODE_COUNT)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        RadientSurfaceMaterialInstanceImpl& Instance = GetInstance();
        const RADIENT_MATERIAL_SURFACE_MODE CurrentMode =
            m_SurfaceModeChanged ? m_SurfaceMode : Instance.m_SurfaceMode;
        if (CurrentMode == SurfaceMode)
            return RADIENT_STATUS_NO_CHANGE;

        m_SurfaceMode        = SurfaceMode;
        m_SurfaceModeChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetAlphaCutoff(Float32 AlphaCutoff) override final
    {
        RadientSurfaceMaterialInstanceImpl& Instance = GetInstance();
        const Float32                       CurrentValue =
            m_AlphaCutoffChanged ? m_AlphaCutoff : Instance.m_AlphaCutoff;
        if (CurrentValue == AlphaCutoff)
            return RADIENT_STATUS_NO_CHANGE;

        m_AlphaCutoff        = AlphaCutoff;
        m_AlphaCutoffChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetDoubleSided(Bool DoubleSided) override final
    {
        DoubleSided                                  = DoubleSided != False ? True : False;
        RadientSurfaceMaterialInstanceImpl& Instance = GetInstance();
        const Bool                          CurrentValue =
            m_DoubleSidedChanged ? m_IsDoubleSided : Instance.m_IsDoubleSided;
        if (CurrentValue == DoubleSided)
            return RADIENT_STATUS_NO_CHANGE;

        m_IsDoubleSided      = DoubleSided;
        m_DoubleSidedChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        bool                                StateChanged = ApplyParameterChanges();
        RadientSurfaceMaterialInstanceImpl& Instance     = GetInstance();

        if (m_SurfaceModeChanged && Instance.m_SurfaceMode != m_SurfaceMode)
        {
            Instance.m_SurfaceMode = m_SurfaceMode;
            StateChanged           = true;
        }
        if (m_AlphaCutoffChanged && Instance.m_AlphaCutoff != m_AlphaCutoff)
        {
            Instance.m_AlphaCutoff = m_AlphaCutoff;
            StateChanged           = true;
        }
        if (m_DoubleSidedChanged && Instance.m_IsDoubleSided != m_IsDoubleSided)
        {
            Instance.m_IsDoubleSided = m_IsDoubleSided;
            StateChanged             = true;
        }

        m_SurfaceModeChanged = false;
        m_AlphaCutoffChanged = false;
        m_DoubleSidedChanged = false;
        return FinishCommit(StateChanged);
    }

private:
    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode        = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff        = 0.5f;
    Bool                          m_IsDoubleSided      = False;
    bool                          m_SurfaceModeChanged = false;
    bool                          m_AlphaCutoffChanged = false;
    bool                          m_DoubleSidedChanged = false;
};

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
                this)};
        *ppInstance = pInstance.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to clone Radient material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientSurfaceMaterialInstanceImpl::CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppWriter = nullptr;

    try
    {
        RefCntAutoPtr<RadientSurfaceMaterialInstanceWriterImpl> pWriter{
            MakeNewRCObj<RadientSurfaceMaterialInstanceWriterImpl>()(
                const_cast<RadientSurfaceMaterialInstanceImpl*>(this))};
        *ppWriter = pWriter.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient surface material instance writer: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientSurfaceMaterialInstanceImpl::Clone(IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    try
    {
        RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl> pInstance{
            MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(
                m_pDefinition,
                m_DefinitionHandle,
                this)};
        *ppInstance = pInstance.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to clone Radient surface material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

const PackedMaterialInstanceData& GetMaterialInstanceData(const IRadientMaterialInstance& Instance) noexcept
{
    if (Instance.GetDefinition()->GetDesc().Type == RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE)
        return static_cast<const RadientSurfaceMaterialInstanceImpl&>(Instance).GetPackedData();

    return static_cast<const RadientMaterialInstanceImpl&>(Instance).GetPackedData();
}

} // namespace

void RadientMaterialDefinitionImpl::WriteShaderData(
    const IRadientMaterialInstance& Instance,
    void*                           pData) const noexcept
{
    if (m_Data.PackingPlan.Size == 0)
        return;

    Uint8* const                                    pShaderData  = static_cast<Uint8*>(pData);
    const PackedMaterialInstanceData&               InstanceData = GetMaterialInstanceData(Instance);
    const RadientSurfaceMaterialInstanceImpl* const pSurfaceInstance =
        m_Data.GetDesc().Type == RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE ?
        &static_cast<const RadientSurfaceMaterialInstanceImpl&>(Instance) :
        nullptr;

    std::memset(pShaderData, 0, m_Data.PackingPlan.Size);
    for (Uint32 InitializationIndex = 0;
         InitializationIndex < m_Data.PackingPlan.InitializationCount;
         ++InitializationIndex)
    {
        const RadientMaterialShaderDataInitialization& Initialization =
            m_Data.PackingPlan.pInitializations[InitializationIndex];
        std::memcpy(pShaderData + Initialization.Offset,
                    Initialization.pData,
                    Initialization.Size);
    }

    const RadientSurfaceMaterialShaderParameterPacking* const pSurfacePacking =
        m_Data.PackingPlan.pSurfacePacking;
    if (pSurfacePacking != nullptr && pSurfacePacking->SurfaceModeOffset != ~Uint32{0})
    {
        VERIFY_EXPR(pSurfaceInstance != nullptr);
        const Uint32 SurfaceMode = static_cast<Uint32>(pSurfaceInstance->GetSurfaceMode());
        std::memcpy(pShaderData + pSurfacePacking->SurfaceModeOffset,
                    &SurfaceMode,
                    sizeof(SurfaceMode));
    }
    if (pSurfacePacking != nullptr && pSurfacePacking->AlphaCutoffOffset != ~Uint32{0})
    {
        VERIFY_EXPR(pSurfaceInstance != nullptr);
        const Float32 AlphaCutoff = pSurfaceInstance->GetAlphaCutoff();
        std::memcpy(pShaderData + pSurfacePacking->AlphaCutoffOffset,
                    &AlphaCutoff,
                    sizeof(AlphaCutoff));
    }

    for (Uint32 CommandIndex = 0; CommandIndex < m_Data.PackingPlan.CopyCommandCount; ++CommandIndex)
    {
        const ShaderDataCopyCommand& Command = m_Data.PackingPlan.pCopyCommands[CommandIndex];
        std::memcpy(pShaderData + Command.DestinationOffset,
                    InstanceData.GetValueData(Command.ParameterIndex),
                    Command.Size);
    }

    for (Uint32 CommandIndex = 0; CommandIndex < m_Data.PackingPlan.TextureCommandCount; ++CommandIndex)
    {
        const RadientMaterialShaderTexturePacking& Command =
            m_Data.PackingPlan.pTextureCommands[CommandIndex];

        const Int32 UVSelector =
            *static_cast<const Int32*>(InstanceData.GetValueData(Command.UVSelectorParameterIndex));
        const Uint32 WrapU =
            *static_cast<const Uint32*>(InstanceData.GetValueData(Command.WrapUParameterIndex));
        const Uint32 WrapV =
            *static_cast<const Uint32*>(InstanceData.GetValueData(Command.WrapVParameterIndex));
        ShaderTextureAttribs TextureAttribs{};
        TextureAttribs.SetUVSelector(UVSelector);
        TextureAttribs.SetWrapUMode(static_cast<TEXTURE_ADDRESS_MODE>(WrapU));
        TextureAttribs.SetWrapVMode(static_cast<TEXTURE_ADDRESS_MODE>(WrapV));
        std::memcpy(pShaderData + Command.Offset + ShaderTexturePackedPropsOffset,
                    &TextureAttribs.PackedProps,
                    sizeof(TextureAttribs.PackedProps));

        IRadientTextureAsset* const pTexture = InstanceData.GetTexture(Command.TextureParameterIndex, 0);
        if (pTexture != nullptr)
        {
            RadientTextureSamplingInfo SamplingInfo{};
            const bool                 SamplingInfoAvailable =
                RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo);
            VERIFY_EXPR(SamplingInfoAvailable);
            if (SamplingInfoAvailable)
            {
                std::memcpy(pShaderData + Command.Offset + ShaderTextureSliceOffset,
                            &SamplingInfo.TextureSlice,
                            sizeof(SamplingInfo.TextureSlice));
                std::memcpy(pShaderData + Command.Offset + ShaderTextureAtlasUVOffset,
                            &SamplingInfo.UVScaleBias,
                            sizeof(SamplingInfo.UVScaleBias));
            }
        }
    }
}

RADIENT_STATUS DILIGENT_CALL_TYPE RadientMaterialDefinitionImpl::CreateInstance(
    IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    const RADIENT_STATUS DefinitionStatus = GetStatus();
    if (RADIENT_FAILED(DefinitionStatus))
        return DefinitionStatus;

    try
    {
        switch (m_Data.GetDesc().Type)
        {
            case RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE:
            {
                RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl> pInstance{
                    MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(
                        const_cast<RadientMaterialDefinitionImpl*>(this),
                        m_DefinitionHandle)};
                *ppInstance = pInstance.Detach();
                return RADIENT_STATUS_OK;
            }

            case RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS:
            case RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE:
            {
                RefCntAutoPtr<RadientMaterialInstanceImpl> pInstance{
                    MakeNewRCObj<RadientMaterialInstanceImpl>()(
                        const_cast<RadientMaterialDefinitionImpl*>(this),
                        m_DefinitionHandle)};
                *ppInstance = pInstance.Detach();
                return RADIENT_STATUS_OK;
            }

            default:
                UNEXPECTED("Unexpected material definition type");
                return RADIENT_STATUS_INVALID_OPERATION;
        }
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientMaterialAssetManager::CreateDefinition(const RadientMaterialDefinitionDesc& DefinitionDesc,
                                                             IRadientMaterialDefinition**         ppDefinition)
{
    return CreateDefinition(DefinitionDesc, {}, ppDefinition);
}

RADIENT_STATUS RadientMaterialAssetManager::CreateDefinition(
    const RadientMaterialDefinitionDesc&       DefinitionDesc,
    const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout,
    IRadientMaterialDefinition**               ppDefinition)
{
    if (ppDefinition == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppDefinition = nullptr;

    try
    {
        const RADIENT_STATUS ValidationStatus = ValidateMaterialDefinitionDesc(DefinitionDesc);
        if (ValidationStatus != RADIENT_STATUS_OK)
            return ValidationStatus;

        const RADIENT_STATUS LayoutValidationStatus =
            ValidateMaterialShaderDataLayout(DefinitionDesc, ShaderDataLayout);
        if (LayoutValidationStatus != RADIENT_STATUS_OK)
            return LayoutValidationStatus;

        RefCntAutoPtr<RadientMaterialDefinitionImpl> pDefinition{
            MakeNewRCObj<RadientMaterialDefinitionImpl>()(DefinitionDesc, ShaderDataLayout)};
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

} // namespace Diligent
