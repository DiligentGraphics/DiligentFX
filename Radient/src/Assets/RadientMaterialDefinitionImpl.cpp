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

#include "Assets/RadientMaterialDefinitionImpl.hpp"

#include "Assets/RadientMaterialAssetFactory.hpp"
#include "Assets/RadientMaterialStorage.hpp"
#include "Assets/RadientTextureAssetManager.hpp"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "GLTFLoader.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace Diligent
{

namespace RadientMaterialDetail
{

namespace
{

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

} // namespace

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

} // namespace RadientMaterialDetail

using RadientMaterialDetail::GetMaterialParameterDataSize;
using RadientMaterialDetail::IsTextureParameter;

namespace
{

std::atomic<RadientHandle> s_NextMaterialDefinitionHandle{1};

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

} // namespace

RADIENT_STATUS RadientMaterialDetail::ValidateMaterialDefinitionDesc(const RadientMaterialDefinitionDesc& Desc)
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

namespace
{

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

} // namespace

RADIENT_STATUS RadientMaterialDetail::ValidateMaterialShaderDataLayout(
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
    // other and ranges subsequently written from material parameters.
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

RefCntAutoPtr<IRadientMaterialAsset> RadientMaterialDefinitionImpl::CreateAsset()
{
    switch (m_Data.GetDesc().Type)
    {
        case RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE:
            return RadientMaterialDetail::MakeSurfaceMaterialAsset(this, m_DefinitionHandle);

        case RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS:
        case RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE:
            return RadientMaterialDetail::MakeGenericMaterialAsset(this, m_DefinitionHandle);

        default:
            UNEXPECTED("Unexpected material definition type");
            return {};
    }
}

void RadientMaterialDefinitionImpl::WriteShaderData(
    const IRadientMaterialAsset& Material,
    void*                        pData) const noexcept
{
    if (m_Data.PackingPlan.Size == 0)
        return;

    Uint8* const                              pShaderData = static_cast<Uint8*>(pData);
    const bool                                IsSurface   = m_Data.GetDesc().Type == RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE;
    const IRadientSurfaceMaterialAsset* const pSurfaceMaterial =
        IsSurface ? &static_cast<const IRadientSurfaceMaterialAsset&>(Material) : nullptr;
    const RadientMaterialDetail::MaterialStorage* const pStorage =
        RadientMaterialDetail::TryGetMaterialStorage(&Material);
    VERIFY_EXPR(pStorage != nullptr);
    if (pStorage == nullptr)
        return;
    const RadientMaterialDetail::PackedMaterialData& MaterialData =
        pStorage->GetPackedData();

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
        VERIFY_EXPR(pSurfaceMaterial != nullptr);
        const Uint32 SurfaceMode = static_cast<Uint32>(pSurfaceMaterial->GetSurfaceMode());
        std::memcpy(pShaderData + pSurfacePacking->SurfaceModeOffset,
                    &SurfaceMode,
                    sizeof(SurfaceMode));
    }
    if (pSurfacePacking != nullptr && pSurfacePacking->AlphaCutoffOffset != ~Uint32{0})
    {
        VERIFY_EXPR(pSurfaceMaterial != nullptr);
        const Float32 AlphaCutoff = pSurfaceMaterial->GetAlphaCutoff();
        std::memcpy(pShaderData + pSurfacePacking->AlphaCutoffOffset,
                    &AlphaCutoff,
                    sizeof(AlphaCutoff));
    }

    for (Uint32 CommandIndex = 0; CommandIndex < m_Data.PackingPlan.CopyCommandCount; ++CommandIndex)
    {
        const ShaderDataCopyCommand& Command = m_Data.PackingPlan.pCopyCommands[CommandIndex];
        std::memcpy(pShaderData + Command.DestinationOffset,
                    MaterialData.GetValueData(Command.ParameterIndex),
                    Command.Size);
    }

    for (Uint32 CommandIndex = 0; CommandIndex < m_Data.PackingPlan.TextureCommandCount; ++CommandIndex)
    {
        const RadientMaterialShaderTexturePacking& Command =
            m_Data.PackingPlan.pTextureCommands[CommandIndex];

        const Int32 UVSelector =
            *static_cast<const Int32*>(MaterialData.GetValueData(Command.UVSelectorParameterIndex));
        const Uint32 WrapU =
            *static_cast<const Uint32*>(MaterialData.GetValueData(Command.WrapUParameterIndex));
        const Uint32 WrapV =
            *static_cast<const Uint32*>(MaterialData.GetValueData(Command.WrapVParameterIndex));
        ShaderTextureAttribs TextureAttribs{};
        TextureAttribs.SetUVSelector(UVSelector);
        TextureAttribs.SetWrapUMode(static_cast<TEXTURE_ADDRESS_MODE>(WrapU));
        TextureAttribs.SetWrapVMode(static_cast<TEXTURE_ADDRESS_MODE>(WrapV));
        std::memcpy(pShaderData + Command.Offset + ShaderTexturePackedPropsOffset,
                    &TextureAttribs.PackedProps,
                    sizeof(TextureAttribs.PackedProps));

        IRadientTextureAsset* const pTexture = MaterialData.GetTexture(Command.TextureParameterIndex, 0);
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

} // namespace Diligent
