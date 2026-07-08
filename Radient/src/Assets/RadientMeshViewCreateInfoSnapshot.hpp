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

#include "Assets/RadientMeshViewSource.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

// Owns a stable copy of RadientMeshViewCreateInfo data for asynchronous mesh
// view creation. The public create-info struct contains borrowed arrays,
// strings, and material pointers, so the worker task must use this snapshot
// after CreateMeshView() returns.
class MeshViewCreateInfoSnapshot
{
public:
    explicit MeshViewCreateInfoSnapshot(const RadientMeshViewCreateInfo& CI)
    {
        if (CI.PrimitiveCount == 0 || CI.pPrimitives == nullptr)
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }

        m_Primitives.assign(CI.pPrimitives, CI.pPrimitives + CI.PrimitiveCount);
        m_PrimitiveNames.resize(m_Primitives.size());
        m_Materials.reserve(m_Primitives.size());

        for (size_t PrimitiveIndex = 0; PrimitiveIndex < m_Primitives.size(); ++PrimitiveIndex)
        {
            const Char* const Name = CI.pPrimitives[PrimitiveIndex].Name;
            if (Name != nullptr)
                m_PrimitiveNames[PrimitiveIndex] = Name;

            m_Materials.emplace_back(CI.pPrimitives[PrimitiveIndex].pMaterial);
        }

        if (CI.pGeometryIndices != nullptr)
            m_GeometryIndices.assign(CI.pGeometryIndices, CI.pGeometryIndices + CI.PrimitiveCount);

        BindPointers();
    }

    MeshViewCreateInfoSnapshot(MeshViewCreateInfoSnapshot&& Rhs) noexcept :
        m_Status{Rhs.m_Status},
        m_Primitives{std::move(Rhs.m_Primitives)},
        m_GeometryIndices{std::move(Rhs.m_GeometryIndices)},
        m_PrimitiveNames{std::move(Rhs.m_PrimitiveNames)},
        m_Materials{std::move(Rhs.m_Materials)}
    {
        BindPointers();
    }

    // clang-format off
    MeshViewCreateInfoSnapshot           (const MeshViewCreateInfoSnapshot&) = delete;
    MeshViewCreateInfoSnapshot& operator=(const MeshViewCreateInfoSnapshot&) = delete;
    MeshViewCreateInfoSnapshot& operator=(MeshViewCreateInfoSnapshot&&)      = delete;
    // clang-format on

    RadientMeshViewCreateInfo GetCreateInfo() const
    {
        RadientMeshViewCreateInfo CI;
        CI.pPrimitives      = m_Primitives.data();
        CI.PrimitiveCount   = static_cast<Uint32>(m_Primitives.size());
        CI.pGeometryIndices = m_GeometryIndices.empty() ? nullptr : m_GeometryIndices.data();
        return CI;
    }

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

private:
    void BindPointers() noexcept
    {
        VERIFY_EXPR(m_Primitives.size() == m_PrimitiveNames.size());
        VERIFY_EXPR(m_Primitives.size() == m_Materials.size());

        for (size_t PrimitiveIndex = 0; PrimitiveIndex < m_Primitives.size(); ++PrimitiveIndex)
        {
            m_Primitives[PrimitiveIndex].Name =
                m_Primitives[PrimitiveIndex].Name != nullptr ? m_PrimitiveNames[PrimitiveIndex].c_str() : nullptr;
            m_Primitives[PrimitiveIndex].pMaterial = m_Materials[PrimitiveIndex].RawPtr();
        }
    }

private:
    RADIENT_STATUS m_Status = RADIENT_STATUS_OK;

    std::vector<RadientMeshPrimitiveCreateInfo>       m_Primitives;
    std::vector<Uint32>                               m_GeometryIndices;
    std::vector<std::string>                          m_PrimitiveNames;
    std::vector<RefCntAutoPtr<IRadientMaterialAsset>> m_Materials;
};

} // namespace Diligent
