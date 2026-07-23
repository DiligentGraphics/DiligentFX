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

#include "GPUTestingEnvironment.hpp"
#include "TempDirectory.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace std::chrono_literals;

namespace
{

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
    for (Uint32 i = 0; i < 256; ++i)
    {
        const RadientTextureAssetManagerStats Stats = AssetManager.GetTextureManagerStats();
        if (Stats.PendingCopyCommandEnqueueCallbacks != 0)
            return true;

        std::this_thread::sleep_for(1ms);
    }

    return AssetManager.GetTextureManagerStats().PendingCopyCommandEnqueueCallbacks != 0;
}

bool WaitForTextureManagerIdle(RadientAssetManagerImpl& AssetManager,
                               IRenderDevice*           pDevice,
                               IDeviceContext*          pContext)
{
    for (Uint32 i = 0; i < 256; ++i)
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
        else
        {
            std::this_thread::sleep_for(1ms);
        }
    }

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
            "KHR_materials_pbrSpecularGlossiness",
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
            },
            {
                "extensions": {
                    "KHR_materials_pbrSpecularGlossiness": {
                        "diffuseTexture": {"index": 0},
                        "specularGlossinessTexture": {"index": 0}
                    }
                }
            }
        ]
    })GLTF";

    return Path;
}

void ExpectTextureURI(IRadientTextureAsset* pTexture, const char* ExpectedURI)
{
    ASSERT_NE(pTexture, nullptr);
    ASSERT_NE(pTexture->GetReference().URI, nullptr);
    EXPECT_STREQ(pTexture->GetReference().URI, ExpectedURI);
}

void ExpectMetallicRoughnessTextureDefaults(const RadientMaterialRenderData&      MaterialData,
                                            const RadientMaterialDefaultTextures& DefaultTextures)
{
    ASSERT_TRUE(MaterialData);
    ASSERT_EQ(MaterialData.TextureCount, GLTF::DefaultThicknessTextureAttribId + 1);

    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultBaseColorTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultMetallicRoughnessTextureAttribId), DefaultTextures.pPhysicalDesc);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultNormalTextureAttribId), DefaultTextures.pNormal);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultOcclusionTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultEmissiveTextureAttribId), DefaultTextures.pBlack);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultClearcoatTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultClearcoatRoughnessTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultClearcoatNormalTextureAttribId), DefaultTextures.pNormal);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultSheenColorTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultSheenRoughnessTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultAnisotropyTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultIridescenceTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultIridescenceThicknessTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultTransmissionTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultThicknessTextureAttribId), DefaultTextures.pWhite);
}

void ExpectSpecularGlossinessTextureDefaults(const RadientMaterialRenderData&      MaterialData,
                                             const RadientMaterialDefaultTextures& DefaultTextures)
{
    ASSERT_TRUE(MaterialData);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultDiffuseTextureAttribId), DefaultTextures.pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultSpecularGlossinessTextureAttibId), DefaultTextures.pWhite);
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
    RadientMaterialCreateInfo            MaterialCI{};
    ASSERT_EQ(pAssetManager->CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    // Default textures use the normal asynchronous texture path and become
    // material dependencies before the first material is created.
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    const RadientMaterialRenderData MaterialData = RadientMaterialAssetManager::GetRenderData(pMaterial);
    ASSERT_TRUE(MaterialData);
    ASSERT_EQ(MaterialData.TextureCount, GLTF::DefaultEmissiveTextureAttribId + 1);

    for (Uint32 TextureAttribId = 0; TextureAttribId < MaterialData.TextureCount; ++TextureAttribId)
        EXPECT_NE(MaterialData.GetTexture(TextureAttribId), nullptr) << TextureAttribId;

    IRadientTextureAsset* pWhite        = MaterialData.GetTexture(GLTF::DefaultBaseColorTextureAttribId);
    IRadientTextureAsset* pBlack        = MaterialData.GetTexture(GLTF::DefaultEmissiveTextureAttribId);
    IRadientTextureAsset* pNormal       = MaterialData.GetTexture(GLTF::DefaultNormalTextureAttribId);
    IRadientTextureAsset* pPhysicalDesc = MaterialData.GetTexture(GLTF::DefaultMetallicRoughnessTextureAttribId);

    EXPECT_NE(pWhite, nullptr);
    EXPECT_NE(pBlack, nullptr);
    EXPECT_NE(pNormal, nullptr);
    EXPECT_NE(pPhysicalDesc, nullptr);
    ExpectTextureURI(pWhite, "radient://default-texture/white");
    ExpectTextureURI(pBlack, "radient://default-texture/black");
    ExpectTextureURI(pNormal, "radient://default-texture/normal");
    ExpectTextureURI(pPhysicalDesc, "radient://default-texture/physical-description");
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultOcclusionTextureAttribId), pWhite);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultClearcoatTextureAttribId), nullptr);
    EXPECT_EQ(MaterialData.GetTexture(GLTF::DefaultClearcoatNormalTextureAttribId), nullptr);

    EXPECT_NE(RadientAssetManagerImpl::GetTextureSRV(pWhite), nullptr);
    EXPECT_NE(RadientAssetManagerImpl::GetTextureSRV(pBlack), nullptr);
    EXPECT_NE(RadientAssetManagerImpl::GetTextureSRV(pNormal), nullptr);
    EXPECT_NE(RadientAssetManagerImpl::GetTextureSRV(pPhysicalDesc), nullptr);

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
    RadientMaterialCreateInfo            DefaultMaterialCI{};
    ASSERT_EQ(pAssetManager->CreateMaterial(DefaultMaterialCI, &pDefaultMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pDefaultMaterial, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pDevice, pContext));

    const RadientMaterialRenderData DefaultMaterialData =
        RadientMaterialAssetManager::GetRenderData(pDefaultMaterial);
    ASSERT_TRUE(DefaultMaterialData);

    RadientMaterialDefaultTextures DefaultTextures;
    DefaultTextures.pWhite =
        DefaultMaterialData.GetTexture(GLTF::DefaultBaseColorTextureAttribId);
    DefaultTextures.pBlack =
        DefaultMaterialData.GetTexture(GLTF::DefaultEmissiveTextureAttribId);
    DefaultTextures.pNormal =
        DefaultMaterialData.GetTexture(GLTF::DefaultNormalTextureAttribId);
    DefaultTextures.pPhysicalDesc =
        DefaultMaterialData.GetTexture(GLTF::DefaultMetallicRoughnessTextureAttribId);

    ExpectTextureURI(DefaultTextures.pWhite, "radient://default-texture/white");
    ExpectTextureURI(DefaultTextures.pBlack, "radient://default-texture/black");
    ExpectTextureURI(DefaultTextures.pNormal, "radient://default-texture/normal");
    ExpectTextureURI(DefaultTextures.pPhysicalDesc, "radient://default-texture/physical-description");

    RadientMaterialAssetManager::CreateInfo MaterialManagerCI{};
    MaterialManagerCI.DefaultTextures = DefaultTextures;

    RadientMaterialAssetManagerSharedPtr pMaterialManager =
        RadientMaterialAssetManager::Create(MaterialManagerCI);
    ASSERT_NE(pMaterialManager, nullptr);

    GLTF::Material Material;
    Material.HasClearcoat = true;
    Material.Sheen        = std::make_unique<GLTF::Material::SheenShaderAttribs>();
    Material.Anisotropy   = std::make_unique<GLTF::Material::AnisotropyShaderAttribs>();
    Material.Iridescence  = std::make_unique<GLTF::Material::IridescenceShaderAttribs>();
    Material.Transmission = std::make_unique<GLTF::Material::TransmissionShaderAttribs>();
    Material.Volume       = std::make_unique<GLTF::Material::VolumeShaderAttribs>();

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateGLTFMaterial(std::move(Material), nullptr, 0, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    const RadientMaterialRenderData MaterialData = RadientMaterialAssetManager::GetRenderData(pMaterial);
    ExpectMetallicRoughnessTextureDefaults(MaterialData, DefaultTextures);

    GLTF::Material SpecGlossMaterial;
    SpecGlossMaterial.Attribs.Workflow = GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS;

    RefCntAutoPtr<IRadientMaterialAsset> pSpecGlossMaterial;
    ASSERT_EQ(pMaterialManager->CreateGLTFMaterial(std::move(SpecGlossMaterial), nullptr, 0, &pSpecGlossMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pSpecGlossMaterial, nullptr);

    const RadientMaterialRenderData SpecGlossMaterialData =
        RadientMaterialAssetManager::GetRenderData(pSpecGlossMaterial);
    ExpectSpecularGlossinessTextureDefaults(SpecGlossMaterialData, DefaultTextures);

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
    RadientMaterialCreateInfo            DefaultMaterialCI{};
    ASSERT_EQ(pAssetManager->CreateMaterial(DefaultMaterialCI, &pDefaultMaterial), RADIENT_STATUS_OK);
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
    ASSERT_EQ(pImportedScene->Materials.size(), 2u);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pImportedScene->Textures[0]), RADIENT_STATUS_NOT_FOUND);

    const RadientMaterialRenderData MetallicRoughnessMaterialData =
        RadientMaterialAssetManager::GetRenderData(pImportedScene->Materials[0]);
    const RadientMaterialRenderData SpecularGlossinessMaterialData =
        RadientMaterialAssetManager::GetRenderData(pImportedScene->Materials[1]);

    const RadientMaterialRenderData DefaultMaterialData =
        RadientMaterialAssetManager::GetRenderData(pDefaultMaterial);
    ASSERT_TRUE(DefaultMaterialData);

    RadientMaterialDefaultTextures DefaultTextures;
    DefaultTextures.pWhite =
        DefaultMaterialData.GetTexture(GLTF::DefaultBaseColorTextureAttribId);
    DefaultTextures.pBlack =
        DefaultMaterialData.GetTexture(GLTF::DefaultEmissiveTextureAttribId);
    DefaultTextures.pNormal =
        DefaultMaterialData.GetTexture(GLTF::DefaultNormalTextureAttribId);
    DefaultTextures.pPhysicalDesc =
        DefaultMaterialData.GetTexture(GLTF::DefaultMetallicRoughnessTextureAttribId);

    ExpectTextureURI(DefaultTextures.pWhite, "radient://default-texture/white");
    ExpectTextureURI(DefaultTextures.pBlack, "radient://default-texture/black");
    ExpectTextureURI(DefaultTextures.pNormal, "radient://default-texture/normal");
    ExpectTextureURI(DefaultTextures.pPhysicalDesc, "radient://default-texture/physical-description");

    ExpectMetallicRoughnessTextureDefaults(MetallicRoughnessMaterialData, DefaultTextures);
    ExpectSpecularGlossinessTextureDefaults(SpecularGlossinessMaterialData, DefaultTextures);

    for (Uint32 TextureAttribId = 0; TextureAttribId < MetallicRoughnessMaterialData.TextureCount; ++TextureAttribId)
    {
        EXPECT_NE(MetallicRoughnessMaterialData.GetTexture(TextureAttribId),
                  pImportedScene->Textures[0].RawPtr());
    }

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

    // After Stop() and manager release, texture load tasks observe expired GPU
    // upload dependencies and exit without completing the texture load.
    ReleaseWorker.Trigger();
    pThreadPool->StopThreads();

    for (size_t i = 0; i < NumTextures; ++i)
    {
        EXPECT_NE(Textures[i], nullptr) << i;
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[i]), RADIENT_STATUS_OK) << i;
        EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[i]), RADIENT_STATUS_INVALID_OPERATION) << i;
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
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(RadientAssetManagerImpl::GetTextureSRV(pTexture), nullptr);
}

} // namespace
