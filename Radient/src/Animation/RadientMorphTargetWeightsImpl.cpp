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

#include "Assets/RadientMorphTargetData.hpp"
#include "Core/RadientValidation.hpp"
#include "RadientAnimation.h"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

class RadientMorphTargetWeightsImpl;
class RadientMorphTargetWeightsAnimationDestinationBindingImpl;

class RadientMorphTargetWeightsAnimationDestinationImpl final : public IRadientAnimationDestination
{
public:
    explicit RadientMorphTargetWeightsAnimationDestinationImpl(RadientMorphTargetWeightsImpl& Weights) noexcept :
        m_Weights{Weights}
    {}

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final;
    using IObject::QueryInterface;

    virtual ReferenceCounterValueType DILIGENT_CALL_TYPE AddRef() override final;
    virtual ReferenceCounterValueType DILIGENT_CALL_TYPE Release() override final;
    virtual IReferenceCounters* DILIGENT_CALL_TYPE       GetReferenceCounters() const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateBinding(
        const RadientAnimationPropertyBindingDesc* pProperties,
        Uint32                                     PropertyCount,
        RadientAnimationResolvedPropertyDesc*      pResolvedProperties,
        IRadientAnimationDestinationBinding**      ppBinding) override final;

private:
    RadientMorphTargetWeightsImpl& m_Weights;
};

class RadientMorphTargetWeightsImpl final : public ObjectBase<IRadientMorphTargetWeights>
{
public:
    using TBase = ObjectBase<IRadientMorphTargetWeights>;

    RadientMorphTargetWeightsImpl(IReferenceCounters*         pRefCounters,
                                  IRadientMeshAsset*          pMesh,
                                  const RadientMeshAssetDesc& MeshDesc) :
        TBase{pRefCounters},
        m_pMesh{pMesh},
        m_Weights(MeshDesc.MorphTargetCount),
        m_AnimationDestination{*this}
    {
        VERIFY_EXPR(m_pMesh != nullptr);
        for (Uint32 TargetIndex = 0; TargetIndex < MeshDesc.MorphTargetCount; ++TargetIndex)
            m_Weights[TargetIndex] = MeshDesc.pMorphTargets[TargetIndex].DefaultWeight;
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientMorphTargetWeights)
        {
            *ppInterface = static_cast<IRadientMorphTargetWeights*>(this);
            (*ppInterface)->AddRef();
        }
        else if (IID == IID_RadientAnimationDestination)
        {
            *ppInterface = static_cast<IRadientAnimationDestination*>(&m_AnimationDestination);
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual IRadientMeshAsset* DILIGENT_CALL_TYPE GetMesh() const override final
    {
        return m_pMesh;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_Version;
    }

    virtual Uint32 DILIGENT_CALL_TYPE GetWeightCount() const override final
    {
        return static_cast<Uint32>(m_Weights.size());
    }

    virtual const Float32* DILIGENT_CALL_TYPE GetWeights() const override final
    {
        return m_Weights.empty() ? nullptr : m_Weights.data();
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetWeights(Uint32         FirstTarget,
                                                         Uint32         WeightCount,
                                                         const Float32* pWeights) override final
    {
        const Uint32 TotalWeightCount = static_cast<Uint32>(m_Weights.size());
        if (!RadientValidation::IsValidSubrange(FirstTarget, WeightCount, TotalWeightCount))
            return RADIENT_STATUS_INVALID_ARGUMENT;
        if (WeightCount == 0)
            return RADIENT_STATUS_NO_CHANGE;
        if (pWeights == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        if (!AdvanceVersion())
            return RADIENT_STATUS_INVALID_OPERATION;

        std::memcpy(m_Weights.data() + FirstTarget, pWeights, sizeof(*pWeights) * WeightCount);
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ResetToDefaults() override final
    {
        if (m_Weights.empty())
            return RADIENT_STATUS_NO_CHANGE;
        if (!AdvanceVersion())
            return RADIENT_STATUS_INVALID_OPERATION;

        const RadientMeshAssetDesc& MeshDesc = m_pMesh->GetDesc();
        VERIFY_EXPR(MeshDesc.MorphTargetCount == m_Weights.size());
        for (Uint32 TargetIndex = 0; TargetIndex < MeshDesc.MorphTargetCount; ++TargetIndex)
            m_Weights[TargetIndex] = MeshDesc.pMorphTargets[TargetIndex].DefaultWeight;
        return RADIENT_STATUS_OK;
    }

private:
    friend class RadientMorphTargetWeightsAnimationDestinationBindingImpl;
    friend class RadientMorphTargetWeightsAnimationDestinationImpl;

    bool AdvanceVersion() noexcept
    {
        if (m_Version == (std::numeric_limits<Uint64>::max)())
        {
            LOG_ERROR_MESSAGE("Morph-target weight version is exhausted");
            return false;
        }
        ++m_Version;
        return true;
    }

private:
    const RefCntAutoPtr<IRadientMeshAsset>            m_pMesh;
    std::vector<Float32>                              m_Weights;
    Uint64                                            m_Version = 1;
    RadientMorphTargetWeightsAnimationDestinationImpl m_AnimationDestination;
};

struct MorphTargetWeightsAnimationRange
{
    Uint32 First = 0;
    Uint32 End   = 0;

    bool operator<(const MorphTargetWeightsAnimationRange& Rhs) const noexcept
    {
        return First < Rhs.First;
    }
};

class RadientMorphTargetWeightsAnimationDestinationBindingImpl final : public ObjectBase<IRadientAnimationDestinationBinding>
{
public:
    using TBase = ObjectBase<IRadientAnimationDestinationBinding>;

    RadientMorphTargetWeightsAnimationDestinationBindingImpl(IReferenceCounters*            pRefCounters,
                                                             IRadientAnimationDestination*  pDestination,
                                                             RadientMorphTargetWeightsImpl& Weights,
                                                             std::vector<void*>             Outputs) :
        TBase{pRefCounters},
        m_pDestination{pDestination},
        m_Weights{Weights},
        m_Outputs{std::move(Outputs)}
    {
        VERIFY_EXPR(m_pDestination != nullptr);
        VERIFY_EXPR(!m_Outputs.empty());
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationDestinationBinding, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE BeginUpdate(void* const** ppOutputs) override final
    {
        if (ppOutputs == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppOutputs = nullptr;

        if (m_Weights.m_Version == (std::numeric_limits<Uint64>::max)())
        {
            LOG_ERROR_MESSAGE("Morph-target weight version is exhausted");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        *ppOutputs = m_Outputs.data();
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE EndUpdate(Bool UpdateDerivedState) override final
    {
        static_cast<void>(UpdateDerivedState);
        ++m_Weights.m_Version;
        return RADIENT_STATUS_OK;
    }

private:
    const RefCntAutoPtr<IRadientAnimationDestination> m_pDestination;
    RadientMorphTargetWeightsImpl&                    m_Weights;
    const std::vector<void*>                          m_Outputs;
};

void RadientMorphTargetWeightsAnimationDestinationImpl::QueryInterface(const INTERFACE_ID& IID,
                                                                       IObject**           ppInterface)
{
    m_Weights.QueryInterface(IID, ppInterface);
}

ReferenceCounterValueType RadientMorphTargetWeightsAnimationDestinationImpl::AddRef()
{
    return m_Weights.AddRef();
}

ReferenceCounterValueType RadientMorphTargetWeightsAnimationDestinationImpl::Release()
{
    return m_Weights.Release();
}

IReferenceCounters* RadientMorphTargetWeightsAnimationDestinationImpl::GetReferenceCounters() const
{
    return m_Weights.GetReferenceCounters();
}

RADIENT_STATUS RadientMorphTargetWeightsAnimationDestinationImpl::CreateBinding(
    const RadientAnimationPropertyBindingDesc* pProperties,
    Uint32                                     PropertyCount,
    RadientAnimationResolvedPropertyDesc*      pResolvedProperties,
    IRadientAnimationDestinationBinding**      ppBinding)
{
    if (ppBinding == nullptr || *ppBinding != nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (PropertyCount == 0 || pProperties == nullptr || pResolvedProperties == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (!RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationPropertyBindingDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationResolvedPropertyDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(MorphTargetWeightsAnimationRange)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(void*)))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    try
    {
        const Uint32 WeightCount = static_cast<Uint32>(m_Weights.m_Weights.size());

        std::vector<MorphTargetWeightsAnimationRange> Ranges;
        std::vector<void*>                            Outputs;
        Ranges.reserve(PropertyCount);
        Outputs.reserve(PropertyCount);
        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            const RadientAnimationPropertyBindingDesc& Property = pProperties[PropertyIndex];
            if (Property.Schema == InvalidRadientAnimationSchemaID ||
                Property.Property == InvalidRadientAnimationPropertyID ||
                Property.Value.Type <= RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN ||
                Property.Value.Type >= RADIENT_ANIMATION_VALUE_TYPE_COUNT ||
                Property.Value.ArraySize == 0)
            {
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }

            if (Property.Schema != RadientMorphWeightsAnimationSchemaID ||
                Property.Property != RadientMorphWeightsProperty ||
                Property.Value.Type != RADIENT_ANIMATION_VALUE_TYPE_FLOAT)
            {
                return RADIENT_STATUS_UNSUPPORTED;
            }

            if (Property.DestinationElement != 0)
                return RADIENT_STATUS_NOT_FOUND;

            if (!RadientValidation::IsValidSubrange(Property.FirstArrayElement,
                                                    Property.Value.ArraySize,
                                                    WeightCount))
            {
                return RADIENT_STATUS_UNSUPPORTED;
            }

            const Uint32 End = Property.FirstArrayElement + Property.Value.ArraySize;
            Ranges.push_back({Property.FirstArrayElement, End});
            Outputs.push_back(m_Weights.m_Weights.data() + Property.FirstArrayElement);
        }

        std::sort(Ranges.begin(), Ranges.end());
        for (size_t RangeIndex = 1; RangeIndex < Ranges.size(); ++RangeIndex)
        {
            if (Ranges[RangeIndex].First < Ranges[RangeIndex - 1].End)
                return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        RefCntAutoPtr<RadientMorphTargetWeightsAnimationDestinationBindingImpl> pBinding{
            MakeNewRCObj<RadientMorphTargetWeightsAnimationDestinationBindingImpl>()(
                static_cast<IRadientAnimationDestination*>(this),
                m_Weights,
                std::move(Outputs))};

        for (Uint32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
        {
            pResolvedProperties[PropertyIndex].Semantic =
                RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
        }

        *ppBinding = pBinding.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a morph-target weight animation binding: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create a morph-target weight animation binding due to an unknown error");
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace

RADIENT_STATUS CreateRadientMorphTargetWeights(IRadientMeshAsset*           pMesh,
                                               const RadientMeshAssetDesc&  MeshDesc,
                                               IRadientMorphTargetWeights** ppWeights)
{
    if (ppWeights == nullptr || pMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppWeights == nullptr, "Output morph-target weights pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppWeights = nullptr;

    try
    {
        RefCntAutoPtr<RadientMorphTargetWeightsImpl> pWeights{
            MakeNewRCObj<RadientMorphTargetWeightsImpl>()(pMesh, MeshDesc)};
        *ppWeights = pWeights.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient morph-target weights: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
