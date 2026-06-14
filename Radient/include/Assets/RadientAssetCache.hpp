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

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace Diligent
{

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
