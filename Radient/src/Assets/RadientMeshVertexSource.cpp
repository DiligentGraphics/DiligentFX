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

#include "Assets/RadientMeshVertexSource.hpp"

#include "Assets/RadientAssetValidation.hpp"
#include "Assets/RadientVertexLayout.hpp"
#include "Core/RadientValidation.hpp"

#include "GLTFVertexDataConverter.hpp"
#include "GraphicsAccessories.hpp"
#include "XXH128Hasher.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Diligent
{

namespace
{

constexpr Uint32 MeshVertexSourceCacheKeyVersion = 3;

using RadientValidation::IsProductRepresentable;

VALUE_TYPE GetVertexValueType(RADIENT_VERTEX_COMPONENT_TYPE Type)
{
    switch (Type)
    {
        case RADIENT_VERTEX_COMPONENT_TYPE_INT8: return VT_INT8;
        case RADIENT_VERTEX_COMPONENT_TYPE_UINT8: return VT_UINT8;
        case RADIENT_VERTEX_COMPONENT_TYPE_INT16: return VT_INT16;
        case RADIENT_VERTEX_COMPONENT_TYPE_UINT16: return VT_UINT16;
        case RADIENT_VERTEX_COMPONENT_TYPE_INT32: return VT_INT32;
        case RADIENT_VERTEX_COMPONENT_TYPE_UINT32: return VT_UINT32;
        case RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16: return VT_FLOAT16;
        case RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32: return VT_FLOAT32;
        default: return VT_UNDEFINED;
    }
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
    const Char* Name  = nullptr;
    Uint32      Begin = 0;
    Uint32      End   = 0;
};

bool FindOverlappingRanges(std::vector<VertexAttributeRange>& Ranges,
                           VertexAttributeRange&              PrevRange,
                           VertexAttributeRange&              CurrRange)
{
    std::sort(Ranges.begin(), Ranges.end(), [](const VertexAttributeRange& Lhs, const VertexAttributeRange& Rhs) //
              {
                  return Lhs.Begin < Rhs.Begin ||
                      (Lhs.Begin == Rhs.Begin && Lhs.End < Rhs.End);
              });

    for (size_t RangeIndex = 1; RangeIndex < Ranges.size(); ++RangeIndex)
    {
        if (Ranges[RangeIndex].Begin < Ranges[RangeIndex - 1].End)
        {
            PrevRange = Ranges[RangeIndex - 1];
            CurrRange = Ranges[RangeIndex];
            return true;
        }
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

} // namespace

RadientMeshVertexSource::RadientMeshVertexSource(const RadientMeshVertexData& VertexData)
{
    ResolvedVertexLayout Resolved;
    if (!ValidateMeshVertexData(VertexData, Resolved).empty())
        return;

    const RadientVertexLayoutDesc&         Layout = VertexData.VertexLayout;
    decltype(m_SrcAttributes)              SrcAttributes;
    std::vector<RadientDataBlobReadAccess> Buffers(Layout.BufferCount);
    for (Uint32 AttributeIndex = 0; AttributeIndex < Layout.AttributeCount; ++AttributeIndex)
    {
        const RadientVertexAttributeDesc& Attribute   = Layout.pAttributes[AttributeIndex];
        const VALUE_TYPE                  Type        = GetVertexValueType(Attribute.ComponentType);
        const Uint32                      ElementSize = GetValueSize(Type) * Attribute.ComponentCount;
        const Uint32                      SrcStride   = Resolved.BufferStrides[Attribute.BufferIndex];
        const Uint32                      ByteOffset  = Resolved.AttributeOffsets[AttributeIndex];

        RadientDataBlobReadAccess& Buffer = Buffers[Attribute.BufferIndex];
        if (!Buffer)
        {
            Buffer = RadientDataBlobReadAccess{VertexData.ppVertexBuffers[Attribute.BufferIndex]};
            if (!Buffer)
            {
                m_Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }
        }
        const Uint64 DataSize = Uint64{VertexData.VertexCount - 1} * SrcStride + ElementSize;
        if (!RadientValidation::IsAddressableSize(Buffer.GetSize()) ||
            ByteOffset > Buffer.GetSize() || DataSize > Buffer.GetSize() - ByteOffset)
        {
            return;
        }
        if (Buffer.GetData() == nullptr)
        {
            m_Status = RADIENT_STATUS_INVALID_DATA;
            return;
        }

        SrcAttributeData Data;
        Data.Type          = Type;
        Data.NumComponents = static_cast<Uint8>(Attribute.ComponentCount);
        Data.IsNormalized  = Attribute.Normalized;
        Data.ElementSize   = ElementSize;
        Data.Stride        = SrcStride;
        Data.pData         = static_cast<const Uint8*>(Buffer.GetData()) + ByteOffset;

        SrcAttributes.emplace(HashMapStringKey{Attribute.Semantic, true}, std::move(Data));
    }

    // Preserve resolved source layouts for the direct-copy compatibility check.
    m_SrcBufferLayouts.resize(Layout.BufferCount);
    for (Uint32 BufferIndex = 0; BufferIndex < Layout.BufferCount; ++BufferIndex)
        m_SrcBufferLayouts[BufferIndex].ByteStride = Resolved.BufferStrides[BufferIndex];

    m_SrcLayoutAttributes.assign(Layout.pAttributes, Layout.pAttributes + Layout.AttributeCount);
    m_SrcAttributeNames.resize(Layout.AttributeCount);
    for (Uint32 AttributeIndex = 0; AttributeIndex < Layout.AttributeCount; ++AttributeIndex)
    {
        RadientVertexAttributeDesc& Attribute = m_SrcLayoutAttributes[AttributeIndex];
        m_SrcAttributeNames[AttributeIndex]   = Attribute.Semantic;
        Attribute.Semantic                    = m_SrcAttributeNames[AttributeIndex].c_str();
        Attribute.ByteOffset                  = Resolved.AttributeOffsets[AttributeIndex];
    }

    m_VertexCount   = VertexData.VertexCount;
    m_SrcAttributes = std::move(SrcAttributes);
    m_SrcBuffers    = std::move(Buffers);
    m_Status        = RADIENT_STATUS_OK;
}

std::string RadientMeshVertexSource::MakeCacheKey() const
{
    if (RADIENT_FAILED(m_Status) || m_DstAttributes.empty())
        return {};

    XXH128State Hasher;
    Hasher.Update(MeshVertexSourceCacheKeyVersion,
                  m_VertexCount,
                  m_ActiveVertexBufferMask);

    Hasher.Update(static_cast<Uint64>(m_VertexStrides.size()));
    for (Uint32 BufferIndex = 0; BufferIndex < m_VertexStrides.size(); ++BufferIndex)
    {
        if (IsActiveVertexBuffer(m_ActiveVertexBufferMask, BufferIndex))
            Hasher.Update(m_VertexStrides[BufferIndex]);
    }

    std::vector<const GLTF::VertexAttributeDesc*> UsedDstAttributes;
    UsedDstAttributes.reserve(m_DstAttributes.size());
    for (const GLTF::VertexAttributeDesc& DstAttrib : m_DstAttributes)
    {
        // Cached reflection includes every attribute in an active buffer,
        // including missing attributes whose stored elements are zero-filled.
        if (IsActiveVertexBuffer(m_ActiveVertexBufferMask, DstAttrib.BufferId))
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
            const bool HasDefault = DstAttrib.pDefaultValue != nullptr;
            Hasher.Update(false, HasDefault);
            if (HasDefault)
            {
                const Uint32 DstAttribSize = GetValueSize(DstAttrib.ValueType) * DstAttrib.NumComponents;
                UpdateRawIfNotEmpty(Hasher, DstAttrib.pDefaultValue, DstAttribSize);
            }
        }
    }

    return std::string{"mesh-vertex:"} + Hasher.Digest().ToString();
}

RADIENT_STATUS RadientMeshVertexSource::SetVertexAttributes(const GLTF::VertexAttributeDesc* pDstAttributes,
                                                            Uint32                           NumDstAttributes)
{
    if (RADIENT_FAILED(m_Status))
        return m_Status;

    if (pDstAttributes == nullptr || NumDstAttributes == 0)
    {
        LOG_ERROR_MESSAGE("Destination vertex attributes must not be empty.");
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return m_Status;
    }

    std::vector<GLTF::VertexAttributeDesc> DstAttributes{pDstAttributes, pDstAttributes + NumDstAttributes};

    Uint32 MaxBufferId = 0;
    for (Uint32 AttribIndex = 0; AttribIndex < NumDstAttributes; ++AttribIndex)
    {
        const GLTF::VertexAttributeDesc& DstAttrib = DstAttributes[AttribIndex];
        if (DstAttrib.Name == nullptr || DstAttrib.Name[0] == '\0' ||
            DstAttrib.ValueType != VT_FLOAT32 ||
            DstAttrib.NumComponents == 0 ||
            DstAttrib.NumComponents > 4 ||
            DstAttrib.BufferId >= GLTF::ModelCreateInfo::MaxBuffers)
        {
            LOG_ERROR_MESSAGE("Invalid destination vertex attribute at index ", AttribIndex,
                              ": name='", DstAttrib.Name != nullptr ? DstAttrib.Name : "<null>",
                              "', value type=", static_cast<Int32>(DstAttrib.ValueType),
                              ", component count=", static_cast<Uint32>(DstAttrib.NumComponents),
                              ", buffer id=", static_cast<Uint32>(DstAttrib.BufferId), ".");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }

        for (Uint32 PrevAttribIndex = 0; PrevAttribIndex < AttribIndex; ++PrevAttribIndex)
        {
            if (std::strcmp(DstAttrib.Name, DstAttributes[PrevAttribIndex].Name) == 0)
            {
                LOG_ERROR_MESSAGE("Duplicate destination vertex attribute '", DstAttrib.Name,
                                  "' at indices ", PrevAttribIndex, " and ", AttribIndex, ".");
                m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
                return m_Status;
            }
        }

        const Uint32 DstAttribSize = GetValueSize(DstAttrib.ValueType) * DstAttrib.NumComponents;
        if (DstAttribSize == 0)
        {
            LOG_ERROR_MESSAGE("Invalid destination vertex attribute '", DstAttrib.Name,
                              "': computed attribute size is zero.");
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
        if (!RadientValidation::IsSumRepresentable<Uint32>(RelativeOffset, DstAttribSize))
        {
            LOG_ERROR_MESSAGE("Destination vertex attribute '", DstAttrib.Name,
                              "' range overflows 32-bit vertex buffer offset: offset=",
                              RelativeOffset, ", size=", DstAttribSize, ".");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }

        const Uint32 DstAttribEnd = RelativeOffset + DstAttribSize;
        VertexAttributeRanges[DstAttrib.BufferId].push_back(VertexAttributeRange{DstAttrib.Name, RelativeOffset, DstAttribEnd});
        BufferStride = std::max(BufferStride, DstAttribEnd);
    }

    for (Uint32 BufferIndex = 0; BufferIndex < VertexAttributeRanges.size(); ++BufferIndex)
    {
        // Attributes in the same destination buffer must not overwrite each
        // other during PackVertexData().
        VertexAttributeRange PrevRange;
        VertexAttributeRange CurrRange;
        if (FindOverlappingRanges(VertexAttributeRanges[BufferIndex], PrevRange, CurrRange))
        {
            LOG_ERROR_MESSAGE("Destination vertex attributes '", PrevRange.Name,
                              "' [", PrevRange.Begin, ", ", PrevRange.End,
                              ") and '", CurrRange.Name, "' [", CurrRange.Begin,
                              ", ", CurrRange.End, ") overlap in vertex buffer ",
                              BufferIndex, ".");
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
        LOG_ERROR_MESSAGE("Destination vertex layout must include source-backed POSITION attribute.");
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return m_Status;
    }

    Uint32 ActiveVertexBufferMask = 0;
    for (const GLTF::VertexAttributeDesc& DstAttrib : DstAttributes)
    {
        // Only buffers with source-backed attributes are uploaded. Default-only
        // attributes in inactive buffers do not create GPU vertex buffers.
        const auto SrcAttribIt = m_SrcAttributes.find(DstAttrib.Name);
        if (SrcAttribIt == m_SrcAttributes.end())
            continue;
        if (!GLTF::VertexDataConverter::IsConversionSupported(SrcAttribIt->second.Type, DstAttrib.ValueType))
        {
            LOG_ERROR_MESSAGE("Unsupported vertex attribute conversion for '", DstAttrib.Name, "'.");
            m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
            return m_Status;
        }
        ActiveVertexBufferMask |= Uint32{1} << DstAttrib.BufferId;
    }

    PBR_Renderer::PSO_FLAGS VertexAttribFlags = PBR_Renderer::PSO_FLAG_NONE;
    if (HasSourceBackedDstAttribute(GLTF::NormalAttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
    if (HasSourceBackedDstAttribute(GLTF::TangentAttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS;
    if (HasSourceBackedDstAttribute(GLTF::Texcoord0AttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
    if (HasSourceBackedDstAttribute(GLTF::Texcoord1AttributeName))
        VertexAttribFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;
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
        if (VertexStride == 0 || VertexStride == RADIENT_VERTEX_AUTO_STRIDE ||
            !IsProductRepresentable<Uint32>(m_VertexCount, VertexStride))
        {
            LOG_ERROR_MESSAGE("Invalid vertex buffer ", BufferIndex, " stride ",
                              VertexStride, " for ", m_VertexCount, " vertices.");
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

    // Describe exactly the stored streams, including default and zero-filled
    // attributes that share an active buffer with source-backed attributes.
    m_StoredLayoutAttributes.clear();
    m_StoredBufferLayouts.clear();
    std::vector<Uint32> StoredBufferIndices(m_VertexStrides.size(), ~0u);
    for (Uint32 BufferIndex = 0; BufferIndex < m_VertexStrides.size(); ++BufferIndex)
    {
        if (!IsVertexBufferActive(BufferIndex))
            continue;
        StoredBufferIndices[BufferIndex] = static_cast<Uint32>(m_StoredBufferLayouts.size());
        m_StoredBufferLayouts.push_back({m_VertexStrides[BufferIndex]});
    }
    for (const auto& Attribute : m_DstAttributes)
    {
        if (!IsVertexBufferActive(Attribute.BufferId))
            continue;
        m_StoredLayoutAttributes.push_back({Attribute.Name,
                                            StoredBufferIndices[Attribute.BufferId],
                                            Attribute.RelativeOffset,
                                            RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32,
                                            Attribute.NumComponents,
                                            False});
    }

    // Find source buffers whose layouts already match the renderer's destination
    // buffers. Each matching source index lets PackVertexData use one memcpy;
    // ~0u selects attribute-by-attribute packing and conversion instead. Temporary
    // destination descriptors adapt the renderer's layout for the compatibility check.
    m_CopySourceBufferIndices.assign(m_VertexStrides.size(), ~0u);
    if (!m_SrcLayoutAttributes.empty())
    {
        const RadientVertexLayoutDesc SourceLayout{
            m_SrcLayoutAttributes.data(),
            static_cast<Uint32>(m_SrcLayoutAttributes.size()),
            m_SrcBufferLayouts.data(),
            static_cast<Uint32>(m_SrcBufferLayouts.size()),
        };
        for (Uint32 BufferIndex = 0; BufferIndex < m_VertexStrides.size(); ++BufferIndex)
        {
            if (!IsVertexBufferActive(BufferIndex))
                continue;
            std::vector<RadientVertexAttributeDesc> Attributes;
            for (const auto& Attribute : m_DstAttributes)
            {
                if (Attribute.BufferId == BufferIndex)
                {
                    Attributes.push_back({
                        Attribute.Name,
                        0,
                        Attribute.RelativeOffset,
                        RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32,
                        Attribute.NumComponents,
                        False,
                    });
                }
            }
            const RadientVertexBufferLayoutDesc Buffer{m_VertexStrides[BufferIndex]};
            const RadientVertexLayoutDesc       DestinationLayout{Attributes.data(), static_cast<Uint32>(Attributes.size()), &Buffer, 1};
            for (Uint32 SourceBuffer = 0; SourceBuffer < m_SrcBuffers.size(); ++SourceBuffer)
            {
                // A whole-buffer copy requires the full span, including padding
                // after the final vertex. Sources that omit this padding remain
                // valid for attribute-by-attribute packing, but not for this copy.
                if (m_SrcBuffers[SourceBuffer].GetSize() >= m_VertexBufferDataSizes[BufferIndex] &&
                    AreVertexBuffersCompatible(SourceLayout, SourceBuffer, DestinationLayout, 0))
                {
                    m_CopySourceBufferIndices[BufferIndex] = SourceBuffer;
                    break;
                }
            }
        }
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientMeshVertexSource::PackVertexData(Uint32          VertexBufferIndex,
                                                       PackDestination Destination) const noexcept
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

    const Uint32 SourceBuffer = m_CopySourceBufferIndices[VertexBufferIndex];
    if (SourceBuffer != ~0u)
    {
        std::memcpy(Destination.pData, m_SrcBuffers[SourceBuffer].GetData(), m_VertexBufferDataSizes[VertexBufferIndex]);
        return RADIENT_STATUS_OK;
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
