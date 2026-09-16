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

#include "RadientDataBlob.h"

#include "Core/RadientValidation.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace Diligent
{

namespace
{

// Both capabilities use the same storage, reader accounting, and callback rules.
// The read-only instantiation has no mutable interface or write methods in its vtable.
template <typename InterfaceType>
class RadientDataBlobBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    RadientDataBlobBase(IReferenceCounters*              pRefCounters,
                        const RadientDataBlobCreateInfo& CI,
                        RADIENT_DATA_BLOB_STORAGE_MODE   StorageMode) :
        TBase{pRefCounters},
        m_Data(StorageMode == RADIENT_DATA_BLOB_STORAGE_MODE_COPY ? static_cast<size_t>(CI.Size) : 0),
        m_pData{CI.Size == 0 ? nullptr :
                               (StorageMode == RADIENT_DATA_BLOB_STORAGE_MODE_COPY ? m_Data.data() : CI.pData)},
        m_Size{CI.Size},
        m_OnLastReaderReleased{CI.OnLastReaderReleased},
        m_pUserData{CI.pUserData},
        m_OnDestroy{CI.OnDestroy}
    {
        if (CI.pData != nullptr && !m_Data.empty())
            std::memcpy(m_Data.data(), CI.pData, m_Data.size());
    }

    ~RadientDataBlobBase()
    {
        if (m_OnDestroy != nullptr)
        {
            try
            {
                m_OnDestroy(m_pUserData);
            }
            catch (...)
            {
                LOG_ERROR_MESSAGE("Radient data blob destruction callback threw an exception.");
            }
        }
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientDataBlob, TBase);

    Uint64 DILIGENT_CALL_TYPE GetSize() const override final
    {
        std::lock_guard<std::mutex> Lock{m_AccessMutex};
        return m_Size;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE BeginRead(const void** ppData) override final
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppData = nullptr;

        std::lock_guard<std::mutex> Lock{m_AccessMutex};
        if (m_Writing || m_ReaderCount == (std::numeric_limits<size_t>::max)())
            return RADIENT_STATUS_INVALID_OPERATION;

        ++m_ReaderCount;
        *ppData = m_pData;
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE EndRead() override final
    {
        RefCntAutoPtr<IRadientDataBlob> KeepAlive;
        {
            std::lock_guard<std::mutex> Lock{m_AccessMutex};
            if (m_ReaderCount == 0)
                return RADIENT_STATUS_INVALID_OPERATION;

            --m_ReaderCount;
            if (m_ReaderCount == 0 && m_OnLastReaderReleased != nullptr)
                KeepAlive = this;
        }

        if (KeepAlive)
        {
            // Notifications may reenter or release the caller's reference. This
            // reference also postpones OnDestroy until the notification finishes.
            try
            {
                m_OnLastReaderReleased(this, m_pUserData);
            }
            catch (...)
            {
                return RADIENT_STATUS_FAILED;
            }
        }
        return RADIENT_STATUS_OK;
    }

protected:
    // In REFERENCE mode the vector is empty and m_pData points to the caller's
    // bytes. Mutable blobs update the pointer and size after resizing storage.
    std::vector<Uint8> m_Data;
    const void*        m_pData;
    Uint64             m_Size;

private:
    const RadientDataBlobReadReleaseCallbackType m_OnLastReaderReleased;
    void* const                                  m_pUserData;
    const RadientDataBlobDestroyCallbackType     m_OnDestroy;

protected:
    mutable std::mutex m_AccessMutex;
    size_t             m_ReaderCount = 0;
    bool               m_Writing     = false;
};

class RadientDataBlobImpl final : public RadientDataBlobBase<IRadientDataBlob>
{
public:
    using TBase = RadientDataBlobBase<IRadientDataBlob>;
    using TBase::TBase;
};

class RadientMutableDataBlobImpl final : public RadientDataBlobBase<IRadientMutableDataBlob>
{
public:
    using TBase = RadientDataBlobBase<IRadientMutableDataBlob>;
    using TBase::TBase;

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMutableDataBlob, TBase);

    RADIENT_STATUS DILIGENT_CALL_TYPE BeginWrite(void** ppData) override final
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppData = nullptr;

        std::lock_guard<std::mutex> Lock{m_AccessMutex};
        if (m_Writing || m_ReaderCount != 0)
            return RADIENT_STATUS_INVALID_OPERATION;

        m_Writing = true;
        *ppData   = m_Data.empty() ? nullptr : m_Data.data();
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE EndWrite() override final
    {
        std::lock_guard<std::mutex> Lock{m_AccessMutex};
        if (!m_Writing)
            return RADIENT_STATUS_INVALID_OPERATION;

        m_Writing = false;
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE Resize(Uint64 NewSize) override final
    {
        std::lock_guard<std::mutex> Lock{m_AccessMutex};
        if (m_Writing || m_ReaderCount != 0)
            return RADIENT_STATUS_INVALID_OPERATION;
        if (NewSize > m_Data.max_size())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        try
        {
            m_Data.resize(static_cast<size_t>(NewSize));
        }
        catch (...)
        {
            return RADIENT_STATUS_FAILED;
        }

        m_pData = m_Data.empty() ? nullptr : m_Data.data();
        m_Size  = NewSize;
        return RADIENT_STATUS_OK;
    }
};

template <typename ImplType, typename InterfaceType>
RADIENT_STATUS CreateBlob(const RadientDataBlobCreateInfo& CI,
                          RADIENT_DATA_BLOB_STORAGE_MODE   StorageMode,
                          InterfaceType**                  ppBlob)
{
    if (ppBlob == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppBlob == nullptr, "Output data blob pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppBlob = nullptr;

    if (StorageMode == RADIENT_DATA_BLOB_STORAGE_MODE_COPY)
    {
        if (CI.Size > std::vector<Uint8>{}.max_size())
            return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    else if (StorageMode == RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE)
    {
        if (!RadientValidation::IsAddressableSize(CI.Size) || (CI.Size != 0 && CI.pData == nullptr))
            return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    else
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    try
    {
        // Byte allocation is the only throwing construction step and precedes
        // callback registration. Failure therefore leaves context cleanup to the caller.
        RefCntAutoPtr<ImplType> pBlob{MakeNewRCObj<ImplType>()(CI, StorageMode)};
        *ppBlob = pBlob.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (...)
    {
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace

RADIENT_STATUS CreateRadientDataBlob(const RadientDataBlobCreateInfo& CreateInfo,
                                     RADIENT_DATA_BLOB_STORAGE_MODE   StorageMode,
                                     IRadientDataBlob**               ppBlob)
{
    return CreateBlob<RadientDataBlobImpl>(CreateInfo, StorageMode, ppBlob);
}

RADIENT_STATUS CreateRadientMutableDataBlob(const RadientDataBlobCreateInfo& CreateInfo,
                                            IRadientMutableDataBlob**        ppBlob)
{
    return CreateBlob<RadientMutableDataBlobImpl>(CreateInfo, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, ppBlob);
}

} // namespace Diligent

extern "C"
{
    Diligent::RADIENT_STATUS Diligent_CreateRadientDataBlob(
        const Diligent::RadientDataBlobCreateInfo* pCreateInfo,
        Diligent::RADIENT_DATA_BLOB_STORAGE_MODE   StorageMode,
        Diligent::IRadientDataBlob**               ppBlob)
    {
        if (pCreateInfo == nullptr)
        {
            if (ppBlob != nullptr)
                *ppBlob = nullptr;
            return Diligent::RADIENT_STATUS_INVALID_ARGUMENT;
        }
        return Diligent::CreateRadientDataBlob(*pCreateInfo, StorageMode, ppBlob);
    }

    Diligent::RADIENT_STATUS Diligent_CreateRadientMutableDataBlob(
        const Diligent::RadientDataBlobCreateInfo* pCreateInfo,
        Diligent::IRadientMutableDataBlob**        ppBlob)
    {
        if (pCreateInfo == nullptr)
        {
            if (ppBlob != nullptr)
                *ppBlob = nullptr;
            return Diligent::RADIENT_STATUS_INVALID_ARGUMENT;
        }
        return Diligent::CreateRadientMutableDataBlob(*pCreateInfo, ppBlob);
    }
}
