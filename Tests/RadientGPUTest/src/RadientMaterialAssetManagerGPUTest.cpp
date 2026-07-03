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

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "GPUTestingEnvironment.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "ThreadPool.hpp"
#include "ThreadSignal.hpp"

#include "gtest/gtest.h"

#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;

namespace
{

void ExpectTextureAttribsEqual(const GLTF::Material::TextureShaderAttribs& ActualAttribs,
                               const GLTF::Material::TextureShaderAttribs& ExpectedAttribs)
{
    EXPECT_FLOAT_EQ(ActualAttribs.TextureSlice, ExpectedAttribs.TextureSlice);
    EXPECT_FLOAT_EQ(ActualAttribs.AtlasUVScaleAndBias.x, ExpectedAttribs.AtlasUVScaleAndBias.x);
    EXPECT_FLOAT_EQ(ActualAttribs.AtlasUVScaleAndBias.y, ExpectedAttribs.AtlasUVScaleAndBias.y);
    EXPECT_FLOAT_EQ(ActualAttribs.AtlasUVScaleAndBias.z, ExpectedAttribs.AtlasUVScaleAndBias.z);
    EXPECT_FLOAT_EQ(ActualAttribs.AtlasUVScaleAndBias.w, ExpectedAttribs.AtlasUVScaleAndBias.w);
}

struct MaterialWithTextureManagers
{
    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager;
    RefCntAutoPtr<IGPUUploadManager>     pUploadManager;
    RadientTextureAssetManagerSharedPtr  pTextureManager;
    RadientMaterialAssetManagerSharedPtr pMaterialManager;
};

bool CreateMaterialWithBaseColorTexture(IRenderDevice*                        pDevice,
                                        IDeviceContext*                       pContext,
                                        IThreadPool&                          ThreadPool,
                                        const RadientTextureData&             TextureData,
                                        MaterialWithTextureManagers&          Managers,
                                        RefCntAutoPtr<IRadientTextureAsset>&  pTexture,
                                        RefCntAutoPtr<IRadientMaterialAsset>& pMaterial)
{
    Managers.pResourceManager = CreateTestResourceManager(pDevice);
    if (Managers.pResourceManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create resource manager";
        return false;
    }

    Managers.pUploadManager = CreateTestUploadManager(pDevice, pContext);
    if (Managers.pUploadManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create upload manager";
        return false;
    }

    Managers.pTextureManager = CreateTextureManager(pDevice, Managers.pResourceManager, Managers.pUploadManager);
    if (Managers.pTextureManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create texture manager";
        return false;
    }

    Managers.pMaterialManager = RadientMaterialAssetManager::Create();
    if (Managers.pMaterialManager == nullptr)
    {
        ADD_FAILURE() << "Failed to create material manager";
        return false;
    }

    const RADIENT_STATUS TextureStatus =
        Managers.pTextureManager->LoadTexture(ThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture);
    if (!IsPendingOrOK(TextureStatus) || pTexture == nullptr)
    {
        ADD_FAILURE() << "Failed to load texture: " << TextureStatus;
        return false;
    }

    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.pBaseColorTexture = pTexture;

    const RADIENT_STATUS MaterialStatus = Managers.pMaterialManager->CreateMaterial(MaterialCI, &pMaterial);
    if (MaterialStatus != RADIENT_STATUS_OK || pMaterial == nullptr)
    {
        ADD_FAILURE() << "Failed to create material: " << MaterialStatus;
        return false;
    }

    return true;
}

TEST(RadientMaterialAssetManagerGPUTest, WaitsForTextureStorage)
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

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pTextureManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pTextureManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.pBaseColorTexture = pTexture;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);

    // The texture worker is blocked, so the material must not expose texture
    // attributes that depend on texture storage placement.
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientMaterialAssetManager::GetMaterial(pMaterial), nullptr);

    ReleaseWorker.Trigger();

    ASSERT_TRUE(WaitForTextureManagerIdle(pTextureManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);

    const GLTF::Material* pGLTFMaterial = RadientMaterialAssetManager::GetMaterial(pMaterial);
    ASSERT_NE(pGLTFMaterial, nullptr);
    EXPECT_EQ(pGLTFMaterial->GetTextureId(GLTF::DefaultBaseColorTextureAttribId), 0);

    GLTF::Material::TextureShaderAttribs ExpectedAttribs;
    ASSERT_TRUE(RadientTextureAssetManager::ApplyTextureAtlasAttribs(pTexture, ExpectedAttribs));

    const GLTF::Material::TextureShaderAttribs& ActualAttribs =
        pGLTFMaterial->GetTextureAttrib(GLTF::DefaultBaseColorTextureAttribId);
    EXPECT_EQ(ActualAttribs.GetUVSelector(), 0);
    ExpectTextureAttribsEqual(ActualAttribs, ExpectedAttribs);

    pThreadPool->StopThreads();
}

TEST(RadientMaterialAssetManagerGPUTest, MaterialHandleMayOutliveManagersAfterTextureUpload)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    ASSERT_NE(pThreadPool, nullptr);

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    GLTF::Material::TextureShaderAttribs ExpectedAttribs;
    RefCntAutoPtr<IRadientTextureAsset>  pTexture;
    {
        MaterialWithTextureManagers Managers;
        ASSERT_TRUE(CreateMaterialWithBaseColorTexture(pDevice, pContext, *pThreadPool, TextureData, Managers, pTexture, pMaterial));

        ASSERT_TRUE(WaitForTextureManagerIdle(Managers.pTextureManager, *Managers.pUploadManager, *pContext));
        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);

        ProcessUploads(*Managers.pUploadManager, *pContext, *pTexture);
        ASSERT_TRUE(RadientTextureAssetManager::ApplyTextureAtlasAttribs(pTexture, ExpectedAttribs));

        // Drop the explicit texture handle. The material payload keeps the
        // texture dependency alive after all managers leave this scope.
        pTexture.Release();
    }

    pThreadPool->StopThreads();

    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);

    const GLTF::Material* pGLTFMaterial = RadientMaterialAssetManager::GetMaterial(pMaterial);
    ASSERT_NE(pGLTFMaterial, nullptr);
    EXPECT_EQ(pGLTFMaterial->GetTextureId(GLTF::DefaultBaseColorTextureAttribId), 0);

    const GLTF::Material::TextureShaderAttribs& ActualAttribs =
        pGLTFMaterial->GetTextureAttrib(GLTF::DefaultBaseColorTextureAttribId);
    EXPECT_EQ(ActualAttribs.GetUVSelector(), 0);
    ExpectTextureAttribsEqual(ActualAttribs, ExpectedAttribs);
}

TEST(RadientMaterialAssetManagerGPUTest, MaterialHandleMayOutliveManagersBeforeTextureUpload)
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

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    RefCntAutoPtr<IRadientTextureAsset>  pTexture;
    {
        MaterialWithTextureManagers Managers;
        ASSERT_TRUE(CreateMaterialWithBaseColorTexture(pDevice, pContext, *pThreadPool, TextureData, Managers, pTexture, pMaterial));

        EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_PENDING);
        EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_PENDING);
        EXPECT_EQ(RadientMaterialAssetManager::GetMaterial(pMaterial), nullptr);

        // Drop the explicit texture handle before managers are destroyed. The
        // material payload must keep the pending texture dependency alive.
        pTexture.Release();
    }

    // The queued texture load now runs without the resource/upload managers.
    // The material asset must remain valid and report the dependency failure.
    ReleaseWorker.Trigger();
    pThreadPool->StopThreads();

    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(RadientMaterialAssetManager::GetMaterial(pMaterial), nullptr);
}

} // namespace
