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
#include "STDAllocator.hpp"

#include <memory>

namespace Diligent
{

/// Packed immutable morph-target data copied from RadientMeshCreateInfo.
class RadientMorphTargetData final
{
public:
    RadientMorphTargetData(const RadientMorphTargetCreateInfo* pTargets,
                           Uint32                              TargetCount,
                           Uint32                              VertexCount);

    RadientMorphTargetData(const RadientMorphTargetData&)            = delete;
    RadientMorphTargetData& operator=(const RadientMorphTargetData&) = delete;

    const RadientMeshAssetDesc& GetDesc() const noexcept
    {
        return m_Desc;
    }

    /// Returns the copied delta stream for one target attribute.
    const Float32* GetDeltas(Uint32 TargetIndex, Uint32 AttributeIndex) const noexcept;

private:
    struct AttributeData
    {
        const Float32* pDeltas = nullptr;
    };

    using PackedMemory = std::unique_ptr<void, STDDeleterRawMem<void>>;

    PackedMemory                           m_Memory;
    RadientMeshAssetDesc                   m_Desc;
    const RadientMorphTargetAttributeDesc* m_pAttributes    = nullptr;
    const AttributeData*                   m_pAttributeData = nullptr;
};

RADIENT_STATUS CreateRadientMorphTargetWeights(IRadientMeshAsset*           pMesh,
                                               const RadientMeshAssetDesc&  MeshDesc,
                                               IRadientMorphTargetWeights** ppWeights);

} // namespace Diligent
