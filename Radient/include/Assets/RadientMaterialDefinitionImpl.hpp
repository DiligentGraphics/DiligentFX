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

// Internal implementation identity used by the material asset manager.
// {CD2E807C-E8F5-411B-B7F1-F4E741F2AB58}
static constexpr INTERFACE_ID IID_MaterialDefinitionImpl =
    {0xcd2e807c, 0xe8f5, 0x411b, {0xb7, 0xf1, 0xf4, 0xe7, 0x41, 0xf2, 0xab, 0x58}};

struct RadientMaterialShaderDataLayoutDesc;

namespace RadientMaterialDetail
{

bool IsTextureParameter(RADIENT_MATERIAL_PARAMETER_TYPE Type) noexcept;

bool GetMaterialParameterDataSize(const RadientMaterialParameterDesc& Desc,
                                  Uint32&                             DataSize) noexcept;

RADIENT_STATUS ValidateMaterialDefinitionDesc(const RadientMaterialDefinitionDesc& Desc);

RADIENT_STATUS ValidateMaterialShaderDataLayout(
    const RadientMaterialDefinitionDesc&       DefinitionDesc,
    const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout);

} // namespace RadientMaterialDetail

/// Maps one complete non-texture material parameter to its byte offset in the
/// shader-readable material data block.
struct RadientMaterialShaderParameterPacking
{
    Uint32 ParameterIndex = 0;
    Uint32 Offset         = 0;
};

/// Packs one texture parameter and its UV selector and address modes into a
/// shader texture-attribute record. UV transform and bias parameters use
/// ordinary RadientMaterialShaderParameterPacking mappings.
struct RadientMaterialShaderTexturePacking
{
    Uint32 TextureParameterIndex    = 0;
    Uint32 UVSelectorParameterIndex = 0;
    Uint32 WrapUParameterIndex      = 0;
    Uint32 WrapVParameterIndex      = 0;
    Uint32 Offset                   = 0;
};

/// Initializes a fixed byte range in the shader-readable material data block.
/// Initializations are applied in declaration order before material parameter
/// and texture packing.
struct RadientMaterialShaderDataInitialization
{
    const void* pData  = nullptr;
    Uint32      Size   = 0;
    Uint32      Offset = 0;
};

/// Describes where specialized surface material properties are packed
/// in shader-readable data.
struct RadientSurfaceMaterialShaderParameterPacking
{
    /// Optional byte offset at which the surface mode is written.
    Uint32 SurfaceModeOffset = ~Uint32{0};

    /// Optional byte offset at which the surface alpha cutoff is written.
    Uint32 AlphaCutoffOffset = ~Uint32{0};
};

/// Describes the immutable shader-readable data layout owned by a material
/// definition. The definition copies the mappings during creation.
struct RadientMaterialShaderDataLayoutDesc
{
    Uint32 Size = 0;

    const RadientMaterialShaderParameterPacking* pMappings    = nullptr;
    Uint32                                       MappingCount = 0;

    const RadientMaterialShaderTexturePacking* pTexturePackings    = nullptr;
    Uint32                                     TexturePackingCount = 0;

    const RadientMaterialShaderDataInitialization* pInitializations    = nullptr;
    Uint32                                         InitializationCount = 0;

    /// Optional surface-material-specific parameter packing. The definition
    /// copies the packing during creation.
    const RadientSurfaceMaterialShaderParameterPacking* pSurfacePacking = nullptr;
};

class RadientMaterialDefinitionImpl final : public ObjectBase<IRadientMaterialDefinitionAsset>
{
public:
    using TBase = ObjectBase<IRadientMaterialDefinitionAsset>;

    RadientMaterialDefinitionImpl(IReferenceCounters*                        pRefCounters,
                                  const RadientMaterialDefinitionDesc&       Desc,
                                  const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout = {});

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientMaterialDefinitionAsset ||
            IID == IID_RadientAsset ||
            IID == IID_MaterialDefinitionImpl)
        {
            *ppInterface = this;
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Data.GetDesc().Reference;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_MATERIAL_DEFINITION;
    }

    virtual const RadientMaterialDefinitionDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Data.GetDesc();
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetStatus() const override final
    {
        return RADIENT_STATUS_OK;
    }

    virtual Uint32 DILIGENT_CALL_TYPE GetParameterCount() const override final
    {
        return m_Data.GetDesc().ParameterCount;
    }

    virtual const RadientMaterialParameterDesc& DILIGENT_CALL_TYPE GetParameterDesc(Uint32 Index) const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameterHandle(Uint32                          Index,
                                                                 RadientMaterialParameterHandle* pHandle) const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE FindParameter(const Char*                     Name,
                                                            RadientMaterialParameterHandle* pHandle) const override final;

    Uint32 GetShaderDataSize() const noexcept
    {
        return m_Data.PackingPlan.Size;
    }

    /// Writes the complete shader-readable data block for Material. Material
    /// must have been created by this definition, and pData must reference at
    /// least GetShaderDataSize() bytes. Padding and unmapped bytes are set to
    /// zero. Definition-owned initializations are applied before material
    /// parameters. Non-null texture parameters must have initialized sampling
    /// data.
    void WriteShaderData(const IRadientMaterialAsset& Material,
                         void*                        pData) const noexcept;

    RadientHandle GetDefinitionHandle() const noexcept
    {
        return m_DefinitionHandle;
    }

private:
    struct ShaderDataCopyCommand
    {
        Uint32 ParameterIndex    = 0;
        Uint32 DestinationOffset = 0;
        Uint32 Size              = 0;
    };

    struct ShaderDataPackingPlan
    {
        Uint32                                              Size                = 0;
        Uint32                                              InitializationCount = 0;
        const RadientMaterialShaderDataInitialization*      pInitializations    = nullptr;
        Uint32                                              CopyCommandCount    = 0;
        const ShaderDataCopyCommand*                        pCopyCommands       = nullptr;
        Uint32                                              TextureCommandCount = 0;
        const RadientMaterialShaderTexturePacking*          pTextureCommands    = nullptr;
        const RadientSurfaceMaterialShaderParameterPacking* pSurfacePacking     = nullptr;
    };

    // Parameter descriptors, strings, default values, and shader data packing
    // commands reside in Memory. Default texture pointers are retained directly
    // by their descriptors.
    struct PackedData
    {
        PackedData(void* pData, IMemoryAllocator& Allocator) :
            Memory{pData, STDDeleterRawMem<void>{Allocator}}
        {}

        PackedData(PackedData&& Other) noexcept :
            Memory{std::move(Other.Memory)},
            pDesc{Other.pDesc},
            PackingPlan{Other.PackingPlan}
        {
            Other.pDesc       = nullptr;
            Other.PackingPlan = {};
        }

        PackedData(const PackedData&)            = delete;
        PackedData& operator=(const PackedData&) = delete;
        PackedData& operator=(PackedData&&)      = delete;

        ~PackedData()
        {
            if (pDesc == nullptr)
                return;

            for (Uint32 Index = 0; Index < pDesc->ParameterCount; ++Index)
            {
                if (pDesc->pParameters[Index].pDefaultTexture != nullptr)
                    pDesc->pParameters[Index].pDefaultTexture->Release();
            }
        }

        const RadientMaterialDefinitionDesc& GetDesc() const noexcept
        {
            VERIFY_EXPR(pDesc != nullptr);
            return *pDesc;
        }

        std::unique_ptr<void, STDDeleterRawMem<void>> Memory;
        const RadientMaterialDefinitionDesc*          pDesc = nullptr;
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
