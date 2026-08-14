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

#include "RefCntAutoPtr.hpp"
#include "ShaderResourceBinding.h"
#include "TextureView.h"

#include <memory>

namespace Diligent
{

class RadientFrameSRBCacheState;

/// View-owned IBL resources used as the identity of a cached frame SRB.
/// The object address is stable for its lifetime and its destructor automatically
/// removes the associated SRB from the renderer cache.
class RadientIBLResources final
{
public:
    RadientIBLResources(ITextureView* pIrradianceCubeSRV,
                        ITextureView* pPrefilteredEnvMapSRV,
                        ITextureView* pPrefilteredSheenEnvMapSRV);
    ~RadientIBLResources();

    // clang-format off
    RadientIBLResources           (const RadientIBLResources&) = delete;
    RadientIBLResources& operator=(const RadientIBLResources&) = delete;
    RadientIBLResources           (RadientIBLResources&&)      = delete;
    RadientIBLResources& operator=(RadientIBLResources&&)      = delete;
    // clang-format on

    ITextureView* GetIrradianceCubeSRV() const noexcept
    {
        return m_pIrradianceCubeSRV;
    }

    ITextureView* GetPrefilteredEnvMapSRV() const noexcept
    {
        return m_pPrefilteredEnvMapSRV;
    }

    ITextureView* GetPrefilteredSheenEnvMapSRV() const noexcept
    {
        return m_pPrefilteredSheenEnvMapSRV;
    }

private:
    friend class RadientFrameSRBCache;

    void SetCache(const std::shared_ptr<RadientFrameSRBCacheState>& pCache) noexcept;

    RefCntAutoPtr<ITextureView> m_pIrradianceCubeSRV;
    RefCntAutoPtr<ITextureView> m_pPrefilteredEnvMapSRV;
    RefCntAutoPtr<ITextureView> m_pPrefilteredSheenEnvMapSRV;

    std::weak_ptr<RadientFrameSRBCacheState> m_WeakCache;
};

/// Renderer-owned cache of immutable frame SRBs associated with view IBL resources.
/// The cache does not retain the resource bundle; destroying the bundle evicts its SRB.
class RadientFrameSRBCache final
{
public:
    RadientFrameSRBCache();
    ~RadientFrameSRBCache();

    // clang-format off
    RadientFrameSRBCache           (const RadientFrameSRBCache&) = delete;
    RadientFrameSRBCache& operator=(const RadientFrameSRBCache&) = delete;
    RadientFrameSRBCache           (RadientFrameSRBCache&&)      = delete;
    RadientFrameSRBCache& operator=(RadientFrameSRBCache&&)      = delete;
    // clang-format on

    /// Adds the SRB unless the resources are already cached.
    void Add(RadientIBLResources*    pResources,
             IShaderResourceBinding* pSRB);

    /// Returns a strong reference that remains valid if the cache entry is
    /// concurrently removed.
    RefCntAutoPtr<IShaderResourceBinding> Get(const RadientIBLResources* pResources) const noexcept;

    size_t GetSize() const noexcept;

private:
    std::shared_ptr<RadientFrameSRBCacheState> m_pState;
};

} // namespace Diligent
