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

template <typename ValueType>
void CopyArray(std::vector<ValueType>& Dst, const ValueType* pSrc, Uint32 Count)
{
    if (pSrc != nullptr && Count != 0)
        Dst.assign(pSrc, pSrc + Count);
}

Uint32 GetIndexElementSize(RADIENT_INDEX_TYPE IndexType)
{
    switch (IndexType)
    {
        case RADIENT_INDEX_TYPE_UINT16:
            return sizeof(Uint16);

        case RADIENT_INDEX_TYPE_UINT32:
            return sizeof(Uint32);

        default:
            return 0;
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

    ElementSize = GetIndexElementSize(Indices.Type);
    if (ElementSize == 0)
        return false;

    return CheckByteSize(IndexCount, ElementSize);
}

bool ValidateMeshSourceCI(const RadientMeshSource::CreateInfo& CI)
{
    if (CI.VertexCount == 0 ||
        CI.AttributeCount == 0 ||
        CI.pAttributes == nullptr ||
        CI.IndexCount == 0 ||
        CI.PrimitiveCount == 0 ||
        CI.pPrimitives == nullptr)
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

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < CI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = CI.pPrimitives[PrimitiveIndex];
        if (PrimitiveCI.IndexCount == 0 ||
            PrimitiveCI.FirstIndex >= CI.IndexCount ||
            PrimitiveCI.IndexCount > CI.IndexCount - PrimitiveCI.FirstIndex)
        {
            return false;
        }
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

void HashMaterial(XXH128State& Hasher, IRadientMaterialAsset* pMaterial)
{
    const bool HasMaterial = pMaterial != nullptr;
    Hasher.Update(HasMaterial);
    if (!HasMaterial)
        return;

    const RadientAssetReference& MaterialRef = pMaterial->GetReference();
    const bool                   HasURI      = MaterialRef.URI != nullptr && MaterialRef.URI[0] != '\0';
    Hasher.Update(HasURI);
    if (HasURI)
    {
        UpdateString(Hasher, MaterialRef.URI);
        Hasher.Update(MaterialRef.Version);
    }
    else
    {
        Hasher.Update(reinterpret_cast<uintptr_t>(pMaterial));
    }
}

bool ValidateRadientMeshSourceCI(const RadientMeshCreateInfo& MeshCI)
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
    CI.Indices.Type   = MeshCI.IndexType;
    CI.IndexCount     = MeshCI.IndexCount;
    CI.pPrimitives    = MeshCI.pPrimitives;
    CI.PrimitiveCount = MeshCI.PrimitiveCount;

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
            Data.Stride = SrcStride;
            Data.pData  = pSrcBytes;
        }
        else
        {
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
        m_pIndexData = pSrcIndices;
    }
    else
    {
        if (m_IndexType == RADIENT_INDEX_TYPE_UINT32)
        {
            m_Indices32.resize(m_IndexCount);
            for (Uint32 Index = 0; Index < m_IndexCount; ++Index)
            {
                std::memcpy(&m_Indices32[Index],
                            pSrcIndices + size_t{Index} * IndexElementSize,
                            sizeof(m_Indices32[Index]));
            }
        }
        else
        {
            m_Indices16.resize(m_IndexCount);
            for (Uint32 Index = 0; Index < m_IndexCount; ++Index)
            {
                std::memcpy(&m_Indices16[Index],
                            pSrcIndices + size_t{Index} * IndexElementSize,
                            sizeof(m_Indices16[Index]));
            }
        }
    }

    CopyArray(m_Primitives, CI.pPrimitives, CI.PrimitiveCount);
    m_PrimitiveMaterials.reserve(CI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < CI.PrimitiveCount; ++PrimitiveIndex)
        m_PrimitiveMaterials.emplace_back(CI.pPrimitives[PrimitiveIndex].pMaterial);
}

std::string RadientMeshSource::MakeCacheKey() const
{
    if (RADIENT_FAILED(m_Status))
        return {};

    XXH128State Hasher;
    Hasher.Update(Uint32{3}, // Raw mesh cache key version.
                  m_VertexCount,
                  m_IndexCount,
                  m_IndexType);

    std::vector<const Char*> AttributeNames;
    AttributeNames.reserve(m_SrcAttributes.size());
    for (const auto& SrcAttrib : m_SrcAttributes)
        AttributeNames.push_back(SrcAttrib.first.GetStr());
    std::sort(AttributeNames.begin(), AttributeNames.end(), [](const Char* Lhs, const Char* Rhs) //
              {
                  return std::strcmp(Lhs, Rhs) < 0;
              });

    Hasher.Update(static_cast<Uint64>(AttributeNames.size()));
    for (const Char* Name : AttributeNames)
    {
        const auto SrcAttribIt = m_SrcAttributes.find(Name);
        VERIFY_EXPR(SrcAttribIt != m_SrcAttributes.end());
        if (SrcAttribIt == m_SrcAttributes.end())
            continue;

        const SrcAttributeData& SrcAttrib = SrcAttribIt->second;
        UpdateString(Hasher, Name);
        Hasher.Update(SrcAttrib.Type,
                      SrcAttrib.NumComponents,
                      SrcAttrib.IsNormalized,
                      SrcAttrib.ElementSize,
                      Uint64{m_VertexCount} * SrcAttrib.ElementSize);
        UpdateStridedRaw(Hasher, SrcAttrib.pData, m_VertexCount, SrcAttrib.ElementSize, SrcAttrib.Stride);
    }

    const Uint32 IndexElementSize = GetIndexElementSize(m_IndexType);
    const Uint8* pIndexData       = m_pIndexData;
    if (!m_Indices16.empty())
        pIndexData = reinterpret_cast<const Uint8*>(m_Indices16.data());
    else if (!m_Indices32.empty())
        pIndexData = reinterpret_cast<const Uint8*>(m_Indices32.data());

    Hasher.Update(Uint64{m_IndexCount} * IndexElementSize);
    UpdateStridedRaw(Hasher, pIndexData, m_IndexCount, IndexElementSize, IndexElementSize);

    Hasher.Update(static_cast<Uint64>(m_Primitives.size()));
    for (const RadientMeshPrimitiveCreateInfo& Primitive : m_Primitives)
    {
        Hasher.Update(Primitive.FirstIndex,
                      Primitive.IndexCount);
        HashMaterial(Hasher, Primitive.pMaterial);
    }

    return std::string{"raw-mesh:"} + Hasher.Digest().ToString();
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

    std::vector<Uint32> VertexStrides(size_t{MaxBufferId} + 1, 0);
    for (GLTF::VertexAttributeDesc& DstAttrib : DstAttributes)
    {
        Uint32& BufferStride   = VertexStrides[DstAttrib.BufferId];
        Uint32& RelativeOffset = DstAttrib.RelativeOffset;
        if (RelativeOffset == GLTF::VertexAttributeDesc{}.RelativeOffset)
            RelativeOffset = BufferStride;

        const Uint32 DstAttribSize = GetValueSize(DstAttrib.ValueType) * DstAttrib.NumComponents;
        if (!CheckByteOffset(RelativeOffset, DstAttribSize))
        {
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }

        BufferStride = std::max(BufferStride, RelativeOffset + DstAttribSize);
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

    m_DstAttributes          = std::move(DstAttributes);
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
    if (!m_Indices32.empty())
    {
        std::memcpy(pDstIndices, m_Indices32.data(), GetIndexDataSize());
    }
    else if (!m_Indices16.empty())
    {
        for (Uint32 Index = 0; Index < m_IndexCount; ++Index)
        {
            const Uint32 Value = m_Indices16[Index];
            std::memcpy(pDstIndices + size_t{Index} * sizeof(Uint32), &Value, sizeof(Value));
        }
    }
    else if (m_pIndexData != nullptr)
    {
        const Uint32 IndexElementSize = GetIndexElementSize(m_IndexType);
        for (Uint32 Index = 0; Index < m_IndexCount; ++Index)
        {
            Uint32 Value = 0;
            if (m_IndexType == RADIENT_INDEX_TYPE_UINT32)
            {
                std::memcpy(&Value, m_pIndexData + size_t{Index} * IndexElementSize, sizeof(Value));
            }
            else
            {
                Uint16 Value16 = 0;
                std::memcpy(&Value16, m_pIndexData + size_t{Index} * IndexElementSize, sizeof(Value16));
                Value = Value16;
            }
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
