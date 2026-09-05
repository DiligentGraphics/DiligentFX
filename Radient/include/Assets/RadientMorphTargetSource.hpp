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

#include "RadientMorphTargets.h"

#include <string>
#include <vector>

namespace Diligent
{

/// Owns CPU-side morph-target source data that is packed into a GPU buffer.
class RadientMorphTargetSource final
{
public:
    struct Attribute
    {
        std::string Semantic;
        Uint32      ComponentCount = 0;
        Uint32      DataOffset     = 0;
    };

    struct Target
    {
        std::string            Name;
        Float32                DefaultWeight = 0;
        std::vector<Attribute> Attributes;
    };

    struct PackDestination
    {
        void*  pData    = nullptr;
        Uint32 DataSize = 0;
    };

    RadientMorphTargetSource(const RadientMorphTargetCreateInfo* pTargets,
                             Uint32                              TargetCount,
                             Uint32                              VertexCount);

    // clang-format off
    RadientMorphTargetSource(const RadientMorphTargetSource&)            = delete;
    RadientMorphTargetSource(RadientMorphTargetSource&&)                 = delete;
    RadientMorphTargetSource& operator=(const RadientMorphTargetSource&) = delete;
    RadientMorphTargetSource& operator=(RadientMorphTargetSource&&)      = delete;
    // clang-format on

    RADIENT_STATUS GetStatus() const noexcept
    {
        return m_Status;
    }

    Uint32 GetVertexCount() const noexcept
    {
        return m_VertexCount;
    }

    Uint32 GetTargetCount() const noexcept
    {
        return static_cast<Uint32>(m_Targets.size());
    }

    const Target& GetTarget(Uint32 TargetIndex) const noexcept
    {
        return m_Targets[TargetIndex];
    }

    Uint32 GetDataSize() const noexcept
    {
        return static_cast<Uint32>(m_Deltas.size() * sizeof(Float32));
    }

    /// Returns a key for the packed morph-target description and GPU data.
    std::string MakeCacheKey() const;

    RADIENT_STATUS PackData(PackDestination Destination) const noexcept;

private:
    RADIENT_STATUS       m_Status      = RADIENT_STATUS_INVALID_ARGUMENT;
    Uint32               m_VertexCount = 0;
    std::vector<Target>  m_Targets;
    std::vector<Float32> m_Deltas;
};

} // namespace Diligent
