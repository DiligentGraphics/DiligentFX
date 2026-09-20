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
#include "RadientTypesX.hpp"
#include "RadientEngine.h"

#include "GPUTestingEnvironment.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "TempDirectory.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace std::chrono_literals;

namespace
{

static constexpr auto TextureManagerWaitTimeout = std::chrono::seconds{10};

RadientTextureDataX MakeTextureData(Uint32 Width, Uint32 Height, Uint32 Stride, IRadientDataBlob* pDataBlob)
{
    RadientTextureDataX TextureData{Width, Height, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
    TextureData.AddMip(pDataBlob, 0, Stride);
    return TextureData;
}

RadientTextureLoadInfoX MakeTextureLoadInfo(const RadientTextureData& TextureData)
{
    RadientTextureLoadInfoX LoadInfo{};
    LoadInfo.SetTextureData(TextureData).SetSRGB(False);
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
    if (MaterialData.pMaterial == nullptr)
    {
        ADD_FAILURE() << "Material render data has no material";
        return Handle;
    }

    IRadientMaterialDefinitionAsset* const pDefinition = MaterialData.pMaterial->GetDefinition();
    if (pDefinition == nullptr)
    {
        ADD_FAILURE() << "Material has no definition";
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

void ExpectMaterialTextures(const IRadientMaterialAsset&    Material,
                            const RadientMaterialAssetView& MaterialData)
{
    IRadientMaterialDefinitionAsset* const pDefinition = Material.GetDefinition();
    ASSERT_NE(pDefinition, nullptr);

    ASSERT_TRUE(MaterialData);
    ASSERT_EQ(MaterialData.pMaterial, &Material);
    ASSERT_NE(MaterialData.pTextures, nullptr);

    for (Uint32 TextureIndex = 0; TextureIndex < MaterialData.TextureCount; ++TextureIndex)
    {
        const RadientMaterialTextureEntry& TextureData = MaterialData.pTextures[TextureIndex];
        RadientMaterialParameterHandle     Handle;
        ASSERT_EQ(pDefinition->GetParameterHandle(TextureData.ParameterIndex, &Handle), RADIENT_STATUS_OK);

        RefCntAutoPtr<IRadientTextureAsset> pTexture;
        ASSERT_EQ(Material.GetTexture(Handle, TextureData.ArrayIndex, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        EXPECT_EQ(pTexture, TextureData.pTexture);
    }
}

TEST(RadientAssetManagerGPUTest, UsesEngineResourceManagerSettings)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv = GPUTestingEnvironment::GetInstance();

    RadientEngineCreateInfo EngineCI;
    EngineCI.Backend.pDevice            = pEnv->GetDevice();
    EngineCI.Backend.pImmediateContext  = pEnv->GetDeviceContext();
    EngineCI.WorkerThreadCount          = 1;
    auto& Resources                     = EngineCI.Resources;
    Resources.IndexBufferSize           = 1024;
    Resources.MaxIndexBufferSize        = 2048;
    Resources.MorphTargetBufferSize     = 256;
    Resources.MaxMorphTargetBufferSize  = 768;
    Resources.VertexPoolSize            = 1024;
    Resources.TextureAtlasSize          = 512;
    Resources.TextureAtlasMipLevel0Size = 256 * 1024;
    Resources.TextureAtlasSlices        = 2;
    Resources.TextureAtlasMaxSlices     = 3;

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);
    RefCntAutoPtr<IRadientAssetManager> pAssets;
    ASSERT_EQ(pEngine->GetAssetManager(&pAssets), RADIENT_STATUS_OK);
    auto* pAssetManager = static_cast<RadientAssetManagerImpl*>(pAssets.RawPtr());
    auto* pResources    = pAssetManager->GetResourceManager();
    ASSERT_NE(pResources, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pEnv->GetDevice(), pEnv->GetDeviceContext()));

    ASSERT_NE(pResources->GetIndexBuffer(), nullptr);
    EXPECT_EQ(pResources->GetIndexBuffer()->GetDesc().Size, Resources.IndexBufferSize);
    ASSERT_NE(pResources->GetMorphTargetBuffer(), nullptr);
    EXPECT_EQ(pResources->GetMorphTargetBuffer()->GetDesc().Size, Resources.MorphTargetBufferSize);

    // The first index buffer grows to its limit; the next allocation uses a new buffer.
    auto pIndices0 = pResources->AllocateIndices(Resources.IndexBufferSize);
    auto pIndices1 = pResources->AllocateIndices(Resources.IndexBufferSize);
    auto pIndices2 = pResources->AllocateIndices(Resources.IndexBufferSize);
    ASSERT_NE(pIndices0, nullptr);
    ASSERT_NE(pIndices1, nullptr);
    ASSERT_NE(pIndices2, nullptr);
    EXPECT_EQ(pIndices0->GetAllocator(), pIndices1->GetAllocator());
    EXPECT_NE(pIndices0->GetAllocator(), pIndices2->GetAllocator());
    EXPECT_EQ(pResources->GetIndexBufferCount(), 2u);
    auto* pIndexBuffer = pResources->UpdateIndexBuffer(pEnv->GetDevice(), pEnv->GetDeviceContext());
    ASSERT_NE(pIndexBuffer, nullptr);
    EXPECT_EQ(pIndexBuffer->GetDesc().Size, Resources.MaxIndexBufferSize);

    auto pMorphData = pResources->AllocateMorphTargetData(static_cast<Uint32>(Resources.MaxMorphTargetBufferSize));
    ASSERT_NE(pMorphData, nullptr);
    EXPECT_EQ(pResources->AllocateMorphTargetData(4), nullptr);
    auto* pMorphBuffer = pResources->UpdateMorphTargetBuffer(pEnv->GetDevice(), pEnv->GetDeviceContext());
    ASSERT_NE(pMorphBuffer, nullptr);
    EXPECT_EQ(pMorphBuffer->GetDesc().Size, Resources.MaxMorphTargetBufferSize);

    const GLTF::ResourceManager::VertexLayoutKey Layout{{sizeof(float) * 3, BIND_VERTEX_BUFFER}};
    auto                                         pVertices = pResources->AllocateVertices(Layout, 3);
    ASSERT_NE(pVertices, nullptr);
    EXPECT_EQ(pVertices->GetPool()->GetDesc().VertexCount, Resources.VertexPoolSize);

    const TextureDesc R8Desc = pResources->GetAtlasDesc(TEX_FORMAT_R8_UNORM);
    EXPECT_EQ(R8Desc.Width, 512u);
    EXPECT_EQ(R8Desc.Height, 512u);
    EXPECT_EQ(R8Desc.ArraySize, Resources.TextureAtlasSlices);
    const TextureDesc RGBA8Desc = pResources->GetAtlasDesc(TEX_FORMAT_RGBA8_TYPELESS);
    EXPECT_EQ(RGBA8Desc.Width, 256u);
    EXPECT_EQ(RGBA8Desc.Height, 256u);

    // Fill the otherwise unused R8 atlas to verify the configured slice limit.
    std::vector<RefCntAutoPtr<ITextureAtlasSuballocation>> Slices;
    for (Uint32 Slice = 0; Slice < Resources.TextureAtlasMaxSlices; ++Slice)
    {
        auto pSlice = pResources->AllocateTextureSpace(TEX_FORMAT_R8_UNORM, R8Desc.Width, R8Desc.Height);
        ASSERT_NE(pSlice, nullptr);
        Slices.push_back(std::move(pSlice));
    }
    {
        TestingEnvironment::ErrorScope            ExpectedErrors{"Failed to suballocate texture subregion"};
        RefCntAutoPtr<ITextureAtlasSuballocation> pExtraSlice;
        Slices.front()->GetAtlas()->Allocate(R8Desc.Width, R8Desc.Height, &pExtraSlice);
        EXPECT_EQ(pExtraSlice, nullptr);
    }

    EXPECT_EQ(pAssets->Stop(pEnv->GetDeviceContext()), RADIENT_STATUS_OK);
}

TEST(RadientAssetManagerGPUTest, ZeroPoolSizesSelectEngineDefaults)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv = GPUTestingEnvironment::GetInstance();

    RadientEngineCreateInfo EngineCI;
    EngineCI.Backend.pDevice                 = pEnv->GetDevice();
    EngineCI.Backend.pImmediateContext       = pEnv->GetDeviceContext();
    EngineCI.WorkerThreadCount               = 1;
    EngineCI.Resources.IndexBufferSize       = 0;
    EngineCI.Resources.MorphTargetBufferSize = 0;
    EngineCI.Resources.VertexPoolSize        = 0;
    EngineCI.Resources.TextureAtlasSize      = 0;
    EngineCI.Resources.TextureAtlasMaxSlices = 0;

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);
    RefCntAutoPtr<IRadientAssetManager> pAssets;
    ASSERT_EQ(pEngine->GetAssetManager(&pAssets), RADIENT_STATUS_OK);
    auto* pAssetManager = static_cast<RadientAssetManagerImpl*>(pAssets.RawPtr());
    auto* pResources    = pAssetManager->GetResourceManager();
    ASSERT_NE(pResources, nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pEnv->GetDevice(), pEnv->GetDeviceContext()));

    constexpr RadientResourceManagerCreateInfo Defaults{};
    ASSERT_NE(pResources->GetIndexBuffer(), nullptr);
    EXPECT_EQ(pResources->GetIndexBuffer()->GetDesc().Size, Defaults.IndexBufferSize);
    EXPECT_NE(pResources->AllocateIndices(4), nullptr);
    ASSERT_NE(pResources->GetMorphTargetBuffer(), nullptr);
    EXPECT_EQ(pResources->GetMorphTargetBuffer()->GetDesc().Size, Defaults.MorphTargetBufferSize);
    EXPECT_NE(pResources->AllocateMorphTargetData(4), nullptr);

    const GLTF::ResourceManager::VertexLayoutKey Layout{{sizeof(float) * 3, BIND_VERTEX_BUFFER}};
    auto                                         pVertices = pResources->AllocateVertices(Layout, 3);
    ASSERT_NE(pVertices, nullptr);
    EXPECT_EQ(pVertices->GetPool()->GetDesc().VertexCount, Defaults.VertexPoolSize);

    const TextureDesc AtlasDesc = pResources->GetAtlasDesc(TEX_FORMAT_R8_UNORM);
    EXPECT_EQ(AtlasDesc.Width, Defaults.TextureAtlasSize);
    EXPECT_EQ(AtlasDesc.Height, Defaults.TextureAtlasSize);

    // The default slice limit allows the atlas to grow beyond its initial slice.
    auto pSlice0 = pResources->AllocateTextureSpace(TEX_FORMAT_R8_UNORM, AtlasDesc.Width, AtlasDesc.Height);
    auto pSlice1 = pResources->AllocateTextureSpace(TEX_FORMAT_R8_UNORM, AtlasDesc.Width, AtlasDesc.Height);
    ASSERT_NE(pSlice0, nullptr);
    ASSERT_NE(pSlice1, nullptr);
    EXPECT_NE(pSlice0->GetSlice(), pSlice1->GetSlice());

    EXPECT_EQ(pAssets->Stop(pEnv->GetDeviceContext()), RADIENT_STATUS_OK);
}

TEST(RadientAssetManagerGPUTest, RoundsEngineResourceSettingsUp)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv = GPUTestingEnvironment::GetInstance();

    struct TestCase
    {
        Uint32 VertexPoolSize;
        Uint64 TextureAtlasMipLevel0Size;
        Uint32 ExpectedVertexPoolSize;
        Uint32 ExpectedRG8AtlasSize;
    };
    const TestCase Cases[] = {
        {1, 1, 1024, 128},
        {1023, 64u * 1024u - 1u, 1024, 128},
        {1024, 64u * 1024u, 1024, 128},
        {1025, 64u * 1024u + 1u, 2048, 256},
    };
    for (const auto& Case : Cases)
    {
        SCOPED_TRACE(Case.VertexPoolSize);
        RadientEngineCreateInfo EngineCI;
        EngineCI.Backend.pDevice                     = pEnv->GetDevice();
        EngineCI.Backend.pImmediateContext           = pEnv->GetDeviceContext();
        EngineCI.WorkerThreadCount                   = 1;
        EngineCI.Resources.VertexPoolSize            = Case.VertexPoolSize;
        EngineCI.Resources.TextureAtlasSize          = 512;
        EngineCI.Resources.TextureAtlasMipLevel0Size = Case.TextureAtlasMipLevel0Size;

        RefCntAutoPtr<IRadientEngine> pEngine;
        ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
        ASSERT_NE(pEngine, nullptr);
        RefCntAutoPtr<IRadientAssetManager> pAssets;
        ASSERT_EQ(pEngine->GetAssetManager(&pAssets), RADIENT_STATUS_OK);
        auto* pAssetManager = static_cast<RadientAssetManagerImpl*>(pAssets.RawPtr());
        auto* pResources    = pAssetManager->GetResourceManager();
        ASSERT_NE(pResources, nullptr);
        ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pEnv->GetDevice(), pEnv->GetDeviceContext()));

        const GLTF::ResourceManager::VertexLayoutKey Layout{{sizeof(float) * 3, BIND_VERTEX_BUFFER}};
        auto                                         pVertices = pResources->AllocateVertices(Layout, 3);
        ASSERT_NE(pVertices, nullptr);
        EXPECT_EQ(pVertices->GetPool()->GetDesc().VertexCount, Case.ExpectedVertexPoolSize);

        // A 64 KiB budget fits a 256x256 R8 slice, including when the input was smaller.
        const TextureDesc AtlasDesc = pResources->GetAtlasDesc(TEX_FORMAT_R8_UNORM);
        EXPECT_EQ(AtlasDesc.Width, 256u);
        EXPECT_EQ(AtlasDesc.Height, 256u);
        const TextureDesc RG8AtlasDesc = pResources->GetAtlasDesc(TEX_FORMAT_RG8_UNORM);
        EXPECT_EQ(RG8AtlasDesc.Width, Case.ExpectedRG8AtlasSize);
        EXPECT_EQ(RG8AtlasDesc.Height, Case.ExpectedRG8AtlasSize);
        EXPECT_EQ(EngineCI.Resources.VertexPoolSize, Case.VertexPoolSize);
        EXPECT_EQ(EngineCI.Resources.TextureAtlasMipLevel0Size, Case.TextureAtlasMipLevel0Size);
        EXPECT_EQ(pAssets->Stop(pEnv->GetDeviceContext()), RADIENT_STATUS_OK);
    }
}

TEST(RadientAssetManagerGPUTest, RejectsVertexPoolAlignmentOverflow)
{
    TestingEnvironment::ErrorScope ExpectedErrors{"VertexPoolSize"};
    RadientEngineCreateInfo        EngineCI;
    EngineCI.Backend.pDevice          = GPUTestingEnvironment::GetInstance()->GetDevice();
    EngineCI.Resources.VertexPoolSize = std::numeric_limits<Uint32>::max();

    RefCntAutoPtr<IRadientEngine> pEngine;
    EXPECT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pEngine, nullptr);
}

TEST(RadientAssetManagerGPUTest, RejectsTextureAtlasBudgetAlignmentOverflow)
{
    TestingEnvironment::ErrorScope ExpectedErrors{"TextureAtlasMipLevel0Size"};
    RadientEngineCreateInfo        EngineCI;
    EngineCI.Backend.pDevice                     = GPUTestingEnvironment::GetInstance()->GetDevice();
    EngineCI.Resources.TextureAtlasMipLevel0Size = std::numeric_limits<Uint64>::max();

    RefCntAutoPtr<IRadientEngine> pEngine;
    EXPECT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pEngine, nullptr);
}

TEST(RadientAssetManagerGPUTest, EngineAllowsLazyTextureAtlasWithoutSizeLimit)
{
    GPUTestingEnvironment::ScopedReset AutoReset;
    auto*                              pEnv = GPUTestingEnvironment::GetInstance();

    RadientEngineCreateInfo EngineCI;
    EngineCI.Backend.pDevice                     = pEnv->GetDevice();
    EngineCI.Backend.pImmediateContext           = pEnv->GetDeviceContext();
    EngineCI.WorkerThreadCount                   = 1;
    EngineCI.Resources.IndexBufferSize           = 1024;
    EngineCI.Resources.TextureAtlasSlices        = 0;
    EngineCI.Resources.TextureAtlasMipLevel0Size = 0;

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);
    RefCntAutoPtr<IRadientAssetManager> pAssets;
    ASSERT_EQ(pEngine->GetAssetManager(&pAssets), RADIENT_STATUS_OK);
    auto* pAssetManager = static_cast<RadientAssetManagerImpl*>(pAssets.RawPtr());
    auto* pResources    = pAssetManager->GetResourceManager();
    ASSERT_NE(pResources, nullptr);
    const GLTF::ResourceManager::VertexLayoutKey Layout{{sizeof(float) * 3, BIND_VERTEX_BUFFER}};
    EXPECT_NE(pResources->AllocateVertices(Layout, 3), nullptr);
    EXPECT_EQ(pResources->GetAtlasDesc(TEX_FORMAT_RGBA8_TYPELESS).Width, EngineCI.Resources.TextureAtlasSize);
    EXPECT_EQ(pResources->GetAtlasDesc(TEX_FORMAT_RGBA32_FLOAT).Width, EngineCI.Resources.TextureAtlasSize);
    EXPECT_NE(pResources->AllocateTextureSpace(TEX_FORMAT_RGBA8_TYPELESS, 16, 16), nullptr);
    ASSERT_TRUE(WaitForTextureManagerIdle(*pAssetManager, pEnv->GetDevice(), pEnv->GetDeviceContext()));
    EXPECT_EQ(pAssets->Stop(pEnv->GetDeviceContext()), RADIENT_STATUS_OK);
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

    GLTF::ResourceManager* pResourceManager = pAssetManager->GetResourceManager();
    ASSERT_NE(pResourceManager, nullptr);

    const TextureDesc R8AtlasDesc = pResourceManager->GetAtlasDesc(TEX_FORMAT_R8_UNORM);
    EXPECT_EQ(R8AtlasDesc.Width, 2048u);
    EXPECT_EQ(R8AtlasDesc.Height, 2048u);

    const TextureDesc RGBA8AtlasDesc = pResourceManager->GetAtlasDesc(TEX_FORMAT_RGBA8_TYPELESS);
    EXPECT_EQ(RGBA8AtlasDesc.Width, 2048u);
    EXPECT_EQ(RGBA8AtlasDesc.Height, 2048u);

    const TextureDesc RGBA32FAtlasDesc = pResourceManager->GetAtlasDesc(TEX_FORMAT_RGBA32_FLOAT);
    EXPECT_EQ(RGBA32FAtlasDesc.Width, 1024u);
    EXPECT_EQ(RGBA32FAtlasDesc.Height, 1024u);

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
    ASSERT_NE(MaterialData.pMaterial->GetDefinition(), nullptr);
    EXPECT_EQ(MaterialData.pMaterial->GetDefinition()->FindParameter(
                  RadientStandardMaterialClearCoatTextureName, &UnusedHandle),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(MaterialData.pMaterial->GetDefinition()->FindParameter(
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
        ASSERT_EQ(Renderer.BeginFrame(pDevice, pContext), RADIENT_STATUS_OK);
        ASSERT_NE(Renderer.GetMaterialCache(), nullptr);
        ASSERT_EQ(Renderer.Prepare(pDevice, pContext, pAssetManager->GetResourceManager()), RADIENT_STATUS_OK);
        ASSERT_NE(Renderer.GetJointBuffer().GetBuffer(), nullptr);

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

    ExpectMaterialTextures(*pMaterial, MaterialData);

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

    RefCntAutoPtr<IRadientMaterialAsset> pMetallicRoughnessMaterial =
        pImportedScene->Materials[0];
    ASSERT_NE(pMetallicRoughnessMaterial, nullptr);
    const Uint64 InitialMaterialVersion = pMetallicRoughnessMaterial->GetVersion();

    const RadientMaterialAssetView MetallicRoughnessMaterialData =
        RadientMaterialAssetManager::GetMaterialView(pImportedScene->Materials[0]);
    ASSERT_TRUE(MetallicRoughnessMaterialData);
    EXPECT_EQ(pMetallicRoughnessMaterial->GetVersion(), InitialMaterialVersion + 1);

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

    ExpectMaterialTextures(*pMetallicRoughnessMaterial, MetallicRoughnessMaterialData);

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

    const Uint32                            TextureWidth  = 64;
    const Uint32                            TextureHeight = 64;
    const Uint32                            TextureStride = TextureWidth * 4;
    const RadientGPUTest::TestTextureParams TextureParams{TextureWidth, TextureHeight, 4, TextureStride};

    static constexpr size_t NumTextures = 4;

    std::array<RefCntAutoPtr<IRadientDataBlob>, NumTextures>     TextureBlobs;
    std::array<RadientTextureDataX, NumTextures>                 TextureData;
    std::array<RefCntAutoPtr<IRadientTextureAsset>, NumTextures> Textures;

    for (size_t i = 0; i < NumTextures; ++i)
    {
        TextureBlobs[i] = RadientGPUTest::MakeTextureDataBlob(static_cast<Uint32>(i + 1), TextureParams);
        ASSERT_NE(TextureBlobs[i], nullptr);
        TextureData[i] = MakeTextureData(TextureWidth, TextureHeight, TextureStride, TextureBlobs[i]);
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

    const Uint32                            TextureWidth  = 64;
    const Uint32                            TextureHeight = 64;
    const Uint32                            TextureStride = TextureWidth * 4;
    const RadientGPUTest::TestTextureParams TextureParams{TextureWidth, TextureHeight, 4, TextureStride};

    const RefCntAutoPtr<IRadientDataBlob> pTextureBlob = RadientGPUTest::MakeTextureDataBlob(1, TextureParams);
    ASSERT_NE(pTextureBlob, nullptr);
    RadientTextureDataX TextureData = MakeTextureData(TextureWidth, TextureHeight, TextureStride, pTextureBlob);

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
