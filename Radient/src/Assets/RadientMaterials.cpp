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
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
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
    if (CreateInfo.Desc.Domain >= RADIENT_MATERIAL_DOMAIN_COUNT ||
        (CreateInfo.ParameterCount != 0 && CreateInfo.pParameters == nullptr))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    std::unordered_map<std::string, Uint32>                Names;
    std::unordered_map<RadientMaterialParameterID, Uint32> IDs;
    Names.reserve(CreateInfo.ParameterCount);
    IDs.reserve(CreateInfo.ParameterCount);

    for (Uint32 Index = 0; Index < CreateInfo.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Desc = CreateInfo.pParameters[Index];
        Uint32                              DataSize;
        if (Desc.Name == nullptr || Desc.Name[0] == '\0' ||
            Desc.ID == InvalidRadientMaterialParameterID ||
            Desc.Type <= RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN ||
            Desc.Type >= RADIENT_MATERIAL_PARAMETER_TYPE_COUNT ||
            (Desc.Flags & ~RADIENT_MATERIAL_PARAMETER_FLAGS_ALL) != RADIENT_MATERIAL_PARAMETER_FLAG_NONE ||
            !GetMaterialParameterDataSize(Desc, DataSize) ||
            (IsTextureParameter(Desc.Type) &&
             (Desc.pDefaultValue != nullptr || (Desc.ArraySize != 1 && Desc.pDefaultTexture != nullptr))) ||
            (!IsTextureParameter(Desc.Type) && Desc.pDefaultTexture != nullptr) ||
            !Names.emplace(Desc.Name, Index).second ||
            !IDs.emplace(Desc.ID, Index).second)
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
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

struct MaterialInstanceState
{
    std::vector<MaterialParameterValue> Values;
};

using MaterialInstanceStatePtr = std::shared_ptr<const MaterialInstanceState>;

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
        RadientMaterialInstanceImpl{
            pRefCounters,
            pDefinition,
            std::move(Handles),
            std::move(Types),
            std::make_shared<MaterialInstanceState>(MaterialInstanceState{std::move(Values)})}
    {}

    RadientMaterialInstanceImpl(IReferenceCounters*                          pRefCounters,
                                IRadientMaterialDefinition*                  pDefinition,
                                std::vector<RadientMaterialParameterHandle>  Handles,
                                std::vector<RADIENT_MATERIAL_PARAMETER_TYPE> Types,
                                MaterialInstanceStatePtr                     pState) :
        TBase{pRefCounters},
        m_pDefinition{pDefinition},
        m_Handles{std::move(Handles)},
        m_Types{std::move(Types)},
        m_pState{std::move(pState)}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstance, TBase)

    virtual IRadientMaterialDefinition* DILIGENT_CALL_TYPE GetDefinition() const override final
    {
        return m_pDefinition;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_Version.load(std::memory_order_acquire);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameter(RadientMaterialParameterHandle Handle,
                                                           void*                          pData,
                                                           Uint32                         DataSize) const override final
    {
        if (!IsValidHandle(Handle))
            return RADIENT_STATUS_INVALID_ARGUMENT;
        if (IsTextureParameter(m_Types[Handle.Index]))
            return RADIENT_STATUS_INVALID_OPERATION;

        const MaterialInstanceStatePtr pState = GetState();
        const std::vector<Uint8>&      Data   = pState->Values[Handle.Index].Data;
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

        const MaterialInstanceStatePtr pState   = GetState();
        const auto&                    Textures = pState->Values[Handle.Index].Textures;
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

    MaterialInstanceStatePtr GetState() const
    {
        std::lock_guard<std::mutex> Lock{m_StateMutex};
        return m_pState;
    }

    RADIENT_STATUS Commit(const std::vector<std::optional<MaterialParameterValue>>& Changes)
    {
        bool HasChanges = false;
        for (const auto& Change : Changes)
            HasChanges = HasChanges || Change.has_value();
        if (!HasChanges)
            return RADIENT_STATUS_NO_CHANGE;

        try
        {
            std::lock_guard<std::mutex> Lock{m_StateMutex};

            auto pNewState    = std::make_shared<MaterialInstanceState>(*m_pState);
            bool StateChanged = false;
            for (size_t Index = 0; Index < Changes.size(); ++Index)
            {
                if (Changes[Index].has_value() &&
                    !(pNewState->Values[Index] == *Changes[Index]))
                {
                    pNewState->Values[Index] = *Changes[Index];
                    StateChanged             = true;
                }
            }

            if (!StateChanged)
                return RADIENT_STATUS_NO_CHANGE;

            m_pState = std::move(pNewState);
            m_Version.fetch_add(1, std::memory_order_release);
            return RADIENT_STATUS_OK;
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to update Radient material instance: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }
    }

private:
    RefCntAutoPtr<IRadientMaterialDefinition>          m_pDefinition;
    const std::vector<RadientMaterialParameterHandle>  m_Handles;
    const std::vector<RADIENT_MATERIAL_PARAMETER_TYPE> m_Types;

    mutable std::mutex       m_StateMutex;
    MaterialInstanceStatePtr m_pState;
    std::atomic<Uint64>      m_Version{1};
};

class RadientMaterialInstanceWriterImpl final : public ObjectBase<IRadientMaterialInstanceWriter>
{
public:
    using TBase = ObjectBase<IRadientMaterialInstanceWriter>;

    RadientMaterialInstanceWriterImpl(IReferenceCounters*          pRefCounters,
                                      RadientMaterialInstanceImpl* pInstance,
                                      MaterialInstanceStatePtr     pBaseState) :
        TBase{pRefCounters},
        m_pInstance{pInstance},
        m_pBaseState{std::move(pBaseState)},
        m_Changes(m_pBaseState->Values.size())
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
            m_pBaseState->Values[Handle.Index];
        const std::vector<Uint8>&     Data         = CurrentValue.Data;
        if (pData == nullptr || DataSize != static_cast<Uint32>(Data.size()))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (std::memcmp(Data.data(), pData, Data.size()) == 0)
            return RADIENT_STATUS_OK;

        MaterialParameterValue NewValue = CurrentValue;
        std::memcpy(NewValue.Data.data(), pData, NewValue.Data.size());
        if (NewValue == m_pBaseState->Values[Handle.Index])
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
            m_pBaseState->Values[Handle.Index];
        const auto&                   Textures     = CurrentValue.Textures;
        if (ArrayIndex >= Textures.size())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (Textures[ArrayIndex] == pTexture)
            return RADIENT_STATUS_OK;

        MaterialParameterValue NewValue = CurrentValue;
        NewValue.Textures[ArrayIndex]   = pTexture;
        if (NewValue == m_pBaseState->Values[Handle.Index])
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
            m_pBaseState = m_pInstance->GetState();
            for (auto& Change : m_Changes)
                Change.reset();
        }
        return Status;
    }

private:
    RefCntAutoPtr<RadientMaterialInstanceImpl>         m_pInstance;
    MaterialInstanceStatePtr                           m_pBaseState;
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
                const_cast<RadientMaterialInstanceImpl*>(this),
                GetState())};
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
                GetState())};
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

} // namespace Diligent
