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

#include "GLTFLoader.hpp"
#include "HashUtils.hpp"
#include "RadientAssets.h"
#include "RefCntAutoPtr.hpp"

#include "../../../PBR/interface/PBR_Renderer.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Diligent
{

/// Owns CPU-side mesh source data that is packed into mesh vertex and index buffers.
class RadientMeshSource final
{
public:
    struct SourceAttribute
    {
        /// Attribute name (`"POSITION"`, `"NORMAL"`, `"TEXCOORD_0"`, etc.).
        const Char* Name = nullptr;

        /// Source component type.
        VALUE_TYPE Type = VT_UNDEFINED;

        /// Number of source components.
        Uint8 NumComponents = 0;

        /// Indicates if integer source values are normalized.
        bool IsNormalized = false;

        /// Pointer to the first attribute element.
        const void* pData = nullptr;

        /// Distance, in bytes, between consecutive elements. Zero means tightly packed.
        Uint32 Stride = 0;
    };

    struct SourceIndexData
    {
        /// Pointer to the first tightly packed index.
        const void* pData = nullptr;

        /// Source index type.
        RADIENT_INDEX_TYPE Type = RADIENT_INDEX_TYPE_NONE;
    };

    struct CreateInfo
    {
        /// Source vertex attributes.
        const SourceAttribute* pAttributes = nullptr;

        /// Number of source vertex attributes.
        Uint32 AttributeCount = 0;

        /// Number of source vertices.
        Uint32 VertexCount = 0;

        /// Source index data.
        SourceIndexData Indices;

        /// Number of source indices.
        Uint32 IndexCount = 0;

        /// Mesh primitives.
        const RadientMeshPrimitiveCreateInfo* pPrimitives = nullptr;

        /// Number of primitives.
        Uint32 PrimitiveCount = 0;

        /// Keeps borrowed source memory alive.
        /// If null, source data is copied into RadientMeshSource.
        /// If non-null, source data is borrowed and this owner must keep all source spans alive.
        std::shared_ptr<const void> pSourceDataOwner;
    };

    explicit RadientMeshSource(const CreateInfo& CI);
    explicit RadientMeshSource(const RadientMeshCreateInfo& MeshCI);

    RadientMeshSource(const RadientMeshSource&)            = delete;
    RadientMeshSource(RadientMeshSource&&)                 = delete;
    RadientMeshSource& operator=(const RadientMeshSource&) = delete;
    RadientMeshSource& operator=(RadientMeshSource&&)      = delete;

    struct PackDestination
    {
        void*  pData    = nullptr;
        Uint32 DataSize = 0;
    };

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

    RADIENT_STATUS SetVertexAttributes(const GLTF::VertexAttributeDesc* pDstAttributes, Uint32 NumDstAttributes);

    PBR_Renderer::PSO_FLAGS GetVertexAttribFlags() const
    {
        VerifyVertexAttributesSet();
        return m_VertexAttribFlags;
    }

    Uint32 GetVertexCount() const
    {
        return m_VertexCount;
    }

    Uint32 GetIndexCount() const
    {
        return m_IndexCount;
    }

    Uint32 GetIndexDataSize() const
    {
        return m_IndexCount * sizeof(Uint32);
    }

    Uint32 GetVertexBufferCount() const
    {
        VerifyVertexAttributesSet();
        return static_cast<Uint32>(m_VertexStrides.size());
    }

    Uint32 GetVertexStride(Uint32 BufferIndex) const
    {
        VerifyVertexAttributesSet();
        VERIFY(BufferIndex < m_VertexStrides.size(), "Invalid vertex buffer index");
        return BufferIndex < m_VertexStrides.size() ? m_VertexStrides[BufferIndex] : 0;
    }

    Uint32 GetVertexBufferDataSize(Uint32 BufferIndex) const
    {
        VerifyVertexAttributesSet();
        VERIFY(BufferIndex < m_VertexBufferDataSizes.size(), "Invalid vertex buffer index");
        return BufferIndex < m_VertexBufferDataSizes.size() ? m_VertexBufferDataSizes[BufferIndex] : 0;
    }

    Uint32 GetActiveVertexBufferMask() const
    {
        VerifyVertexAttributesSet();
        return m_ActiveVertexBufferMask;
    }

    bool IsVertexBufferActive(Uint32 BufferIndex) const
    {
        VerifyVertexAttributesSet();
        return BufferIndex < sizeof(m_ActiveVertexBufferMask) * 8 &&
            (m_ActiveVertexBufferMask & (Uint32{1} << BufferIndex)) != 0;
    }

    RADIENT_STATUS PackIndexData(PackDestination Destination) const;
    RADIENT_STATUS PackVertexData(Uint32 VertexBufferIndex, PackDestination Destination) const;

    std::string MakeCacheKey() const;

private:
    struct SrcAttributeData
    {
        VALUE_TYPE Type          = VT_UNDEFINED;
        Uint8      NumComponents = 0;
        bool       IsNormalized  = false;
        Uint32     ElementSize   = 0;
        Uint32     Stride        = 0;

        const Uint8* pData = nullptr;

        std::vector<Uint8> OwnedBytes;
    };

    void Initialize(const CreateInfo& CI);

    void VerifyVertexAttributesSet() const
    {
        VERIFY(!m_DstAttributes.empty(), "Vertex attributes have not been set");
    }

private:
    RADIENT_STATUS m_Status = RADIENT_STATUS_OK;

    std::unordered_map<HashMapStringKey, SrcAttributeData, HashMapStringKey::Hasher> m_SrcAttributes;

    PBR_Renderer::PSO_FLAGS m_VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32             m_VertexCount            = 0;
    Uint32             m_IndexCount             = 0;
    Uint32             m_ActiveVertexBufferMask = 0;
    RADIENT_INDEX_TYPE m_IndexType              = RADIENT_INDEX_TYPE_NONE;
    const Uint8*       m_pIndexData             = nullptr;

    std::vector<GLTF::VertexAttributeDesc> m_DstAttributes;
    std::vector<Uint32>                    m_VertexStrides;
    std::vector<Uint32>                    m_VertexBufferDataSizes;

    std::vector<Uint16> m_Indices16;
    std::vector<Uint32> m_Indices32;

    std::vector<RadientMeshPrimitiveCreateInfo> m_Primitives;

    // Keeps material assets alive while m_Primitives stores the public raw pointers.
    std::vector<RefCntAutoPtr<IRadientMaterialAsset>> m_PrimitiveMaterials;

    std::shared_ptr<const void> m_pSourceDataOwner;
};

} // namespace Diligent
