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

#include "Buffers.h"

#include <cstring>
#include <vector>
#include <emscripten/heap.h>

namespace Diligent
{
namespace
{
Uint32 LiveBufferCount = 0;
Uint32 ReleaseCount    = 0;

// Controlled byte storage for marshalling tests, independent of any production
// blob implementation. Read and write acquisitions deliberately return different
// success codes to exercise the explicit adapter contract.
class MarshalBytes final : public IMarshalBytes
{
public:
    explicit MarshalBytes(const MarshalBufferDesc& Desc) :
        m_Data(static_cast<size_t>(Desc.ByteCount)),
        m_ReportedSize{Desc.ByteCount}
    {
        if (Desc.pBytes && !m_Data.empty())
            std::memcpy(m_Data.data(), Desc.pBytes, m_Data.size());
        ++LiveBufferCount;
    }

    void QueryInterface(const INTERFACE_ID& IID, IObject** ppObject) override
    {
        *ppObject = nullptr;
        if (IID == IID_Unknown || IID == IID_MarshalBytes)
        {
            *ppObject = this;
            AddRef();
        }
    }
    ReferenceCounterValueType AddRef() override { return ++m_Refs; }
    ReferenceCounterValueType Release() override
    {
        const Uint32 Count = --m_Refs;
        if (Count == 0)
            delete this;
        return Count;
    }
    IReferenceCounters* GetReferenceCounters() const override { return nullptr; }
    Uint64              Size() const override { return m_ReportedSize; }
    Uint32              RefCount() const override { return m_Refs; }
    Uint32              ReaderCount() const override { return m_Readers; }
    Bool                IsWriting() const override { return m_Writing; }
    void                SetReportedSize(Uint64 Size) override { m_ReportedSize = Size; }
    void                SetReleaseStatus(MARSHAL_STATUS Status) override { m_ReleaseStatus = Status; }

    MARSHAL_STATUS MapRead(const void** ppData) override
    {
        *ppData = nullptr;
        if (m_Writing)
            return MARSHAL_STATUS_FAILED;
        ++m_Readers;
        *ppData = m_Data.empty() ? nullptr : m_Data.data();
        return MARSHAL_STATUS_OK;
    }
    MARSHAL_STATUS UnmapRead() override
    {
        if (m_Readers == 0)
            return MARSHAL_STATUS_FAILED;
        --m_Readers;
        ++ReleaseCount;
        return m_ReleaseStatus;
    }
    MARSHAL_STATUS MapWrite(void** ppData) override
    {
        *ppData = nullptr;
        if (m_Writing || m_Readers != 0)
            return MARSHAL_STATUS_FAILED;
        m_Writing = true;
        *ppData   = m_Data.empty() ? nullptr : m_Data.data();
        return MARSHAL_STATUS_PENDING;
    }
    MARSHAL_STATUS UnmapWrite() override
    {
        if (!m_Writing)
            return MARSHAL_STATUS_FAILED;
        m_Writing = false;
        ++ReleaseCount;
        return m_ReleaseStatus;
    }
    MARSHAL_STATUS Resize(Uint64 Size) override
    {
        if (m_Readers != 0 || m_Writing || Size > 1024 * 1024)
            return MARSHAL_STATUS_FAILED;
        m_Data.resize(static_cast<size_t>(Size));
        m_ReportedSize = Size;
        return MARSHAL_STATUS_OK;
    }

private:
    ~MarshalBytes() { --LiveBufferCount; }
    std::vector<Uint8> m_Data;
    Uint64             m_ReportedSize;
    Uint32             m_Refs          = 1;
    Uint32             m_Readers       = 0;
    bool               m_Writing       = false;
    MARSHAL_STATUS     m_ReleaseStatus = MARSHAL_STATUS_OK;
};
} // namespace

MARSHAL_STATUS CreateMarshalBytes(const MarshalBufferDesc& Desc, IMarshalBytes** ppBuffer)
{
    *ppBuffer = nullptr;
    if (Desc.ByteCount > 1024 * 1024)
        return MARSHAL_STATUS_FAILED;
    *ppBuffer = new MarshalBytes{Desc};
    return MARSHAL_STATUS_OK;
}
Uint32 ChecksumBytes(const void* pBytes, Uint64 ByteCount, Bool Invert)
{
    const Uint8* pData    = static_cast<const Uint8*>(pBytes);
    Uint32       Checksum = 0;
    for (Uint64 i = 0; i < ByteCount; ++i)
        Checksum = Checksum * 31 + pData[i];
    return Invert ? ~Checksum : Checksum;
}
Bool CheckBytes(Uint32 ByteCount, const Uint8* pBytes)
{
    return ByteCount == 3 && pBytes[0] == 17 && pBytes[1] == 128 && pBytes[2] == 254;
}
Uint32 GetLiveBufferCount() { return LiveBufferCount; }
Uint32 GetBufferReleaseCount() { return ReleaseCount; }
Uint64 GetTestHeapSize() { return emscripten_get_heap_size(); }
Bool   GrowTestHeap() { return emscripten_resize_heap(emscripten_get_heap_size() + 65536) != 0; }
} // namespace Diligent
