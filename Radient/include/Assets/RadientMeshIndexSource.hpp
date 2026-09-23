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
#include "RadientMeshImportServices.h"
#include "Core/RadientDataBlobReadAccess.hpp"

#include <string>

namespace Diligent
{

/// Retains read access to CPU-side index data that is packed into a mesh index buffer.
class RadientMeshIndexSource final
{
public:
    struct PackDestination
    {
        void*  pData    = nullptr;
        Uint32 DataSize = 0;
    };

    explicit RadientMeshIndexSource(const RadientMeshCreateInfo& MeshCI);
    explicit RadientMeshIndexSource(const RadientMeshIndexDataCreateInfo& CI);

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

    RADIENT_STATUS PackIndexData(PackDestination Destination) const noexcept;

    /// Returns a key for packed GPU index data.
    std::string MakeCacheKey() const;

private:
    RADIENT_STATUS m_Status = RADIENT_STATUS_INVALID_ARGUMENT;

    Uint32 m_IndexCount = 0;

    VALUE_TYPE   m_IndexType  = VT_UNDEFINED;
    const Uint8* m_pIndexData = nullptr;

    RadientDataBlobReadAccess m_IndexBuffer;
};

} // namespace Diligent
