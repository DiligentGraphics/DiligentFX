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

#include "Assets/RadientMaterialInstanceImpl.hpp"

#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>

namespace Diligent
{

namespace RadientMaterialDetail
{

struct PackedMaterialInstanceData::MaterialParameterValue
{
    // Size is the byte size for value parameters and the element count for textures.
    RADIENT_MATERIAL_PARAMETER_TYPE Type  = RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN;
    Uint32                          Size  = 0;
    void*                           pData = nullptr;
};

PackedMaterialInstanceData::~PackedMaterialInstanceData()
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

Uint32 PackedMaterialInstanceData::GetValueCount() const noexcept
{
    return m_ValueCount;
}

RADIENT_MATERIAL_PARAMETER_TYPE PackedMaterialInstanceData::GetValueType(Uint32 Index) const noexcept
{
    return GetValue(Index).Type;
}

Uint32 PackedMaterialInstanceData::GetValueSize(Uint32 Index) const noexcept
{
    return GetValue(Index).Size;
}

const void* PackedMaterialInstanceData::GetValueData(Uint32 Index) const noexcept
{
    return GetValue(Index).pData;
}

bool PackedMaterialInstanceData::HasSameValue(Uint32 Index, const void* pData) const noexcept
{
    const MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(!IsTextureParameter(Value.Type));
    return std::memcmp(Value.pData, pData, Value.Size) == 0;
}

void PackedMaterialInstanceData::CopyValue(Uint32 Index, const void* pData) noexcept
{
    MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(!IsTextureParameter(Value.Type));
    std::memcpy(Value.pData, pData, Value.Size);
}

IRadientTextureAsset* PackedMaterialInstanceData::GetTexture(Uint32 Index, Uint32 ArrayIndex) const noexcept
{
    const MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
    return static_cast<const TexturePtr*>(Value.pData)[ArrayIndex];
}

void PackedMaterialInstanceData::SetTexture(Uint32                Index,
                                            Uint32                ArrayIndex,
                                            IRadientTextureAsset* pTexture) noexcept
{
    MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
    static_cast<TexturePtr*>(Value.pData)[ArrayIndex] = pTexture;
}

PackedMaterialInstanceData::MaterialParameterValue& PackedMaterialInstanceData::GetValue(Uint32 Index) noexcept
{
    VERIFY_EXPR(Index < m_ValueCount);
    return m_pValues[Index];
}

const PackedMaterialInstanceData::MaterialParameterValue& PackedMaterialInstanceData::GetValue(Uint32 Index) const noexcept
{
    VERIFY_EXPR(Index < m_ValueCount);
    return m_pValues[Index];
}

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
            Allocator.AddSpace<TexturePtr>(Parameter.ArraySize);
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
            auto* const pTextures = Writer.ConstructArray<TexturePtr>(Parameter.ArraySize);
            Value.pData           = pTextures;
            Value.Size            = Parameter.ArraySize;
            ++m_ValueCount;

            if (pSource != nullptr)
            {
                const MaterialParameterValue& SourceValue = pSource->GetValue(Index);
                VERIFY_EXPR(SourceValue.Type == Value.Type && SourceValue.Size == Value.Size);
                const auto* const pSourceTextures = static_cast<const TexturePtr*>(SourceValue.pData);
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

MaterialInstanceState::MaterialInstanceState(IRadientMaterialDefinitionAsset* pDefinition,
                                             RadientHandle                    DefinitionHandle,
                                             const MaterialInstanceState*     pSource) :
    m_pDefinition{pDefinition},
    m_DefinitionHandle{DefinitionHandle},
    m_Data{pDefinition->GetDesc(), pSource != nullptr ? &pSource->m_Data : nullptr}
{}

IRadientMaterialDefinitionAsset* MaterialInstanceState::GetDefinition() const noexcept
{
    return m_pDefinition;
}

RadientHandle MaterialInstanceState::GetDefinitionHandle() const noexcept
{
    return m_DefinitionHandle;
}

Uint64 MaterialInstanceState::GetVersion() const noexcept
{
    return m_Version;
}

RADIENT_STATUS MaterialInstanceState::GetParameter(RadientMaterialParameterHandle Handle,
                                                   void*                          pData,
                                                   Uint32                         DataSize) const
{
    if (!IsValidHandle(Handle))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (IsTextureParameter(m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_OPERATION;

    if (pData == nullptr || DataSize != m_Data.GetValueSize(Handle.Index))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::memcpy(pData, m_Data.GetValueData(Handle.Index), DataSize);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS MaterialInstanceState::GetTexture(RadientMaterialParameterHandle Handle,
                                                 Uint32                         ArrayIndex,
                                                 IRadientTextureAsset**         ppTexture) const
{
    if (ppTexture == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppTexture = nullptr;

    if (!IsValidHandle(Handle))
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (!IsTextureParameter(m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_OPERATION;

    if (ArrayIndex >= m_Data.GetValueSize(Handle.Index))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppTexture = m_Data.GetTexture(Handle.Index, ArrayIndex);
    if (*ppTexture != nullptr)
        (*ppTexture)->AddRef();
    return RADIENT_STATUS_OK;
}

const PackedMaterialInstanceData& MaterialInstanceState::GetPackedData() const noexcept
{
    return m_Data;
}

bool MaterialInstanceState::IsValidHandle(RadientMaterialParameterHandle Handle) const noexcept
{
    return Handle.Definition == m_DefinitionHandle &&
        Handle.Index < m_Data.GetValueCount() &&
        Handle.Reserved == 0;
}

MaterialInstanceWriterState::MaterialInstanceWriterState(IRadientMaterialInstance* pOwner,
                                                         MaterialInstanceState&    Instance) noexcept :
    m_pOwner{pOwner},
    m_Instance{Instance}
{}

RADIENT_STATUS MaterialInstanceWriterState::SetParameter(RadientMaterialParameterHandle Handle,
                                                         const void*                    pData,
                                                         Uint32                         DataSize)
{
    if (!m_Instance.IsValidHandle(Handle) ||
        IsTextureParameter(m_Instance.m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const Uint32 InstanceValueSize = m_Instance.m_Data.GetValueSize(Handle.Index);
    if (pData == nullptr || DataSize != InstanceValueSize)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ChangeIterator    ChangeIt     = FindChange(Handle.Index, ValueArrayIndex);
    const bool        HasChange    = ChangeIt != m_Changes.end();
    const void* const pCurrentData = HasChange ?
        m_ValueData.data() + ChangeIt->DataOffset :
        m_Instance.m_Data.GetValueData(Handle.Index);
    if (std::memcmp(pCurrentData, pData, InstanceValueSize) == 0)
        return RADIENT_STATUS_NO_CHANGE;

    if (HasChange)
    {
        std::memcpy(m_ValueData.data() + ChangeIt->DataOffset, pData, InstanceValueSize);
        return RADIENT_STATUS_OK;
    }

    if (m_ValueData.size() > std::numeric_limits<Uint32>::max() - InstanceValueSize)
    {
        LOG_ERROR_MESSAGE("Material instance writer value storage exceeds the supported size");
        return RADIENT_STATUS_FAILED;
    }

    try
    {
        const Uint32 DataOffset = static_cast<Uint32>(m_ValueData.size());
        m_Changes.reserve(m_Changes.size() + 1);
        m_ValueData.resize(m_ValueData.size() + InstanceValueSize);
        std::memcpy(m_ValueData.data() + DataOffset, pData, InstanceValueSize);
        m_Changes.push_back(ParameterChange{Handle.Index, ValueArrayIndex, DataOffset});
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to store a material instance parameter change: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS MaterialInstanceWriterState::SetTexture(RadientMaterialParameterHandle Handle,
                                                       Uint32                         ArrayIndex,
                                                       IRadientTextureAsset*          pTexture)
{
    if (!m_Instance.IsValidHandle(Handle) ||
        !IsTextureParameter(m_Instance.m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (ArrayIndex >= m_Instance.m_Data.GetValueSize(Handle.Index))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ChangeIterator              ChangeIt        = FindChange(Handle.Index, ArrayIndex);
    const bool                  HasChange       = ChangeIt != m_Changes.end();
    IRadientTextureAsset* const pCurrentTexture = HasChange ?
        m_TextureData[ChangeIt->DataOffset].RawPtr() :
        m_Instance.m_Data.GetTexture(Handle.Index, ArrayIndex);
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

bool MaterialInstanceWriterState::ApplyParameterChanges()
{
    bool StateChanged = false;
    for (const ParameterChange& Change : m_Changes)
    {
        if (IsTextureParameter(m_Instance.m_Data.GetValueType(Change.ParameterIndex)))
        {
            VERIFY_EXPR(Change.ArrayIndex != ValueArrayIndex);
            IRadientTextureAsset* const pTexture = m_TextureData[Change.DataOffset];
            if (m_Instance.m_Data.GetTexture(Change.ParameterIndex, Change.ArrayIndex) != pTexture)
            {
                m_Instance.m_Data.SetTexture(Change.ParameterIndex, Change.ArrayIndex, pTexture);
                StateChanged = true;
            }
        }
        else
        {
            VERIFY_EXPR(Change.ArrayIndex == ValueArrayIndex);
            const void* const pChangedData = m_ValueData.data() + Change.DataOffset;
            if (!m_Instance.m_Data.HasSameValue(Change.ParameterIndex, pChangedData))
            {
                m_Instance.m_Data.CopyValue(Change.ParameterIndex, pChangedData);
                StateChanged = true;
            }
        }
    }
    return StateChanged;
}

RADIENT_STATUS MaterialInstanceWriterState::FinishCommit(bool StateChanged)
{
    m_Changes.clear();
    m_ValueData.clear();
    m_TextureData.clear();

    if (!StateChanged)
        return RADIENT_STATUS_NO_CHANGE;

    ++m_Instance.m_Version;
    return RADIENT_STATUS_OK;
}

MaterialInstanceWriterState::ChangeIterator MaterialInstanceWriterState::FindChange(
    Uint32 ParameterIndex,
    Uint32 ArrayIndex) noexcept
{
    return std::find_if(
        m_Changes.begin(), m_Changes.end(),
        [ParameterIndex, ArrayIndex](const ParameterChange& Change) {
            return Change.ParameterIndex == ParameterIndex && Change.ArrayIndex == ArrayIndex;
        });
}

} // namespace RadientMaterialDetail

namespace
{

using namespace RadientMaterialDetail;

class RadientMaterialInstanceWriterImpl;

class RadientMaterialInstanceImpl final : public ObjectBase<IRadientMaterialInstance>
{
public:
    using TBase = ObjectBase<IRadientMaterialInstance>;

    RadientMaterialInstanceImpl(IReferenceCounters*                pRefCounters,
                                IRadientMaterialDefinitionAsset*   pDefinition,
                                RadientHandle                      DefinitionHandle,
                                const RadientMaterialInstanceImpl* pSource = nullptr) :
        TBase{pRefCounters},
        m_State{pDefinition, DefinitionHandle, pSource != nullptr ? &pSource->m_State : nullptr}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstance, TBase)

    virtual IRadientMaterialDefinitionAsset* DILIGENT_CALL_TYPE GetDefinition() const override final
    {
        return m_State.GetDefinition();
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_State.GetVersion();
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameter(RadientMaterialParameterHandle Handle,
                                                           void*                          pData,
                                                           Uint32                         DataSize) const override final
    {
        return m_State.GetParameter(Handle, pData, DataSize);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset**         ppTexture) const override final
    {
        return m_State.GetTexture(Handle, ArrayIndex, ppTexture);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final;
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final;

    const MaterialInstanceState& GetState() const noexcept
    {
        return m_State;
    }

private:
    friend class RadientMaterialInstanceWriterImpl;

    MaterialInstanceState m_State;
};

class RadientMaterialInstanceWriterImpl final : public ObjectBase<IRadientMaterialInstanceWriter>
{
public:
    using TBase = ObjectBase<IRadientMaterialInstanceWriter>;

    RadientMaterialInstanceWriterImpl(IReferenceCounters*          pRefCounters,
                                      RadientMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters},
        m_State{pInstance, pInstance->m_State}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstanceWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetParameter(RadientMaterialParameterHandle Handle,
                                                           const void*                    pData,
                                                           Uint32                         DataSize) override final
    {
        return m_State.SetParameter(Handle, pData, DataSize);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset*          pTexture) override final
    {
        return m_State.SetTexture(Handle, ArrayIndex, pTexture);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        return m_State.FinishCommit(m_State.ApplyParameterChanges());
    }

private:
    MaterialInstanceWriterState m_State;
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
                m_State.GetDefinition(),
                m_State.GetDefinitionHandle(),
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

} // namespace

RefCntAutoPtr<IRadientMaterialInstance> RadientMaterialDetail::MakeMaterialInstance(
    IRadientMaterialDefinitionAsset* pDefinition,
    RadientHandle                    DefinitionHandle)
{
    return RefCntAutoPtr<RadientMaterialInstanceImpl>{
        MakeNewRCObj<RadientMaterialInstanceImpl>()(pDefinition, DefinitionHandle)};
}

const RadientMaterialDetail::PackedMaterialInstanceData& RadientMaterialDetail::GetMaterialInstanceData(
    const IRadientMaterialInstance& Instance) noexcept
{
    return static_cast<const RadientMaterialInstanceImpl&>(Instance).GetState().GetPackedData();
}

} // namespace Diligent
