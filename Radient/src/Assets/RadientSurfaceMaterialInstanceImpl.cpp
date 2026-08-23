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

#include "RadientMaterialInstanceImplBase.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace
{

using namespace RadientMaterialDetail;

class RadientSurfaceMaterialInstanceImpl;
class RadientSurfaceMaterialInstanceWriterImpl;

using RadientSurfaceMaterialInstanceImplBase =
    MaterialInstanceImplBase<RadientSurfaceMaterialInstanceImpl,
                             IRadientSurfaceMaterialInstance,
                             IID_RadientSurfaceMaterialInstance>;

class RadientSurfaceMaterialInstanceImpl final : public RadientSurfaceMaterialInstanceImplBase
{
public:
    using TBase = RadientSurfaceMaterialInstanceImplBase;

    RadientSurfaceMaterialInstanceImpl(IReferenceCounters*                       pRefCounters,
                                       IRadientMaterialDefinitionAsset*          pDefinition,
                                       RadientHandle                             DefinitionHandle,
                                       const RadientSurfaceMaterialInstanceImpl* pSource = nullptr) :
        TBase{pRefCounters, pDefinition, DefinitionHandle, pSource != nullptr ? &pSource->GetState() : nullptr},
        m_SurfaceMode{pSource != nullptr ? pSource->m_SurfaceMode : RADIENT_MATERIAL_SURFACE_MODE_OPAQUE},
        m_AlphaCutoff{pSource != nullptr ? pSource->m_AlphaCutoff : 0.5f},
        m_IsDoubleSided{pSource != nullptr ? pSource->m_IsDoubleSided : False}
    {}

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

    RefCntAutoPtr<RadientSurfaceMaterialInstanceWriterImpl> MakeWriter() const;
    RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl>       MakeClone() const;

private:
    friend class RadientSurfaceMaterialInstanceWriterImpl;

    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode   = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff   = 0.5f;
    Bool                          m_IsDoubleSided = False;
};

using RadientSurfaceMaterialInstanceWriterImplBase =
    MaterialInstanceWriterImplBase<RadientSurfaceMaterialInstanceWriterImpl,
                                   IRadientSurfaceMaterialInstanceWriter,
                                   IID_RadientSurfaceMaterialInstanceWriter,
                                   RadientSurfaceMaterialInstanceImpl>;

class RadientSurfaceMaterialInstanceWriterImpl final : public RadientSurfaceMaterialInstanceWriterImplBase
{
public:
    using TBase = RadientSurfaceMaterialInstanceWriterImplBase;
    using TBase::TBase;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) override final
    {
        if (SurfaceMode >= RADIENT_MATERIAL_SURFACE_MODE_COUNT)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const RADIENT_MATERIAL_SURFACE_MODE CurrentMode =
            m_SurfaceModeChanged ? m_SurfaceMode : GetInstance().m_SurfaceMode;
        if (CurrentMode == SurfaceMode)
            return RADIENT_STATUS_NO_CHANGE;

        m_SurfaceMode        = SurfaceMode;
        m_SurfaceModeChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetAlphaCutoff(Float32 AlphaCutoff) override final
    {
        const Float32 CurrentValue =
            m_AlphaCutoffChanged ? m_AlphaCutoff : GetInstance().m_AlphaCutoff;
        if (CurrentValue == AlphaCutoff)
            return RADIENT_STATUS_NO_CHANGE;

        m_AlphaCutoff        = AlphaCutoff;
        m_AlphaCutoffChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetDoubleSided(Bool DoubleSided) override final
    {
        DoubleSided             = DoubleSided != False ? True : False;
        const Bool CurrentValue = m_DoubleSidedChanged ? m_IsDoubleSided : GetInstance().m_IsDoubleSided;
        if (CurrentValue == DoubleSided)
            return RADIENT_STATUS_NO_CHANGE;

        m_IsDoubleSided      = DoubleSided;
        m_DoubleSidedChanged = true;
        return RADIENT_STATUS_OK;
    }

    bool ApplySpecializedChanges() noexcept
    {
        bool StateChanged = false;

        if (m_SurfaceModeChanged && GetInstance().m_SurfaceMode != m_SurfaceMode)
        {
            GetInstance().m_SurfaceMode = m_SurfaceMode;
            StateChanged                = true;
        }
        if (m_AlphaCutoffChanged && GetInstance().m_AlphaCutoff != m_AlphaCutoff)
        {
            GetInstance().m_AlphaCutoff = m_AlphaCutoff;
            StateChanged                = true;
        }
        if (m_DoubleSidedChanged && GetInstance().m_IsDoubleSided != m_IsDoubleSided)
        {
            GetInstance().m_IsDoubleSided = m_IsDoubleSided;
            StateChanged                  = true;
        }

        m_SurfaceModeChanged = false;
        m_AlphaCutoffChanged = false;
        m_DoubleSidedChanged = false;
        return StateChanged;
    }

private:
    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode        = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff        = 0.5f;
    Bool                          m_IsDoubleSided      = False;
    bool                          m_SurfaceModeChanged = false;
    bool                          m_AlphaCutoffChanged = false;
    bool                          m_DoubleSidedChanged = false;
};

RefCntAutoPtr<RadientSurfaceMaterialInstanceWriterImpl> RadientSurfaceMaterialInstanceImpl::MakeWriter() const
{
    return RefCntAutoPtr<RadientSurfaceMaterialInstanceWriterImpl>{
        MakeNewRCObj<RadientSurfaceMaterialInstanceWriterImpl>()(
            const_cast<RadientSurfaceMaterialInstanceImpl*>(this))};
}

RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl> RadientSurfaceMaterialInstanceImpl::MakeClone() const
{
    return RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl>{
        MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(
            GetState().GetDefinition(),
            GetState().GetDefinitionHandle(),
            this)};
}

} // namespace

RefCntAutoPtr<IRadientMaterialInstance> RadientMaterialDetail::MakeSurfaceMaterialInstance(
    IRadientMaterialDefinitionAsset* pDefinition,
    RadientHandle                    DefinitionHandle)
{
    return RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl>{
        MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(pDefinition, DefinitionHandle)};
}

} // namespace Diligent
