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

#include "Core/RadientDataBlobReadAccess.hpp"
#include "DebugUtilities.hpp"
#include "GLTFLoader.hpp"
#include "GraphicsTypes.h"
#include "HashUtils.hpp"
#include "RadientAssets.h"

#include "../../../PBR/interface/PBR_Renderer.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Diligent
{

/// Retains read access to CPU vertex data and packs it into renderer-selected buffers.
class RadientMeshVertexSource final
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

        /// Blob containing the attribute elements. Retained with read access.
        IRadientDataBlob* pDataBlob = nullptr;

        /// Distance, in bytes, between consecutive elements. Zero means tightly packed.
        Uint32 Stride = 0;

        /// Offset of the first attribute element from the blob's first byte.
        Uint64 ByteOffset = 0;
    };

    struct CreateInfo
    {
        /// Source attributes. Metadata and names are copied; blobs retain read access.
        const SourceAttribute* pAttributes = nullptr;

        /// Number of source vertex attributes.
        Uint32 AttributeCount = 0;

        /// Number of source vertices.
        Uint32 VertexCount = 0;
    };

    struct PackDestination
    {
        void*  pData    = nullptr;
        Uint32 DataSize = 0;
    };

    explicit RadientMeshVertexSource(const CreateInfo& CI);
    explicit RadientMeshVertexSource(const RadientMeshCreateInfo& MeshCI);

    // clang-format off
    RadientMeshVertexSource(const RadientMeshVertexSource&)            = delete;
    RadientMeshVertexSource(RadientMeshVertexSource&&)                 = delete;
    RadientMeshVertexSource& operator=(const RadientMeshVertexSource&) = delete;
    RadientMeshVertexSource& operator=(RadientMeshVertexSource&&)      = delete;
    // clang-format on

    RADIENT_STATUS GetStatus() const
    {
        return m_Status;
    }

    RADIENT_STATUS SetVertexAttributes(const GLTF::VertexAttributeDesc* pDstAttributes, Uint32 NumDstAttributes);

    bool HasVertexAttributes() const
    {
        return !m_DstAttributes.empty();
    }

    PBR_Renderer::PSO_FLAGS GetVertexAttribFlags() const
    {
        VerifyVertexAttributesSet();
        return m_VertexAttribFlags;
    }

    Uint32 GetVertexCount() const
    {
        return m_VertexCount;
    }

    Uint32 GetVertexBufferCount() const
    {
        VerifyVertexAttributesSet();
        return static_cast<Uint32>(m_VertexStrides.size());
    }

    /// Returns the stored layout with active buffers numbered densely from zero.
    /// Arrays and semantic strings remain valid until SetVertexAttributes is called
    /// again or this source is destroyed.
    RadientVertexLayoutDesc GetVertexLayout() const
    {
        VerifyVertexAttributesSet();
        return {
            m_StoredLayoutAttributes.data(),
            static_cast<Uint32>(m_StoredLayoutAttributes.size()),
            m_StoredBufferLayouts.data(),
            static_cast<Uint32>(m_StoredBufferLayouts.size()),
        };
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

    RADIENT_STATUS PackVertexData(Uint32 VertexBufferIndex, PackDestination Destination) const noexcept;

    /// Returns a key for vertex attribute data and destination layout, excluding padding.
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
    };

    // Both constructors validate their metadata before calling this function.
    // pLayout supplies public buffer-slot mapping; source offsets and strides are
    // already resolved in CI. Without a layout, each attribute has its own slot.
    void Initialize(const CreateInfo& CI, const RadientVertexLayoutDesc* pLayout = nullptr);

    void VerifyVertexAttributesSet() const
    {
        VERIFY(!m_DstAttributes.empty(), "Vertex attributes have not been set");
    }

private:
    RADIENT_STATUS m_Status = RADIENT_STATUS_INVALID_ARGUMENT;

    std::unordered_map<HashMapStringKey, SrcAttributeData, HashMapStringKey::Hasher> m_SrcAttributes;

    PBR_Renderer::PSO_FLAGS m_VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32 m_VertexCount = 0;

    static_assert(GLTF::ModelCreateInfo::MaxBuffers <= sizeof(Uint32) * 8,
                  "RadientMeshVertexSource active vertex buffer mask must fit all GLTF vertex buffers.");
    Uint32 m_ActiveVertexBufferMask = 0;

    std::vector<GLTF::VertexAttributeDesc> m_DstAttributes;
    std::vector<std::string>               m_DstAttributeNames;
    std::vector<std::unique_ptr<Uint8[]>>  m_DstAttributeDefaultValues;
    std::vector<Uint32>                    m_VertexStrides;
    std::vector<Uint32>                    m_VertexBufferDataSizes;

    // Reflection includes all attributes in uploaded buffers. Semantic strings
    // use the owned destination names; buffer indices omit inactive buffers.
    std::vector<RadientVertexAttributeDesc>    m_StoredLayoutAttributes;
    std::vector<RadientVertexBufferLayoutDesc> m_StoredBufferLayouts;

    // Public layout metadata is copied and resolved; source bytes stay in the blobs.
    std::vector<RadientVertexAttributeDesc>    m_SrcLayoutAttributes;
    std::vector<std::string>                   m_SrcAttributeNames;
    std::vector<RadientVertexBufferLayoutDesc> m_SrcBufferLayouts;
    std::vector<RadientDataBlobReadAccess>     m_SrcBuffers;

    // One source buffer index per destination buffer; ~0u selects conversion.
    std::vector<Uint32> m_CopySourceBufferIndices;
};

} // namespace Diligent
