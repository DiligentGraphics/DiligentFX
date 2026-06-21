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

#include "RadientAssets.h"
#include "DebugUtilities.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <utility>

namespace Diligent
{

class IRadientAssetCacheRemovalHandler
{
public:
    virtual void RemoveAssetFromCache(const Char* CacheKey) noexcept = 0;

protected:
    ~IRadientAssetCacheRemovalHandler() = default;
};

template <typename StorageType, typename PayloadType>
class RadientAssetPayloadImpl : public ObjectBase<IObject>
{
public:
    using TBase   = ObjectBase<IObject>;
    using Storage = StorageType;
    using Payload = PayloadType;

    RadientAssetPayloadImpl(IReferenceCounters* pRefCounters,
                            StorageType&&       Storage) :
        TBase{pRefCounters},
        m_Storage{std::move(Storage)}
    {
    }

    static RefCntAutoPtr<PayloadType> Create(StorageType&& Storage)
    {
        return RefCntAutoPtr<PayloadType>{
            MakeNewRCObj<PayloadType>()(std::move(Storage))};
    }

    StorageType& GetStorage()
    {
        return m_Storage;
    }

    const StorageType& GetStorage() const
    {
        return m_Storage;
    }

    void SetCacheRemovalHandler(std::weak_ptr<IRadientAssetCacheRemovalHandler> Handler,
                                const Char*                                     CacheKey)
    {
        m_CacheRemovalHandler = std::move(Handler);
        m_CacheKey            = CacheKey != nullptr ? CacheKey : "";
    }

    virtual ReferenceCounterValueType DILIGENT_CALL_TYPE Release() override final
    {
        return TBase::Release([this]() {
            RemoveFromCache();
        });
    }

private:
    void RemoveFromCache() noexcept
    {
        if (m_CacheKey.empty())
            return;

        if (std::shared_ptr<IRadientAssetCacheRemovalHandler> pHandler = m_CacheRemovalHandler.lock())
            pHandler->RemoveAssetFromCache(m_CacheKey.c_str());
    }

private:
    StorageType m_Storage;

    std::weak_ptr<IRadientAssetCacheRemovalHandler> m_CacheRemovalHandler;
    std::string                                     m_CacheKey;
};

template <typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          const INTERFACE_ID& ImplID,
          RADIENT_ASSET_TYPE  AssetType,
          typename PayloadType>
class RadientAssetImpl final : public ObjectBase<InterfaceType>
{
public:
    using TBase   = ObjectBase<InterfaceType>;
    using Payload = PayloadType;
    using Storage = typename PayloadType::Storage;

    RadientAssetImpl(IReferenceCounters*          pRefCounters,
                     std::string&&                AssetURI,
                     RefCntAutoPtr<PayloadType>&& pPayload = {}) :
        TBase{pRefCounters},
        m_URI{std::move(AssetURI)}
    {
        m_Ref.URI     = m_URI.c_str();
        m_Ref.Version = 1;

        if (pPayload)
            SetPayload(std::move(pPayload));
    }

    static RefCntAutoPtr<RadientAssetImpl> Create(std::string                  AssetURI,
                                                  RefCntAutoPtr<PayloadType>&& pPayload = {})
    {
        return RefCntAutoPtr<RadientAssetImpl>{
            MakeNewRCObj<RadientAssetImpl>()(std::move(AssetURI), std::move(pPayload))};
    }

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Ref;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return AssetType;
    }

    Storage& GetStorage()
    {
        VERIFY_EXPR(m_pPayload != nullptr);
        return m_pPayload->GetStorage();
    }

    const Storage& GetStorage() const
    {
        VERIFY_EXPR(m_pPayload != nullptr);
        return m_pPayload->GetStorage();
    }

    /// Reports whether this asset handle has been assigned a payload.
    /// RADIENT_STATUS_OK only means payload storage is available; it does not imply
    /// that payload content has loaded or that GPU resources are ready.
    RADIENT_STATUS GetPayloadStatus() const
    {
        return m_PayloadStatus.load(std::memory_order_acquire);
    }

    bool SetPayload(RefCntAutoPtr<PayloadType>&& pPayload)
    {
        if (pPayload == nullptr)
        {
            Fail(RADIENT_STATUS_INVALID_OPERATION);
            return false;
        }

        m_pPayload = std::move(pPayload);
        m_PayloadStatus.store(RADIENT_STATUS_OK, std::memory_order_release);
        return true;
    }

    void Fail(RADIENT_STATUS Status)
    {
        if (Status == RADIENT_STATUS_OK ||
            Status == RADIENT_STATUS_PENDING)
        {
            Status = RADIENT_STATUS_INVALID_OPERATION;
        }

        m_PayloadStatus.store(Status, std::memory_order_release);
    }

    RefCntAutoPtr<PayloadType> GetPayload() const
    {
        if (GetPayloadStatus() != RADIENT_STATUS_OK)
            return {};

        return m_pPayload;
    }

    static RefCntAutoPtr<RadientAssetImpl> ResolveAsset(InterfaceType* pAsset)
    {
        RefCntAutoPtr<RadientAssetImpl> pImpl{pAsset, ImplID};
        if (!pImpl || pImpl->GetPayloadStatus() != RADIENT_STATUS_OK)
            return {};

        return pImpl;
    }

    /// Reports payload load/source-processing status.
    /// RADIENT_STATUS_OK does not imply that queued GPU upload callbacks have completed.
    template <typename T = Storage>
    auto GetLoadStatus() const -> decltype(std::declval<const T&>().LoadStatus.load())
    {
        const RADIENT_STATUS PayloadStatus = GetPayloadStatus();
        if (PayloadStatus != RADIENT_STATUS_OK)
            return PayloadStatus;

        RefCntAutoPtr<PayloadType> pPayload = GetPayload();
        return pPayload ? pPayload->GetStorage().LoadStatus.load(std::memory_order_acquire) : RADIENT_STATUS_INVALID_OPERATION;
    }

    /// Reports payload load/source-processing status for a generic asset pointer.
    /// RADIENT_STATUS_OK does not imply that queued GPU upload callbacks have completed.
    template <typename T = Storage>
    static auto GetLoadStatus(IRadientAsset* pAsset) -> decltype(std::declval<const T&>().LoadStatus.load())
    {
        const RadientAssetImpl* pImpl = ClassPtrCast<const RadientAssetImpl>(pAsset);
        return pImpl ? pImpl->GetLoadStatus() : RADIENT_STATUS_INVALID_ARGUMENT;
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override
    {
        if (ppInterface == nullptr)
            return;

        if (IID == InterfaceID || IID == IID_RadientAsset || IID == ImplID)
        {
            *ppInterface = this;
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

private:
    std::string           m_URI;
    RadientAssetReference m_Ref;

    RefCntAutoPtr<PayloadType>  m_pPayload;
    std::atomic<RADIENT_STATUS> m_PayloadStatus{RADIENT_STATUS_PENDING};
};

} // namespace Diligent
