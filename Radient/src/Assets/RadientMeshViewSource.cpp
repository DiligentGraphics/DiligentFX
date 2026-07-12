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

#include "Assets/RadientMeshViewSource.hpp"

#include "XXH128Hasher.hpp"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

void UpdateString(XXH128State& Hasher, const Char* Str)
{
    const Uint64 Len = Str != nullptr ? static_cast<Uint64>(std::strlen(Str)) : 0;
    Hasher.Update(Len);
    if (Len != 0)
        Hasher.UpdateRaw(Str, Len);
}

void HashMaterial(XXH128State& Hasher, IRadientMaterialAsset* pMaterial)
{
    const bool HasMaterial = pMaterial != nullptr;
    Hasher.Update(HasMaterial);
    if (!HasMaterial)
        return;

    const RadientAssetReference& MaterialRef = pMaterial->GetReference();
    const bool                   HasURI      = MaterialRef.URI != nullptr && MaterialRef.URI[0] != '\0';
    Hasher.Update(HasURI);
    if (HasURI)
    {
        UpdateString(Hasher, MaterialRef.URI);
        Hasher.Update(MaterialRef.Version);
    }
    else
    {
        Hasher.Update(reinterpret_cast<uintptr_t>(pMaterial));
    }
}

bool ValidateMeshViewCreateInfo(const RadientMeshViewCreateInfo& CI,
                                const Uint32*                    pGeometryIndexCounts,
                                Uint32                           GeometryCount)
{
    if (GeometryCount == 0 ||
        pGeometryIndexCounts == nullptr ||
        CI.PrimitiveCount == 0 ||
        CI.pPrimitives == nullptr)
    {
        return false;
    }

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < CI.PrimitiveCount; ++PrimitiveIndex)
    {
        const Uint32 GeometryIndex = CI.pGeometryIndices != nullptr ? CI.pGeometryIndices[PrimitiveIndex] : 0;
        if (GeometryIndex >= GeometryCount)
            return false;

        const Uint32                          IndexCount  = pGeometryIndexCounts[GeometryIndex];
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = CI.pPrimitives[PrimitiveIndex];
        if (IndexCount == 0 ||
            PrimitiveCI.IndexCount == 0 ||
            PrimitiveCI.FirstIndex >= IndexCount ||
            PrimitiveCI.IndexCount > IndexCount - PrimitiveCI.FirstIndex)
        {
            return false;
        }
    }

    return true;
}

} // namespace

RadientMeshViewGeometryRemap BuildMeshViewGeometryRemap(const Uint32* pGeometryIndices,
                                                        Uint32        PrimitiveCount,
                                                        Uint32        GeometryCount)
{
    RadientMeshViewGeometryRemap Remap;
    if (PrimitiveCount == 0 || GeometryCount == 0)
    {
        Remap.Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return Remap;
    }

    static constexpr Uint32 InvalidGeometryIndex = ~Uint32{0};

    // Only geometry referenced by primitives contributes to the view identity.
    // Keep first-use order stable so unused geometry entries do not affect
    // cache reuse or the stored payload representation.
    Remap.UsedGeometryIndices.reserve(PrimitiveCount);
    Remap.PrimitiveGeometryIndices.resize(PrimitiveCount);

    std::vector<Uint32> GeometryIndexMap(GeometryCount, InvalidGeometryIndex);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < PrimitiveCount; ++PrimitiveIndex)
    {
        const Uint32 GeometryIndex = pGeometryIndices != nullptr ? pGeometryIndices[PrimitiveIndex] : 0;
        if (GeometryIndex >= GeometryCount)
        {
            Remap.Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return Remap;
        }

        Uint32& MappedIndex = GeometryIndexMap[GeometryIndex];
        if (MappedIndex == InvalidGeometryIndex)
        {
            MappedIndex = static_cast<Uint32>(Remap.UsedGeometryIndices.size());
            Remap.UsedGeometryIndices.push_back(GeometryIndex);
        }

        Remap.PrimitiveGeometryIndices[PrimitiveIndex] = MappedIndex;
    }

    return Remap;
}

RadientMeshViewSource::RadientMeshViewSource(const RadientMeshViewCreateInfo& CI,
                                             Uint32                           IndexCount) :
    RadientMeshViewSource{CI, &IndexCount, 1}
{
}

RadientMeshViewSource::RadientMeshViewSource(const RadientMeshViewCreateInfo& CI,
                                             const Uint32*                    pGeometryIndexCounts,
                                             Uint32                           GeometryCount)
{
    if (!ValidateMeshViewCreateInfo(CI, pGeometryIndexCounts, GeometryCount))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    m_Primitives.assign(CI.pPrimitives, CI.pPrimitives + CI.PrimitiveCount);
    m_GeometryIndices.resize(CI.PrimitiveCount);
    m_PrimitiveNames.resize(CI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < CI.PrimitiveCount; ++PrimitiveIndex)
    {
        m_GeometryIndices[PrimitiveIndex] = CI.pGeometryIndices != nullptr ? CI.pGeometryIndices[PrimitiveIndex] : 0;

        const Char* const Name = CI.pPrimitives[PrimitiveIndex].Name;
        if (Name != nullptr)
            m_PrimitiveNames[PrimitiveIndex] = Name;
    }
    BindPrimitiveNames();

    m_Materials.reserve(CI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < CI.PrimitiveCount; ++PrimitiveIndex)
        m_Materials.emplace_back(CI.pPrimitives[PrimitiveIndex].pMaterial);
}

RadientMeshViewSource::RadientMeshViewSource(RadientMeshViewSource&& Rhs) noexcept :
    m_Status{Rhs.m_Status},
    m_Primitives{std::move(Rhs.m_Primitives)},
    m_GeometryIndices{std::move(Rhs.m_GeometryIndices)},
    m_PrimitiveNames{std::move(Rhs.m_PrimitiveNames)},
    m_Materials{std::move(Rhs.m_Materials)}
{
    BindPrimitiveNames();
}

void RadientMeshViewSource::BindPrimitiveNames() noexcept
{
    VERIFY_EXPR(m_Primitives.size() == m_PrimitiveNames.size());
    if (m_Primitives.size() != m_PrimitiveNames.size())
        return;

    for (size_t PrimitiveIndex = 0; PrimitiveIndex < m_Primitives.size(); ++PrimitiveIndex)
    {
        m_Primitives[PrimitiveIndex].Name =
            m_Primitives[PrimitiveIndex].Name != nullptr ? m_PrimitiveNames[PrimitiveIndex].c_str() : nullptr;
    }
}

std::string RadientMeshViewSource::MakeCacheKey(const char* GeometryCacheKey) const
{
    if (GeometryCacheKey == nullptr)
        return {};

    return MakeCacheKey({std::string{GeometryCacheKey}});
}

std::string RadientMeshViewSource::MakeCacheKey(const std::vector<std::string>& GeometryCacheKeys) const
{
    if (RADIENT_FAILED(m_Status) ||
        GeometryCacheKeys.empty() ||
        m_GeometryIndices.size() != m_Primitives.size())
    {
        return {};
    }

    const RadientMeshViewGeometryRemap Remap =
        BuildMeshViewGeometryRemap(m_GeometryIndices.data(),
                                   static_cast<Uint32>(m_GeometryIndices.size()),
                                   static_cast<Uint32>(GeometryCacheKeys.size()));
    if (RADIENT_FAILED(Remap.Status))
        return {};

    for (const Uint32 GeometryIndex : Remap.UsedGeometryIndices)
    {
        if (GeometryCacheKeys[GeometryIndex].empty())
            return {};
    }

    XXH128State Hasher;
    Hasher.Update(Uint32{3}); // Mesh view cache key version.

    Hasher.Update(static_cast<Uint64>(Remap.UsedGeometryIndices.size()));
    for (const Uint32 GeometryIndex : Remap.UsedGeometryIndices)
    {
        UpdateString(Hasher, GeometryCacheKeys[GeometryIndex].c_str());
    }

    Hasher.Update(static_cast<Uint64>(m_Primitives.size()));
    for (size_t PrimitiveIndex = 0; PrimitiveIndex < m_Primitives.size(); ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& Primitive = m_Primitives[PrimitiveIndex];
        Hasher.Update(Remap.PrimitiveGeometryIndices[PrimitiveIndex]);
        Hasher.Update(Primitive.FirstIndex,
                      Primitive.IndexCount);
        HashMaterial(Hasher, m_Materials[PrimitiveIndex]);
    }

    return std::string{"mesh-view:"} + Hasher.Digest().ToString();
}

} // namespace Diligent
