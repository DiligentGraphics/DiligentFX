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
#include "HashUtils.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace Diligent
{

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
    std::string           m_URI;
    std::string           m_Name;
    RadientAssetReference m_Ref;
    StorageType           m_Storage;
};

template <typename InterfaceType>
class RadientAssetCache
{
public:
    template <typename ImplType, typename CreateAssetFuncType>
    std::pair<RefCntAutoPtr<ImplType>, bool> GetOrCreate(const std::string&    CacheKey,
                                                         const INTERFACE_ID&   ImplID,
                                                         CreateAssetFuncType&& CreateAssetFunc)
    {
        std::unique_lock<std::shared_mutex> Lock{m_Mutex};

        const auto It = m_Assets.find(HashMapStringKey{CacheKey.c_str()});
        if (It != m_Assets.end())
        {
            RefCntAutoPtr<InterfaceType> pExisting = It->second.Lock();
            if (pExisting != nullptr)
                return {RefCntAutoPtr<ImplType>{pExisting, ImplID}, false};
        }

        RefCntAutoPtr<ImplType> pAsset = CreateAssetFunc();
        if (!pAsset)
        {
            LOG_ERROR_MESSAGE("Failed to create asset for cache key '", CacheKey, "'");
            return {};
        }

        if (It != m_Assets.end())
        {
            It->second = RefCntWeakPtr<InterfaceType>{pAsset.RawPtr()};
        }
        else
        {
            m_Assets.emplace(HashMapStringKey{CacheKey, true},
                             RefCntWeakPtr<InterfaceType>{pAsset.RawPtr()});
        }

        return {pAsset, true};
    }

private:
    using AssetMapType = std::unordered_map<HashMapStringKey, RefCntWeakPtr<InterfaceType>>;

    mutable std::shared_mutex m_Mutex;
    AssetMapType              m_Assets;
};

} // namespace Diligent
