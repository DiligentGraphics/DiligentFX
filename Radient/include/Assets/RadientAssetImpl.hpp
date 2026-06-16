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

template <typename InterfaceType,
          const INTERFACE_ID& InterfaceID,
          const INTERFACE_ID& ImplID,
          RADIENT_ASSET_TYPE  AssetType,
          typename StorageType>
class RadientAssetImpl final : public ObjectBase<InterfaceType>
{
public:
    using TBase   = ObjectBase<InterfaceType>;
    using Storage = StorageType;

    RadientAssetImpl(IReferenceCounters* pRefCounters,
                     std::string&&       URI,
                     const Char*         Name,
                     StorageType&&       Storage) :
        TBase{pRefCounters},
        m_URI{std::move(URI)},
        m_Name{Name != nullptr ? Name : ""},
        m_Storage{std::move(Storage)}
    {
        m_Ref.URI     = m_URI.c_str();
        m_Ref.Version = 1;
    }

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Ref;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return AssetType;
    }

    StorageType& GetStorage()
    {
        return m_Storage;
    }

    const StorageType& GetStorage() const
    {
        return m_Storage;
    }

    template <typename T = StorageType>
    auto GetLoadStatus() const -> decltype(std::declval<const T&>().LoadStatus.load())
    {
        return m_Storage.LoadStatus.load(std::memory_order_acquire);
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

    template <typename T = StorageType>
    static auto GetLoadStatus(IRadientAsset* pAsset) -> decltype(std::declval<const T&>().LoadStatus.load())
    {
        RefCntAutoPtr<RadientAssetImpl> pImpl{pAsset, ImplID};
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
    void RemoveFromCache() noexcept
    {
        if (m_CacheKey.empty())
            return;

        if (std::shared_ptr<IRadientAssetCacheRemovalHandler> pHandler = m_CacheRemovalHandler.lock())
            pHandler->RemoveAssetFromCache(m_CacheKey.c_str());
    }

private:
    std::string           m_URI;
    std::string           m_Name;
    RadientAssetReference m_Ref;
    StorageType           m_Storage;

    std::weak_ptr<IRadientAssetCacheRemovalHandler> m_CacheRemovalHandler;
    std::string                                     m_CacheKey;
};

} // namespace Diligent
