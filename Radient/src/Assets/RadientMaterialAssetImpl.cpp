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

#include "Assets/RadientMaterialAssetFactory.hpp"
#include "Assets/RadientMaterialStorage.hpp"

#include "RadientMaterialAssetImplBase.hpp"

#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

namespace Diligent
{

namespace RadientMaterialDetail
{

struct PackedMaterialData::MaterialParameterValue
{
    RADIENT_MATERIAL_PARAMETER_TYPE Type = RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN;
    // Size is the byte size for value parameters and the element count for textures.
    Uint32 Size  = 0;
    void*  pData = nullptr;
};

PackedMaterialData::~PackedMaterialData()
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

Uint32 PackedMaterialData::GetValueCount() const noexcept
{
    return m_ValueCount;
}

RADIENT_MATERIAL_PARAMETER_TYPE PackedMaterialData::GetValueType(Uint32 Index) const noexcept
{
    return GetValue(Index).Type;
}

Uint32 PackedMaterialData::GetValueSize(Uint32 Index) const noexcept
{
    return GetValue(Index).Size;
}

const void* PackedMaterialData::GetValueData(Uint32 Index) const noexcept
{
    return GetValue(Index).pData;
}

bool PackedMaterialData::HasSameValue(Uint32 Index, const void* pData) const noexcept
{
    const MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(!IsTextureParameter(Value.Type));
    return std::memcmp(Value.pData, pData, Value.Size) == 0;
}

void PackedMaterialData::CopyValue(Uint32 Index, const void* pData) noexcept
{
    MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(!IsTextureParameter(Value.Type));
    std::memcpy(Value.pData, pData, Value.Size);
}

IRadientTextureAsset* PackedMaterialData::GetTexture(Uint32 Index, Uint32 ArrayIndex) const noexcept
{
    const MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
    return static_cast<const TexturePtr*>(Value.pData)[ArrayIndex];
}

void PackedMaterialData::SetTexture(Uint32                Index,
                                    Uint32                ArrayIndex,
                                    IRadientTextureAsset* pTexture) noexcept
{
    MaterialParameterValue& Value = GetValue(Index);
    VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
    static_cast<TexturePtr*>(Value.pData)[ArrayIndex] = pTexture;
}

PackedMaterialData::MaterialParameterValue& PackedMaterialData::GetValue(Uint32 Index) noexcept
{
    VERIFY_EXPR(Index < m_ValueCount);
    return m_pValues[Index];
}

const PackedMaterialData::MaterialParameterValue& PackedMaterialData::GetValue(Uint32 Index) const noexcept
{
    VERIFY_EXPR(Index < m_ValueCount);
    return m_pValues[Index];
}

PackedMaterialData::PackedMaterialData(const RadientMaterialDefinitionDesc& Desc)
{
    FixedLinearAllocator Allocator{GetRawAllocator()};
    Allocator.AddSpace<MaterialParameterValue>(Desc.ParameterCount);
    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Parameter = Desc.pParameters[Index];

        Uint32     DataSize;
        const bool IsValidDataSize = GetMaterialParameterDataSize(Parameter, DataSize);
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

        MaterialParameterValue& Value = m_pValues[Index];
        Value.Type                    = Parameter.Type;

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

            if (Parameter.ArraySize == 1)
            {
                pTextures[0] = Parameter.pDefaultTexture;
            }
        }
        else
        {
            Value.pData = Writer.Allocate(DataSize, alignof(Uint32));
            Value.Size  = DataSize;
            ++m_ValueCount;

            if (Parameter.pDefaultValue != nullptr)
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

namespace
{

struct MaterialTextureSource
{
    RadientMaterialParameterHandle      Parameter;
    Uint32                              ArrayIndex = 0;
    RefCntAutoPtr<IRadientTextureAsset> pFallbackTexture;

    IRadientTextureAsset* GetRequestedTexture(const PackedMaterialData& Data) const noexcept
    {
        return Data.GetTexture(Parameter.Index, ArrayIndex);
    }

    IRadientTextureAsset* GetRenderTexture(const PackedMaterialData& Data) const noexcept
    {
        IRadientTextureAsset* const pRequestedTexture = GetRequestedTexture(Data);
        if (pRequestedTexture != nullptr &&
            RADIENT_SUCCEEDED(RadientTextureAssetManager::GetLoadStatus(pRequestedTexture)))
        {
            return pRequestedTexture;
        }

        return pFallbackTexture != nullptr ? pFallbackTexture.RawPtr() : pRequestedTexture;
    }

    RADIENT_STATUS GetRenderTextureLoadStatus(const PackedMaterialData& Data) const noexcept
    {
        IRadientTextureAsset* const pRequestedTexture = GetRequestedTexture(Data);
        if (pRequestedTexture != nullptr)
        {
            const RADIENT_STATUS RequestedStatus =
                RadientTextureAssetManager::GetLoadStatus(pRequestedTexture);
            if (RADIENT_SUCCEEDED(RequestedStatus) || pFallbackTexture == nullptr)
                return RequestedStatus;
        }

        return pFallbackTexture != nullptr ?
            RadientTextureAssetManager::GetLoadStatus(pFallbackTexture) :
            RADIENT_STATUS_OK;
    }
};

} // namespace

struct MaterialStorage::TextureState
{
    // Terminal statuses and the finalized view are intentionally not invalidated.
    // The material-asset contract currently requires all writer commits to finish
    // before the first status or view query.
    using TextureSourceArray = std::vector<MaterialTextureSource>;
    using TextureEntryArray  = std::vector<RadientMaterialTextureEntry>;
    using TextureIndexArray  = std::vector<Uint32>;

    explicit TextureState(MaterialStorage& Storage) :
        Storage{Storage}
    {
        const RadientMaterialDefinitionDesc& Desc = Storage.m_pDefinition->GetDesc();
        TextureSources.reserve(Desc.ParameterCount);
        TextureIndexByParameter.resize(Desc.ParameterCount, RadientMaterialAssetView::InvalidTextureIndex);

        for (Uint32 ParameterIndex = 0; ParameterIndex < Desc.ParameterCount; ++ParameterIndex)
        {
            const RadientMaterialParameterDesc& ParameterDesc = Desc.pParameters[ParameterIndex];
            if (ParameterDesc.Type != RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE)
                continue;

            TextureIndexByParameter[ParameterIndex] = static_cast<Uint32>(TextureSources.size());
            for (Uint32 ArrayIndex = 0; ArrayIndex < ParameterDesc.ArraySize; ++ArrayIndex)
            {
                MaterialTextureSource Source;
                Source.Parameter.Definition = Storage.m_DefinitionHandle;
                Source.Parameter.Index      = ParameterIndex;
                Source.ArrayIndex           = ArrayIndex;
                Source.pFallbackTexture     = ParameterDesc.pDefaultTexture;
                TextureSources.push_back(std::move(Source));
            }
        }

        const RADIENT_STATUS InitialStatus =
            TextureSources.empty() ? RADIENT_STATUS_OK : RADIENT_STATUS_PENDING;
        LoadStatus.store(InitialStatus, std::memory_order_relaxed);
        GPUResourceStatus.store(InitialStatus, std::memory_order_relaxed);
    }

    RADIENT_STATUS GetLoadStatus() const noexcept
    {
        RADIENT_STATUS Status = LoadStatus.load(std::memory_order_acquire);
        if (Status != RADIENT_STATUS_PENDING)
            return Status;

        Status = RADIENT_STATUS_OK;
        for (const MaterialTextureSource& Source : TextureSources)
        {
            Status = CombineDependencyStatus(
                Status,
                Source.GetRenderTextureLoadStatus(Storage.m_Data));
        }

        if (Status != RADIENT_STATUS_PENDING)
            LoadStatus.store(Status, std::memory_order_release);
        return Status;
    }

    RADIENT_STATUS GetGPUResourceStatus() const noexcept
    {
        const RADIENT_STATUS Status = GetLoadStatus();
        if (Status != RADIENT_STATUS_OK)
            return Status;

        RADIENT_STATUS GPUStatus = GPUResourceStatus.load(std::memory_order_acquire);
        if (GPUStatus != RADIENT_STATUS_PENDING)
            return GPUStatus;

        GPUStatus = RADIENT_STATUS_OK;
        for (const MaterialTextureSource& Source : TextureSources)
        {
            IRadientTextureAsset* const pTexture = Source.GetRenderTexture(Storage.m_Data);
            if (pTexture == nullptr)
                continue;

            GPUStatus = CombineDependencyStatus(
                GPUStatus,
                RadientTextureAssetManager::GetGPUResourceStatus(pTexture));
        }

        if (GPUStatus != RADIENT_STATUS_PENDING)
            GPUResourceStatus.store(GPUStatus, std::memory_order_release);
        return GPUStatus;
    }

    RADIENT_STATUS FinalizeTextureSelection()
    {
        if (TextureSelectionReady)
            return RADIENT_STATUS_OK;

        const RADIENT_STATUS Status = GetLoadStatus();
        if (Status != RADIENT_STATUS_OK)
            return Status;

        try
        {
            TextureEntryArray NewTextureEntries;
            NewTextureEntries.reserve(TextureSources.size());

            bool StateChanged = false;
            for (const MaterialTextureSource& Source : TextureSources)
            {
                IRadientTextureAsset* const pRequestedTexture =
                    Source.GetRequestedTexture(Storage.m_Data);
                IRadientTextureAsset* const pSelectedTexture =
                    Source.GetRenderTexture(Storage.m_Data);

                RadientMaterialTextureEntry& Texture = NewTextureEntries.emplace_back();
                Texture.ParameterIndex               = Source.Parameter.Index;
                Texture.ArrayIndex                   = Source.ArrayIndex;
                Texture.pTexture                     = pSelectedTexture;

                if (pSelectedTexture != pRequestedTexture)
                {
                    Storage.m_Data.SetTexture(
                        Source.Parameter.Index,
                        Source.ArrayIndex,
                        pSelectedTexture);
                    StateChanged = true;
                }
            }

            if (StateChanged)
                ++Storage.m_Version;

            TextureEntries        = std::move(NewTextureEntries);
            TextureSelectionReady = true;
            return RADIENT_STATUS_OK;
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to finalize Radient material texture selection: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }
    }

    MaterialStorage&                    Storage;
    bool                                TextureSelectionReady = false;
    mutable std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    mutable std::atomic<RADIENT_STATUS> GPUResourceStatus{RADIENT_STATUS_OK};
    TextureSourceArray                  TextureSources;
    TextureIndexArray                   TextureIndexByParameter;
    TextureEntryArray                   TextureEntries;
};

MaterialStorage::MaterialStorage(IRadientMaterialDefinitionAsset* pDefinition,
                                 RadientHandle                    DefinitionHandle) :
    m_pDefinition{pDefinition},
    m_DefinitionHandle{DefinitionHandle},
    m_Data{pDefinition->GetDesc()},
    m_pTextureState{std::make_unique<TextureState>(*this)}
{}

MaterialStorage::~MaterialStorage() = default;

IRadientMaterialDefinitionAsset* MaterialStorage::GetDefinition() const noexcept
{
    return m_pDefinition;
}

Uint64 MaterialStorage::GetVersion() const noexcept
{
    return m_Version;
}

RADIENT_STATUS MaterialStorage::GetParameter(RadientMaterialParameterHandle Handle,
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

RADIENT_STATUS MaterialStorage::GetTexture(RadientMaterialParameterHandle Handle,
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

RADIENT_STATUS MaterialStorage::GetLoadStatus() const noexcept
{
    return m_pTextureState->GetLoadStatus();
}

RADIENT_STATUS MaterialStorage::GetGPUResourceStatus() const noexcept
{
    return m_pTextureState->GetGPUResourceStatus();
}

RadientMaterialAssetView MaterialStorage::GetMaterialView(IRadientMaterialAsset* pMaterial)
{
    if (pMaterial == nullptr || m_pTextureState->FinalizeTextureSelection() != RADIENT_STATUS_OK)
        return {};

    return {
        pMaterial,
        m_pTextureState->TextureEntries.data(),
        static_cast<Uint32>(m_pTextureState->TextureEntries.size()),
        m_pTextureState->TextureIndexByParameter.data(),
        static_cast<Uint32>(m_pTextureState->TextureIndexByParameter.size()),
    };
}

const PackedMaterialData& MaterialStorage::GetPackedData() const noexcept
{
    return m_Data;
}

bool MaterialStorage::IsValidHandle(RadientMaterialParameterHandle Handle) const noexcept
{
    return Handle.Definition == m_DefinitionHandle &&
        Handle.Index < m_Data.GetValueCount() &&
        Handle.Reserved == 0;
}

MaterialWriterState::MaterialWriterState(IRadientMaterialAsset* pMaterial,
                                         MaterialStorage&       Storage) noexcept :
    m_pMaterial{pMaterial},
    m_Storage{Storage}
{}

RADIENT_STATUS MaterialWriterState::SetParameter(RadientMaterialParameterHandle Handle,
                                                 const void*                    pData,
                                                 Uint32                         DataSize)
{
    if (!m_Storage.IsValidHandle(Handle) ||
        IsTextureParameter(m_Storage.m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const Uint32 ValueSize = m_Storage.m_Data.GetValueSize(Handle.Index);
    if (pData == nullptr || DataSize != ValueSize)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ChangeIterator    ChangeIt     = FindChange(Handle.Index, ValueArrayIndex);
    const bool        HasChange    = ChangeIt != m_Changes.end();
    const void* const pCurrentData = HasChange ?
        m_ValueData.data() + ChangeIt->DataOffset :
        m_Storage.m_Data.GetValueData(Handle.Index);
    if (std::memcmp(pCurrentData, pData, ValueSize) == 0)
        return RADIENT_STATUS_NO_CHANGE;

    if (HasChange)
    {
        std::memcpy(m_ValueData.data() + ChangeIt->DataOffset, pData, ValueSize);
        return RADIENT_STATUS_OK;
    }

    if (m_ValueData.size() > std::numeric_limits<Uint32>::max() - ValueSize)
    {
        LOG_ERROR_MESSAGE("Material writer value storage exceeds the supported size");
        return RADIENT_STATUS_FAILED;
    }

    try
    {
        const Uint32 DataOffset = static_cast<Uint32>(m_ValueData.size());
        m_Changes.reserve(m_Changes.size() + 1);
        m_ValueData.resize(m_ValueData.size() + ValueSize);
        std::memcpy(m_ValueData.data() + DataOffset, pData, ValueSize);
        m_Changes.push_back(ParameterChange{Handle.Index, ValueArrayIndex, DataOffset});
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to store a material parameter change: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS MaterialWriterState::SetTexture(RadientMaterialParameterHandle Handle,
                                               Uint32                         ArrayIndex,
                                               IRadientTextureAsset*          pTexture)
{
    if (!m_Storage.IsValidHandle(Handle) ||
        !IsTextureParameter(m_Storage.m_Data.GetValueType(Handle.Index)))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (ArrayIndex >= m_Storage.m_Data.GetValueSize(Handle.Index))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    ChangeIterator              ChangeIt        = FindChange(Handle.Index, ArrayIndex);
    const bool                  HasChange       = ChangeIt != m_Changes.end();
    IRadientTextureAsset* const pCurrentTexture = HasChange ?
        m_TextureData[ChangeIt->DataOffset].RawPtr() :
        m_Storage.m_Data.GetTexture(Handle.Index, ArrayIndex);
    if (pCurrentTexture == pTexture)
        return RADIENT_STATUS_NO_CHANGE;

    if (HasChange)
    {
        m_TextureData[ChangeIt->DataOffset] = pTexture;
        return RADIENT_STATUS_OK;
    }

    if (m_TextureData.size() >= std::numeric_limits<Uint32>::max())
    {
        LOG_ERROR_MESSAGE("Material writer texture storage exceeds the supported size");
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
        LOG_ERROR_MESSAGE("Failed to store a material texture change: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }

    return RADIENT_STATUS_OK;
}

bool MaterialWriterState::ApplyParameterChanges()
{
    bool StateChanged = false;
    for (const ParameterChange& Change : m_Changes)
    {
        if (IsTextureParameter(m_Storage.m_Data.GetValueType(Change.ParameterIndex)))
        {
            VERIFY_EXPR(Change.ArrayIndex != ValueArrayIndex);
            IRadientTextureAsset* const pTexture = m_TextureData[Change.DataOffset];
            if (m_Storage.m_Data.GetTexture(Change.ParameterIndex, Change.ArrayIndex) != pTexture)
            {
                m_Storage.m_Data.SetTexture(Change.ParameterIndex, Change.ArrayIndex, pTexture);
                StateChanged = true;
            }
        }
        else
        {
            VERIFY_EXPR(Change.ArrayIndex == ValueArrayIndex);
            const void* const pChangedData = m_ValueData.data() + Change.DataOffset;
            if (!m_Storage.m_Data.HasSameValue(Change.ParameterIndex, pChangedData))
            {
                m_Storage.m_Data.CopyValue(Change.ParameterIndex, pChangedData);
                StateChanged = true;
            }
        }
    }
    return StateChanged;
}

RADIENT_STATUS MaterialWriterState::FinishCommit(bool StateChanged)
{
    m_Changes.clear();
    m_ValueData.clear();
    m_TextureData.clear();

    if (!StateChanged)
        return RADIENT_STATUS_NO_CHANGE;

    ++m_Storage.m_Version;
    return RADIENT_STATUS_OK;
}

MaterialWriterState::ChangeIterator MaterialWriterState::FindChange(
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

class RadientMaterialAssetImpl;
class RadientMaterialWriterImpl;

using RadientMaterialAssetImplBase =
    MaterialAssetImplBase<RadientMaterialAssetImpl,
                          IRadientMaterialAsset,
                          IID_RadientMaterialAsset>;

class RadientMaterialAssetImpl final : public RadientMaterialAssetImplBase
{
public:
    using TBase = RadientMaterialAssetImplBase;
    using TBase::TBase;

    RefCntAutoPtr<RadientMaterialWriterImpl> MakeWriter() const;
};

using RadientMaterialWriterImplBase =
    MaterialWriterImplBase<RadientMaterialWriterImpl,
                           IRadientMaterialWriter,
                           IID_RadientMaterialWriter,
                           RadientMaterialAssetImpl>;

class RadientMaterialWriterImpl final : public RadientMaterialWriterImplBase
{
public:
    using TBase = RadientMaterialWriterImplBase;
    using TBase::TBase;
};

RefCntAutoPtr<RadientMaterialWriterImpl> RadientMaterialAssetImpl::MakeWriter() const
{
    return RefCntAutoPtr<RadientMaterialWriterImpl>{
        MakeNewRCObj<RadientMaterialWriterImpl>()(
            const_cast<RadientMaterialAssetImpl*>(this))};
}

} // namespace

RefCntAutoPtr<IRadientMaterialAsset> RadientMaterialDetail::MakeGenericMaterialAsset(
    IRadientMaterialDefinitionAsset* pDefinition,
    RadientHandle                    DefinitionHandle)
{
    return RefCntAutoPtr<RadientMaterialAssetImpl>{MakeNewRCObj<RadientMaterialAssetImpl>()(pDefinition, DefinitionHandle)};
}

RadientMaterialDetail::MaterialStorage* RadientMaterialDetail::TryGetMaterialStorage(
    IRadientMaterialAsset* pMaterial) noexcept
{
    RefCntAutoPtr<IMaterialStorageProvider> pStorageProvider{pMaterial, IID_MaterialStorageProvider};
    return pStorageProvider != nullptr ? &pStorageProvider->GetStorage() : nullptr;
}

const RadientMaterialDetail::MaterialStorage* RadientMaterialDetail::TryGetMaterialStorage(
    const IRadientMaterialAsset* pMaterial) noexcept
{
    return TryGetMaterialStorage(const_cast<IRadientMaterialAsset*>(pMaterial));
}

} // namespace Diligent
