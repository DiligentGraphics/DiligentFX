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

#include "DebugUtilities.hpp"
#include "GLTFVertexDataConverter.hpp"
#include "GraphicsAccessories.hpp"
#include "XXH128Hasher.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace Diligent
{

namespace
{

bool CheckByteSize(Uint32 Count, Uint32 Stride)
{
    return Uint64{Count} * Stride <= (std::numeric_limits<Uint32>::max)();
}

bool CheckByteOffset(Uint32 Offset, Uint32 Size)
{
    return Offset <= (std::numeric_limits<Uint32>::max)() - Size;
}

bool CheckStridedByteSize(Uint32 Count, Uint32 Stride, Uint32 ElementSize)
{
    if (Count == 0)
        return true;

    return Uint64{Count - 1} * Stride + ElementSize <= (std::numeric_limits<Uint32>::max)();
}

bool IsSupportedSourceIndexType(VALUE_TYPE IndexType)
{
    return (IndexType == VT_UINT8 ||
            IndexType == VT_UINT16 ||
            IndexType == VT_UINT32);
}

VALUE_TYPE GetSourceIndexType(RADIENT_INDEX_TYPE IndexType)
{
    switch (IndexType)
    {
        case RADIENT_INDEX_TYPE_UINT16:
            return VT_UINT16;

        case RADIENT_INDEX_TYPE_UINT32:
            return VT_UINT32;

        default:
            return VT_UNDEFINED;
    }
}

bool GetSourceAttributeLayout(const RadientMeshSource::SourceAttribute& Attribute,
                              Uint32                                    VertexCount,
                              Uint32&                                   ElementSize,
                              Uint32&                                   Stride)
{
    if (Attribute.Name == nullptr ||
        Attribute.pData == nullptr ||
        Attribute.Type <= VT_UNDEFINED ||
        Attribute.Type >= VT_NUM_TYPES ||
        Attribute.NumComponents == 0 ||
        Attribute.NumComponents > 4)
    {
        return false;
    }

    const Uint32 ValueSize = GetValueSize(Attribute.Type);
    if (ValueSize == 0 || !CheckByteSize(Attribute.NumComponents, ValueSize))
        return false;

    ElementSize = ValueSize * Attribute.NumComponents;
    Stride      = Attribute.Stride != 0 ? Attribute.Stride : ElementSize;

    return Stride >= ElementSize &&
        CheckByteSize(VertexCount, ElementSize) &&
        CheckStridedByteSize(VertexCount, Stride, ElementSize);
}

bool GetSourceIndexLayout(const RadientMeshSource::SourceIndexData& Indices,
                          Uint32                                    IndexCount,
                          Uint32&                                   ElementSize)
{
    if (Indices.pData == nullptr)
        return false;

    if (!IsSupportedSourceIndexType(Indices.Type))
        return false;

    ElementSize = GetValueSize(Indices.Type);
    if (ElementSize == 0)
        return false;

    return CheckByteSize(IndexCount, ElementSize);
}

bool ValidateMeshSourceCI(const RadientMeshSource::CreateInfo& CI)
{
    if (CI.VertexCount == 0 ||
        CI.AttributeCount == 0 ||
        CI.pAttributes == nullptr ||
        CI.IndexCount == 0)
    {
        return false;
    }

    if (!CheckByteSize(CI.IndexCount, sizeof(Uint32)))
        return false;

    Uint32 IndexElementSize = 0;
    if (!GetSourceIndexLayout(CI.Indices, CI.IndexCount, IndexElementSize))
        return false;

    for (Uint32 AttributeIndex = 0; AttributeIndex < CI.AttributeCount; ++AttributeIndex)
    {
        Uint32 ElementSize = 0;
        Uint32 Stride      = 0;
        if (!GetSourceAttributeLayout(CI.pAttributes[AttributeIndex], CI.VertexCount, ElementSize, Stride))
            return false;
    }

    return true;
}

bool IsAttributeName(const GLTF::VertexAttributeDesc& DstAttrib, const char* Name)
{
    return DstAttrib.Name != nullptr && std::strcmp(DstAttrib.Name, Name) == 0;
}

bool IsActiveVertexBuffer(Uint32 ActiveVertexBufferMask, Uint32 BufferIndex)
{
    return BufferIndex < sizeof(ActiveVertexBufferMask) * 8 &&
        (ActiveVertexBufferMask & (Uint32{1} << BufferIndex)) != 0;
}

struct VertexAttributeRange
{
    Uint32 Begin = 0;
    Uint32 End   = 0;
};

bool HasOverlappingRanges(std::vector<VertexAttributeRange>& Ranges)
{
    std::sort(Ranges.begin(), Ranges.end(), [](const VertexAttributeRange& Lhs, const VertexAttributeRange& Rhs) //
              {
                  return Lhs.Begin < Rhs.Begin ||
                      (Lhs.Begin == Rhs.Begin && Lhs.End < Rhs.End);
              });

    for (size_t RangeIndex = 1; RangeIndex < Ranges.size(); ++RangeIndex)
    {
        if (Ranges[RangeIndex].Begin < Ranges[RangeIndex - 1].End)
            return true;
    }

    return false;
}

void UpdateRawIfNotEmpty(XXH128State& Hasher, const void* pData, size_t Size)
{
    if (pData != nullptr && Size != 0)
        Hasher.UpdateRaw(pData, static_cast<Uint64>(Size));
}

void UpdateStridedRaw(XXH128State& Hasher, const Uint8* pData, Uint32 Count, Uint32 ElementSize, Uint32 Stride)
{
    if (pData == nullptr || Count == 0 || ElementSize == 0)
        return;

    if (Stride == ElementSize)
    {
        UpdateRawIfNotEmpty(Hasher, pData, size_t{Count} * ElementSize);
    }
    else
    {
        for (Uint32 Elem = 0; Elem < Count; ++Elem)
            Hasher.UpdateRaw(pData + size_t{Elem} * Stride, ElementSize);
    }
}

void UpdateString(XXH128State& Hasher, const Char* Str)
{
    const Uint64 Len = Str != nullptr ? static_cast<Uint64>(std::strlen(Str)) : 0;
    Hasher.Update(Len);
    if (Len != 0)
        Hasher.UpdateRaw(Str, Len);
}

bool ValidateRadientMeshSourceCI(const RadientMeshCreateInfo& MeshCI)
{
    if (MeshCI.VertexCount == 0 ||
        MeshCI.pPositions == nullptr ||
        MeshCI.IndexCount == 0 ||
        MeshCI.pIndices == nullptr)
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

    return true;
}

} // namespace

RadientMeshSource::RadientMeshSource(const CreateInfo& CI)
{
    Initialize(CI);
}

RadientMeshSource::RadientMeshSource(const RadientMeshCreateInfo& MeshCI)
{
    if (!ValidateRadientMeshSourceCI(MeshCI))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    std::array<SourceAttribute, 7> Attributes{};

    Uint32 AttributeCount = 0;
    auto   AddAttribute =
        [&Attributes, &AttributeCount](const char* Name, VALUE_TYPE Type, Uint8 NumComponents, bool IsNormalized, const auto* pSrcData) //
    {
        if (pSrcData == nullptr)
            return;

        SourceAttribute& Attribute = Attributes[AttributeCount++];
        Attribute.Name             = Name;
        Attribute.Type             = Type;
        Attribute.NumComponents    = NumComponents;
        Attribute.IsNormalized     = IsNormalized;
        Attribute.pData            = pSrcData;
        Attribute.Stride           = sizeof(*pSrcData);
    };

    AddAttribute(GLTF::PositionAttributeName, VT_FLOAT32, 3, false, MeshCI.pPositions);
    AddAttribute(GLTF::NormalAttributeName, VT_FLOAT32, 3, false, MeshCI.pNormals);
    AddAttribute(GLTF::TangentAttributeName, VT_FLOAT32, 4, false, MeshCI.pTangents);
    AddAttribute(GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, MeshCI.pTexCoords0);
    AddAttribute(GLTF::VertexColorAttributeName, VT_UINT8, 4, true, MeshCI.pColors0);
    AddAttribute(GLTF::JointsAttributeName, VT_UINT16, 4, false, MeshCI.pBoneIndices0);
    AddAttribute(GLTF::WeightsAttributeName, VT_FLOAT32, 4, false, MeshCI.pBoneWeights0);

    CreateInfo CI;
    CI.pAttributes    = Attributes.data();
    CI.AttributeCount = AttributeCount;
    CI.VertexCount    = MeshCI.VertexCount;
    CI.Indices.pData  = MeshCI.pIndices;
    CI.Indices.Type   = GetSourceIndexType(MeshCI.IndexType);
    CI.IndexCount     = MeshCI.IndexCount;

    Initialize(CI);
}

void RadientMeshSource::Initialize(const CreateInfo& CI)
{
    if (!ValidateMeshSourceCI(CI))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    m_VertexCount = CI.VertexCount;
    m_IndexCount  = CI.IndexCount;
    m_IndexType   = CI.Indices.Type;

    const bool BorrowSourceData = CI.pSourceDataOwner != nullptr;
    if (BorrowSourceData)
        m_pSourceDataOwner = CI.pSourceDataOwner;

    for (Uint32 AttributeIndex = 0; AttributeIndex < CI.AttributeCount; ++AttributeIndex)
    {
        const SourceAttribute& Attribute = CI.pAttributes[AttributeIndex];

        Uint32 ElementSize = 0;
        Uint32 SrcStride   = 0;
        if (!GetSourceAttributeLayout(Attribute, m_VertexCount, ElementSize, SrcStride))
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }

        SrcAttributeData Data;
        Data.Type                    = Attribute.Type;
        Data.NumComponents           = Attribute.NumComponents;
        Data.IsNormalized            = Attribute.IsNormalized;
        Data.ElementSize             = ElementSize;
        const Uint8* const pSrcBytes = static_cast<const Uint8*>(Attribute.pData);

        if (BorrowSourceData)
        {
            // Keep the original source layout; the owner supplied in CreateInfo
            // keeps these spans alive.
            Data.Stride = SrcStride;
            Data.pData  = pSrcBytes;
        }
        else
        {
            // Store each copied attribute tightly packed, even when it came
            // from an interleaved source vertex stream.
            Data.Stride = ElementSize;
            Data.OwnedBytes.resize(size_t{m_VertexCount} * ElementSize);

            Uint8* const pDstBytes = Data.OwnedBytes.data();
            if (SrcStride == ElementSize)
            {
                std::memcpy(pDstBytes, pSrcBytes, Data.OwnedBytes.size());
            }
            else
            {
                for (Uint32 Vertex = 0; Vertex < m_VertexCount; ++Vertex)
                {
                    std::memcpy(pDstBytes + size_t{Vertex} * ElementSize,
                                pSrcBytes + size_t{Vertex} * SrcStride,
                                ElementSize);
                }
            }
            Data.pData = Data.OwnedBytes.data();
        }

        auto [It, Inserted] = m_SrcAttributes.emplace(HashMapStringKey{Attribute.Name, true}, std::move(Data));
        if (!Inserted)
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return;
        }
    }

    if (m_SrcAttributes.find(GLTF::PositionAttributeName) == m_SrcAttributes.end())
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    Uint32 IndexElementSize = 0;
    if (!GetSourceIndexLayout(CI.Indices, m_IndexCount, IndexElementSize))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    const Uint8* const pSrcIndices = static_cast<const Uint8*>(CI.Indices.pData);
    if (BorrowSourceData)
    {
        // Index data is tightly packed by contract, so the source bytes can be
        // referenced directly when the source owner is retained.
        m_pIndexData = pSrcIndices;
    }
    else
    {
        m_Indices.resize(size_t{m_IndexCount} * IndexElementSize);
        std::memcpy(m_Indices.data(), pSrcIndices, m_Indices.size());
        m_pIndexData = m_Indices.data();
    }
}

std::string RadientMeshSource::MakeCacheKey() const
{
    if (RADIENT_FAILED(m_Status) || m_DstAttributes.empty())
        return {};

    XXH128State Hasher;
    Hasher.Update(Uint32{6}, // Packed mesh geometry cache key version.
                  m_VertexCount,
                  m_IndexCount,
                  m_IndexType,
                  m_ActiveVertexBufferMask);

    const Uint32 IndexElementSize = GetValueSize(m_IndexType);
    // Keep the source index representation in the key for now. PackIndexData()
    // always expands UINT8/UINT16 indices to UINT32 during upload.
    Hasher.Update(Uint64{m_IndexCount} * IndexElementSize);
    UpdateStridedRaw(Hasher, m_pIndexData, m_IndexCount, IndexElementSize, IndexElementSize);

    Hasher.Update(static_cast<Uint64>(m_VertexStrides.size()));
    for (Uint32 Stride : m_VertexStrides)
        Hasher.Update(Stride);

    std::vector<const GLTF::VertexAttributeDesc*> UsedDstAttributes;
    UsedDstAttributes.reserve(m_DstAttributes.size());
    for (const GLTF::VertexAttributeDesc& DstAttrib : m_DstAttributes)
    {
        const auto SrcAttribIt = m_SrcAttributes.find(DstAttrib.Name);
        const bool HasSource   = SrcAttribIt != m_SrcAttributes.end();
        // Default values only matter when they are written into an active
        // vertex buffer. Source-backed attributes ignore defaults.
        const bool UsesDefault = !HasSource &&
            DstAttrib.pDefaultValue != nullptr &&
            IsActiveVertexBuffer(m_ActiveVertexBufferMask, DstAttrib.BufferId);

        if (HasSource || UsesDefault)
            UsedDstAttributes.push_back(&DstAttrib);
    }

    Hasher.Update(static_cast<Uint64>(UsedDstAttributes.size()));
    for (const GLTF::VertexAttributeDesc* pDstAttrib : UsedDstAttributes)
    {
        const GLTF::VertexAttributeDesc& DstAttrib = *pDstAttrib;
        UpdateString(Hasher, DstAttrib.Name);
        Hasher.Update(DstAttrib.BufferId,
                      DstAttrib.ValueType,
                      DstAttrib.NumComponents,
                      DstAttrib.RelativeOffset);

        const auto SrcAttribIt = m_SrcAttributes.find(DstAttrib.Name);
        if (SrcAttribIt != m_SrcAttributes.end())
        {
            const SrcAttributeData& SrcAttrib = SrcAttribIt->second;
            Hasher.Update(true,
                          SrcAttrib.Type,
                          SrcAttrib.NumComponents,
                          SrcAttrib.IsNormalized,
                          SrcAttrib.ElementSize,
                          Uint64{m_VertexCount} * SrcAttrib.ElementSize);
            UpdateStridedRaw(Hasher, SrcAttrib.pData, m_VertexCount, SrcAttrib.ElementSize, SrcAttrib.Stride);
        }
        else
        {
            const Uint32 DstAttribSize = GetValueSize(DstAttrib.ValueType) * DstAttrib.NumComponents;
            Hasher.Update(false);
            UpdateRawIfNotEmpty(Hasher, DstAttrib.pDefaultValue, DstAttribSize);
        }
    }

    return std::string{"mesh-geometry:"} + Hasher.Digest().ToString();
}

RADIENT_STATUS RadientMeshSource::SetVertexAttributes(const GLTF::VertexAttributeDesc* pDstAttributes,
                                                      Uint32                           NumDstAttributes)
{
    if (RADIENT_FAILED(m_Status))
        return m_Status;

    if (pDstAttributes == nullptr || NumDstAttributes == 0)
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return m_Status;
    }

    std::vector<GLTF::VertexAttributeDesc> DstAttributes{pDstAttributes, pDstAttributes + NumDstAttributes};

    Uint32 MaxBufferId = 0;
    for (Uint32 AttribIndex = 0; AttribIndex < NumDstAttributes; ++AttribIndex)
    {
        const GLTF::VertexAttributeDesc& DstAttrib = DstAttributes[AttribIndex];
        if (DstAttrib.Name == nullptr ||
            DstAttrib.ValueType != VT_FLOAT32 ||
            DstAttrib.NumComponents == 0 ||
            DstAttrib.NumComponents > 4 ||
            DstAttrib.BufferId >= GLTF::ModelCreateInfo::MaxBuffers)
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }

        MaxBufferId = std::max<Uint32>(MaxBufferId, DstAttrib.BufferId);
    }

    std::vector<Uint32>                            VertexStrides(size_t{MaxBufferId} + 1, 0);
    std::vector<std::vector<VertexAttributeRange>> VertexAttributeRanges(size_t{MaxBufferId} + 1);
    for (GLTF::VertexAttributeDesc& DstAttrib : DstAttributes)
    {
        Uint32& BufferStride   = VertexStrides[DstAttrib.BufferId];
        Uint32& RelativeOffset = DstAttrib.RelativeOffset;
        // Resolve automatic offsets before validating ranges and computing
        // final strides.
        if (RelativeOffset == GLTF::VertexAttributeDesc{}.RelativeOffset)
            RelativeOffset = BufferStride;

        const Uint32 DstAttribSize = GetValueSize(DstAttrib.ValueType) * DstAttrib.NumComponents;
        if (!CheckByteOffset(RelativeOffset, DstAttribSize))
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }

        const Uint32 DstAttribEnd = RelativeOffset + DstAttribSize;
        VertexAttributeRanges[DstAttrib.BufferId].push_back(VertexAttributeRange{RelativeOffset, DstAttribEnd});
        BufferStride = std::max(BufferStride, DstAttribEnd);
    }

    for (std::vector<VertexAttributeRange>& Ranges : VertexAttributeRanges)
    {
        // Attributes in the same destination buffer must not overwrite each
        // other during PackVertexData().
        if (HasOverlappingRanges(Ranges))
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }
    }

    auto HasSourceBackedDstAttribute = [this, &DstAttributes](const char* Name) //
    {
        if (m_SrcAttributes.find(Name) == m_SrcAttributes.end())
            return false;

        for (const GLTF::VertexAttributeDesc& DstAttrib : DstAttributes)
        {
            if (IsAttributeName(DstAttrib, Name))
                return true;
        }
        return false;
    };

    if (!HasSourceBackedDstAttribute(GLTF::PositionAttributeName))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return m_Status;
    }

    Uint32 ActiveVertexBufferMask = 0;
    for (const GLTF::VertexAttributeDesc& DstAttrib : DstAttributes)
    {
        // Only buffers with source-backed attributes are uploaded. Default-only
        // attributes in inactive buffers do not create GPU vertex buffers.
        if (m_SrcAttributes.find(DstAttrib.Name) != m_SrcAttributes.end())
            ActiveVertexBufferMask |= Uint32{1} << DstAttrib.BufferId;
    }

    PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;
    if (HasSourceBackedDstAttribute(GLTF::NormalAttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
    if (HasSourceBackedDstAttribute(GLTF::TangentAttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
    if (HasSourceBackedDstAttribute(GLTF::Texcoord0AttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
    if (HasSourceBackedDstAttribute(GLTF::VertexColorAttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
    if (HasSourceBackedDstAttribute(GLTF::JointsAttributeName) &&
        HasSourceBackedDstAttribute(GLTF::WeightsAttributeName))
    {
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_JOINTS;
    }

    Uint32 VertexBufferCount = 0;
    for (Uint32 BufferIndex = 0; BufferIndex < VertexStrides.size(); ++BufferIndex)
    {
        if (IsActiveVertexBuffer(ActiveVertexBufferMask, BufferIndex))
            VertexBufferCount = BufferIndex + 1;
    }

    if (VertexBufferCount == 0 || VertexBufferCount > VertexStrides.size())
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return m_Status;
    }

    VertexStrides.resize(VertexBufferCount);
    std::vector<Uint32> VertexBufferDataSizes(VertexBufferCount, 0);

    for (Uint32 BufferIndex = 0; BufferIndex < VertexBufferCount; ++BufferIndex)
    {
        if (!IsActiveVertexBuffer(ActiveVertexBufferMask, BufferIndex))
            continue;

        const Uint32 VertexStride = VertexStrides[BufferIndex];
        if (VertexStride == 0 || !CheckByteSize(m_VertexCount, VertexStride))
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }

        VertexBufferDataSizes[BufferIndex] = m_VertexCount * VertexStride;
    }

    std::vector<std::string>              DstAttributeNames(DstAttributes.size());
    std::vector<std::unique_ptr<Uint8[]>> DstAttributeDefaultValues(DstAttributes.size());
    for (size_t AttribIndex = 0; AttribIndex < DstAttributes.size(); ++AttribIndex)
    {
        // VertexAttributeDesc contains borrowed pointers. Copy their backing
        // storage so packing and cache-key generation do not depend on caller
        // lifetime.
        const GLTF::VertexAttributeDesc& DstAttrib = DstAttributes[AttribIndex];
        DstAttributeNames[AttribIndex]             = DstAttrib.Name;

        if (DstAttrib.pDefaultValue != nullptr)
        {
            const Uint32 DstAttribSize = GetValueSize(DstAttrib.ValueType) * DstAttrib.NumComponents;
            DstAttributeDefaultValues[AttribIndex].reset(new Uint8[DstAttribSize]);
            std::memcpy(DstAttributeDefaultValues[AttribIndex].get(), DstAttrib.pDefaultValue, DstAttribSize);
        }
    }

    m_DstAttributes             = std::move(DstAttributes);
    m_DstAttributeNames         = std::move(DstAttributeNames);
    m_DstAttributeDefaultValues = std::move(DstAttributeDefaultValues);
    for (size_t AttribIndex = 0; AttribIndex < m_DstAttributes.size(); ++AttribIndex)
    {
        // Repoint descriptors to the final member storage after the moves.
        m_DstAttributes[AttribIndex].Name = m_DstAttributeNames[AttribIndex].c_str();

        m_DstAttributes[AttribIndex].pDefaultValue = m_DstAttributeDefaultValues[AttribIndex].get();
    }

    m_VertexAttribFlags      = VertexAttribFlags;
    m_ActiveVertexBufferMask = ActiveVertexBufferMask;
    m_VertexStrides          = std::move(VertexStrides);
    m_VertexBufferDataSizes  = std::move(VertexBufferDataSizes);

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientMeshSource::PackIndexData(PackDestination Destination) const
{
    if (RADIENT_FAILED(m_Status))
        return m_Status;

    if (Destination.pData == nullptr ||
        Destination.DataSize < GetIndexDataSize())
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    Uint8* const pDstIndices = static_cast<Uint8*>(Destination.pData);
    if (m_pIndexData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_IndexType == VT_UINT32)
    {
        std::memcpy(pDstIndices, m_pIndexData, GetIndexDataSize());
    }
    else if (m_IndexType == VT_UINT16)
    {
        for (Uint32 Index = 0; Index < m_IndexCount; ++Index)
        {
            Uint16 Value16 = 0;
            std::memcpy(&Value16, m_pIndexData + size_t{Index} * sizeof(Value16), sizeof(Value16));

            const Uint32 Value = Value16;
            std::memcpy(pDstIndices + size_t{Index} * sizeof(Uint32), &Value, sizeof(Value));
        }
    }
    else if (m_IndexType == VT_UINT8)
    {
        for (Uint32 Index = 0; Index < m_IndexCount; ++Index)
        {
            const Uint32 Value = m_pIndexData[Index];
            std::memcpy(pDstIndices + size_t{Index} * sizeof(Uint32), &Value, sizeof(Value));
        }
    }
    else
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientMeshSource::PackVertexData(Uint32          VertexBufferIndex,
                                                 PackDestination Destination) const
{
    if (RADIENT_FAILED(m_Status))
        return m_Status;

    if (m_DstAttributes.empty() ||
        Destination.pData == nullptr ||
        VertexBufferIndex >= m_VertexBufferDataSizes.size() ||
        !IsVertexBufferActive(VertexBufferIndex) ||
        Destination.DataSize == 0 ||
        Destination.DataSize < m_VertexBufferDataSizes[VertexBufferIndex])
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    // Missing attributes without explicit defaults remain zero-filled.
    std::memset(Destination.pData, 0, m_VertexBufferDataSizes[VertexBufferIndex]);

    for (const GLTF::VertexAttributeDesc& DstAttrib : m_DstAttributes)
    {
        if (DstAttrib.BufferId != VertexBufferIndex)
            continue;

        const auto SrcAttribIt    = m_SrcAttributes.find(DstAttrib.Name);
        Uint8*     pDstAttribData = static_cast<Uint8*>(Destination.pData) + DstAttrib.RelativeOffset;

        if (SrcAttribIt != m_SrcAttributes.end())
        {
            const SrcAttributeData& SrcAttrib = SrcAttribIt->second;
            const bool              Written   = GLTF::VertexDataConverter::Write({
                SrcAttrib.pData,
                SrcAttrib.Type,
                SrcAttrib.NumComponents,
                SrcAttrib.Stride,
                pDstAttribData,
                DstAttrib.ValueType,
                DstAttrib.NumComponents,
                m_VertexStrides[VertexBufferIndex],
                m_VertexCount,
                SrcAttrib.IsNormalized,
            });
            if (!Written)
                return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        else if (DstAttrib.pDefaultValue != nullptr)
        {
            const bool Written = GLTF::VertexDataConverter::WriteDefault({
                DstAttrib.pDefaultValue,
                pDstAttribData,
                DstAttrib.ValueType,
                DstAttrib.NumComponents,
                m_VertexStrides[VertexBufferIndex],
                m_VertexCount,
            });
            if (!Written)
                return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
