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

#include "DebugUtilities.hpp"
#include "HashUtils.hpp"
#include "RefCntAutoPtr.hpp"
#include "SpinLock.hpp"

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace Diligent
{

template <typename InterfaceType>
class RadientAssetCache
{
private:
#ifdef __cpp_lib_hardware_interference_size
    static constexpr size_t CacheLineSize = std::max(std::hardware_destructive_interference_size, size_t{64});
#else
    static constexpr size_t CacheLineSize = 64;
#endif

    class AssetEntry
    {
    public:
        enum class CreateAction
        {
            Create,
            Wait,
            Recursive
        };

        struct CreateState
        {
            CreateAction Action     = CreateAction::Wait;
            Uint64       Generation = 0;
        };

        RefCntAutoPtr<InterfaceType> Lock()
        {
            Threading::SpinLockGuard LockGuard{m_Lock};
            return m_Asset.Lock();
        }

        void Set(InterfaceType* pAsset)
        {
            Threading::SpinLockGuard LockGuard{m_Lock};
            m_Asset = RefCntWeakPtr<InterfaceType>{pAsset};
        }

        CreateState BeginCreate()
        {
            std::lock_guard<std::mutex> LockGuard{m_CreateMtx};

            if (m_IsCreating)
            {
                return CreateState{
                    m_CreatorThread == std::this_thread::get_id() ? CreateAction::Recursive : CreateAction::Wait,
                    m_Generation,
                };
            }

            m_IsCreating    = true;
            m_CreatorThread = std::this_thread::get_id();
            return CreateState{CreateAction::Create, m_Generation};
        }

        bool WaitCreate(Uint64 Generation)
        {
            std::unique_lock<std::mutex> LockGuard{m_CreateMtx};
            m_CreateCV.wait(LockGuard, [&]() {
                return !m_IsCreating || m_Generation != Generation;
            });

            return m_LastCreateSucceeded;
        }

        void EndCreate(bool Succeeded)
        {
            {
                std::lock_guard<std::mutex> LockGuard{m_CreateMtx};
                VERIFY_EXPR(m_IsCreating);
                VERIFY_EXPR(m_CreatorThread == std::this_thread::get_id());

                m_IsCreating          = false;
                m_LastCreateSucceeded = Succeeded;
                m_CreatorThread       = {};
                ++m_Generation;
            }

            m_CreateCV.notify_all();
        }

    private:
        // While multiple weak pointers referencing the same object may be safely used concurrently,
        // the weak pointer itself is not thread safe and must be protected by a lock.
        Threading::SpinLock          m_Lock;
        RefCntWeakPtr<InterfaceType> m_Asset;

        std::mutex              m_CreateMtx;
        std::condition_variable m_CreateCV;
        bool                    m_IsCreating          = false;
        bool                    m_LastCreateSucceeded = false;
        Uint64                  m_Generation          = 0;
        std::thread::id         m_CreatorThread;
    };

    using AssetMapType = std::unordered_map<HashMapStringKey, std::shared_ptr<AssetEntry>>;

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

    struct alignas(CacheLineSize) Shard
    {
        mutable std::shared_mutex Mutex;
        AssetMapType              Assets;
    };

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

public:
    explicit RadientAssetCache(size_t ShardCount = 0) :
        m_ShardCount{GetActualShardCount(ShardCount)},
        m_Shards{std::make_unique<Shard[]>(m_ShardCount)}
    {
    }

    template <typename ImplType, typename CreateAssetFuncType>
    std::pair<RefCntAutoPtr<ImplType>, bool> GetOrCreate(const std::string&    CacheKey,
                                                         const INTERFACE_ID&   ImplID,
                                                         CreateAssetFuncType&& CreateAssetFunc)
    {
        const HashMapStringKey      Key{CacheKey.c_str()};
        Shard&                      CacheShard = GetShard(Key.GetHash());
        std::shared_ptr<AssetEntry> pEntry;

        // First, try to find the asset in the cache with shared lock.
        {
            std::shared_lock<std::shared_mutex> Lock{CacheShard.Mutex};

            const auto It = CacheShard.Assets.find(Key);
            if (It != CacheShard.Assets.end())
                pEntry = It->second;
        }

        if (!pEntry)
        {
            std::unique_lock<std::shared_mutex> Lock{CacheShard.Mutex};

            const auto It = CacheShard.Assets.find(Key);
            if (It != CacheShard.Assets.end())
            {
                pEntry = It->second;
            }
            else
            {
                pEntry                 = std::make_shared<AssetEntry>();
                auto [NewIt, Inserted] = CacheShard.Assets.emplace(HashMapStringKey{CacheKey, true}, pEntry);
                VERIFY_EXPR(Inserted);
            }
        }

        for (;;)
        {
            if (RefCntAutoPtr<InterfaceType> pExisting = pEntry->Lock())
                return {RefCntAutoPtr<ImplType>{pExisting, ImplID}, false};

            const typename AssetEntry::CreateState State = pEntry->BeginCreate();
            if (State.Action == AssetEntry::CreateAction::Wait)
            {
                if (pEntry->WaitCreate(State.Generation))
                    continue;

                return {};
            }

            if (State.Action == AssetEntry::CreateAction::Recursive)
            {
                LOG_ERROR_MESSAGE("Recursive asset creation detected for cache key '", CacheKey, "'");
                return {};
            }

            RefCntAutoPtr<ImplType> pAsset;
            try
            {
                pAsset = CreateAssetFunc();
            }
            catch (...)
            {
                pEntry->EndCreate(false);
                throw;
            }

            if (!pAsset)
            {
                LOG_ERROR_MESSAGE("Failed to create asset for cache key '", CacheKey, "'");
                pEntry->EndCreate(false);
                return {};
            }

            pEntry->Set(pAsset.RawPtr());
            pEntry->EndCreate(true);

            return {pAsset, true};
        }
    }

private:
    static size_t GetActualShardCount(size_t ShardCount)
    {
        if (ShardCount != 0)
            return ShardCount;

        const unsigned int ThreadCount = std::thread::hardware_concurrency();
        return ThreadCount != 0 ? static_cast<size_t>(ThreadCount) : size_t{1};
    }

    Shard& GetShard(size_t Hash)
    {
        VERIFY_EXPR(m_ShardCount > 0);
        return m_Shards[Hash % m_ShardCount];
    }

    const size_t             m_ShardCount;
    std::unique_ptr<Shard[]> m_Shards;
};

} // namespace Diligent
