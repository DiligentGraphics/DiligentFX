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

#include "Render/RadientFrameSRBCache.hpp"

#include "DebugUtilities.hpp"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace Diligent
{

class RadientFrameSRBCacheState final
{
public:
    void Add(const RadientIBLResources* pResources,
             IShaderResourceBinding*    pSRB)
    {
        std::lock_guard<std::mutex> Lock{m_Mutex};
        m_SRBs.emplace(pResources, pSRB);
    }

    RefCntAutoPtr<IShaderResourceBinding> Get(const RadientIBLResources* pResources) const noexcept
    {
        std::lock_guard<std::mutex> Lock{m_Mutex};
        const auto                  It = m_SRBs.find(pResources);
        return It != m_SRBs.end() ? It->second : RefCntAutoPtr<IShaderResourceBinding>{};
    }

    void Remove(const RadientIBLResources* pResources) noexcept
    {
        // Release the SRB after unlocking because final object release may invoke external code.
        RefCntAutoPtr<IShaderResourceBinding> pRemovedSRB;
        {
            std::lock_guard<std::mutex> Lock{m_Mutex};
            const auto                  It = m_SRBs.find(pResources);
            if (It != m_SRBs.end())
            {
                pRemovedSRB = std::move(It->second);
                m_SRBs.erase(It);
            }
        }
    }

    size_t GetSize() const noexcept
    {
        std::lock_guard<std::mutex> Lock{m_Mutex};
        return m_SRBs.size();
    }

private:
    mutable std::mutex m_Mutex;

    std::unordered_map<const RadientIBLResources*, RefCntAutoPtr<IShaderResourceBinding>> m_SRBs;
};

RadientIBLResources::RadientIBLResources(ITextureView* pIrradianceCubeSRV,
                                         ITextureView* pPrefilteredEnvMapSRV,
                                         ITextureView* pPrefilteredSheenEnvMapSRV) :
    m_pIrradianceCubeSRV{pIrradianceCubeSRV},
    m_pPrefilteredEnvMapSRV{pPrefilteredEnvMapSRV},
    m_pPrefilteredSheenEnvMapSRV{pPrefilteredSheenEnvMapSRV}
{
}

RadientIBLResources::~RadientIBLResources()
{
    if (std::shared_ptr<RadientFrameSRBCacheState> pCache = m_WeakCache.lock())
        pCache->Remove(this);
}

void RadientIBLResources::SetCache(const std::shared_ptr<RadientFrameSRBCacheState>& pCache) noexcept
{
    VERIFY_EXPR(pCache != nullptr);

    std::shared_ptr<RadientFrameSRBCacheState> pCurrentCache = m_WeakCache.lock();
    VERIFY_EXPR(pCurrentCache == nullptr || pCurrentCache == pCache);

    m_WeakCache = pCache;
}

RadientFrameSRBCache::RadientFrameSRBCache() :
    m_pState{std::make_shared<RadientFrameSRBCacheState>()}
{
}

RadientFrameSRBCache::~RadientFrameSRBCache()
{
}

void RadientFrameSRBCache::Add(RadientIBLResources*    pResources,
                               IShaderResourceBinding* pSRB)
{
    VERIFY_EXPR(pResources != nullptr);
    VERIFY_EXPR(pSRB != nullptr);

    pResources->SetCache(m_pState);
    m_pState->Add(pResources, pSRB);
}

RefCntAutoPtr<IShaderResourceBinding> RadientFrameSRBCache::Get(const RadientIBLResources* pResources) const noexcept
{
    return pResources != nullptr ? m_pState->Get(pResources) : RefCntAutoPtr<IShaderResourceBinding>{};
}

size_t RadientFrameSRBCache::GetSize() const noexcept
{
    return m_pState->GetSize();
}

} // namespace Diligent
