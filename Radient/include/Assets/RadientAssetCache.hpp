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
 *  for loss of goodwill, work stoppage, computer failure or malfunction, and any
 *  and all other commercial damages or losses), even if such Contributor has been
 *  advised of the possibility of such damages.
 */

#pragma once

#include "Assets/RadientAssetImpl.hpp"
#include "WeakObjectCache.hpp"

#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace Diligent
{

template <typename InterfaceType>
class RadientAssetCache final
{
private:
    class State final : public IRadientAssetCacheRemovalHandler, public std::enable_shared_from_this<State>
    {
    public:
        explicit State(size_t ShardCount) :
            Cache{ShardCount}
        {
        }

        template <typename CreateAssetFuncType>
        std::pair<RefCntAutoPtr<InterfaceType>, bool> GetOrCreate(const Char*           CacheKey,
                                                                  CreateAssetFuncType&& CreateAssetFunc)
        {
            return Cache.GetOrCreate(
                CacheKey,
                [pState = this->shared_from_this(), CacheKey, &CreateAssetFunc]() {
                    const std::string StableCacheKey{CacheKey};

                    auto pObject = std::forward<CreateAssetFuncType>(CreateAssetFunc)();
                    if (pObject)
                        pObject->SetCacheRemovalHandler(pState, StableCacheKey.c_str());
                    return pObject;
                });
        }

        virtual void RemoveAssetFromCache(const Char* CacheKey) noexcept override final
        {
            Cache.EraseIfExpired(CacheKey);
        }

        WeakObjectCache<InterfaceType> Cache;
    };

public:
    class Accessor final
    {
    public:
        Accessor() = default;

        template <typename CreateAssetFuncType>
        std::pair<RefCntAutoPtr<InterfaceType>, bool> GetOrCreate(const Char*           CacheKey,
                                                                  CreateAssetFuncType&& CreateAssetFunc) const
        {
            return m_pState->GetOrCreate(CacheKey, std::forward<CreateAssetFuncType>(CreateAssetFunc));
        }

    private:
        friend class RadientAssetCache;

        explicit Accessor(std::shared_ptr<State> pState) :
            m_pState{std::move(pState)}
        {
        }

        std::shared_ptr<State> m_pState;
    };

    explicit RadientAssetCache(size_t ShardCount = 0) :
        m_pState{std::make_shared<State>(GetActualShardCount(ShardCount))}
    {
    }

    // clang-format off
    RadientAssetCache           (const RadientAssetCache&) = delete;
    RadientAssetCache& operator=(const RadientAssetCache&) = delete;
    RadientAssetCache           (RadientAssetCache&&)      = delete;
    RadientAssetCache& operator=(RadientAssetCache&&)      = delete;
    // clang-format on

    size_t Size() const noexcept
    {
        return m_pState->Cache.Size();
    }

    void Reserve(size_t ExpectedTotalEntries)
    {
        m_pState->Cache.Reserve(ExpectedTotalEntries);
    }

    bool EraseIfExpired(const Char* CacheKey)
    {
        return m_pState->Cache.EraseIfExpired(CacheKey);
    }

    Accessor GetAccessor() const
    {
        return Accessor{m_pState};
    }

    template <typename CreateAssetFuncType>
    std::pair<RefCntAutoPtr<InterfaceType>, bool> GetOrCreate(const Char*           CacheKey,
                                                              CreateAssetFuncType&& CreateAssetFunc)
    {
        return m_pState->GetOrCreate(CacheKey, std::forward<CreateAssetFuncType>(CreateAssetFunc));
    }

private:
    static size_t GetActualShardCount(size_t ShardCount)
    {
        if (ShardCount != 0)
            return ShardCount;

        static constexpr size_t MaxDefaultShardCount = 8;

        const unsigned int ThreadCount = std::thread::hardware_concurrency();
        if (ThreadCount == 0)
            return size_t{4};

        return ThreadCount < MaxDefaultShardCount ? static_cast<size_t>(ThreadCount) : MaxDefaultShardCount;
    }

    std::shared_ptr<State> m_pState;
};

} // namespace Diligent
