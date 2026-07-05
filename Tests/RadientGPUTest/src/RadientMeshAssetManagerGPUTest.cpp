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
#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "GPUTestingEnvironment.hpp"
#include "RadientGPUTestHelpers.hpp"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <chrono>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;
using namespace Diligent::Testing::RadientGPUTest;
using namespace std::chrono_literals;

namespace
{

RadientMeshAssetManagerSharedPtr CreateMeshManager(IRenderDevice*         pDevice,
                                                   GLTF::ResourceManager* pResourceManager,
                                                   IGPUUploadManager*     pUploadManager)
{
    RadientMeshAssetManager::CreateInfo CI;
    CI.pDevice          = pDevice;
    CI.pResourceManager = pResourceManager;
    CI.pUploadManager   = pUploadManager;
    return RadientMeshAssetManager::Create(CI);
}

bool WaitForMeshDrawableStatus(IRadientMeshAsset* pMesh,
                               RADIENT_STATUS     ExpectedStatus)
{
    for (Uint32 i = 0; i < 256; ++i)
    {
        const RadientDrawableMeshResolveResult Result =
            RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
        if (Result.Status == ExpectedStatus)
            return true;

        std::this_thread::sleep_for(1ms);
    }

    return RadientMeshAssetManager::GetDrawableMesh(pMesh, true).Status == ExpectedStatus;
}

TEST(RadientMeshAssetManagerGPUTest, WaitsForPendingMaterial)
{
    GPUTestingEnvironment::ScopedReset AutoReset;

    GPUTestingEnvironment* pEnv     = GPUTestingEnvironment::GetInstance();
    IRenderDevice*         pDevice  = pEnv->GetDevice();
    IDeviceContext*        pContext = pEnv->GetDeviceContext();
    ASSERT_NE(pDevice, nullptr);
    ASSERT_NE(pContext, nullptr);

    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{2});
    ASSERT_NE(pThreadPool, nullptr);
    ThreadPoolStopGuard StopThreadPool{pThreadPool};

    RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = CreateTestResourceManager(pDevice);
    ASSERT_NE(pResourceManager, nullptr);

    RefCntAutoPtr<IGPUUploadManager> pUploadManager = CreateTestUploadManager(pDevice, pContext);
    ASSERT_NE(pUploadManager, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = CreateTextureManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pTextureManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = CreateMeshManager(pDevice, pResourceManager, pUploadManager);
    ASSERT_NE(pMeshManager, nullptr);

    const std::vector<Uint8> TexturePixels = MakeTexturePixels();
    const RadientTextureData TextureData   = MakeTextureData(TexturePixels);

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_TRUE(IsPendingOrOK(pTextureManager->LoadTexture(*pThreadPool, MakeTextureDataLoadInfo(TextureData), &pTexture)));
    ASSERT_NE(pTexture, nullptr);

    // Keep the texture copy callback queued so the material remains pending
    // for GPU resource readiness while the mesh source itself can still be processed.
    ASSERT_TRUE(WaitForPendingCopyCommandEnqueueCallbacks(pTextureManager));
    ASSERT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_PENDING);

    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.pBaseColorTexture = pTexture;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pMaterialManager->CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);
    ASSERT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_PENDING);

    const RadientFloat3 Positions[] =
        {
            {0.f, 0.f, 0.f},
            {1.f, 0.f, 0.f},
            {0.f, 1.f, 0.f},
        };

    const RadientColorRGBA8 Colors[] =
        {
            {255, 0, 0, 255},
            {0, 255, 0, 255},
            {0, 0, 255, 255},
        };

    const Uint32 Indices[] = {0, 1, 2};

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.FirstIndex = 0;
    PrimitiveCI.IndexCount = 3;
    PrimitiveCI.pMaterial  = pMaterial;

    RadientMeshCreateInfo MeshCI{};
    MeshCI.Name           = "Radient mesh waiting for material";
    MeshCI.pPositions     = Positions;
    MeshCI.pColors0       = Colors;
    MeshCI.VertexCount    = 3;
    MeshCI.pIndices       = Indices;
    MeshCI.IndexCount     = 3;
    MeshCI.IndexType      = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.pPrimitives    = &PrimitiveCI;
    MeshCI.PrimitiveCount = 1;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    EXPECT_TRUE(IsPendingOrOK(pMeshManager->CreateMesh(*pThreadPool, MeshCI, &pMesh)));
    ASSERT_NE(pMesh, nullptr);

    // The mesh payload is available, but render-ready drawable resolution must
    // stay pending until dependent texture GPU work has been enqueued.
    ASSERT_TRUE(WaitForMeshDrawableStatus(pMesh, RADIENT_STATUS_PENDING));
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(pMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh), RADIENT_STATUS_PENDING);
    {
        const RadientDrawableMeshResolveResult Result =
            RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
        EXPECT_EQ(Result.Status, RADIENT_STATUS_PENDING);
        EXPECT_EQ(Result.pMesh, nullptr);
    }

    ASSERT_TRUE(WaitForTextureManagerIdle(pTextureManager, *pUploadManager, *pContext));
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(pTexture), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(pMaterial), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial), RADIENT_STATUS_OK);

    ASSERT_TRUE(WaitForMeshDrawableStatus(pMesh, RADIENT_STATUS_OK));
    const RadientDrawableMeshResolveResult Result =
        RadientMeshAssetManager::GetDrawableMesh(pMesh, true);
    ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Result.pMesh, nullptr);
    ASSERT_EQ(Result.pMesh->Primitives.size(), 1u);
    EXPECT_NE(Result.pMesh->Primitives[0].pMaterial, nullptr);

    pThreadPool->StopThreads();
    StopThreadPool.pThreadPool = nullptr;
}

} // namespace
