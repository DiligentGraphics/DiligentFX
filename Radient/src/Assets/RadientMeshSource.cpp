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

#include "Assets/RadientMeshSource.hpp"

#include "Math/RadientMath.hpp"

#include "DebugUtilities.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace Diligent
{

namespace
{

constexpr Uint32 MeshBuffer0Stride = sizeof(RadientFloat3) + sizeof(RadientFloat3) + sizeof(RadientFloat2);
constexpr Uint32 MeshBuffer1Stride = sizeof(RadientFloat4) + sizeof(RadientFloat4);
constexpr Uint32 MeshBuffer2Stride = sizeof(RadientFloat2);
constexpr Uint32 MeshBuffer3Stride = sizeof(RadientFloat4);
constexpr Uint32 MeshBuffer4Stride = sizeof(RadientFloat3);

constexpr std::array<Uint32, RadientMeshSource::VertexBufferCount> MeshVertexStrides = {
    MeshBuffer0Stride,
    MeshBuffer1Stride,
    MeshBuffer2Stride,
    MeshBuffer3Stride,
    MeshBuffer4Stride,
};

constexpr Uint32 MeshBuffer0PositionOffset  = 0;
constexpr Uint32 MeshBuffer0NormalOffset    = MeshBuffer0PositionOffset + sizeof(RadientFloat3);
constexpr Uint32 MeshBuffer0TexCoord0Offset = MeshBuffer0NormalOffset + sizeof(RadientFloat3);
constexpr Uint32 MeshBuffer1JointsOffset    = 0;
constexpr Uint32 MeshBuffer1WeightsOffset   = MeshBuffer1JointsOffset + sizeof(RadientFloat4);

template <typename ValueType>
void WriteValue(void* pDstData, Uint32 DataSize, size_t Offset, const ValueType& Value)
{
    VERIFY_EXPR(pDstData != nullptr);
    VERIFY_EXPR(Offset + sizeof(ValueType) <= DataSize);
    std::memcpy(static_cast<Uint8*>(pDstData) + Offset, &Value, sizeof(ValueType));
}

bool CheckByteSize(Uint32 Count, Uint32 Stride)
{
    return Uint64{Count} * Stride <= (std::numeric_limits<Uint32>::max)();
}

template <typename ValueType>
void CopyArray(std::vector<ValueType>& Dst, const ValueType* pSrc, Uint32 Count)
{
    if (pSrc != nullptr && Count != 0)
        Dst.assign(pSrc, pSrc + Count);
}

} // namespace

RadientMeshSource::RadientMeshSource(const RadientMeshCreateInfo& MeshCI) :
    m_VertexAttribFlags{GetVertexAttribFlags(MeshCI)}
{
    if (!Validate(MeshCI))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    m_VertexCount = MeshCI.VertexCount;

    CopyArray(m_Positions, MeshCI.pPositions, MeshCI.VertexCount);
    CopyArray(m_Normals, MeshCI.pNormals, MeshCI.VertexCount);
    CopyArray(m_Tangents, MeshCI.pTangents, MeshCI.VertexCount);
    CopyArray(m_TexCoords0, MeshCI.pTexCoords0, MeshCI.VertexCount);
    CopyArray(m_Colors0, MeshCI.pColors0, MeshCI.VertexCount);
    CopyArray(m_BoneIndices0, MeshCI.pBoneIndices0, MeshCI.VertexCount);
    CopyArray(m_BoneWeights0, MeshCI.pBoneWeights0, MeshCI.VertexCount);

    if (MeshCI.IndexType == RADIENT_INDEX_TYPE_UINT32)
        CopyArray(m_Indices32, static_cast<const Uint32*>(MeshCI.pIndices), MeshCI.IndexCount);
    else
        CopyArray(m_Indices16, static_cast<const Uint16*>(MeshCI.pIndices), MeshCI.IndexCount);

    CopyArray(m_Primitives, MeshCI.pPrimitives, MeshCI.PrimitiveCount);
    m_PrimitiveMaterials.reserve(MeshCI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
        m_PrimitiveMaterials.emplace_back(MeshCI.pPrimitives[PrimitiveIndex].pMaterial);
}

PBR_Renderer::PSO_FLAGS RadientMeshSource::GetVertexAttribFlags(const RadientMeshCreateInfo& MeshCI)
{
    PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::PSO_FLAG_NONE;
    if (MeshCI.pNormals != nullptr)
        Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
    if (MeshCI.pTangents != nullptr)
        Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
    if (MeshCI.pTexCoords0 != nullptr)
        Flags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
    if (MeshCI.pColors0 != nullptr)
        Flags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
    if (MeshCI.pBoneIndices0 != nullptr)
        Flags |= PBR_Renderer::PSO_FLAG_USE_JOINTS;

    return Flags;
}

bool RadientMeshSource::Validate(const RadientMeshCreateInfo& MeshCI)
{
    if (MeshCI.VertexCount == 0 ||
        MeshCI.pPositions == nullptr ||
        MeshCI.IndexCount == 0 ||
        MeshCI.pIndices == nullptr ||
        MeshCI.PrimitiveCount == 0 ||
        MeshCI.pPrimitives == nullptr)
    {
        return false;
    }

    if (!CheckByteSize(MeshCI.IndexCount, sizeof(Uint32)))
        return false;

    const bool HasBoneIndices = MeshCI.pBoneIndices0 != nullptr;
    const bool HasBoneWeights = MeshCI.pBoneWeights0 != nullptr;
    if (HasBoneIndices != HasBoneWeights)
        return false;

    if (MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT16 &&
        MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT32)
    {
        return false;
    }

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];
        if (PrimitiveCI.IndexCount == 0 ||
            PrimitiveCI.FirstIndex >= MeshCI.IndexCount ||
            PrimitiveCI.IndexCount > MeshCI.IndexCount - PrimitiveCI.FirstIndex)
        {
            return false;
        }
    }

    return true;
}

RADIENT_STATUS RadientMeshSource::GetUploadData(UploadData& Data) const
{
    Data = {};

    if (RADIENT_FAILED(m_Status))
        return m_Status;

    if (m_VertexCount == 0 || m_Positions.empty())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    Data.VertexAttribFlags = m_VertexAttribFlags;
    Data.VertexCount       = m_VertexCount;

    if (!m_Indices32.empty())
    {
        if (!CheckByteSize(static_cast<Uint32>(m_Indices32.size()), sizeof(Uint32)))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        Data.IndexCount = static_cast<Uint32>(m_Indices32.size());
    }
    else if (!m_Indices16.empty())
    {
        if (!CheckByteSize(static_cast<Uint32>(m_Indices16.size()), sizeof(Uint32)))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        Data.IndexCount = static_cast<Uint32>(m_Indices16.size());
    }
    else
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    // Vertex pool buffers are bound directly to matching PBR input slots.
    // Allocate only the contiguous prefix needed by enabled attributes.
    Data.VertexBufferCount = 1;
    if (!m_BoneIndices0.empty())
    {
        if (m_BoneWeights0.empty())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        Data.VertexBufferCount = std::max(Data.VertexBufferCount, 2u);
    }
    if (!m_Colors0.empty())
        Data.VertexBufferCount = std::max(Data.VertexBufferCount, 4u);
    if (!m_Tangents.empty())
        Data.VertexBufferCount = std::max(Data.VertexBufferCount, 5u);

    for (Uint32 BufferIndex = 0; BufferIndex < Data.VertexBufferCount; ++BufferIndex)
    {
        Data.VertexStrides[BufferIndex] = MeshVertexStrides[BufferIndex];
        if (!CheckByteSize(m_VertexCount, Data.VertexStrides[BufferIndex]))
            return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    auto SetVertexBufferDataSize =
        [&Data, this](Uint32 BufferIndex) //
    {
        Data.VertexBufferDataSizes[BufferIndex] = m_VertexCount * Data.VertexStrides[BufferIndex];
    };

    SetVertexBufferDataSize(0);
    if (!m_BoneIndices0.empty())
        SetVertexBufferDataSize(1);
    if (!m_Colors0.empty())
        SetVertexBufferDataSize(3);
    if (!m_Tangents.empty())
        SetVertexBufferDataSize(4);

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientMeshSource::Pack(const UploadData& Data, const PackDestinations& Destinations) const
{
    if (RADIENT_FAILED(m_Status))
        return m_Status;

    if (Data.VertexCount != m_VertexCount ||
        Data.IndexCount == 0 ||
        Data.GetIndexDataSize() == 0)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    bool WroteData = false;

    if (Destinations.Indices.pData != nullptr)
    {
        if (Destinations.Indices.DataSize != Data.GetIndexDataSize())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        Uint8* pDstIndices = static_cast<Uint8*>(Destinations.Indices.pData);
        if (!m_Indices32.empty())
        {
            if (Data.IndexCount != static_cast<Uint32>(m_Indices32.size()))
                return RADIENT_STATUS_INVALID_ARGUMENT;

            std::memcpy(pDstIndices, m_Indices32.data(), Data.GetIndexDataSize());
        }
        else if (!m_Indices16.empty())
        {
            if (Data.IndexCount != static_cast<Uint32>(m_Indices16.size()))
                return RADIENT_STATUS_INVALID_ARGUMENT;

            for (Uint32 Index = 0; Index < Data.IndexCount; ++Index)
            {
                const Uint32 Value = m_Indices16[Index];
                std::memcpy(pDstIndices + size_t{Index} * sizeof(Uint32), &Value, sizeof(Value));
            }
        }
        else
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        WroteData = true;
    }

    auto ValidateDestination =
        [&Data](const PackDestination& Destination, Uint32 BufferIndex) //
    {
        return Destination.pData == nullptr ||
            (BufferIndex < Data.VertexBufferCount &&
             Destination.DataSize != 0 &&
             Destination.DataSize == Data.VertexBufferDataSizes[BufferIndex]);
    };

    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
    {
        if (!ValidateDestination(Destinations.VertexBuffers[BufferIndex], BufferIndex))
            return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (Destinations.VertexBuffers[0].pData != nullptr)
    {
        void* const  pDstData = Destinations.VertexBuffers[0].pData;
        const Uint32 DataSize = Destinations.VertexBuffers[0].DataSize;

        std::memset(pDstData, 0, DataSize);

        for (Uint32 Vertex = 0; Vertex < m_VertexCount; ++Vertex)
        {
            const size_t Buffer0Offset = size_t{Vertex} * MeshBuffer0Stride;
            WriteValue(pDstData, DataSize, Buffer0Offset + MeshBuffer0PositionOffset, m_Positions[Vertex]);
            if (!m_Normals.empty())
                WriteValue(pDstData, DataSize, Buffer0Offset + MeshBuffer0NormalOffset, m_Normals[Vertex]);
            if (!m_TexCoords0.empty())
                WriteValue(pDstData, DataSize, Buffer0Offset + MeshBuffer0TexCoord0Offset, m_TexCoords0[Vertex]);
        }

        WroteData = true;
    }

    if (Destinations.VertexBuffers[1].pData != nullptr)
    {
        if (m_BoneIndices0.empty() || m_BoneWeights0.empty())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        void* const  pDstData = Destinations.VertexBuffers[1].pData;
        const Uint32 DataSize = Destinations.VertexBuffers[1].DataSize;

        for (Uint32 Vertex = 0; Vertex < m_VertexCount; ++Vertex)
        {
            const size_t Buffer1Offset = size_t{Vertex} * MeshBuffer1Stride;
            const float4 Joints        = RadientMath::ToFloat4(m_BoneIndices0[Vertex]);
            WriteValue(pDstData, DataSize, Buffer1Offset + MeshBuffer1JointsOffset, Joints);
            WriteValue(pDstData, DataSize, Buffer1Offset + MeshBuffer1WeightsOffset, m_BoneWeights0[Vertex]);
        }

        WroteData = true;
    }

    if (Destinations.VertexBuffers[3].pData != nullptr)
    {
        if (m_Colors0.empty())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        void* const  pDstData = Destinations.VertexBuffers[3].pData;
        const Uint32 DataSize = Destinations.VertexBuffers[3].DataSize;

        for (Uint32 Vertex = 0; Vertex < m_VertexCount; ++Vertex)
        {
            const size_t Buffer3Offset = size_t{Vertex} * MeshBuffer3Stride;
            WriteValue(pDstData, DataSize, Buffer3Offset, RadientMath::ToFloat4(m_Colors0[Vertex]));
        }

        WroteData = true;
    }

    if (Destinations.VertexBuffers[4].pData != nullptr)
    {
        if (m_Tangents.empty())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        void* const  pDstData = Destinations.VertexBuffers[4].pData;
        const Uint32 DataSize = Destinations.VertexBuffers[4].DataSize;

        for (Uint32 Vertex = 0; Vertex < m_VertexCount; ++Vertex)
        {
            const size_t        Buffer4Offset = size_t{Vertex} * MeshBuffer4Stride;
            const RadientFloat3 Tangent{m_Tangents[Vertex].x, m_Tangents[Vertex].y, m_Tangents[Vertex].z};
            WriteValue(pDstData, DataSize, Buffer4Offset, Tangent);
        }

        WroteData = true;
    }

    return WroteData ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_ARGUMENT;
}

} // namespace Diligent
