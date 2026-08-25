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

#include "RadientMaterialAssetImplBase.hpp"

namespace Diligent
{

namespace
{

using namespace RadientMaterialDetail;

struct SurfaceMaterialState
{
    RADIENT_MATERIAL_SURFACE_MODE SurfaceMode   = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       AlphaCutoff   = 0.5f;
    Bool                          IsDoubleSided = False;
};

class SurfaceMaterialChanges final
{
public:
    RADIENT_STATUS SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) noexcept
    {
        if (SurfaceMode >= RADIENT_MATERIAL_SURFACE_MODE_COUNT)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (m_HasSurfaceMode && m_SurfaceMode == SurfaceMode)
            return RADIENT_STATUS_NO_CHANGE;

        m_SurfaceMode    = SurfaceMode;
        m_HasSurfaceMode = true;
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS SetAlphaCutoff(Float32 AlphaCutoff) noexcept
    {
        if (m_HasAlphaCutoff && m_AlphaCutoff == AlphaCutoff)
            return RADIENT_STATUS_NO_CHANGE;

        m_AlphaCutoff    = AlphaCutoff;
        m_HasAlphaCutoff = true;
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS SetDoubleSided(Bool DoubleSided) noexcept
    {
        DoubleSided = DoubleSided != False ? True : False;
        if (m_HasDoubleSided && m_IsDoubleSided == DoubleSided)
            return RADIENT_STATUS_NO_CHANGE;

        m_IsDoubleSided  = DoubleSided;
        m_HasDoubleSided = true;
        return RADIENT_STATUS_OK;
    }

    MATERIAL_CHANGE_FLAGS ApplyTo(SurfaceMaterialState& Target) const noexcept
    {
        MATERIAL_CHANGE_FLAGS Flags = MATERIAL_CHANGE_FLAG_NONE;

        if (m_HasSurfaceMode && Target.SurfaceMode != m_SurfaceMode)
        {
            Target.SurfaceMode = m_SurfaceMode;
            Flags |= MATERIAL_CHANGE_FLAG_SHADER_DATA |
                MATERIAL_CHANGE_FLAG_RENDER_STATE;
        }
        if (m_HasAlphaCutoff && Target.AlphaCutoff != m_AlphaCutoff)
        {
            Target.AlphaCutoff = m_AlphaCutoff;
            Flags |= MATERIAL_CHANGE_FLAG_SHADER_DATA;
        }
        if (m_HasDoubleSided && Target.IsDoubleSided != m_IsDoubleSided)
        {
            Target.IsDoubleSided = m_IsDoubleSided;
            Flags |= MATERIAL_CHANGE_FLAG_RENDER_STATE;
        }

        return Flags;
    }

private:
    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode    = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff    = 0.5f;
    Bool                          m_IsDoubleSided  = False;
    bool                          m_HasSurfaceMode = false;
    bool                          m_HasAlphaCutoff = false;
    bool                          m_HasDoubleSided = false;
};

class RadientSurfaceMaterialAssetImpl;
class RadientSurfaceMaterialWriterImpl;

using RadientSurfaceMaterialAssetImplBase =
    MaterialAssetImplBase<RadientSurfaceMaterialAssetImpl,
                          IRadientSurfaceMaterialAsset,
                          IID_RadientSurfaceMaterialAsset,
                          SurfaceMaterialState,
                          SurfaceMaterialChanges>;

class RadientSurfaceMaterialAssetImpl final : public RadientSurfaceMaterialAssetImplBase
{
public:
    using TBase = RadientSurfaceMaterialAssetImplBase;

    RadientSurfaceMaterialAssetImpl(IReferenceCounters*              pRefCounters,
                                    IRadientMaterialDefinitionAsset* pDefinition,
                                    RadientHandle                    DefinitionHandle,
                                    const MaterialAssetIdentity&     Identity) :
        TBase{pRefCounters, pDefinition, DefinitionHandle, Identity}
    {}

    virtual RADIENT_MATERIAL_SURFACE_MODE DILIGENT_CALL_TYPE GetSurfaceMode() const override final
    {
        return GetSpecializedState().SurfaceMode;
    }

    virtual Float32 DILIGENT_CALL_TYPE GetAlphaCutoff() const override final
    {
        return GetSpecializedState().AlphaCutoff;
    }

    virtual Bool DILIGENT_CALL_TYPE IsDoubleSided() const override final
    {
        return GetSpecializedState().IsDoubleSided;
    }

    RefCntAutoPtr<RadientSurfaceMaterialWriterImpl> MakeWriter();
};

using RadientSurfaceMaterialWriterImplBase =
    MaterialWriterImplBase<RadientSurfaceMaterialWriterImpl,
                           IRadientSurfaceMaterialWriter,
                           IID_RadientSurfaceMaterialWriter,
                           RadientSurfaceMaterialAssetImpl>;

class RadientSurfaceMaterialWriterImpl final : public RadientSurfaceMaterialWriterImplBase
{
public:
    using TBase = RadientSurfaceMaterialWriterImplBase;
    using TBase::TBase;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) override final
    {
        return GetSpecializedChanges().SetSurfaceMode(SurfaceMode);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetAlphaCutoff(Float32 AlphaCutoff) override final
    {
        return GetSpecializedChanges().SetAlphaCutoff(AlphaCutoff);
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetDoubleSided(Bool DoubleSided) override final
    {
        return GetSpecializedChanges().SetDoubleSided(DoubleSided);
    }
};

RefCntAutoPtr<RadientSurfaceMaterialWriterImpl> RadientSurfaceMaterialAssetImpl::MakeWriter()
{
    return RefCntAutoPtr<RadientSurfaceMaterialWriterImpl>{
        MakeNewRCObj<RadientSurfaceMaterialWriterImpl>()(this)};
}

} // namespace

RefCntAutoPtr<IRadientMaterialAsset> RadientMaterialDetail::MakeSurfaceMaterialAsset(
    IRadientMaterialDefinitionAsset* pDefinition,
    RadientHandle                    DefinitionHandle,
    const MaterialAssetIdentity&     Identity)
{
    return RefCntAutoPtr<RadientSurfaceMaterialAssetImpl>{
        MakeNewRCObj<RadientSurfaceMaterialAssetImpl>()(pDefinition, DefinitionHandle, Identity)};
}

} // namespace Diligent
