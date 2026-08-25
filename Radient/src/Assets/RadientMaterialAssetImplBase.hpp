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

#include "Assets/RadientAssetURI.hpp"
#include "Assets/RadientMaterialStorage.hpp"

#include "RadientMaterialChanges.hpp"

#include "DebugUtilities.hpp"
#include "ObjectBase.hpp"

#include <exception>
#include <utility>

namespace Diligent
{

namespace RadientMaterialDetail
{

// {484A2342-30BE-447A-AC3C-B7472D3FB6AF}
static constexpr INTERFACE_ID IID_MaterialStorageProvider =
    {0x484a2342, 0x30be, 0x447a, {0xac, 0x3c, 0xb7, 0x47, 0x2d, 0x3f, 0xb6, 0xaf}};


struct IMaterialStorageProvider : IObject
{
    virtual MaterialStorage& DILIGENT_CALL_TYPE GetStorage() noexcept = 0;
};

template <typename InterfaceType>
struct MaterialAssetCombinedInterface :
    InterfaceType,
    IMaterialStorageProvider
{};

template <typename DerivedType,
          typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          typename SpecializedStateType   = EmptyMaterialState,
          typename SpecializedChangesType = EmptyMaterialChanges>
class MaterialAssetImplBase : public RefCountedObject<MaterialAssetCombinedInterface<InterfaceType>>
{
public:
    using TBase     = RefCountedObject<MaterialAssetCombinedInterface<InterfaceType>>;
    using ChangeSet = MaterialChangeSet<SpecializedChangesType>;

    MaterialAssetImplBase(IReferenceCounters*              pRefCounters,
                          IRadientMaterialDefinitionAsset* pDefinition,
                          RadientHandle                    DefinitionHandle,
                          const MaterialAssetIdentity&     Identity) :
        TBase{pRefCounters},
        m_URI{MakeRadientAssetURI("material")},
        m_Storage{pDefinition, DefinitionHandle, Identity}
    {
        m_Reference.URI     = m_URI.c_str();
        m_Reference.Version = 1;
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        *ppInterface = nullptr;
        if (IID == IID_MaterialStorageProvider)
        {
            *ppInterface = static_cast<IMaterialStorageProvider*>(this);
        }
        else if (IID == InterfaceID)
        {
            *ppInterface = static_cast<InterfaceType*>(this);
        }
        else if (IID == IID_RadientMaterialAsset ||
                 IID == IID_RadientAsset ||
                 IID == IID_Unknown)
        {
            *ppInterface = static_cast<IRadientMaterialAsset*>(static_cast<InterfaceType*>(this));
        }

        if (*ppInterface != nullptr)
            (*ppInterface)->AddRef();
    }

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Reference;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_MATERIAL;
    }

    virtual IRadientMaterialDefinitionAsset* DILIGENT_CALL_TYPE GetDefinition() const override final
    {
        return m_Storage.GetDefinition();
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_Storage.GetVersion();
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameter(RadientMaterialParameterHandle Handle,
                                                           void*                          pData,
                                                           Uint32                         DataSize) const override final
    {
        return m_Storage.GetParameter(Handle, pData, DataSize);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset**         ppTexture) const override final
    {
        return m_Storage.GetTexture(Handle, ArrayIndex, ppTexture);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialWriter** ppWriter) override final
    {
        if (ppWriter == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppWriter = nullptr;

        try
        {
            auto pWriter = static_cast<DerivedType*>(this)->MakeWriter();
            VERIFY_EXPR(pWriter != nullptr);
            *ppWriter = pWriter.Detach();
            return RADIENT_STATUS_OK;
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to create Radient material writer: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }
    }

    virtual MaterialStorage& DILIGENT_CALL_TYPE GetStorage() noexcept override final
    {
        return m_Storage;
    }

    RADIENT_STATUS SubmitChanges(ChangeSet& Changes) noexcept
    {
        MATERIAL_CHANGE_FLAGS Flags = Changes.Parameters.ApplyTo(m_Storage.GetPackedData());
        Flags |= Changes.Specialized.ApplyTo(m_SpecializedState);
        if (Flags == MATERIAL_CHANGE_FLAG_NONE)
            return RADIENT_STATUS_NO_CHANGE;

        m_Storage.PublishChange(Flags);
        return RADIENT_STATUS_OK;
    }

protected:
    const SpecializedStateType& GetSpecializedState() const noexcept
    {
        return m_SpecializedState;
    }

private:
    const std::string     m_URI;
    RadientAssetReference m_Reference;
    MaterialStorage       m_Storage;
    SpecializedStateType  m_SpecializedState;
};

template <typename DerivedType,
          typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          typename MaterialType>
class MaterialWriterImplBase : public ObjectBase<InterfaceType>
{
public:
    using TBase                  = ObjectBase<InterfaceType>;
    using ChangeSet              = typename MaterialType::ChangeSet;
    using SpecializedChangesType = typename ChangeSet::SpecializedChangesType;

    MaterialWriterImplBase(IReferenceCounters* pRefCounters,
                           MaterialType*       pMaterial) :
        TBase{pRefCounters},
        m_pMaterial{pMaterial}
    {
        VERIFY_EXPR(m_pMaterial != nullptr);
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == InterfaceID || IID == IID_RadientMaterialWriter)
        {
            *ppInterface = static_cast<DerivedType*>(this);
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetParameter(RadientMaterialParameterHandle Handle,
                                                           const void*                    pData,
                                                           Uint32                         DataSize) override final
    {
        return m_Changes.Parameters.SetParameter(m_pMaterial->GetStorage(), Handle, pData, DataSize);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset*          pTexture) override final
    {
        return m_Changes.Parameters.SetTexture(m_pMaterial->GetStorage(), Handle, ArrayIndex, pTexture);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        ChangeSet Changes = std::exchange(m_Changes, ChangeSet{});

        const RADIENT_STATUS Status = m_pMaterial->SubmitChanges(Changes);
        if (RADIENT_FAILED(Status))
            m_Changes = std::move(Changes);

        return Status;
    }

protected:
    SpecializedChangesType& GetSpecializedChanges() noexcept
    {
        return m_Changes.Specialized;
    }

private:
    RefCntAutoPtr<MaterialType> m_pMaterial;
    ChangeSet                   m_Changes;
};

} // namespace RadientMaterialDetail

} // namespace Diligent
