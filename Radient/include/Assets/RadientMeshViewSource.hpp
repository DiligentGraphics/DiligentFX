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

#include "DebugUtilities.hpp"
#include "RadientAssets.h"
#include "RefCntAutoPtr.hpp"

#include <string>
#include <vector>

namespace Diligent
{

struct RadientMeshViewCreateInfo
{
    const RadientMeshPrimitiveCreateInfo* pPrimitives    = nullptr;
    Uint32                                PrimitiveCount = 0;

    /// Optional per-primitive geometry indices. If null, all primitives use geometry 0.
    const Uint32* pGeometryIndices = nullptr;
};

struct RadientMeshViewGeometryRemap
{
    /// OK if the remap was built, INVALID_ARGUMENT if any primitive references
    /// a geometry outside the caller-provided geometry array.
    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    /// Original geometry indices referenced by primitives, in first-use order.
    /// Unused geometries are omitted so equivalent views produce equivalent
    /// cache keys and payload representations.
    std::vector<Uint32> UsedGeometryIndices;

    /// Per-primitive geometry indices remapped into UsedGeometryIndices.
    /// For example, original primitive geometry indices [3, 1, 3] produce
    /// UsedGeometryIndices [3, 1] and PrimitiveGeometryIndices [0, 1, 0].
    std::vector<Uint32> PrimitiveGeometryIndices;
};

/// Builds the canonical mesh-view geometry mapping used by both cache-key
/// generation and payload construction.
///
/// pGeometryIndices is the optional per-primitive geometry index array from
/// RadientMeshViewCreateInfo. If it is null, every primitive uses geometry 0.
/// The returned mapping removes unused geometries and remaps primitive geometry
/// indices into compact first-use order.
RadientMeshViewGeometryRemap BuildMeshViewGeometryRemap(const Uint32* pGeometryIndices,
                                                        Uint32        PrimitiveCount,
                                                        Uint32        GeometryCount);

/// Owns mesh primitive ranges and material references that view shared mesh geometry.
class RadientMeshViewSource final
{
public:
    RadientMeshViewSource(const RadientMeshViewCreateInfo& CI,
                          Uint32                           IndexCount);

    RadientMeshViewSource(const RadientMeshViewCreateInfo& CI,
                          const Uint32*                    pGeometryIndexCounts,
                          Uint32                           GeometryCount);

    RadientMeshViewSource(RadientMeshViewSource&& Rhs) noexcept;

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    Uint32 GetPrimitiveCount() const
    {
        return static_cast<Uint32>(m_Primitives.size());
    }

    const RadientMeshPrimitiveCreateInfo& GetPrimitive(Uint32 PrimitiveIndex) const
    {
        VERIFY(PrimitiveIndex < m_Primitives.size(), "Invalid primitive index");
        return m_Primitives[PrimitiveIndex];
    }

    Uint32 GetGeometryIndex(Uint32 PrimitiveIndex) const
    {
        VERIFY(PrimitiveIndex < m_GeometryIndices.size(), "Invalid primitive index");
        return PrimitiveIndex < m_GeometryIndices.size() ? m_GeometryIndices[PrimitiveIndex] : 0;
    }

    IRadientMaterialAsset* GetMaterial(Uint32 PrimitiveIndex) const
    {
        VERIFY(PrimitiveIndex < m_Materials.size(), "Invalid primitive index");
        return PrimitiveIndex < m_Materials.size() ? m_Materials[PrimitiveIndex].RawPtr() : nullptr;
    }

    std::string MakeCacheKey(const char* GeometryCacheKey) const;
    std::string MakeCacheKey(const std::vector<std::string>& GeometryCacheKeys) const;

private:
    void BindPrimitiveNames() noexcept;

private:
    RADIENT_STATUS m_Status = RADIENT_STATUS_OK;

    std::vector<RadientMeshPrimitiveCreateInfo>       m_Primitives;
    std::vector<Uint32>                               m_GeometryIndices;
    std::vector<std::string>                          m_PrimitiveNames;
    std::vector<RefCntAutoPtr<IRadientMaterialAsset>> m_Materials;
};

} // namespace Diligent
