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

#include "Assets/RadientMorphTargetSource.hpp"

#include "Core/RadientValidation.hpp"
#include "Math/RadientMath.hpp"
#include "XXH128Hasher.hpp"

#include <algorithm>
#include <cstring>

namespace Diligent
{

namespace
{

constexpr Uint32 MorphTargetSourceCacheKeyVersion = 1;

bool IsStandardMorphTargetSemantic(const Char* Semantic) noexcept
{
    return (std::strcmp(Semantic, RadientMorphTargetPositionSemantic) == 0 ||
            std::strcmp(Semantic, RadientMorphTargetNormalSemantic) == 0 ||
            std::strcmp(Semantic, RadientMorphTargetTangentSemantic) == 0);
}

void UpdateString(XXH128State& Hasher, const std::string& String) noexcept
{
    Hasher.Update(static_cast<Uint64>(String.size()));
    if (!String.empty())
        Hasher.UpdateRaw(String.data(), static_cast<Uint64>(String.size()));
}

} // namespace

RadientMorphTargetSource::RadientMorphTargetSource(const RadientMorphTargetCreateInfo* pTargets,
                                                   Uint32                              TargetCount,
                                                   Uint32                              VertexCount)
{
    if (pTargets == nullptr || TargetCount == 0 || VertexCount == 0)
        return;

    std::vector<Target> Targets;
    Targets.reserve(TargetCount);

    Uint64 DataSize = 0;
    for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
    {
        const RadientMorphTargetCreateInfo& SourceTarget = pTargets[TargetIndex];
        if (!RadientMath::IsFinite(SourceTarget.Desc.DefaultWeight) ||
            (SourceTarget.Desc.AttributeCount != 0 &&
             (SourceTarget.Desc.pAttributes == nullptr || SourceTarget.pAttributeData == nullptr)))
        {
            return;
        }

        Target& TargetData       = Targets.emplace_back();
        TargetData.Name          = SourceTarget.Desc.Name != nullptr ? SourceTarget.Desc.Name : "";
        TargetData.DefaultWeight = SourceTarget.Desc.DefaultWeight;
        TargetData.Attributes.reserve(SourceTarget.Desc.AttributeCount);

        for (Uint32 AttributeIndex = 0; AttributeIndex < SourceTarget.Desc.AttributeCount; ++AttributeIndex)
        {
            const RadientMorphTargetAttributeDesc&       SourceAttribute = SourceTarget.Desc.pAttributes[AttributeIndex];
            const RadientMorphTargetAttributeCreateInfo& SourceData      = SourceTarget.pAttributeData[AttributeIndex];
            if (SourceAttribute.Semantic == nullptr || *SourceAttribute.Semantic == '\0' ||
                SourceAttribute.ComponentCount == 0 || SourceAttribute.ComponentCount > 4 ||
                SourceData.pDeltaBuffer == nullptr ||
                (IsStandardMorphTargetSemantic(SourceAttribute.Semantic) && SourceAttribute.ComponentCount != 3))
            {
                return;
            }

            const auto DuplicateSemantic =
                std::find_if(TargetData.Attributes.begin(), TargetData.Attributes.end(),
                             [&SourceAttribute](const Attribute& AttributeData) {
                                 return AttributeData.Semantic == SourceAttribute.Semantic;
                             });
            if (DuplicateSemantic != TargetData.Attributes.end())
                return;

            const Uint64 AttributeDataSize =
                Uint64{VertexCount} * SourceAttribute.ComponentCount * sizeof(Float32);
            if (!RadientValidation::IsSumRepresentable<Uint32>(DataSize, AttributeDataSize))
                return;

            RadientDataBlobReadAccess Buffer{SourceData.pDeltaBuffer};
            if (!Buffer)
            {
                m_Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }
            if (!RadientValidation::IsAddressableSize(Buffer.GetSize()) ||
                !RadientValidation::IsValidSubrange(SourceData.ByteOffset, AttributeDataSize, Buffer.GetSize()))
            {
                return;
            }
            if (Buffer.GetData() == nullptr)
            {
                m_Status = RADIENT_STATUS_INVALID_DATA;
                return;
            }

            Attribute& AttributeData     = TargetData.Attributes.emplace_back();
            AttributeData.Semantic       = SourceAttribute.Semantic;
            AttributeData.ComponentCount = SourceAttribute.ComponentCount;
            AttributeData.DataOffset     = static_cast<Uint32>(DataSize);
            AttributeData.pData          = static_cast<const Uint8*>(Buffer.GetData()) + static_cast<size_t>(SourceData.ByteOffset);
            AttributeData.Buffer         = std::move(Buffer);
            DataSize += AttributeDataSize;
        }
    }

    m_VertexCount = VertexCount;
    m_DataSize    = static_cast<Uint32>(DataSize);
    m_Targets     = std::move(Targets);
    m_Status      = RADIENT_STATUS_OK;
}

std::string RadientMorphTargetSource::MakeCacheKey() const
{
    if (RADIENT_FAILED(m_Status))
        return {};

    XXH128State Hasher;
    Hasher.Update(MorphTargetSourceCacheKeyVersion,
                  m_VertexCount,
                  static_cast<Uint32>(m_Targets.size()));
    for (const Target& TargetData : m_Targets)
    {
        UpdateString(Hasher, TargetData.Name);
        Hasher.Update(TargetData.DefaultWeight,
                      static_cast<Uint32>(TargetData.Attributes.size()));
        for (const Attribute& AttributeData : TargetData.Attributes)
        {
            UpdateString(Hasher, AttributeData.Semantic);
            Hasher.Update(AttributeData.ComponentCount,
                          AttributeData.DataOffset);
        }
    }

    Hasher.Update(GetDataSize());
    // Hash only the delta ranges, in packed order, independently of blob layout.
    for (const Target& TargetData : m_Targets)
    {
        for (const Attribute& AttributeData : TargetData.Attributes)
        {
            const Uint64 AttributeDataSize = Uint64{m_VertexCount} * AttributeData.ComponentCount * sizeof(Float32);
            Hasher.UpdateRaw(AttributeData.pData, AttributeDataSize);
        }
    }

    return std::string{"mesh-morph-target-data:"} + Hasher.Digest().ToString();
}

RADIENT_STATUS RadientMorphTargetSource::PackData(PackDestination Destination) const noexcept
{
    if (RADIENT_FAILED(m_Status))
        return m_Status;

    const Uint32 DataSize = GetDataSize();
    if (DataSize == 0)
        return RADIENT_STATUS_OK;
    if (Destination.pData == nullptr || Destination.DataSize < DataSize)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    for (const Target& TargetData : m_Targets)
    {
        for (const Attribute& AttributeData : TargetData.Attributes)
        {
            const size_t AttributeDataSize = size_t{m_VertexCount} * AttributeData.ComponentCount * sizeof(Float32);
            std::memcpy(static_cast<Uint8*>(Destination.pData) + AttributeData.DataOffset,
                        AttributeData.pData, AttributeDataSize);
        }
    }
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
