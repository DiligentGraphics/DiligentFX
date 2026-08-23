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

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "Import/RadientImportedScene.hpp"
#include "Render/Tessera/RadientTesseraGeometryRenderer.hpp"
#include "RadientStandardMaterialParameters.h"

#include "GPUTestingEnvironment.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "TempDirectory.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace std::chrono_literals;

namespace
{

static constexpr auto TextureManagerWaitTimeout = std::chrono::seconds{10};

std::vector<Uint8> MakeTexturePixels(Uint32 Width,
                                     Uint32 Height,
                                     Uint32 Stride,
                                     Uint32 Seed)
{
    static constexpr Uint32 PixelSize = 4;

    std::vector<Uint8> Pixels(static_cast<size_t>(Stride) * Height);

    for (Uint32 y = 0; y < Height; ++y)
    {
        for (Uint32 x = 0; x < Width; ++x)
        {
            const Uint32 Offset = y * Stride + x * PixelSize;
            Pixels[Offset + 0]  = static_cast<Uint8>((x * 3 + y * 5 + Seed * 29) & 0xFF);
            Pixels[Offset + 1]  = static_cast<Uint8>((x * 11 + y * 7 + Seed * 31) & 0xFF);
            Pixels[Offset + 2]  = static_cast<Uint8>((x * y + x * 13 + y * 17 + Seed * 37) & 0xFF);
            Pixels[Offset + 3]  = static_cast<Uint8>(127 + ((x + y + Seed * 3) & 0x7F));
        }
    }

    return Pixels;
}

RadientTextureData MakeTextureData(Uint32      Width,
                                   Uint32      Height,
                                   Uint32      Stride,
                                   const void* pData)
{
    RadientTextureData TextureData{};
    TextureData.Width  = Width;
    TextureData.Height = Height;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pData  = pData;
    TextureData.Stride = Stride;
    return TextureData;
}

RadientTextureLoadInfo MakeTextureLoadInfo(const RadientTextureData& TextureData)
{
    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pTextureData = &TextureData;
    LoadInfo.IsSRGB       = False;
    return LoadInfo;
}

bool IsPendingOrOK(RADIENT_STATUS Status)
{
    return Status == RADIENT_STATUS_PENDING ||
        Status == RADIENT_STATUS_OK;
}

bool WaitForPendingCopyCommandEnqueueCallbacks(RadientAssetManagerImpl& AssetManager)
{
    const auto Deadline = std::chrono::steady_clock::now() + TextureManagerWaitTimeout;
    do
    {
        const RadientTextureAssetManagerStats Stats = AssetManager.GetTextureManagerStats();
        if (Stats.PendingCopyCommandEnqueueCallbacks != 0)
            return true;

        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < Deadline);

    return AssetManager.GetTextureManagerStats().PendingCopyCommandEnqueueCallbacks != 0;
}

bool WaitForTextureManagerIdle(RadientAssetManagerImpl& AssetManager,
                               IRenderDevice*           pDevice,
                               IDeviceContext*          pContext)
{
    const auto Deadline = std::chrono::steady_clock::now() + TextureManagerWaitTimeout;
    do
    {
        const RadientTextureAssetManagerStats Stats = AssetManager.GetTextureManagerStats();
        if (Stats.PendingTextureLoads == 0 &&
            Stats.PendingTextureSourceLoads == 0 &&
            Stats.PendingCopyCommandEnqueueCallbacks == 0)
        {
            return true;
        }

        if (Stats.PendingCopyCommandEnqueueCallbacks != 0)
        {
            AssetManager.UpdateGPUResources(pDevice, pContext);
            pContext->Flush();
            pContext->FinishFrame();
        }

        // Avoid busy-waiting while asynchronous texture work is in progress.
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < Deadline);

    const RadientTextureAssetManagerStats Stats = AssetManager.GetTextureManagerStats();
    return Stats.PendingTextureLoads == 0 &&
        Stats.PendingTextureSourceLoads == 0 &&
        Stats.PendingCopyCommandEnqueueCallbacks == 0;
}

RefCntAutoPtr<IAsyncTask> BlockWorkerThread(IThreadPool&       ThreadPool,
                                            Threading::Signal& ReleaseWorker)
{
    RefCntAutoPtr<IAsyncTask> pTask =
        EnqueueAsyncWork(
            &ThreadPool,
            [&ReleaseWorker](Uint32) //
            {
                ReleaseWorker.Wait();
                return ASYNC_TASK_STATUS_COMPLETE;
            });
    pTask->WaitUntilRunning();
    return pTask;
}

std::string WriteGLTFWithMissingMaterialTextures(const TempDirectory& TempDir)
{
    const std::string Path = TempDir.Get() + "/missing_material_textures.gltf";

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << R"GLTF({
        "asset": {"version": "2.0"},
        "extensionsUsed": [
            "KHR_materials_clearcoat",
            "KHR_materials_sheen",
            "KHR_materials_anisotropy",
            "KHR_materials_iridescence",
            "KHR_materials_transmission",
            "KHR_materials_volume"
        ],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Root"}],
        "images": [{"uri": "missing.png"}],
        "textures": [{"source": 0}],
        "materials": [
            {
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 0},
                    "metallicRoughnessTexture": {"index": 0}
                },
                "normalTexture": {"index": 0},
                "occlusionTexture": {"index": 0},
                "emissiveTexture": {"index": 0},
                "extensions": {
                    "KHR_materials_clearcoat": {
                        "clearcoatFactor": 1.0,
                        "clearcoatTexture": {"index": 0},
                        "clearcoatRoughnessTexture": {"index": 0},
                        "clearcoatNormalTexture": {"index": 0}
                    },
                    "KHR_materials_sheen": {
                        "sheenColorTexture": {"index": 0},
                        "sheenRoughnessTexture": {"index": 0}
                    },
                    "KHR_materials_anisotropy": {
                        "anisotropyTexture": {"index": 0}
                    },
                    "KHR_materials_iridescence": {
                        "iridescenceTexture": {"index": 0},
                        "iridescenceThicknessTexture": {"index": 0}
                    },
                    "KHR_materials_transmission": {
                        "transmissionTexture": {"index": 0}
                    },
                    "KHR_materials_volume": {
                        "thicknessTexture": {"index": 0}
                    }
                }
            }
        ]
    })GLTF";

    return Path;
}

std::string WriteGLTFWithMissingDDSTexture(const TempDirectory& TempDir)
{
    const std::string Path = TempDir.Get() + "/missing_dds_texture.gltf";

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << R"GLTF({
        "asset": {"version": "2.0"},
        "extensionsUsed": ["MSFT_texture_dds"],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Root"}],
        "images": [{"uri": "missing.dds"}],
        "textures": [{
            "extensions": {"MSFT_texture_dds": {"source": 0}}
        }],
        "materials": [{
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0}
            }
        }]
    })GLTF";

    return Path;
}

void ExpectTextureURI(IRadientTextureAsset* pTexture, const char* ExpectedURI)
{
    ASSERT_NE(pTexture, nullptr);
    ASSERT_NE(pTexture->GetReference().URI, nullptr);
    EXPECT_STREQ(pTexture->GetReference().URI, ExpectedURI);
}

RadientMaterialParameterHandle FindMaterialParameter(const RadientMaterialAssetView& MaterialData,
                                                     const char*                     Name)
{
    RadientMaterialParameterHandle Handle;
    if (MaterialData.pInstance == nullptr)
    {
        ADD_FAILURE() << "Material render data has no instance";
        return Handle;
    }

    IRadientMaterialDefinitionAsset* const pDefinition = MaterialData.pInstance->GetDefinition();
    if (pDefinition == nullptr)
    {
        ADD_FAILURE() << "Material instance has no definition";
        return Handle;
    }

    EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK) << Name;
    return Handle;
}

IRadientTextureAsset* GetMaterialTexture(const RadientMaterialAssetView& MaterialData,
                                         const char*                     ParameterName)
{
    const RadientMaterialParameterHandle Handle = FindMaterialParameter(MaterialData, ParameterName);
    return Handle ? MaterialData.GetTextureAsset(Handle.Index) : nullptr;
}

void ExpectMetallicRoughnessTextureDefaults(const RadientMaterialAssetView&       MaterialData,
                                            const RadientMaterialDefaultTextures& DefaultTextures)
{
    ASSERT_TRUE(MaterialData);
    ASSERT_EQ(MaterialData.TextureCount, 15u);

    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialBaseColorTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialMetallicRoughnessTextureName), DefaultTextures.pPhysicalDesc);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialNormalTextureName), DefaultTextures.pNormal);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialOcclusionTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialEmissiveTextureName), DefaultTextures.pBlack);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialClearCoatTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialClearCoatRoughnessTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialClearCoatNormalTextureName), DefaultTextures.pNormal);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialSheenColorTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialSheenRoughnessTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialAnisotropyTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialIridescenceTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialIridescenceThicknessTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialTransmissionTextureName), DefaultTextures.pWhite);
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialThicknessTextureName), DefaultTextures.pWhite);
}

void ExpectMaterialInstanceTextures(const IRadientMaterialInstance& Instance,
                                    const RadientMaterialAssetView& MaterialData)
{
    IRadientMaterialDefinitionAsset* const pDefinition = Instance.GetDefinition();
    ASSERT_NE(pDefinition, nullptr);

    ASSERT_TRUE(MaterialData);
    ASSERT_EQ(MaterialData.pInstance, &Instance);
    ASSERT_NE(MaterialData.pTextures, nullptr);

    for (Uint32 TextureIndex = 0; TextureIndex < MaterialData.TextureCount; ++TextureIndex)
    {
        const RadientMaterialTextureEntry& TextureData = MaterialData.pTextures[TextureIndex];
        RadientMaterialParameterHandle     Handle;
        ASSERT_EQ(pDefinition->GetParameterHandle(TextureData.ParameterIndex, &Handle), RADIENT_STATUS_OK);

        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(Instance.GetTexture(Handle, TextureData.ArrayIndex, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture, TextureData.pTexture);
    }
}

TEST(RadientAssetManagerGPUTest, InitializesDefaultMaterialTextures)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
    AssetManagerCI.pThreadPool = pThreadPool;
    AssetManagerCI.pDevice     = pDevice;

    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    // Default textures use the normal asynchronous texture path and become
    // material dependencies before the first material is created.
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    const RadientMaterialAssetView MaterialData = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ASSERT_TRUE(MaterialData);
    ASSERT_EQ(MaterialData.TextureCount, 5u);

    for (Uint32 TextureIndex = 0; TextureIndex < MaterialData.TextureCount; ++TextureIndex)
        EXPECT_NE(MaterialData.pTextures[TextureIndex].pTexture, nullptr) << TextureIndex;

    IRadientTextureAsset* pWhite        = GetMaterialTexture(MaterialData, RadientStandardMaterialBaseColorTextureName);
    IRadientTextureAsset* pBlack        = GetMaterialTexture(MaterialData, RadientStandardMaterialEmissiveTextureName);
    IRadientTextureAsset* pNormal       = GetMaterialTexture(MaterialData, RadientStandardMaterialNormalTextureName);
    IRadientTextureAsset* pPhysicalDesc = GetMaterialTexture(MaterialData, RadientStandardMaterialMetallicRoughnessTextureName);

    EXPECT_NE(pWhite, nullptr);
    EXPECT_NE(pBlack, nullptr);
    EXPECT_NE(pNormal, nullptr);
    EXPECT_NE(pPhysicalDesc, nullptr);
    ExpectTextureURI(pWhite, "radient://default-texture/white");
    ExpectTextureURI(pBlack, "radient://default-texture/black");
    ExpectTextureURI(pNormal, "radient://default-texture/normal");
    ExpectTextureURI(pPhysicalDesc, "radient://default-texture/physical-description");
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialOcclusionTextureName), pWhite);

    RadientMaterialParameterHandle UnusedHandle;
    ASSERT_NE(MaterialData.pInstance->GetDefinition(), nullptr);
    EXPECT_EQ(MaterialData.pInstance->GetDefinition()->FindParameter(
                  RadientStandardMaterialClearCoatTextureName, &UnusedHandle),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(MaterialData.pInstance->GetDefinition()->FindParameter(
                  RadientStandardMaterialClearCoatNormalTextureName, &UnusedHandle),
              RADIENT_STATUS_NOT_FOUND);

    ITextureView* const pWhiteSRV = RadientAssetManagerImpl::GetTextureSRV(pWhite);
    ASSERT_NE(pWhiteSRV, nullptr);
    const TextureDesc& AtlasDesc = pWhiteSRV->GetTexture()->GetDesc();
    EXPECT_EQ(AtlasDesc.MipLevels, ComputeMipLevelsCount(AtlasDesc));

    EXPECT_NE(RadientAssetManagerImpl::GetTextureSRV(pBlack), nullptr);
    EXPECT_NE(RadientAssetManagerImpl::GetTextureSRV(pNormal), nullptr);
    EXPECT_NE(RadientAssetManagerImpl::GetTextureSRV(pPhysicalDesc), nullptr);

    EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientAssetManagerGPUTest, TesseraMaterialWaitsForPreparedSRB)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
    AssetManagerCI.pThreadPool = pThreadPool;
    AssetManagerCI.pDevice     = pDevice;

    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
    ASSERT_NE(pAssetManager, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));

    {
        RadientTesseraGeometryRenderer Renderer{8, pAssetManager->GetDefaultMaterialTextures()};
        ASSERT_EQ(Renderer.Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
        ASSERT_NE(Renderer.GetMaterialCache(), nullptr);

        PBR_Renderer* const pPBRRenderer = Renderer.GetRenderer();
        ASSERT_NE(pPBRRenderer, nullptr);
        IBuffer* const pPrimitiveAttribsCB = pPBRRenderer->GetPBRPrimitiveAttribsCB();
        ASSERT_NE(pPrimitiveAttribsCB, nullptr);
        EXPECT_GT(pPrimitiveAttribsCB->GetDesc().Size,
                  pPBRRenderer->GetPBRPrimitiveAttribsSize(PBR_Renderer::PSO_FLAG_ALL));

        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
        ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pMaterial), RADIENT_STATUS_OK);
        ASSERT_NE(pMaterial, nullptr);

        const RadientTesseraMaterialResolveResult Result =
            Renderer.GetMaterialCache()->Resolve(*pThreadPool, pMaterial);
        ASSERT_TRUE(Result.Data);
        EXPECT_TRUE(IsPendingOrOK(Result.Status));

        pThreadPool->WaitForAllTasks();
        ASSERT_EQ(Result.Data->GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(Result.Data->GetGPUResourceStatus(), RADIENT_STATUS_PENDING);

        ASSERT_EQ(Renderer.Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
        EXPECT_EQ(Result.Data->GetGPUResourceStatus(), RADIENT_STATUS_OK);
        IShaderResourceBinding* const pMaterialSRB = Result.Data->GetMaterialSRB().GetSRB();
        ASSERT_NE(pMaterialSRB, nullptr);

        IShaderResourceVariable* const pPrimitiveAttribsVar = Result.Data->GetMaterialSRB().GetPrimitiveAttribsVariable();
        ASSERT_NE(pPrimitiveAttribsVar, nullptr);
        EXPECT_EQ(pPrimitiveAttribsVar->Get(), pPrimitiveAttribsCB);
        pPrimitiveAttribsVar->SetBufferOffset(
            pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment);

        // Reusing an already-created SRB must not make a newly allocated
        // material record ready before the render thread uploads its bytes.
        RefCntAutoPtr<IRadientMaterialAsset> pSecondMaterial;
        ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pSecondMaterial), RADIENT_STATUS_OK);
        ASSERT_NE(pSecondMaterial, nullptr);

        const RadientTesseraMaterialResolveResult SecondResult =
            Renderer.GetMaterialCache()->Resolve(*pThreadPool, pSecondMaterial);
        ASSERT_TRUE(SecondResult.Data);
        pThreadPool->WaitForAllTasks();
        ASSERT_EQ(SecondResult.Data->GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(SecondResult.Data->GetMaterialSRB().GetSRB(), pMaterialSRB);
        EXPECT_EQ(SecondResult.Data->GetGPUResourceStatus(), RADIENT_STATUS_PENDING);

        ASSERT_EQ(Renderer.Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
        EXPECT_EQ(SecondResult.Data->GetGPUResourceStatus(), RADIENT_STATUS_OK);
    }

    EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientAssetManagerGPUTest, MapsDefaultsForAllSupportedMaterialTextures)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
    AssetManagerCI.pThreadPool = pThreadPool;
    AssetManagerCI.pDevice     = pDevice;

    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pDefaultMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pDefaultMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pDefaultMaterial, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));

    const RadientMaterialAssetView DefaultMaterialData =
        RadientMaterialAssetManager::GetMaterialView(pDefaultMaterial);
    ASSERT_TRUE(DefaultMaterialData);

    RadientMaterialDefaultTextures DefaultTextures;
    DefaultTextures.pWhite =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialBaseColorTextureName);
    DefaultTextures.pBlack =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialEmissiveTextureName);
    DefaultTextures.pNormal =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialNormalTextureName);
    DefaultTextures.pPhysicalDesc =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialMetallicRoughnessTextureName);

    ExpectTextureURI(DefaultTextures.pWhite, "radient://default-texture/white");
    ExpectTextureURI(DefaultTextures.pBlack, "radient://default-texture/black");
    ExpectTextureURI(DefaultTextures.pNormal, "radient://default-texture/normal");
    ExpectTextureURI(DefaultTextures.pPhysicalDesc, "radient://default-texture/physical-description");

    RadientMaterialAssetManager::CreateInfo MaterialManagerCI{};
    MaterialManagerCI.DefaultTextures = DefaultTextures;

    RadientMaterialAssetManagerSharedPtr pMaterialManager =
        RadientMaterialAssetManager::Create(MaterialManagerCI);
    ASSERT_NE(pMaterialManager, nullptr);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    DefinitionCI.Features =
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT |
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN |
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY |
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE |
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION |
        RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(*pMaterialManager, DefinitionCI, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    const RadientMaterialAssetView MaterialData = RadientMaterialAssetManager::GetMaterialView(pMaterial);
    ExpectMetallicRoughnessTextureDefaults(MaterialData, DefaultTextures);

    RefCntAutoPtr<IRadientMaterialInstance> pInstance =
        RadientMaterialAssetManager::GetInstance(pMaterial);
    ASSERT_NE(pInstance, nullptr);
    ExpectMaterialInstanceTextures(*pInstance, MaterialData);

    EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientAssetManagerGPUTest, SceneWithMissingTexturesUsesDefaultsForAllSupportedMaterialTextures)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
    AssetManagerCI.pThreadPool = pThreadPool;
    AssetManagerCI.pDevice     = pDevice;

    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pDefaultMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pDefaultMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pDefaultMaterial, nullptr);

    TempDirectory     TempDir{"RadientAssetManagerGPUTest"};
    const std::string GLTFPath = WriteGLTFWithMissingMaterialTextures(TempDir);

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    TestingEnvironment::ErrorScope ExpectedErrors{"Failed to open file"};

    RefCntAutoPtr<IRadientSceneAsset> pScene;
    EXPECT_TRUE(IsPendingOrOK(pAssetManager->LoadScene(LoadInfo, &pScene)));
    ASSERT_NE(pScene, nullptr);

    // The missing texture remains failed, but both materials resolve through
    // their semantic defaults and allow the scene source load to succeed.
    // Default texture uploads may temporarily occupy the only worker while
    // waiting for render-thread copy callbacks, so service them before waiting
    // synchronously for the scene source task.
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));
    EXPECT_EQ(RadientAssetManagerImpl::GetSceneGPUResourceStatus(pScene), RADIENT_STATUS_OK);

    const RadientImport::ImportedDocument* pImportedScene = RadientAssetManagerImpl::GetImportedScene(pScene);
    ASSERT_NE(pImportedScene, nullptr);
    ASSERT_EQ(pImportedScene->Textures.size(), 1u);
    ASSERT_EQ(pImportedScene->Materials.size(), 1u);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pImportedScene->Textures[0]), RADIENT_STATUS_NOT_FOUND);

    RefCntAutoPtr<IRadientMaterialInstance> pMetallicRoughnessInstance =
        RadientMaterialAssetManager::GetInstance(pImportedScene->Materials[0]);
    ASSERT_NE(pMetallicRoughnessInstance, nullptr);
    const Uint64 InitialMaterialVersion = pMetallicRoughnessInstance->GetVersion();

    RefCntAutoPtr<IRadientMaterialAsset> pAliasedMaterial;
    ASSERT_EQ(pAssetManager->CreateMaterial(
                  pMetallicRoughnessInstance,
                  pAliasedMaterial.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    const RadientMaterialAssetView MetallicRoughnessMaterialData =
        RadientMaterialAssetManager::GetMaterialView(pImportedScene->Materials[0]);
    ASSERT_TRUE(MetallicRoughnessMaterialData);
    EXPECT_EQ(pMetallicRoughnessInstance->GetVersion(), InitialMaterialVersion + 1);

    const RadientMaterialAssetView DefaultMaterialData =
        RadientMaterialAssetManager::GetMaterialView(pDefaultMaterial);
    ASSERT_TRUE(DefaultMaterialData);

    RadientMaterialDefaultTextures DefaultTextures;
    DefaultTextures.pWhite =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialBaseColorTextureName);
    DefaultTextures.pBlack =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialEmissiveTextureName);
    DefaultTextures.pNormal =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialNormalTextureName);
    DefaultTextures.pPhysicalDesc =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialMetallicRoughnessTextureName);

    ExpectTextureURI(DefaultTextures.pWhite, "radient://default-texture/white");
    ExpectTextureURI(DefaultTextures.pBlack, "radient://default-texture/black");
    ExpectTextureURI(DefaultTextures.pNormal, "radient://default-texture/normal");
    ExpectTextureURI(DefaultTextures.pPhysicalDesc, "radient://default-texture/physical-description");

    ExpectMetallicRoughnessTextureDefaults(MetallicRoughnessMaterialData, DefaultTextures);

    ExpectMaterialInstanceTextures(*pMetallicRoughnessInstance, MetallicRoughnessMaterialData);

    const RadientMaterialAssetView AliasedMaterialData =
        RadientMaterialAssetManager::GetMaterialView(pAliasedMaterial);
    ASSERT_TRUE(AliasedMaterialData);
    EXPECT_EQ(pMetallicRoughnessInstance->GetVersion(), InitialMaterialVersion + 1);
    EXPECT_EQ(AliasedMaterialData.pInstance, pMetallicRoughnessInstance);
    EXPECT_EQ(AliasedMaterialData.pTextures, MetallicRoughnessMaterialData.pTextures);
    EXPECT_EQ(AliasedMaterialData.TextureCount, MetallicRoughnessMaterialData.TextureCount);
    EXPECT_EQ(AliasedMaterialData.pTextureIndexByParameter,
              MetallicRoughnessMaterialData.pTextureIndexByParameter);
    EXPECT_EQ(AliasedMaterialData.ParameterCount, MetallicRoughnessMaterialData.ParameterCount);
    ExpectMetallicRoughnessTextureDefaults(AliasedMaterialData, DefaultTextures);

    for (Uint32 TextureIndex = 0; TextureIndex < MetallicRoughnessMaterialData.TextureCount; ++TextureIndex)
    {
        EXPECT_NE(MetallicRoughnessMaterialData.pTextures[TextureIndex].pTexture,
                  pImportedScene->Textures[0].RawPtr());
    }

    EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientAssetManagerGPUTest, SceneWithMissingDDSTextureUsesDefault)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
    AssetManagerCI.pThreadPool = pThreadPool;
    AssetManagerCI.pDevice     = pDevice;

    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pDefaultMaterial;
    ASSERT_EQ(CreateStandardMaterialAsset(*pAssetManager, {}, &pDefaultMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pDefaultMaterial, nullptr);

    TempDirectory     TempDir{"RadientAssetManagerGPUTest"};
    const std::string GLTFPath = WriteGLTFWithMissingDDSTexture(TempDir);

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    TestingEnvironment::ErrorScope ExpectedErrors{"Failed to open file"};

    RefCntAutoPtr<IRadientSceneAsset> pScene;
    EXPECT_TRUE(IsPendingOrOK(pAssetManager->LoadScene(LoadInfo, &pScene)));
    ASSERT_NE(pScene, nullptr);

    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));
    EXPECT_EQ(RadientAssetManagerImpl::GetSceneGPUResourceStatus(pScene), RADIENT_STATUS_OK);

    const RadientImport::ImportedDocument* pImportedScene = RadientAssetManagerImpl::GetImportedScene(pScene);
    ASSERT_NE(pImportedScene, nullptr);
    ASSERT_EQ(pImportedScene->Textures.size(), 1u);
    ASSERT_EQ(pImportedScene->Materials.size(), 1u);
    ASSERT_NE(pImportedScene->Textures[0], nullptr);
    ASSERT_NE(pImportedScene->Textures[0]->GetReference().URI, nullptr);
    EXPECT_NE(std::string{pImportedScene->Textures[0]->GetReference().URI}.find("missing.dds"), std::string::npos);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pImportedScene->Textures[0]), RADIENT_STATUS_NOT_FOUND);

    const RadientMaterialAssetView MaterialData =
        RadientMaterialAssetManager::GetMaterialView(pImportedScene->Materials[0]);
    const RadientMaterialAssetView DefaultMaterialData =
        RadientMaterialAssetManager::GetMaterialView(pDefaultMaterial);
    ASSERT_TRUE(MaterialData);
    ASSERT_TRUE(DefaultMaterialData);

    IRadientTextureAsset* pDefaultWhite =
        GetMaterialTexture(DefaultMaterialData, RadientStandardMaterialBaseColorTextureName);
    ExpectTextureURI(pDefaultWhite, "radient://default-texture/white");
    EXPECT_EQ(GetMaterialTexture(MaterialData, RadientStandardMaterialBaseColorTextureName), pDefaultWhite);

    EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    pThreadPool->StopThreads();
}

TEST(RadientAssetManagerGPUTest, ManagerMayDieWhileTextureLoadsArePending)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    Threading::Signal         ReleaseWorker;
    RefCntAutoPtr<IAsyncTask> pBlocker = BlockWorkerThread(*pThreadPool, ReleaseWorker);
    ASSERT_NE(pBlocker, nullptr);

    const Uint32 TextureWidth  = 64;
    const Uint32 TextureHeight = 64;
    const Uint32 TextureStride = TextureWidth * 4;

    static constexpr size_t NumTextures = 4;

    std::array<std::vector<Uint8>, NumTextures>                  TexturePixels;
    std::array<RadientTextureData, NumTextures>                  TextureData;
    std::array<RefCntAutoPtr<IRadientTextureAsset>, NumTextures> Textures;

    for (size_t i = 0; i < NumTextures; ++i)
    {
        TexturePixels[i] = MakeTexturePixels(TextureWidth, TextureHeight, TextureStride, static_cast<Uint32>(i + 1));
        TextureData[i]   = MakeTextureData(TextureWidth, TextureHeight, TextureStride, TexturePixels[i].data());
    }

    {
        RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
        AssetManagerCI.pThreadPool = pThreadPool;
        AssetManagerCI.pDevice     = pDevice;

        RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
        ASSERT_NE(pAssetManager, nullptr);

        for (size_t i = 0; i < NumTextures; ++i)
        {
            EXPECT_TRUE(IsPendingOrOK(pAssetManager->LoadTexture(MakeTextureLoadInfo(TextureData[i]), &Textures[i]))) << i;
            ASSERT_NE(Textures[i], nullptr) << i;
            EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_PENDING) << i;
            EXPECT_EQ(RadientAssetManagerImpl::GetTextureSRV(Textures[i]), nullptr) << i;
        }

        EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    }

    // After Stop() and manager release, accepted texture load tasks observe
    // expired GPU upload dependencies and are cancelled.
    ReleaseWorker.Trigger();
    pThreadPool->StopThreads();

    for (size_t i = 0; i < NumTextures; ++i)
    {
        EXPECT_NE(Textures[i], nullptr) << i;
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_OK) << i;
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[i]), RADIENT_STATUS_CANCELLED) << i;
        EXPECT_EQ(RadientAssetManagerImpl::GetTextureSRV(Textures[i]), nullptr) << i;
    }
}

TEST(RadientAssetManagerGPUTest, StopShutsDownUploadManagerForBlockedTextureUpload)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    const Uint32 TextureWidth  = 64;
    const Uint32 TextureHeight = 64;
    const Uint32 TextureStride = TextureWidth * 4;

    std::vector<Uint8> TexturePixels = MakeTexturePixels(TextureWidth, TextureHeight, TextureStride, 1);
    RadientTextureData TextureData   = MakeTextureData(TextureWidth, TextureHeight, TextureStride, TexturePixels.data());

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    bool                                PendingCopyCommandEnqueueCallbacks = false;

    {
        RadientAssetManagerImpl::CreateInfo AssetManagerCI{};
        AssetManagerCI.pThreadPool = pThreadPool;
        AssetManagerCI.pDevice     = pDevice;

        RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create(AssetManagerCI);
        ASSERT_NE(pAssetManager, nullptr);

        // Default material textures are submitted during asset-manager initialization.
        // Drain them so this test observes only the upload scheduled below.
        ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));

        EXPECT_TRUE(IsPendingOrOK(pAssetManager->LoadTexture(MakeTextureLoadInfo(TextureData), &pTexture)));
        ASSERT_NE(pTexture, nullptr);

        PendingCopyCommandEnqueueCallbacks = WaitForPendingCopyCommandEnqueueCallbacks(*pAssetManager);
        ASSERT_TRUE(PendingCopyCommandEnqueueCallbacks);

        // The worker has loaded the source and queued upload callbacks, but
        // they have not reported success or failure yet.
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_PENDING);
        EXPECT_EQ(pAssetManager->Stop(pContext), RADIENT_STATUS_OK);
    }

    pThreadPool->StopThreads();

    // Stop() shuts down the upload manager and drains the blocked callbacks.
    // No texture copy was enqueued, so the GPU resource status reaches a terminal failure.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_FAILED);
    EXPECT_EQ(RadientAssetManagerImpl::GetTextureSRV(pTexture), nullptr);
}

} // namespace
