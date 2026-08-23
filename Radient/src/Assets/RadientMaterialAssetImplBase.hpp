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

#include "DebugUtilities.hpp"
#include "ObjectBase.hpp"

#include <exception>

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
          const INTERFACE_ID& InterfaceID>
class MaterialAssetImplBase : public RefCountedObject<MaterialAssetCombinedInterface<InterfaceType>>
{
public:
    using TBase = RefCountedObject<MaterialAssetCombinedInterface<InterfaceType>>;

    MaterialAssetImplBase(IReferenceCounters*              pRefCounters,
                          IRadientMaterialDefinitionAsset* pDefinition,
                          RadientHandle                    DefinitionHandle) :
        TBase{pRefCounters},
        m_URI{MakeRadientAssetURI("material")},
        m_Storage{pDefinition, DefinitionHandle}
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

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialWriter** ppWriter) const override final
    {
        if (ppWriter == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppWriter = nullptr;

        try
        {
            auto pWriter = static_cast<const DerivedType*>(this)->MakeWriter();
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

    const MaterialStorage& GetStorage() const noexcept
    {
        return m_Storage;
    }

    virtual MaterialStorage& DILIGENT_CALL_TYPE GetStorage() noexcept override final
    {
        return m_Storage;
    }

private:
    const std::string     m_URI;
    RadientAssetReference m_Reference;
    MaterialStorage       m_Storage;
};

template <typename DerivedType,
          typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          typename MaterialType>
class MaterialWriterImplBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    MaterialWriterImplBase(IReferenceCounters* pRefCounters,
                           MaterialType*       pMaterial) :
        TBase{pRefCounters},
        m_pMaterial{pMaterial},
        m_Parameters{pMaterial, pMaterial->GetStorage()}
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
        return m_Parameters.SetParameter(Handle, pData, DataSize);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset*          pTexture) override final
    {
        return m_Parameters.SetTexture(Handle, ArrayIndex, pTexture);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        const bool ParameterStateChanged   = m_Parameters.ApplyParameterChanges();
        const bool SpecializedStateChanged = static_cast<DerivedType*>(this)->ApplySpecializedChanges();
        return m_Parameters.FinishCommit(ParameterStateChanged || SpecializedStateChanged);
    }

protected:
    MaterialType& GetMaterial() const noexcept
    {
        return *m_pMaterial;
    }

    bool ApplySpecializedChanges() noexcept
    {
        return false;
    }

private:
    // m_pMaterial is borrowed. m_Parameters retains the same asset strongly and
    // therefore keeps the typed pointer valid for the writer's lifetime.
    MaterialType*       m_pMaterial = nullptr;
    MaterialWriterState m_Parameters;
};

} // namespace RadientMaterialDetail

} // namespace Diligent
