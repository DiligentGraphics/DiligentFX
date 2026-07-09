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

#include "Assets/RadientMeshIndexSource.hpp"

#include "GraphicsAccessories.hpp"
#include "XXH128Hasher.hpp"

#include <cstring>
#include <limits>

namespace Diligent
{

namespace
{

bool CheckByteSize(Uint32 Count, Uint32 Stride)
{
    return Uint64{Count} * Stride <= (std::numeric_limits<Uint32>::max)();
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

bool GetSourceIndexLayout(const void* pData,
                          VALUE_TYPE  Type,
                          Uint32      IndexCount,
                          Uint32&     ElementSize)
{
    if (pData == nullptr)
        return false;

    if (!IsSupportedSourceIndexType(Type))
        return false;

    ElementSize = GetValueSize(Type);
    if (ElementSize == 0)
        return false;

    return CheckByteSize(IndexCount, ElementSize);
}

bool ValidateMeshIndexSourceCI(const RadientMeshIndexSource::CreateInfo& CI)
{
    if (CI.IndexCount == 0)
        return false;

    if (!CheckByteSize(CI.IndexCount, sizeof(Uint32)))
        return false;

    Uint32 IndexElementSize = 0;
    return GetSourceIndexLayout(CI.pData, CI.Type, CI.IndexCount, IndexElementSize);
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

} // namespace

RadientMeshIndexSource::RadientMeshIndexSource(const CreateInfo& CI)
{
    Initialize(CI);
}

RadientMeshIndexSource::RadientMeshIndexSource(const RadientMeshCreateInfo& MeshCI)
{
    CreateInfo CI;
    CI.pData      = MeshCI.pIndices;
    CI.Type       = GetSourceIndexType(MeshCI.IndexType);
    CI.IndexCount = MeshCI.IndexCount;
    Initialize(CI);
}

void RadientMeshIndexSource::Initialize(const CreateInfo& CI)
{
    m_Status     = RADIENT_STATUS_OK;
    m_IndexCount = 0;
    m_IndexType  = VT_UNDEFINED;
    m_pIndexData = nullptr;
    m_Indices.clear();
    m_pSourceDataOwner.reset();

    if (!ValidateMeshIndexSourceCI(CI))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    m_IndexCount = CI.IndexCount;
    m_IndexType  = CI.Type;

    Uint32 IndexElementSize = 0;
    if (!GetSourceIndexLayout(CI.pData, m_IndexType, m_IndexCount, IndexElementSize))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    const bool BorrowSourceData = CI.pSourceDataOwner != nullptr;
    if (BorrowSourceData)
        m_pSourceDataOwner = CI.pSourceDataOwner;

    const Uint8* const pSrcIndices = static_cast<const Uint8*>(CI.pData);
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

std::string RadientMeshIndexSource::MakeCacheKey() const
{
    if (RADIENT_FAILED(m_Status))
        return {};

    const Uint32 IndexElementSize = GetValueSize(m_IndexType);
    if (IndexElementSize == 0 || m_pIndexData == nullptr)
        return {};

    XXH128State Hasher;
    Hasher.Update(Uint32{1}, // Mesh index source cache key version.
                  m_IndexCount,
                  m_IndexType,
                  Uint64{m_IndexCount} * IndexElementSize);
    UpdateStridedRaw(Hasher, m_pIndexData, m_IndexCount, IndexElementSize, IndexElementSize);

    return std::string{"mesh-index:"} + Hasher.Digest().ToString();
}

RADIENT_STATUS RadientMeshIndexSource::PackIndexData(PackDestination Destination) const
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

} // namespace Diligent
