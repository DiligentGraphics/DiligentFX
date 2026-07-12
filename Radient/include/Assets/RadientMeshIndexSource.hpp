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

#include "GraphicsTypes.h"
#include "RadientAssets.h"

#include <memory>
#include <string>
#include <vector>

namespace Diligent
{

/// Owns CPU-side index source data that is packed into a mesh index buffer.
class RadientMeshIndexSource final
{
public:
    struct CreateInfo
    {
        /// Pointer to the first tightly packed source index.
        const void* pData = nullptr;

        /// Source index type. VT_UINT8, VT_UINT16, and VT_UINT32 are supported.
        VALUE_TYPE Type = VT_UNDEFINED;

        /// Number of source indices.
        Uint32 IndexCount = 0;

        /// Keeps borrowed source memory alive.
        /// If null, source data is copied into RadientMeshIndexSource.
        /// If non-null, source data is borrowed and this owner must keep the source span alive.
        std::shared_ptr<const void> pSourceDataOwner;
    };

    struct PackDestination
    {
        void*  pData    = nullptr;
        Uint32 DataSize = 0;
    };

    explicit RadientMeshIndexSource(const CreateInfo& CI);
    explicit RadientMeshIndexSource(const RadientMeshCreateInfo& MeshCI);

    // clang-format off
    RadientMeshIndexSource(const RadientMeshIndexSource&)            = delete;
    RadientMeshIndexSource(RadientMeshIndexSource&&)                 = delete;
    RadientMeshIndexSource& operator=(const RadientMeshIndexSource&) = delete;
    RadientMeshIndexSource& operator=(RadientMeshIndexSource&&)      = delete;
    // clang-format on

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    Uint32 GetIndexCount() const
    {
        return m_IndexCount;
    }

    Uint32 GetIndexDataSize() const
    {
        return m_IndexCount * sizeof(Uint32);
    }

    static bool IsSupportedIndexType(VALUE_TYPE IndexType);

    RADIENT_STATUS PackIndexData(PackDestination Destination) const noexcept;

    /// Returns a key for packed GPU index data.
    std::string MakeCacheKey() const;

private:
    void Initialize(const CreateInfo& CI);

private:
    RADIENT_STATUS m_Status = RADIENT_STATUS_INVALID_ARGUMENT;

    Uint32 m_IndexCount = 0;

    VALUE_TYPE   m_IndexType  = VT_UNDEFINED;
    const Uint8* m_pIndexData = nullptr;

    std::vector<Uint8> m_Indices;

    std::shared_ptr<const void> m_pSourceDataOwner;
};

} // namespace Diligent
