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

#include "Assets/RadientSurfaceMaterialInstanceImpl.hpp"

#include "DebugUtilities.hpp"
#include "ObjectBase.hpp"

#include <exception>

namespace Diligent
{

namespace
{

using namespace RadientMaterialDetail;

// {0343A08D-C858-466B-B68B-F288DD89C8F0}
static constexpr INTERFACE_ID IID_SurfaceMaterialInstanceImpl =
    {0x343a08d, 0xc858, 0x466b, {0xb6, 0x8b, 0xf2, 0x88, 0xdd, 0x89, 0xc8, 0xf0}};

class RadientSurfaceMaterialInstanceWriterImpl;

class RadientSurfaceMaterialInstanceImpl final : public ObjectBase<IRadientSurfaceMaterialInstance>
{
public:
    using TBase = ObjectBase<IRadientSurfaceMaterialInstance>;

    RadientSurfaceMaterialInstanceImpl(IReferenceCounters*                       pRefCounters,
                                       IRadientMaterialDefinitionAsset*          pDefinition,
                                       RadientHandle                             DefinitionHandle,
                                       const RadientSurfaceMaterialInstanceImpl* pSource = nullptr) :
        TBase{pRefCounters},
        m_State{pDefinition, DefinitionHandle, pSource != nullptr ? &pSource->m_State : nullptr},
        m_SurfaceMode{pSource != nullptr ? pSource->m_SurfaceMode : RADIENT_MATERIAL_SURFACE_MODE_OPAQUE},
        m_AlphaCutoff{pSource != nullptr ? pSource->m_AlphaCutoff : 0.5f},
        m_IsDoubleSided{pSource != nullptr ? pSource->m_IsDoubleSided : False}
    {}

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientSurfaceMaterialInstance ||
            IID == IID_RadientMaterialInstance ||
            IID == IID_SurfaceMaterialInstanceImpl)
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

    virtual RADIENT_MATERIAL_SURFACE_MODE DILIGENT_CALL_TYPE GetSurfaceMode() const override final
    {
        return m_SurfaceMode;
    }

    virtual Float32 DILIGENT_CALL_TYPE GetAlphaCutoff() const override final
    {
        return m_AlphaCutoff;
    }

    virtual Bool DILIGENT_CALL_TYPE IsDoubleSided() const override final
    {
        return m_IsDoubleSided;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final;
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final;

    const MaterialInstanceState& GetState() const noexcept
    {
        return m_State;
    }

    MaterialInstanceState& GetState() noexcept
    {
        return m_State;
    }

private:
    friend class RadientSurfaceMaterialInstanceWriterImpl;

    MaterialInstanceState         m_State;
    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode   = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff   = 0.5f;
    Bool                          m_IsDoubleSided = False;
};

class RadientSurfaceMaterialInstanceWriterImpl final : public ObjectBase<IRadientSurfaceMaterialInstanceWriter>
{
public:
    using TBase = ObjectBase<IRadientSurfaceMaterialInstanceWriter>;

    RadientSurfaceMaterialInstanceWriterImpl(IReferenceCounters*                 pRefCounters,
                                             RadientSurfaceMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters},
        m_pInstance{pInstance},
        m_Parameters{pInstance, pInstance->m_State}
    {}

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientSurfaceMaterialInstanceWriter, IID_RadientMaterialInstanceWriter, TBase)

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

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) override final
    {
        if (SurfaceMode >= RADIENT_MATERIAL_SURFACE_MODE_COUNT)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const RADIENT_MATERIAL_SURFACE_MODE CurrentMode =
            m_SurfaceModeChanged ? m_SurfaceMode : m_pInstance->m_SurfaceMode;
        if (CurrentMode == SurfaceMode)
            return RADIENT_STATUS_NO_CHANGE;

        m_SurfaceMode        = SurfaceMode;
        m_SurfaceModeChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetAlphaCutoff(Float32 AlphaCutoff) override final
    {
        const Float32 CurrentValue =
            m_AlphaCutoffChanged ? m_AlphaCutoff : m_pInstance->m_AlphaCutoff;
        if (CurrentValue == AlphaCutoff)
            return RADIENT_STATUS_NO_CHANGE;

        m_AlphaCutoff        = AlphaCutoff;
        m_AlphaCutoffChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetDoubleSided(Bool DoubleSided) override final
    {
        DoubleSided             = DoubleSided != False ? True : False;
        const Bool CurrentValue = m_DoubleSidedChanged ? m_IsDoubleSided : m_pInstance->m_IsDoubleSided;
        if (CurrentValue == DoubleSided)
            return RADIENT_STATUS_NO_CHANGE;

        m_IsDoubleSided      = DoubleSided;
        m_DoubleSidedChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        bool StateChanged = m_Parameters.ApplyParameterChanges();

        if (m_SurfaceModeChanged && m_pInstance->m_SurfaceMode != m_SurfaceMode)
        {
            m_pInstance->m_SurfaceMode = m_SurfaceMode;
            StateChanged               = true;
        }
        if (m_AlphaCutoffChanged && m_pInstance->m_AlphaCutoff != m_AlphaCutoff)
        {
            m_pInstance->m_AlphaCutoff = m_AlphaCutoff;
            StateChanged               = true;
        }
        if (m_DoubleSidedChanged && m_pInstance->m_IsDoubleSided != m_IsDoubleSided)
        {
            m_pInstance->m_IsDoubleSided = m_IsDoubleSided;
            StateChanged                 = true;
        }

        m_SurfaceModeChanged = false;
        m_AlphaCutoffChanged = false;
        m_DoubleSidedChanged = false;
        return m_Parameters.FinishCommit(StateChanged);
    }

private:
    RadientSurfaceMaterialInstanceImpl* const m_pInstance;
    MaterialInstanceWriterState               m_Parameters;
    RADIENT_MATERIAL_SURFACE_MODE             m_SurfaceMode        = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                                   m_AlphaCutoff        = 0.5f;
    Bool                                      m_IsDoubleSided      = False;
    bool                                      m_SurfaceModeChanged = false;
    bool                                      m_AlphaCutoffChanged = false;
    bool                                      m_DoubleSidedChanged = false;
};

RADIENT_STATUS RadientSurfaceMaterialInstanceImpl::CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppWriter = nullptr;

    try
    {
        RefCntAutoPtr<RadientSurfaceMaterialInstanceWriterImpl> pWriter{
            MakeNewRCObj<RadientSurfaceMaterialInstanceWriterImpl>()(
                const_cast<RadientSurfaceMaterialInstanceImpl*>(this))};
        *ppWriter = pWriter.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient surface material instance writer: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientSurfaceMaterialInstanceImpl::Clone(IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    try
    {
        RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl> pInstance{
            MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(
                m_State.GetDefinition(),
                m_State.GetDefinitionHandle(),
                this)};
        *ppInstance = pInstance.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to clone Radient surface material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace

RefCntAutoPtr<IRadientMaterialInstance> RadientMaterialDetail::MakeSurfaceMaterialInstance(
    IRadientMaterialDefinitionAsset* pDefinition,
    RadientHandle                    DefinitionHandle)
{
    return RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl>{
        MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(pDefinition, DefinitionHandle)};
}

RadientMaterialDetail::MaterialInstanceState* RadientMaterialDetail::TryGetSurfaceMaterialInstanceState(
    IRadientMaterialInstance* pInstance) noexcept
{
    RefCntAutoPtr<IObject> pImpl{pInstance, IID_SurfaceMaterialInstanceImpl};
    return pImpl != nullptr ?
        &static_cast<RadientSurfaceMaterialInstanceImpl*>(pInstance)->GetState() :
        nullptr;
}

const RadientMaterialDetail::PackedMaterialInstanceData& RadientMaterialDetail::GetSurfaceMaterialInstanceData(
    const IRadientSurfaceMaterialInstance& Instance) noexcept
{
    return static_cast<const RadientSurfaceMaterialInstanceImpl&>(Instance).GetState().GetPackedData();
}

} // namespace Diligent
