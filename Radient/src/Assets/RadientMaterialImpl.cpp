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

#include "DebugUtilities.hpp"
#include "DynamicBitSet.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <atomic>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

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

} // namespace

RadientMaterialDefinitionImpl::PackedData RadientMaterialDefinitionImpl::PackData(
    const RadientMaterialDefinitionDesc& Desc)
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

    PackedData           Data{pMemory, GetRawAllocator()};
    FixedLinearAllocator Writer{pMemory, MemorySize};

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
    if (Writer.GetReservedSize() != 0)
        VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
}

} // namespace

RadientMaterialDefinitionImpl::RadientMaterialDefinitionImpl(
    IReferenceCounters*                  pRefCounters,
    const RadientMaterialDefinitionDesc& Desc) :
    TBase{pRefCounters},
    m_Data{PackData(Desc)},
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

const RadientMaterialParameterDesc& DILIGENT_CALL_TYPE RadientMaterialDefinitionImpl::GetParameterDesc(Uint32 Index) const
{
    if (Index >= m_Data.Desc.ParameterCount)
    {
        UNEXPECTED("Material parameter index ", Index, " is out of range");
        static constexpr RadientMaterialParameterDesc InvalidDesc{};
        return InvalidDesc;
    }
    return m_Data.Desc.pParameters[Index];
}

RADIENT_STATUS DILIGENT_CALL_TYPE RadientMaterialDefinitionImpl::GetParameterHandle(
    Uint32                          Index,
    RadientMaterialParameterHandle* pHandle) const
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
        return Handle.Definition == m_DefinitionHandle &&
            Handle.Index < m_Data.GetValueCount() &&
            Handle.Reserved == 0;
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

} // namespace Diligent
