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

#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "GLTF_PBR_Renderer.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "TestingEnvironment.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"
#include "gtest/gtest.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace Diligent
{

namespace
{

RadientMaterialTextureSRBSlot MakeTextureBinding(
    Int32                  ResourceId,
    TEXTURE_FORMAT         ViewFormat = TEX_FORMAT_RGBA8_UNORM,
    RadientTextureViewType ViewType   = RadientTextureViewType::Linear)
{
    RadientMaterialTextureSRBSlot Binding;
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
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    EXPECT_NE(pMaterialManager, nullptr);
    if (!pMaterialManager)
        return {};

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    if (EnableClearcoat)
        DefinitionCI.Features |= RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    const RADIENT_STATUS                 Status = Testing::CreateStandardMaterialAsset(
        *pMaterialManager, DefinitionCI, pMaterial.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    return pMaterial;
}

RefCntAutoPtr<IRadientMaterialAsset> MakeUnlitMaterialAsset()
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    EXPECT_NE(pMaterialManager, nullptr);
    if (!pMaterialManager)
        return {};

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_UNLIT;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    const RADIENT_STATUS                 Status = Testing::CreateStandardMaterialAsset(
        *pMaterialManager, DefinitionCI, pMaterial.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    return pMaterial;
}

RefCntAutoPtr<IRadientMaterialAsset> MakeSpecularGlossinessMaterialAsset()
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    EXPECT_NE(pMaterialManager, nullptr);
    if (!pMaterialManager)
        return {};

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.ShadingModel = RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    const RADIENT_STATUS                 Status = Testing::CreateStandardMaterialAsset(
        *pMaterialManager, DefinitionCI, pMaterial.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    return pMaterial;
}

std::unique_ptr<RadientTesseraMaterialCache> MakeMaterialCache(
    PBR_Renderer::PSO_FLAGS EnabledMaterialPSOFlags = PBR_Renderer::PSO_FLAG_NONE)
{
    RadientTesseraMaterialCache::CreateInfo CI;
    CI.MaterialTextureSlotCount      = 8;
    CI.EnabledMaterialPSOFlags       = EnabledMaterialPSOFlags;
    CI.DefaultTextures               = MakeDefaultTextureBindings();
    CI.ConstantBufferOffsetAlignment = 256;
    CI.MaxMaterialAttribsSize        = 4096;
    return std::make_unique<RadientTesseraMaterialCache>(CI);
}

RadientMaterialTextureSRVResolveResult ResolveTestTextureSRV(
    const RadientMaterialTextureSRBSlot& Binding)
{
    return {
        RADIENT_STATUS_OK,
        reinterpret_cast<ITextureView*>(static_cast<uintptr_t>(Binding.BindingIdentity.StandaloneResourceId)),
    };
}

RADIENT_STATUS PrepareMaterialCache(RadientTesseraMaterialCache& Cache)
{
    // The synthetic SRB makes every CPU material record immediately visible.
    return Cache.Prepare(
        1,
        0,
        ~Uint64{0},
        ResolveTestTextureSRV,
        [](ITextureView* const*, Uint32) {
            return Testing::MakeTestShaderResourceBinding();
        });
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

    const auto* const pDefinition =
        static_cast<const RadientMaterialDefinitionImpl*>(pMaterial->GetDefinition());
    ASSERT_NE(pDefinition, nullptr);
    EXPECT_EQ(Result.Data->GetMaterialBufferAllocation().GetSize(),
              pDefinition->GetShaderDataSize());

    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 2u);
}

TEST(RadientTesseraMaterialCacheTest, ProcessesMaterialOnWorkerThread)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker =
        EnqueueAsyncWork(
            pThreadPool,
            [&ReleaseWorker](Uint32) {
                ReleaseWorker.Wait();
                return ASYNC_TASK_STATUS_COMPLETE;
            });
    ASSERT_NE(pBlocker, nullptr);
    pBlocker->WaitUntilRunning();

    std::unique_ptr<RadientTesseraMaterialCache> pCache    = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial = MakeMaterialAsset();
    RadientTesseraMaterialResolveResult          Result    = pCache->Resolve(*pThreadPool, pMaterial);

    EXPECT_TRUE(Result.Data);
    EXPECT_EQ(Result.Status, RADIENT_STATUS_PENDING);
    if (Result.Data)
        EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_PENDING);

    ReleaseWorker.Trigger();
    pThreadPool->WaitForAllTasks();

    ASSERT_TRUE(Result.Data);
    EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientTesseraMaterialCacheTest, DerivesPSOFlagsFromMaterial)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache =
        MakeMaterialCache(ExpectedClearcoatMaterialPSOFlags);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeMaterialAsset(true);

    const RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);

    EXPECT_EQ(Result.Data->GetMaterialPSOFlags(), ExpectedClearcoatMaterialPSOFlags);
}

TEST(RadientTesseraMaterialCacheTest, UnlitMaterialDoesNotUseUnshadedPSOFlag)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache =
        MakeMaterialCache(PBR_Renderer::PSO_FLAG_UNSHADED);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeUnlitMaterialAsset();

    const RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);

    EXPECT_EQ(Result.Data->GetMaterialPSOFlags(), PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientTesseraMaterialCacheTest, ProcessesSpecularGlossinessMaterialWithoutEnabledTextures)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache      = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial   = MakeSpecularGlossinessMaterialAsset();

    const RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);

    EXPECT_EQ(Result.Data->GetMaterialPSOFlags(), ExpectedCoreMaterialPSOFlags);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(Result.Data->GetShaderTextureIds()[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 2u);
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
    EXPECT_NE(Result0.Data->GetUniqueID(), 0);
    EXPECT_NE(Result1.Data->GetUniqueID(), 0);
    EXPECT_NE(Result0.Data->GetUniqueID(), Result1.Data->GetUniqueID());
    EXPECT_NE(Result0.Data->GetMaterialBufferAllocation().GetOffset(),
              Result1.Data->GetMaterialBufferAllocation().GetOffset());
}

TEST(RadientTesseraMaterialCacheTest, DifferentMaterialsWithSameRecipeShareSRB)
{
    RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    std::unique_ptr<RadientTesseraMaterialCache> pCache      = MakeMaterialCache();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial0  = MakeMaterialAsset();
    RefCntAutoPtr<IRadientMaterialAsset>         pMaterial1  = MakeMaterialAsset();

    RadientTesseraMaterialResolveResult Result0 = pCache->Resolve(*pThreadPool, pMaterial0);
    RadientTesseraMaterialResolveResult Result1 = pCache->Resolve(*pThreadPool, pMaterial1);

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(Result0.Data->GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Result1.Data->GetStatus(), RADIENT_STATUS_OK);
    EXPECT_NE(Result0.Data.Get(), Result1.Data.Get());

    ASSERT_EQ(PrepareMaterialCache(*pCache), RADIENT_STATUS_OK);
    IShaderResourceBinding* const pSRB = Result0.Data->GetMaterialSRB().GetSRB();
    ASSERT_NE(pSRB, nullptr);
    EXPECT_EQ(Result1.Data->GetMaterialSRB().GetSRB(), pSRB);
    EXPECT_NE(Result0.Data->GetMaterialBufferAllocation().GetOffset(),
              Result1.Data->GetMaterialBufferAllocation().GetOffset());
}

TEST(RadientTesseraMaterialCacheTest, MissingRequiredWorkflowTextureIsTerminal)
{
    struct TestCase
    {
        RADIENT_SURFACE_SHADING_MODEL ShadingModel;
        PBR_Renderer::PSO_FLAGS       PSOFlags;
        const char*                   ExpectedError;
    };

    static constexpr TestCase TestCases[] = {
        {RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS,
         PBR_Renderer::PSO_FLAG_USE_COLOR_MAP,
         "BaseColorTexture' is not initialized"},
        {RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS,
         PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP,
         "MetallicRoughnessTexture' is not initialized"},
        {RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS,
         PBR_Renderer::PSO_FLAG_USE_COLOR_MAP,
         "DiffuseTexture' is not initialized"},
        {RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS,
         PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP,
         "SpecularGlossinessTexture' is not initialized"},
        {RADIENT_SURFACE_SHADING_MODEL_UNLIT,
         PBR_Renderer::PSO_FLAG_USE_COLOR_MAP,
         "BaseColorTexture' is not initialized"},
    };

    for (const TestCase& Case : TestCases)
    {
        SCOPED_TRACE(Case.ExpectedError);

        RefCntAutoPtr<IThreadPool>                   pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
        std::unique_ptr<RadientTesseraMaterialCache> pCache      = MakeMaterialCache(Case.PSOFlags);
        RefCntAutoPtr<IRadientMaterialAsset>         pMaterial;
        switch (Case.ShadingModel)
        {
            case RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS:
                pMaterial = MakeMaterialAsset();
                break;
            case RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS:
                pMaterial = MakeSpecularGlossinessMaterialAsset();
                break;
            case RADIENT_SURFACE_SHADING_MODEL_UNLIT:
                pMaterial = MakeUnlitMaterialAsset();
                break;
            default:
                FAIL() << "Unexpected shading model";
        }

        ASSERT_NE(pMaterial, nullptr);
        RadientTesseraMaterialResolveResult Result = pCache->Resolve(*pThreadPool, pMaterial);

        Testing::TestingEnvironment::ErrorScope ExpectedErrors{Case.ExpectedError};
        ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_INVALID_OPERATION);

        const RadientTesseraMaterialResolveResult Retry = pCache->Resolve(*pThreadPool, pMaterial);
        EXPECT_EQ(Retry.Data.Get(), Result.Data.Get());
        EXPECT_EQ(Retry.Status, RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    }
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

TEST(RadientTesseraMaterialCacheTest, MaterialDataRetainsOnlySRBRecipeTextures)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});

    RadientTesseraMaterialCache::CreateInfo CI;
    CI.MaterialTextureSlotCount      = 1;
    CI.EnabledMaterialPSOFlags       = PBR_Renderer::PSO_FLAG_NONE;
    CI.DefaultTextures               = MakeDefaultTextureBindings();
    CI.ConstantBufferOffsetAlignment = 256;
    CI.MaxMaterialAttribsSize        = 4096;

    RefCntWeakPtr<IRadientTextureAsset> pWeakWhiteLinear{CI.DefaultTextures.WhiteLinear.pTexture.RawPtr()};
    RefCntWeakPtr<IRadientTextureAsset> pWeakWhiteSRGB{CI.DefaultTextures.WhiteSRGB.pTexture.RawPtr()};
    RefCntWeakPtr<IRadientTextureAsset> pWeakBlackSRGB{CI.DefaultTextures.BlackSRGB.pTexture.RawPtr()};
    RefCntWeakPtr<IRadientTextureAsset> pWeakNormal{CI.DefaultTextures.Normal.pTexture.RawPtr()};
    RefCntWeakPtr<IRadientTextureAsset> pWeakPhysicalDesc{CI.DefaultTextures.PhysicalDesc.pTexture.RawPtr()};

    std::unique_ptr<RadientTesseraMaterialCache> pCache =
        std::make_unique<RadientTesseraMaterialCache>(CI);
    CI.DefaultTextures = {};

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeMaterialAsset();
    RadientTesseraMaterialResolveResult  Result    = pCache->Resolve(*pThreadPool, pMaterial);
    ASSERT_TRUE(Result.Data);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);

    // Slot zero uses the sRGB white default. The material data must not retain
    // the other renderer defaults after its cache has been released.
    pCache.reset();
    EXPECT_EQ(pWeakWhiteLinear.Lock(), nullptr);
    EXPECT_NE(pWeakWhiteSRGB.Lock(), nullptr);
    EXPECT_EQ(pWeakBlackSRGB.Lock(), nullptr);
    EXPECT_EQ(pWeakNormal.Lock(), nullptr);
    EXPECT_EQ(pWeakPhysicalDesc.Lock(), nullptr);

    Result.Data = {};
    EXPECT_EQ(pWeakWhiteSRGB.Lock(), nullptr);
}

} // namespace Diligent
