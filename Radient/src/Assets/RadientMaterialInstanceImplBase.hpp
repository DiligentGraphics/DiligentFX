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

#include "Assets/RadientMaterialInstanceState.hpp"

#include "DebugUtilities.hpp"
#include "ObjectBase.hpp"

#include <exception>

namespace Diligent
{

namespace RadientMaterialDetail
{

template <typename DerivedType,
          typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          const INTERFACE_ID& ImplID>
class MaterialInstanceImplBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    MaterialInstanceImplBase(IReferenceCounters*              pRefCounters,
                             IRadientMaterialDefinitionAsset* pDefinition,
                             RadientHandle                    DefinitionHandle,
                             const MaterialInstanceState*     pSource = nullptr) :
        TBase{pRefCounters},
        m_State{pDefinition, DefinitionHandle, pSource}
    {}

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == InterfaceID ||
            IID == IID_RadientMaterialInstance ||
            IID == ImplID)
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

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final
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
            LOG_ERROR_MESSAGE("Failed to create Radient material instance writer: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final
    {
        if (ppInstance == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppInstance = nullptr;

        try
        {
            auto pInstance = static_cast<const DerivedType*>(this)->MakeClone();
            VERIFY_EXPR(pInstance != nullptr);
            *ppInstance = pInstance.Detach();
            return RADIENT_STATUS_OK;
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to clone Radient material instance: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }
    }

    const MaterialInstanceState& GetState() const noexcept
    {
        return m_State;
    }

    MaterialInstanceState& GetState() noexcept
    {
        return m_State;
    }

    static MaterialInstanceState* TryGetState(IRadientMaterialInstance* pInstance) noexcept
    {
        RefCntAutoPtr<IObject> pImpl{pInstance, ImplID};
        return pImpl != nullptr ?
            &static_cast<DerivedType*>(pImpl.RawPtr())->GetState() :
            nullptr;
    }

private:
    MaterialInstanceState m_State;
};

template <typename DerivedType,
          typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          typename InstanceType>
class MaterialInstanceWriterImplBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    MaterialInstanceWriterImplBase(IReferenceCounters* pRefCounters,
                                   InstanceType*       pInstance) :
        TBase{pRefCounters},
        m_pInstance{pInstance},
        m_Parameters{pInstance, pInstance->GetState()}
    {
        VERIFY_EXPR(m_pInstance != nullptr);
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == InterfaceID || IID == IID_RadientMaterialInstanceWriter)
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
    InstanceType& GetInstance() const noexcept
    {
        return *m_pInstance;
    }

    bool ApplySpecializedChanges() noexcept
    {
        return false;
    }

private:
    // m_pInstance is borrowed. m_Parameters retains the same instance strongly
    // and therefore keeps the typed pointer valid for the writer's lifetime.
    InstanceType*               m_pInstance = nullptr;
    MaterialInstanceWriterState m_Parameters;
};

} // namespace RadientMaterialDetail

} // namespace Diligent
