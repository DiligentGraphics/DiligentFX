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
#include "Render/RadientPBRRenderer.hpp"

#include "GPUTestingEnvironment.hpp"

#include "gtest/gtest.h"

#include <memory>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

RefCntAutoPtr<IShaderResourceBinding> CreateTestFrameSRB()
{
    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    if (pDevice == nullptr || pContext == nullptr)
        return {};

    PBR_Renderer::CreateInfo RendererCI{};
    RendererCI.EnableIBL             = false;
    RendererCI.CreateDefaultTextures = false;

    RadientPBRRenderer Renderer{pDevice, nullptr, pContext, RendererCI};

    RefCntAutoPtr<IShaderResourceBinding> pFrameSRB;
    Renderer.CreateResourceBinding(pFrameSRB.GetAddressOfEmpty(), 0);
    return pFrameSRB;
}

TEST(RadientFrameSRBCacheGPUTest, ResourceDestructionRemovesEntry)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    RefCntAutoPtr<IShaderResourceBinding> pFrameSRB = CreateTestFrameSRB();
    ASSERT_NE(pFrameSRB, nullptr);

    RadientFrameSRBCache Cache;

    auto pResources      = std::make_unique<RadientIBLResources>(nullptr, nullptr);
    auto pOtherResources = std::make_unique<RadientIBLResources>(nullptr, nullptr);

    Cache.Add(pResources.get(), pFrameSRB);
    Cache.Add(pResources.get(), pFrameSRB);
    EXPECT_EQ(Cache.GetSize(), 1u);
    EXPECT_EQ(Cache.Get(pResources.get()).RawPtr(), pFrameSRB.RawPtr());
    EXPECT_EQ(Cache.Get(pOtherResources.get()), nullptr);

    pResources.reset();
    EXPECT_EQ(Cache.GetSize(), 0u);
}

TEST(RadientFrameSRBCacheGPUTest, ResourcesMayOutliveCache)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    RefCntAutoPtr<IShaderResourceBinding> pFrameSRB = CreateTestFrameSRB();
    ASSERT_NE(pFrameSRB, nullptr);

    auto pResources = std::make_unique<RadientIBLResources>(nullptr, nullptr);
    {
        RadientFrameSRBCache Cache;
        Cache.Add(pResources.get(), pFrameSRB);
        EXPECT_EQ(Cache.GetSize(), 1u);
    }

    // The expired weak cache reference lets the same resources attach to a new renderer cache.
    RadientFrameSRBCache NewCache;
    NewCache.Add(pResources.get(), pFrameSRB);
    pResources.reset();
    EXPECT_EQ(NewCache.GetSize(), 0u);
}

} // namespace
