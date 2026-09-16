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

class RadientDataBlobImpl final : public ObjectBase<IRadientDataBlob>
{
public:
    using TBase = ObjectBase<IRadientDataBlob>;

    RadientDataBlobImpl(IReferenceCounters* pRefCounters, const RadientDataBlobCreateInfo& CI) :
        TBase{pRefCounters},
        m_Data(static_cast<size_t>(CI.Size)),
        m_OnLastReaderReleased{CI.OnLastReaderReleased},
        m_pUserData{CI.pUserData}
    {
        if (CI.pInitialData != nullptr && !m_Data.empty())
            std::memcpy(m_Data.data(), CI.pInitialData, m_Data.size());
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientDataBlob, TBase);

    Uint64 DILIGENT_CALL_TYPE GetSize() const override final
    {
        return static_cast<Uint64>(m_Data.size());
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
        *ppData = m_Data.empty() ? nullptr : m_Data.data();
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
            // A callback can reenter this object or release the caller's reference.
            // Access is already released; no internal lock is held during the call.
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

private:
    // Never resized, so acquiring access cannot move the bytes or change GetSize().
    std::vector<Uint8>                           m_Data;
    const RadientDataBlobReadReleaseCallbackType m_OnLastReaderReleased;
    void* const                                  m_pUserData;
    std::mutex                                   m_AccessMutex;
    size_t                                       m_ReaderCount = 0;
    bool                                         m_Writing     = false;
};

} // namespace

RADIENT_STATUS CreateRadientDataBlob(const RadientDataBlobCreateInfo& CreateInfo,
                                     IRadientDataBlob**               ppBlob)
{
    if (ppBlob == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppBlob == nullptr, "Output data blob pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppBlob = nullptr;

    if (CreateInfo.Size > std::vector<Uint8>{}.max_size())
        return RADIENT_STATUS_INVALID_ARGUMENT;

    try
    {
        RefCntAutoPtr<RadientDataBlobImpl> pBlob{MakeNewRCObj<RadientDataBlobImpl>()(CreateInfo)};
        *ppBlob = pBlob.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (...)
    {
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent

extern "C"
{
    Diligent::RADIENT_STATUS Diligent_CreateRadientDataBlob(
        const Diligent::RadientDataBlobCreateInfo* pCreateInfo,
        Diligent::IRadientDataBlob**               ppBlob)
    {
        if (pCreateInfo == nullptr)
        {
            if (ppBlob != nullptr)
                *ppBlob = nullptr;
            return Diligent::RADIENT_STATUS_INVALID_ARGUMENT;
        }
        return Diligent::CreateRadientDataBlob(*pCreateInfo, ppBlob);
    }
}
