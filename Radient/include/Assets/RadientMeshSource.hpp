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

#include "RadientAssets.h"
#include "RefCntAutoPtr.hpp"

#include "../../../PBR/interface/PBR_Renderer.hpp"

#include <array>
#include <vector>

namespace Diligent
{

/// Owns the temporary CPU-side mesh upload payload derived from RadientMeshCreateInfo.
class RadientMeshSource final
{
public:
    static constexpr Uint32 VertexBufferCount = 5;

    using VertexBufferStrides = std::array<Uint32, VertexBufferCount>;
    using VertexBufferSizes   = std::array<Uint32, VertexBufferCount>;

    struct UploadData
    {
        PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;
        Uint32                  VertexCount       = 0;
        Uint32                  IndexCount        = 0;
        Uint32                  VertexBufferCount = 0;

        VertexBufferStrides VertexStrides{};
        VertexBufferSizes   VertexBufferDataSizes{};

        Uint32 GetIndexDataSize() const
        {
            return IndexCount * sizeof(Uint32);
        }
    };

    struct PackDestination
    {
        void*  pData    = nullptr;
        Uint32 DataSize = 0;
    };

    using VertexBufferDestinations = std::array<PackDestination, VertexBufferCount>;

    struct PackDestinations
    {
        PackDestination          Indices;
        VertexBufferDestinations VertexBuffers{};
    };

    explicit RadientMeshSource(const RadientMeshCreateInfo& MeshCI);

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    PBR_Renderer::PSO_FLAGS GetVertexAttribFlags() const
    {
        return m_VertexAttribFlags;
    }

    Uint32 GetVertexCount() const
    {
        return m_VertexCount;
    }

    const std::vector<RadientMeshPrimitiveCreateInfo>& GetPrimitives() const
    {
        return m_Primitives;
    }

    IRadientMaterialAsset* GetPrimitiveMaterial(Uint32 PrimitiveIndex) const
    {
        return PrimitiveIndex < m_PrimitiveMaterials.size() ? m_PrimitiveMaterials[PrimitiveIndex] : nullptr;
    }

    RADIENT_STATUS GetUploadData(UploadData& Data) const;
    RADIENT_STATUS Pack(const UploadData& Data, const PackDestinations& Destinations) const;

    static PBR_Renderer::PSO_FLAGS GetVertexAttribFlags(const RadientMeshCreateInfo& MeshCI);

private:
    static bool Validate(const RadientMeshCreateInfo& MeshCI);

private:
    RADIENT_STATUS m_Status = RADIENT_STATUS_OK;

    PBR_Renderer::PSO_FLAGS m_VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;
    Uint32                  m_VertexCount       = 0;

    std::vector<RadientFloat3>       m_Positions;
    std::vector<RadientFloat3>       m_Normals;
    std::vector<RadientFloat4>       m_Tangents;
    std::vector<RadientFloat2>       m_TexCoords0;
    std::vector<RadientColorRGBA8>   m_Colors0;
    std::vector<RadientBoneIndices4> m_BoneIndices0;
    std::vector<RadientFloat4>       m_BoneWeights0;

    std::vector<Uint16> m_Indices16;
    std::vector<Uint32> m_Indices32;

    std::vector<RadientMeshPrimitiveCreateInfo>       m_Primitives;
    std::vector<RefCntAutoPtr<IRadientMaterialAsset>> m_PrimitiveMaterials;
};

} // namespace Diligent
