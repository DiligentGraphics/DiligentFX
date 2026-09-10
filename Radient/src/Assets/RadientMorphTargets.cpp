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
#include "Assets/RadientMorphTargetSource.hpp"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"

#include <cstddef>

namespace Diligent
{

RadientMorphTargetData::RadientMorphTargetData(const RadientMorphTargetSource& Source)
{
    VERIFY_EXPR(Source.GetStatus() == RADIENT_STATUS_OK);

    const Uint32 TargetCount = Source.GetTargetCount();

    size_t AttributeCount = 0;
    for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
        AttributeCount += Source.GetTarget(TargetIndex).Attributes.size();

    FixedLinearAllocator Allocator{GetRawAllocator()};
    Allocator.AddSpace<RadientMorphTargetDesc>(TargetCount);
    Allocator.AddSpace<RadientMorphTargetAttributeDesc>(AttributeCount);
    Allocator.AddSpace<AttributeLayout>(AttributeCount);
    for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
    {
        const RadientMorphTargetSource::Target& Target = Source.GetTarget(TargetIndex);
        Allocator.AddSpaceForString(Target.Name.c_str());
        for (const RadientMorphTargetSource::Attribute& Attribute : Target.Attributes)
        {
            Allocator.AddSpaceForString(Attribute.Semantic.c_str());
        }
    }

    Allocator.Reserve();
    const size_t MemorySize = Allocator.GetReservedSize();
    m_Memory                = PackedMemory{Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

    FixedLinearAllocator          Writer{m_Memory.get(), MemorySize};
    RadientMorphTargetDesc* const pTargetDescs      = Writer.ConstructArray<RadientMorphTargetDesc>(TargetCount);
    auto* const                   pAttributes       = Writer.ConstructArray<RadientMorphTargetAttributeDesc>(AttributeCount);
    auto* const                   pAttributeLayouts = Writer.ConstructArray<AttributeLayout>(AttributeCount);

    size_t FirstAttribute = 0;
    for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
    {
        const RadientMorphTargetSource::Target& SourceTarget = Source.GetTarget(TargetIndex);
        RadientMorphTargetDesc&                 Target       = pTargetDescs[TargetIndex];

        Target.Name           = Writer.CopyString(SourceTarget.Name.c_str());
        Target.AttributeCount = static_cast<Uint32>(SourceTarget.Attributes.size());
        Target.DefaultWeight  = SourceTarget.DefaultWeight;
        Target.pAttributes    = !SourceTarget.Attributes.empty() ? pAttributes + FirstAttribute : nullptr;
        for (Uint32 AttributeIndex = 0; AttributeIndex < SourceTarget.Attributes.size(); ++AttributeIndex)
        {
            const RadientMorphTargetSource::Attribute& SourceAttribute = SourceTarget.Attributes[AttributeIndex];
            RadientMorphTargetAttributeDesc&           Attribute       = pAttributes[FirstAttribute + AttributeIndex];
            AttributeLayout&                           Layout          = pAttributeLayouts[FirstAttribute + AttributeIndex];

            Attribute.Semantic       = Writer.CopyString(SourceAttribute.Semantic.c_str());
            Attribute.ComponentCount = SourceAttribute.ComponentCount;
            Layout.DataOffset        = SourceAttribute.DataOffset;
        }
        FirstAttribute += SourceTarget.Attributes.size();
    }

    m_Desc.pMorphTargets    = pTargetDescs;
    m_Desc.MorphTargetCount = TargetCount;
    m_pAttributes           = pAttributes;
    m_pAttributeLayouts     = pAttributeLayouts;
    m_VertexCount           = Source.GetVertexCount();
    m_DataSize              = Source.GetDataSize();
    VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
}

Uint32 RadientMorphTargetData::GetAttributeDataOffset(Uint32 TargetIndex, Uint32 AttributeIndex) const noexcept
{
    VERIFY_EXPR(TargetIndex < m_Desc.MorphTargetCount);
    const RadientMorphTargetDesc& Target = m_Desc.pMorphTargets[TargetIndex];
    VERIFY_EXPR(AttributeIndex < Target.AttributeCount);
    const size_t DataIndex = static_cast<size_t>(Target.pAttributes - m_pAttributes) + AttributeIndex;
    return m_pAttributeLayouts[DataIndex].DataOffset;
}

} // namespace Diligent
