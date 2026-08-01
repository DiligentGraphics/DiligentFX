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

#include "Render/Tessera/RadientTesseraMaterialCache.hpp"

#include "GLTF_PBR_Renderer.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "TestingEnvironment.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"
#include "gtest/gtest.h"

#include <atomic>
#include <thread>
#include <vector>

namespace Diligent
{

namespace
{

RadientMaterialTextureRenderData MakeTextureBinding(
    Int32                  ResourceId,
    TEXTURE_FORMAT         ViewFormat = TEX_FORMAT_RGBA8_UNORM,
    RadientTextureViewType ViewType   = RadientTextureViewType::Linear)
{
    RadientMaterialTextureRenderData Binding;
    Binding.pTexture        = Testing::MakeTestTextureAsset();
    Binding.ViewType        = ViewType;
    Binding.BindingIdentity = {ResourceId, ViewFormat};
    return Binding;
}

RadientMaterialDefaultTextureBindings MakeDefaultTextureBindings()
{
    RadientMaterialDefaultTextureBindings Bindings;
    Bindings.WhiteLinear  = MakeTextureBinding(100);
    Bindings.WhiteSRGB    = MakeTextureBinding(101, TEX_FORMAT_RGBA8_UNORM_SRGB, RadientTextureViewType::SRGB);
    Bindings.BlackSRGB    = MakeTextureBinding(102, TEX_FORMAT_RGBA8_UNORM_SRGB, RadientTextureViewType::SRGB);
    Bindings.Normal       = MakeTextureBinding(103);
    Bindings.PhysicalDesc = MakeTextureBinding(104);
    return Bindings;
}

RefCntAutoPtr<IRadientMaterialAsset> MakeMaterialAsset(bool EnableClearcoat = false)
{
    GLTF::Material Material;
    Material.HasClearcoat = EnableClearcoat;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    RadientMaterialAssetManager::Create()->CreateGLTFMaterial(
        std::move(Material), nullptr, 0, pMaterial.GetAddressOfEmpty());
    return pMaterial;
}

std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> MakeTextureAttribIndices()
{
    std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> Indices;
    for (size_t TextureAttribId = 0; TextureAttribId < Indices.size(); ++TextureAttribId)
        Indices[TextureAttribId] = static_cast<int>(TextureAttribId);
    return Indices;
}

std::unique_ptr<RadientTesseraMaterialCache> MakeMaterialCache(
    const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices    = MakeTextureAttribIndices(),
    PBR_Renderer::PSO_FLAGS                                       EnabledMaterialPSOFlags = PBR_Renderer::PSO_FLAG_NONE)
{
    RadientTesseraMaterialCache::CreateInfo CI;
    CI.TextureAttribIndices     = TextureAttribIndices;
    CI.MaterialTextureSlotCount = 8;
    CI.EnabledMaterialPSOFlags  = EnabledMaterialPSOFlags;
    CI.DefaultTextures          = MakeDefaultTextureBindings();
    return std::make_unique<RadientTesseraMaterialCache>(CI);
}

constexpr PBR_Renderer::PSO_FLAGS ExpectedCoreMaterialPSOFlags =
    PBR_Renderer::PSO_FLAG_NONE;

constexpr PBR_Renderer::PSO_FLAGS ExpectedClearcoatMaterialPSOFlags =
    PBR_Renderer::PSO_FLAG_ENABLE_CLEAR_COAT;

} // namespace

TEST(PBRRendererPSOFlagsTest, EnabledFlagsFollowRendererSettings)
{
    PBR_Renderer::CreateInfo Settings;
    Settings.EnableIBL          = false;
    Settings.EnableAO           = false;
    Settings.EnableEmissive     = false;
    Settings.EnableClearCoat    = false;
    Settings.EnableSheen        = false;
    Settings.EnableAnisotropy   = false;
    Settings.EnableIridescence  = false;
    Settings.EnableTransmission = false;
    Settings.EnableVolume       = false;
    Settings.EnableShadows      = false;
    Settings.MaxJointCount      = 0;

    const PBR_Renderer::PSO_FLAGS DisabledFlags =
        PBR_Renderer::PSO_FLAG_USE_IBL |
        PBR_Renderer::PSO_FLAG_USE_AO_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP |
        PBR_Renderer::PSO_FLAG_ALL_CLEAR_COAT |
        PBR_Renderer::PSO_FLAG_ALL_SHEEN |
        PBR_Renderer::PSO_FLAG_ALL_ANISOTROPY |
        PBR_Renderer::PSO_FLAG_ALL_IRIDESCENCE |
        PBR_Renderer::PSO_FLAG_ALL_TRANSMISSION |
        PBR_Renderer::PSO_FLAG_ALL_VOLUME |
        PBR_Renderer::PSO_FLAG_ENABLE_SHADOWS |
        PBR_Renderer::PSO_FLAG_USE_JOINTS;

    const PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::GetEnabledPSOFlags(Settings);
    EXPECT_EQ(Flags & DisabledFlags, PBR_Renderer::PSO_FLAG_NONE);
    EXPECT_NE(Flags & PBR_Renderer::PSO_FLAG_USE_COLOR_MAP, PBR_Renderer::PSO_FLAG_NONE);
    EXPECT_NE(Flags & PBR_Renderer::PSO_FLAG_FIRST_USER_DEFINED, PBR_Renderer::PSO_FLAG_NONE);
}

TEST(GLTFPBRRendererPSOFlagsTest, DefaultMaterial)
{
    GLTF::Material Material;
    EXPECT_EQ(GLTF_PBR_Renderer::GetMaterialPSOFlags(Material),
              PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES);
}

TEST(GLTFPBRRendererPSOFlagsTest, ClearCoat)
{
    GLTF::Material Material;
    Material.HasClearcoat = true;
    EXPECT_EQ(GLTF_PBR_Renderer::GetMaterialPSOFlags(Material),
              PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
                  PBR_Renderer::PSO_FLAG_ALL_CLEAR_COAT);
}

TEST(GLTFPBRRendererPSOFlagsTest, Sheen)
{
    GLTF::Material Material;
    Material.Sheen = std::make_unique<GLTF::Material::SheenShaderAttribs>();
    EXPECT_EQ(GLTF_PBR_Renderer::GetMaterialPSOFlags(Material),
              PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
                  PBR_Renderer::PSO_FLAG_ALL_SHEEN);
}

TEST(GLTFPBRRendererPSOFlagsTest, Anisotropy)
{
    GLTF::Material Material;
    Material.Anisotropy = std::make_unique<GLTF::Material::AnisotropyShaderAttribs>();
    EXPECT_EQ(GLTF_PBR_Renderer::GetMaterialPSOFlags(Material),
              PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
                  PBR_Renderer::PSO_FLAG_ALL_ANISOTROPY);
}

TEST(GLTFPBRRendererPSOFlagsTest, Iridescence)
{
    GLTF::Material Material;
    Material.Iridescence = std::make_unique<GLTF::Material::IridescenceShaderAttribs>();
    EXPECT_EQ(GLTF_PBR_Renderer::GetMaterialPSOFlags(Material),
              PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
                  PBR_Renderer::PSO_FLAG_ALL_IRIDESCENCE);
}

TEST(GLTFPBRRendererPSOFlagsTest, Transmission)
{
    GLTF::Material Material;
    Material.Transmission = std::make_unique<GLTF::Material::TransmissionShaderAttribs>();
    EXPECT_EQ(GLTF_PBR_Renderer::GetMaterialPSOFlags(Material),
              PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
                  PBR_Renderer::PSO_FLAG_ALL_TRANSMISSION);
}

TEST(GLTFPBRRendererPSOFlagsTest, Volume)
{
    GLTF::Material Material;
    Material.Volume = std::make_unique<GLTF::Material::VolumeShaderAttribs>();
    EXPECT_EQ(GLTF_PBR_Renderer::GetMaterialPSOFlags(Material),
              PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES |
                  PBR_Renderer::PSO_FLAG_ALL_VOLUME);
}

TEST(RadientTesseraMaterialCacheTest, ProcessesMaterialThroughQueuedTask)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache      = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial   = MakeMaterialAsset();

    RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);

    ASSERT_NE(Result.Data.Get(), nullptr);
    EXPECT_EQ(Result.Status, RADIENT_STATUS_PENDING);
    EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_PENDING);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 1u);

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);
    EXPECT_TRUE(Result.Data->GetMaterialSRB());
    EXPECT_EQ(Result.Data->GetMaterialPSOFlags(), ExpectedCoreMaterialPSOFlags);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 2u);
}

TEST(RadientTesseraMaterialCacheTest, DerivesPSOFlagsFromMaterial)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache =
        MakeMaterialCache(MakeTextureAttribIndices(),
                          ExpectedClearcoatMaterialPSOFlags);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeMaterialAsset(true);

    const RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);

    EXPECT_EQ(Result.Data->GetMaterialPSOFlags(), ExpectedClearcoatMaterialPSOFlags);
}

TEST(RadientTesseraMaterialCacheTest, RespectsDisabledRendererFeatures)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache      = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial   = MakeMaterialAsset(true);

    const RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(Result.Data->GetMaterialPSOFlags(), ExpectedCoreMaterialPSOFlags);
}

TEST(RadientTesseraMaterialCacheTest, ConcurrentResolveSchedulesOneTask)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache      = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial   = MakeMaterialAsset();
    RadientTesseraMaterialResolveResult          Initial     = pCache->Resolve(*pThreadPool, pMaterial);

    constexpr size_t                                 RequestCount = 16;
    std::vector<RadientTesseraMaterialResolveResult> Results(RequestCount);
    std::vector<std::thread>                         Threads;
    std::atomic<size_t>                              ReadyCount{0};
    Threading::Signal                                StartSignal;
    Threads.reserve(RequestCount);

    for (size_t Request = 0; Request < RequestCount; ++Request)
    {
        Threads.emplace_back(
            [&, Request] {
                ReadyCount.fetch_add(1, std::memory_order_release);
                StartSignal.Wait(true, static_cast<int>(RequestCount));

                Results[Request] = pCache->Resolve(*pThreadPool, pMaterial);
            });
    }

    while (ReadyCount.load(std::memory_order_acquire) != RequestCount)
        std::this_thread::yield();
    StartSignal.Trigger(true);

    for (std::thread& Thread : Threads)
        Thread.join();

    ASSERT_NE(Results[0].Data.Get(), nullptr);
    for (const RadientTesseraMaterialResolveResult& Result : Results)
    {
        EXPECT_EQ(Result.Data.Get(), Results[0].Data.Get());
        EXPECT_EQ(Result.Status, RADIENT_STATUS_PENDING);
    }
    EXPECT_EQ(pThreadPool->GetQueueSize(), 1u);

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(Initial.Data.Get(), Results[0].Data.Get());
    EXPECT_EQ(Results[0].Data->GetStatus(), RADIENT_STATUS_OK);
}

TEST(RadientTesseraMaterialCacheTest, DifferentMaterialsUseDistinctCachedData)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache      = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial0  = MakeMaterialAsset();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial1  = MakeMaterialAsset();

    RadientTesseraMaterialResolveResult Result0 = pCache->Resolve(*pThreadPool, pMaterial0);
    RadientTesseraMaterialResolveResult Result1 = pCache->Resolve(*pThreadPool, pMaterial1);

    EXPECT_EQ(pThreadPool->GetQueueSize(), 2u);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    ASSERT_EQ(Result0.Data->GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Result1.Data->GetStatus(), RADIENT_STATUS_OK);
    EXPECT_NE(Result0.Data.Get(), Result1.Data.Get());
}

TEST(RadientTesseraMaterialCacheTest, ProcessingFailureIsTerminal)
{
    RefCntAutoPtr<IThreadPool> pThreadPool                           = CreateThreadPool(ThreadPoolCreateInfo{0});
    auto                       TextureAttribIndices                  = MakeTextureAttribIndices();
    TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = -1;
    std::unique_ptr<RadientTesseraMaterialCache> pCache =
        MakeMaterialCache(TextureAttribIndices,
                          PBR_Renderer::PSO_FLAG_USE_COLOR_MAP);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeMaterialAsset();

    RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);

    Testing::TestingEnvironment::ErrorScope ExpectedErrors{"does not have a material texture mapping"};
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_INVALID_OPERATION);

    const RadientTesseraMaterialResolveResult Retry = pCache->Resolve(*pThreadPool, pMaterial);
    EXPECT_EQ(Retry.Data.Get(), Result.Data.Get());
    EXPECT_EQ(Retry.Status, RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
}

TEST(RadientTesseraMaterialCacheTest, RejectedEnqueueIsTerminal)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    pThreadPool->StopThreads();

    std::unique_ptr<RadientTesseraMaterialCache> pCache    = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial = MakeMaterialAsset();

    Testing::TestingEnvironment::ErrorScope   ExpectedErrors{"Enqueue on a stopped ThreadPool"};
    const RadientTesseraMaterialResolveResult Result =
        pCache->Resolve(*pThreadPool, pMaterial);

    ASSERT_NE(Result.Data.Get(), nullptr);
    EXPECT_EQ(Result.Status, RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_INVALID_OPERATION);
}

TEST(RadientTesseraMaterialCacheTest, MaterialDataMayOutliveCacheAndMaterialOwner)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});

    RadientTesseraMaterialResolveResult  Result;
    RefCntWeakPtr<IRadientMaterialAsset> pWeakMaterial;
    {
        std::unique_ptr<RadientTesseraMaterialCache> pCache    = MakeMaterialCache();
        RefCntAutoPtr<IRadientMaterialAsset>         pMaterial = MakeMaterialAsset();
        pWeakMaterial                                          = static_cast<IRadientMaterialAsset*>(pMaterial.RawPtr());

        Result = pCache->Resolve(*pThreadPool, pMaterial);
        pMaterial.Release();
        EXPECT_NE(pWeakMaterial.Lock(), nullptr);
    }

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);
    EXPECT_NE(pWeakMaterial.Lock(), nullptr);

    Result.Data = {};
    EXPECT_EQ(pWeakMaterial.Lock(), nullptr);
}

} // namespace Diligent
