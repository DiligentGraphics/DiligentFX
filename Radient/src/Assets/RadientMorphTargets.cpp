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

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "Errors.hpp"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <cstring>
#include <limits>
#include <vector>

namespace Diligent
{

RadientMorphTargetData::RadientMorphTargetData(const RadientMorphTargetCreateInfo* pTargets,
                                               Uint32                              TargetCount,
                                               Uint32                              VertexCount)
{
    VERIFY_EXPR(pTargets != nullptr && TargetCount != 0 && VertexCount != 0);

    size_t AttributeCount = 0;
    for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
        AttributeCount += pTargets[TargetIndex].Desc.AttributeCount;

    FixedLinearAllocator Allocator{GetRawAllocator()};
    Allocator.AddSpace<RadientMorphTargetDesc>(TargetCount);
    Allocator.AddSpace<RadientMorphTargetAttributeDesc>(AttributeCount);
    Allocator.AddSpace<AttributeData>(AttributeCount);
    for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
    {
        const RadientMorphTargetDesc& Target = pTargets[TargetIndex].Desc;
        Allocator.AddSpaceForString(Target.Name != nullptr ? Target.Name : "");
        for (Uint32 AttributeIndex = 0; AttributeIndex < Target.AttributeCount; ++AttributeIndex)
        {
            const RadientMorphTargetAttributeDesc& Attribute = Target.pAttributes[AttributeIndex];
            Allocator.AddSpaceForString(Attribute.Semantic);
            Allocator.AddSpace<Float32>(size_t{VertexCount} * Attribute.ComponentCount);
        }
    }

    Allocator.Reserve();
    const size_t MemorySize = Allocator.GetReservedSize();
    m_Memory                = PackedMemory{Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

    FixedLinearAllocator          Writer{m_Memory.get(), MemorySize};
    RadientMorphTargetDesc* const pTargetDescs   = Writer.ConstructArray<RadientMorphTargetDesc>(TargetCount);
    auto* const                   pAttributes    = Writer.ConstructArray<RadientMorphTargetAttributeDesc>(AttributeCount);
    auto* const                   pAttributeData = Writer.ConstructArray<AttributeData>(AttributeCount);

    size_t FirstAttribute = 0;
    for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
    {
        const RadientMorphTargetCreateInfo& SourceTargetCI = pTargets[TargetIndex];
        const RadientMorphTargetDesc&       SourceTarget   = SourceTargetCI.Desc;
        RadientMorphTargetDesc&             Target         = pTargetDescs[TargetIndex];

        Target.Name           = Writer.CopyString(SourceTarget.Name != nullptr ? SourceTarget.Name : "");
        Target.AttributeCount = SourceTarget.AttributeCount;
        Target.DefaultWeight  = SourceTarget.DefaultWeight;
        Target.pAttributes    = SourceTarget.AttributeCount != 0 ? pAttributes + FirstAttribute : nullptr;
        for (Uint32 AttributeIndex = 0; AttributeIndex < SourceTarget.AttributeCount; ++AttributeIndex)
        {
            const RadientMorphTargetAttributeDesc& SourceAttribute     = SourceTarget.pAttributes[AttributeIndex];
            const auto&                            SourceAttributeData = SourceTargetCI.pAttributeData[AttributeIndex];
            RadientMorphTargetAttributeDesc&       Attribute           = pAttributes[FirstAttribute + AttributeIndex];
            auto&                                  AttributeData       = pAttributeData[FirstAttribute + AttributeIndex];

            Attribute.Semantic       = Writer.CopyString(SourceAttribute.Semantic);
            Attribute.ComponentCount = SourceAttribute.ComponentCount;
            AttributeData.pDeltas    = Writer.CopyArray(SourceAttributeData.pDeltas,
                                                        size_t{VertexCount} * SourceAttribute.ComponentCount);
        }
        FirstAttribute += SourceTarget.AttributeCount;
    }

    m_Desc.pMorphTargets    = pTargetDescs;
    m_Desc.MorphTargetCount = TargetCount;
    m_pAttributes           = pAttributes;
    m_pAttributeData        = pAttributeData;
    VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
}

const Float32* RadientMorphTargetData::GetDeltas(Uint32 TargetIndex, Uint32 AttributeIndex) const noexcept
{
    VERIFY_EXPR(TargetIndex < m_Desc.MorphTargetCount);
    const RadientMorphTargetDesc& Target = m_Desc.pMorphTargets[TargetIndex];
    VERIFY_EXPR(AttributeIndex < Target.AttributeCount);
    const size_t DataIndex = static_cast<size_t>(Target.pAttributes - m_pAttributes) + AttributeIndex;
    return m_pAttributeData[DataIndex].pDeltas;
}

namespace
{

class RadientMorphTargetWeightsImpl final : public ObjectBase<IRadientMorphTargetWeights>
{
public:
    using TBase = ObjectBase<IRadientMorphTargetWeights>;

    RadientMorphTargetWeightsImpl(IReferenceCounters*         pRefCounters,
                                  IRadientMeshAsset*          pMesh,
                                  const RadientMeshAssetDesc& MeshDesc) :
        TBase{pRefCounters},
        m_pMesh{pMesh},
        m_Weights(MeshDesc.MorphTargetCount)
    {
        VERIFY_EXPR(m_pMesh != nullptr);
        for (Uint32 TargetIndex = 0; TargetIndex < MeshDesc.MorphTargetCount; ++TargetIndex)
            m_Weights[TargetIndex] = MeshDesc.pMorphTargets[TargetIndex].DefaultWeight;
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMorphTargetWeights, TBase)

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
        if (FirstTarget > TotalWeightCount || WeightCount > TotalWeightCount - FirstTarget)
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
    const RefCntAutoPtr<IRadientMeshAsset> m_pMesh;
    std::vector<Float32>                   m_Weights;
    Uint64                                 m_Version = 1;
};

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
