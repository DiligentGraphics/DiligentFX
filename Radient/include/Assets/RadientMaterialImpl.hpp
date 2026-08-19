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

#pragma once

#include "RadientMaterials.h"

#include "HashUtils.hpp"
#include "ObjectBase.hpp"
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

#include <memory>
#include <utility>

namespace Diligent
{

/// Maps one complete non-texture material parameter to its byte offset in the
/// shader-readable material data block.
struct RadientMaterialShaderParameterPacking
{
    Uint32 ParameterIndex = 0;
    Uint32 Offset         = 0;
};

/// Describes the immutable shader-readable data layout owned by a material
/// definition. The definition copies the mappings during creation.
struct RadientMaterialShaderDataLayoutDesc
{
    Uint32 Size = 0;

    const RadientMaterialShaderParameterPacking* pMappings    = nullptr;
    Uint32                                       MappingCount = 0;
};

class RadientMaterialDefinitionImpl final : public ObjectBase<IRadientMaterialDefinition>
{
public:
    using TBase = ObjectBase<IRadientMaterialDefinition>;

    RadientMaterialDefinitionImpl(IReferenceCounters*                        pRefCounters,
                                  const RadientMaterialDefinitionDesc&       Desc,
                                  const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout = {});

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

    virtual const RadientMaterialParameterDesc& DILIGENT_CALL_TYPE GetParameterDesc(Uint32 Index) const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameterHandle(Uint32                          Index,
                                                                 RadientMaterialParameterHandle* pHandle) const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE FindParameter(const Char*                     Name,
                                                            RadientMaterialParameterHandle* pHandle) const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateInstance(IRadientMaterialInstance** ppInstance) const override final;

    Uint32 GetShaderDataSize() const noexcept
    {
        return m_Data.PackingPlan.Size;
    }

    /// Writes the complete shader-readable data block for Instance. Instance
    /// must have been created by this definition, and pData must reference at
    /// least GetShaderDataSize() bytes. Padding and unmapped bytes are set to zero.
    void WriteShaderData(const IRadientMaterialInstance& Instance,
                         void*                           pData) const noexcept;

private:
    struct ShaderDataCopyCommand
    {
        Uint32 ParameterIndex    = 0;
        Uint32 DestinationOffset = 0;
        Uint32 Size              = 0;
    };

    struct ShaderDataPackingPlan
    {
        Uint32                       Size             = 0;
        Uint32                       CopyCommandCount = 0;
        const ShaderDataCopyCommand* pCopyCommands    = nullptr;
    };

    // Parameter descriptors, strings, default values, and shader data copy
    // commands reside in Memory. Default texture pointers are retained directly
    // by their descriptors.
    struct PackedData
    {
        PackedData(void* pData, IMemoryAllocator& Allocator) :
            Memory{pData, STDDeleterRawMem<void>{Allocator}}
        {}

        PackedData(PackedData&& Other) noexcept :
            Memory{std::move(Other.Memory)},
            Desc{Other.Desc},
            PackingPlan{Other.PackingPlan}
        {
            Other.Desc        = {};
            Other.PackingPlan = {};
        }

        PackedData(const PackedData&)            = delete;
        PackedData& operator=(const PackedData&) = delete;
        PackedData& operator=(PackedData&&)      = delete;

        ~PackedData()
        {
            for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
            {
                if (Desc.pParameters[Index].pDefaultTexture != nullptr)
                    Desc.pParameters[Index].pDefaultTexture->Release();
            }
        }

        std::unique_ptr<void, STDDeleterRawMem<void>> Memory;
        RadientMaterialDefinitionDesc                 Desc;
        ShaderDataPackingPlan                         PackingPlan;
    };

    static PackedData PackData(const RadientMaterialDefinitionDesc&       Desc,
                               const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout);

    using ParameterIndexMap = absl::flat_hash_map<const Char*, Uint32, CStringHash<Char>, CStringCompare<Char>>;

    const PackedData m_Data;
    // Map keys reference parameter names packed in m_Data. Declaring the map
    // after m_Data ensures that the keys are destroyed before their strings.
    ParameterIndexMap   m_ParameterIndices;
    const RadientHandle m_DefinitionHandle;
};

} // namespace Diligent
