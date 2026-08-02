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

#include "TestingEnvironment.hpp"
#include "TempDirectory.hpp"
#include "gtest/gtest.h"

#include "RadientEngine.h"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Scene/RadientSceneImpl.hpp"
#include "RadientTestAssetHelpers.hpp"

#include "ThreadPool.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <string>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

std::string WriteGLTFFile(const TempDirectory& TempDir, const char* FileName, const char* Contents)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << Contents;

    return Path;
}

std::string WriteBasicGLTFFile(const TempDirectory& TempDir)
{
    return WriteGLTFFile(TempDir, "basic_scene.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "Root"}]
})GLTF");
}

struct TextureReleaseState
{
    Uint32      Count    = 0;
    const void* pData    = nullptr;
    Uint64      DataSize = 0;
};

void ReleaseTextureData(const void* pData, Uint64 DataSize, void* pUserData)
{
    auto& State = *static_cast<TextureReleaseState*>(pUserData);
    ++State.Count;
    State.pData    = pData;
    State.DataSize = DataSize;
}

RefCntAutoPtr<IRadientEngine> CreateTestEngine()
{
    RadientEngineCreateInfo EngineCI{};

    RefCntAutoPtr<IRadientEngine> pEngine;
    EXPECT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    EXPECT_NE(pEngine, nullptr);

    return pEngine;
}

RefCntAutoPtr<IRadientBackend> GetTestBackend(IRadientEngine& Engine)
{
    RefCntAutoPtr<IRadientBackend> pBackend;
    EXPECT_EQ(Engine.GetBackend(&pBackend), RADIENT_STATUS_OK);
    EXPECT_NE(pBackend, nullptr);

    return pBackend;
}

RefCntAutoPtr<IRadientAssetManager> GetTestAssetManager(IRadientEngine& Engine)
{
    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    EXPECT_EQ(Engine.GetAssetManager(&pAssetManager), RADIENT_STATUS_OK);
    EXPECT_NE(pAssetManager, nullptr);

    return pAssetManager;
}

RADIENT_STATUS ProcessTestGLTFLoad(IRadientAssetManager& AssetManager, IRadientSceneAsset* pModel)
{
    return AssetManager.WaitForAssetLoad(pModel);
}

RefCntAutoPtr<IRadientMaterialAsset> CreateTestMaterial(IRadientAssetManager& AssetManager)
{
    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.Name            = "Radient test material";
    MaterialCI.BaseColorFactor = {1.f, 0.f, 0.f, 1.f};

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    EXPECT_EQ(AssetManager.CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
    EXPECT_NE(pMaterial, nullptr);
    if (pMaterial != nullptr)
    {
        EXPECT_NE(pMaterial->GetReference().URI, nullptr);
        EXPECT_NE(pMaterial->GetReference().Version, 0u);
    }

    return pMaterial;
}

RefCntAutoPtr<IRadientMeshAsset> CreateTestMesh(IRadientAssetManager&  AssetManager,
                                                IRadientMaterialAsset* pMaterial,
                                                Float32                PositionOffset = 0.f)
{
    const RadientFloat3 Positions[] =
        {
            {0.f, 0.f, 0.f},
            {1.f + PositionOffset, 0.f, 0.f},
            {0.f, 1.f, 0.f},
        };

    const RadientColorRGBA8 Colors[] =
        {
            {255, 0, 0, 255},
            {0, 255, 0, 255},
            {0, 0, 255, 255},
        };

    const RadientBoneIndices4 BoneIndices[] =
        {
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            {0, 0, 0, 0},
        };

    const RadientFloat4 BoneWeights[] =
        {
            {1.f, 0.f, 0.f, 0.f},
            {1.f, 0.f, 0.f, 0.f},
            {1.f, 0.f, 0.f, 0.f},
        };

    const Uint32 Indices[] = {0, 1, 2};

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.Name       = "Radient test primitive";
    PrimitiveCI.FirstIndex = 0;
    PrimitiveCI.IndexCount = 3;
    PrimitiveCI.pMaterial  = pMaterial;

    RadientMeshCreateInfo MeshCI{};
    MeshCI.Name           = "Radient test mesh";
    MeshCI.pPositions     = Positions;
    MeshCI.pColors0       = Colors;
    MeshCI.pBoneIndices0  = BoneIndices;
    MeshCI.pBoneWeights0  = BoneWeights;
    MeshCI.VertexCount    = 3;
    MeshCI.pIndices       = Indices;
    MeshCI.IndexCount     = 3;
    MeshCI.IndexType      = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.pPrimitives    = &PrimitiveCI;
    MeshCI.PrimitiveCount = 1;

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    const RADIENT_STATUS             CreateStatus = AssetManager.CreateMesh(MeshCI, &pMesh);
    EXPECT_TRUE(CreateStatus == RADIENT_STATUS_OK || CreateStatus == RADIENT_STATUS_PENDING);
    EXPECT_NE(pMesh, nullptr);
    if (pMesh != nullptr)
    {
        EXPECT_NE(pMesh->GetReference().URI, nullptr);
        EXPECT_NE(pMesh->GetReference().Version, 0u);
    }

    return pMesh;
}

RefCntAutoPtr<IRadientScene> CreateTestScene(IRadientEngine& Engine)
{
    RadientSceneDesc SceneDesc{};
    SceneDesc.Name = "Radient test scene";

    RefCntAutoPtr<IRadientScene> pScene;
    EXPECT_EQ(Engine.CreateScene(SceneDesc, &pScene), RADIENT_STATUS_OK);
    EXPECT_NE(pScene, nullptr);
    if (pScene != nullptr)
        EXPECT_STREQ(pScene->GetDesc().Name, SceneDesc.Name);

    return pScene;
}

RefCntAutoPtr<IRadientSceneWriter> CreateTestSceneWriter(IRadientEngine& Engine, IRadientScene* pScene)
{
    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    EXPECT_EQ(Engine.CreateSceneWriter(pScene, &pWriter), RADIENT_STATUS_OK);
    EXPECT_NE(pWriter, nullptr);

    return pWriter;
}

RefCntAutoPtr<IRadientSceneImporter> CreateTestSceneImporter(IRadientEngine& Engine, IRadientSceneWriter* pWriter)
{
    RefCntAutoPtr<IRadientSceneImporter> pImporter;
    EXPECT_EQ(Engine.CreateSceneImporter(pWriter, &pImporter), RADIENT_STATUS_OK);
    EXPECT_NE(pImporter, nullptr);

    return pImporter;
}

RefCntAutoPtr<IRadientRenderer> CreateTestRenderer(IRadientEngine& Engine)
{
    RadientRendererDesc RendererDesc{};
    RendererDesc.Name = "Radient test renderer";

    RefCntAutoPtr<IRadientRenderer> pRenderer;
    EXPECT_EQ(Engine.CreateRenderer(RendererDesc, &pRenderer), RADIENT_STATUS_OK);
    EXPECT_NE(pRenderer, nullptr);
    if (pRenderer != nullptr)
        EXPECT_STREQ(pRenderer->GetDesc().Name, RendererDesc.Name);

    return pRenderer;
}

RefCntAutoPtr<IRadientRenderTarget> CreateTestRenderTarget(IRadientRenderer& Renderer)
{
    RadientRenderTargetDesc TargetDesc{};
    TargetDesc.Name = "Radient test target";
    TargetDesc.Size = {640, 480};

    RefCntAutoPtr<IRadientRenderTarget> pTarget;
    EXPECT_EQ(Renderer.CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_OK);
    EXPECT_NE(pTarget, nullptr);
    if (pTarget != nullptr)
    {
        EXPECT_STREQ(pTarget->GetDesc().Name, TargetDesc.Name);
        EXPECT_EQ(pTarget->GetDesc().Size, TargetDesc.Size);
    }

    return pTarget;
}

RefCntAutoPtr<IRadientView> CreateTestView(IRadientRenderer&     Renderer,
                                           IRadientScene*        pScene,
                                           IRadientRenderTarget* pTarget,
                                           RadientEntityID       Camera = InvalidRadientEntityID)
{
    RadientViewDesc ViewDesc{};
    ViewDesc.Name          = "Radient test view";
    ViewDesc.pScene        = pScene;
    ViewDesc.Camera        = Camera;
    ViewDesc.pRenderTarget = pTarget;

    RefCntAutoPtr<IRadientView> pView;
    EXPECT_EQ(Renderer.CreateView(ViewDesc, &pView), RADIENT_STATUS_OK);
    EXPECT_NE(pView, nullptr);
    if (pView != nullptr)
    {
        EXPECT_STREQ(pView->GetDesc().Name, ViewDesc.Name);
        EXPECT_EQ(pView->GetDesc().pScene, pScene);
        EXPECT_EQ(pView->GetDesc().Camera, Camera);
        EXPECT_EQ(pView->GetDesc().pRenderTarget, pTarget);
    }

    return pView;
}

RadientEntityID CreateTestRenderableEntity(IRadientSceneWriter&   Writer,
                                           IRadientMeshAsset*     pMesh,
                                           IRadientMaterialAsset* pMaterial)
{
    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(Writer.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_NE(Entity, InvalidRadientEntityID);

    if (Entity == InvalidRadientEntityID)
        return Entity;

    RadientMeshComponent MeshComponent{};
    MeshComponent.pMesh = pMesh;
    EXPECT_EQ(Writer.SetMesh(Entity, MeshComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(Writer.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);

    RadientMaterialBinding MaterialBinding{};
    MaterialBinding.PrimitiveIndex = 0;
    MaterialBinding.pMaterial      = pMaterial;

    RadientMaterialBindingsComponent MaterialBindings{};
    MaterialBindings.pBindings    = &MaterialBinding;
    MaterialBindings.BindingCount = 1;
    EXPECT_EQ(Writer.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);

    return Entity;
}

TEST(RadientSceneTest, Create)
{
    // Verifies direct scene implementation creation returns a valid interface.
    RefCntAutoPtr<IRadientScene> pScene = RadientSceneImpl::Create();
    EXPECT_NE(pScene, nullptr);
}

TEST(RadientEngineTest, CreateBackend)
{
    // The default engine should expose a local backend instance.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientBackend> pBackend = GetTestBackend(*pEngine);
    ASSERT_NE(pBackend, nullptr);
    EXPECT_EQ(pBackend->GetDesc().Type, RADIENT_BACKEND_TYPE_LOCAL);
}

TEST(RadientEngineTest, CreateAssetManager)
{
    // The engine owns an asset manager that can be queried through the public API.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    EXPECT_NE(pAssetManager, nullptr);
}

TEST(RadientEngineTest, CreateWithExternalThreadPool)
{
    // Passing an external thread pool should be accepted and used by engine
    // services such as the asset manager.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientEngineCreateInfo EngineCI{};
    EngineCI.pThreadPool = pThreadPool;

    RefCntAutoPtr<IRadientEngine> pEngine;
    EXPECT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    EXPECT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    EXPECT_NE(pAssetManager, nullptr);

    pThreadPool->StopThreads();
}

TEST(RadientAssetManagerTest, CreateMesh)
{
    // Creating a mesh should accept vertex/index buffers and primitive material
    // references, returning a usable asset reference.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(pMaterial, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateTestMesh(*pAssetManager, pMaterial);
    ASSERT_NE(pMesh, nullptr);
    EXPECT_NE(pMesh->GetReference().URI, nullptr);
    EXPECT_NE(pMesh->GetReference().Version, 0u);
}

TEST(RadientAssetManagerTest, CreateMeshDeduplicatesIdenticalRawData)
{
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(pMaterial, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh0 = CreateTestMesh(*pAssetManager, pMaterial);
    RefCntAutoPtr<IRadientMeshAsset> pMesh1 = CreateTestMesh(*pAssetManager, pMaterial);

    ASSERT_NE(pMesh0, nullptr);
    ASSERT_NE(pMesh1, nullptr);
    const RADIENT_STATUS MeshStatus0 = pAssetManager->WaitForAssetLoad(pMesh0);
    const RADIENT_STATUS MeshStatus1 = pAssetManager->WaitForAssetLoad(pMesh1);
    EXPECT_EQ(MeshStatus0, RADIENT_STATUS_OK);
    EXPECT_EQ(MeshStatus1, RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh0), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh1), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(pMesh0.RawPtr(), pMesh1.RawPtr());

    const MeshPayloadImpl* pMeshPayload0 = RadientMeshAssetManager::GetMeshPayload(pMesh0);
    ASSERT_NE(pMeshPayload0, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(pMesh1), pMeshPayload0);
}

TEST(RadientAssetManagerTest, CreateMeshDifferentRawDataUsesDifferentPayload)
{
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(pMaterial, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh0 = CreateTestMesh(*pAssetManager, pMaterial);
    RefCntAutoPtr<IRadientMeshAsset> pMesh1 = CreateTestMesh(*pAssetManager, pMaterial, 0.5f);

    ASSERT_NE(pMesh0, nullptr);
    ASSERT_NE(pMesh1, nullptr);
    const RADIENT_STATUS MeshStatus0 = pAssetManager->WaitForAssetLoad(pMesh0);
    const RADIENT_STATUS MeshStatus1 = pAssetManager->WaitForAssetLoad(pMesh1);
    EXPECT_EQ(MeshStatus0, RADIENT_STATUS_OK);
    EXPECT_EQ(MeshStatus1, RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh0), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(pMesh1), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(pMesh0.RawPtr(), pMesh1.RawPtr());

    const MeshPayloadImpl* pMeshPayload0 = RadientMeshAssetManager::GetMeshPayload(pMesh0);
    ASSERT_NE(pMeshPayload0, nullptr);
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMesh1), pMeshPayload0);
}

TEST(RadientAssetManagerTest, LoadScene)
{
    // LoadScene should reject missing input, then accept a valid URI and finish
    // through WaitForAssetLoad.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RadientSceneLoadInfo              SceneLoadInfo{};
    RefCntAutoPtr<IRadientSceneAsset> pGLTFModel;
    // A missing URI is invalid and should not create an asset reference.
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_EQ(pAssetManager->LoadScene(SceneLoadInfo, &pGLTFModel), RADIENT_STATUS_INVALID_ARGUMENT);
    }

    TempDirectory     TempDir{"RadientAssetManagerTest"};
    const std::string GLTFPath = WriteBasicGLTFFile(TempDir);

    SceneLoadInfo.URI               = GLTFPath.c_str();
    const RADIENT_STATUS LoadStatus = pAssetManager->LoadScene(SceneLoadInfo, &pGLTFModel);
    // Depending on threading, the load may complete immediately or remain pending.
    EXPECT_TRUE(LoadStatus == RADIENT_STATUS_OK || LoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pGLTFModel, nullptr);
    EXPECT_NE(pGLTFModel->GetReference().URI, nullptr);
    EXPECT_STREQ(pGLTFModel->GetReference().URI, SceneLoadInfo.URI);
    EXPECT_NE(pGLTFModel->GetReference().Version, 0u);
    EXPECT_EQ(ProcessTestGLTFLoad(*pAssetManager, pGLTFModel), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientAssetManagerImpl::GetSceneGPUResourceStatus(pGLTFModel), RADIENT_STATUS_NO_GPU_DATA);
}

TEST(RadientAssetManagerTest, RejectsLoadsWithoutThreadPool)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientSceneLoadInfo SceneLoadInfo{};
    SceneLoadInfo.URI = "no_thread_pool.gltf";

    RefCntAutoPtr<IRadientSceneAsset> pGLTFModel;
    EXPECT_EQ(pAssetManager->LoadScene(SceneLoadInfo, &pGLTFModel), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pGLTFModel, nullptr);

    std::array<Uint8, 4> TextureData{1, 2, 3, 4};
    TextureReleaseState  ReleaseState;

    RadientTextureLoadInfo TextureLoadInfo{};
    TextureLoadInfo.pData                = TextureData.data();
    TextureLoadInfo.DataSize             = static_cast<Uint64>(TextureData.size());
    TextureLoadInfo.ReleaseData          = ReleaseTextureData;
    TextureLoadInfo.pReleaseDataUserData = &ReleaseState;

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pAssetManager->LoadTexture(TextureLoadInfo, &pTexture), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pTexture, nullptr);
    EXPECT_EQ(ReleaseState.Count, 1u);
    EXPECT_EQ(ReleaseState.pData, TextureData.data());
    EXPECT_EQ(ReleaseState.DataSize, TextureData.size());
}

TEST(RadientAssetManagerTest, MethodsFailAfterStop)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientEngineCreateInfo EngineCI{};
    EngineCI.pThreadPool = pThreadPool;

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    EXPECT_EQ(pAssetManager->Stop(nullptr), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientMeshAsset>     pMesh;
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    RefCntAutoPtr<IRadientTextureAsset>  pTexture;
    RefCntAutoPtr<IRadientSceneAsset>    pScene;

    static constexpr RadientMaterialCreateInfo MaterialCI{};
    EXPECT_EQ(pAssetManager->CreateMaterial(MaterialCI, pMaterial.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMaterial, nullptr);

    EXPECT_EQ(pAssetManager->CreateMesh(RadientMeshCreateInfo{}, pMesh.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pMesh, nullptr);

    static constexpr std::array<Uint8, 4> TextureData = {1, 2, 3, 4};
    TextureReleaseState                   ReleaseState;
    RadientTextureLoadInfo                TextureLoadInfo;
    TextureLoadInfo.pData                = TextureData.data();
    TextureLoadInfo.DataSize             = static_cast<Uint64>(TextureData.size());
    TextureLoadInfo.ReleaseData          = ReleaseTextureData;
    TextureLoadInfo.pReleaseDataUserData = &ReleaseState;
    EXPECT_EQ(pAssetManager->LoadTexture(TextureLoadInfo, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pTexture, nullptr);
    EXPECT_EQ(ReleaseState.Count, 1u);
    EXPECT_EQ(ReleaseState.pData, TextureData.data());
    EXPECT_EQ(ReleaseState.DataSize, TextureData.size());

    const RadientSceneLoadInfo SceneLoadInfo{"test://scene_after_stop.gltf"};
    EXPECT_EQ(pAssetManager->LoadScene(SceneLoadInfo, pScene.GetAddressOfEmpty()), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pScene, nullptr);

    pThreadPool->StopThreads();
}

TEST(RadientAssetManagerTest, DeduplicatesGLTFLoads)
{
    // Loading two aliases of the same glTF should create distinct lightweight
    // asset handles that resolve to the same cached scene payload.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    TempDirectory     TempDir{"RadientAssetManagerTest"};
    const std::string GLTFPath = WriteBasicGLTFFile(TempDir);

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    const size_t FileNamePos = GLTFPath.find_last_of("/\\");
    ASSERT_NE(FileNamePos, std::string::npos);
    const std::string GLTFAliasPath =
        GLTFPath.substr(0, FileNamePos + 1) + "unused/../" + GLTFPath.substr(FileNamePos + 1);

    RadientSceneLoadInfo AliasLoadInfo = LoadInfo;
    AliasLoadInfo.URI                  = GLTFAliasPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pFirstModel;
    const RADIENT_STATUS              FirstLoadStatus = pAssetManager->LoadScene(LoadInfo, &pFirstModel);
    ASSERT_TRUE(FirstLoadStatus == RADIENT_STATUS_OK || FirstLoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pFirstModel, nullptr);
    ASSERT_EQ(ProcessTestGLTFLoad(*pAssetManager, pFirstModel), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientAssetManagerImpl::GetSceneGPUResourceStatus(pFirstModel), RADIENT_STATUS_NO_GPU_DATA);

    RefCntAutoPtr<IRadientSceneAsset> pSecondModel;
    const RADIENT_STATUS              SecondLoadStatus = pAssetManager->LoadScene(AliasLoadInfo, &pSecondModel);
    EXPECT_TRUE(SecondLoadStatus == RADIENT_STATUS_OK || SecondLoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pSecondModel, nullptr);
    ASSERT_EQ(ProcessTestGLTFLoad(*pAssetManager, pSecondModel), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientAssetManagerImpl::GetSceneGPUResourceStatus(pSecondModel), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(pSecondModel.RawPtr(), pFirstModel.RawPtr());
    ASSERT_NE(pSecondModel->GetReference().URI, nullptr);
    ASSERT_NE(pFirstModel->GetReference().URI, nullptr);
    EXPECT_STREQ(pFirstModel->GetReference().URI, LoadInfo.URI);
    EXPECT_STREQ(pSecondModel->GetReference().URI, AliasLoadInfo.URI);
    EXPECT_STRNE(pSecondModel->GetReference().URI, pFirstModel->GetReference().URI);
    EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pSecondModel), RadientAssetManagerImpl::GetImportedScene(pFirstModel));
}

TEST(RadientAssetManagerTest, TextureWithSourceURIKeepsSourceURI)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientEngineCreateInfo EngineCI{};
    EngineCI.pThreadPool = pThreadPool;

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    std::array<Uint8, TransparentPng.size()> TextureData = TransparentPng;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.URI      = "Textures/TestAlbedo.png";
    LoadInfo.pData    = TextureData.data();
    LoadInfo.DataSize = static_cast<Uint64>(TextureData.size());
    LoadInfo.IsSRGB   = True;

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pAssetManager->LoadTexture(LoadInfo, &pTexture), RADIENT_STATUS_PENDING);
    ASSERT_NE(pTexture, nullptr);
    ASSERT_NE(pTexture->GetReference().URI, nullptr);
    EXPECT_STREQ(pTexture->GetReference().URI, LoadInfo.URI);

    while (pThreadPool->GetQueueSize() != 0)
    {
        pThreadPool->ProcessTask(0, false);
    }
}

TEST(RadientAssetManagerTest, DeduplicatesPendingTextureLoads)
{
    // Duplicate texture requests should create distinct lightweight assets that
    // resolve to the same cached payload when the workers run.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientEngineCreateInfo EngineCI{};
    EngineCI.pThreadPool = pThreadPool;

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    std::array<Uint8, TransparentPng.size()> TextureData = TransparentPng;

    RadientTextureLoadInfo LoadInfo{};
    LoadInfo.pData    = TextureData.data();
    LoadInfo.DataSize = static_cast<Uint64>(TextureData.size());
    LoadInfo.IsSRGB   = True;

    RefCntAutoPtr<IRadientTextureAsset> pFirstTexture;
    EXPECT_EQ(pAssetManager->LoadTexture(LoadInfo, &pFirstTexture), RADIENT_STATUS_PENDING);
    ASSERT_NE(pFirstTexture, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pSecondTexture;
    EXPECT_EQ(pAssetManager->LoadTexture(LoadInfo, &pSecondTexture), RADIENT_STATUS_PENDING);
    ASSERT_NE(pSecondTexture, nullptr);
    EXPECT_NE(pSecondTexture.RawPtr(), pFirstTexture.RawPtr());
    ASSERT_NE(pSecondTexture->GetReference().URI, nullptr);
    ASSERT_NE(pFirstTexture->GetReference().URI, nullptr);
    EXPECT_STRNE(pSecondTexture->GetReference().URI, pFirstTexture->GetReference().URI);

    std::array<Uint8, TransparentPng.size()> DuplicateTextureData = TransparentPng;
    TextureReleaseState                      CacheHitRelease;

    RadientTextureLoadInfo OwnedLoadInfo = LoadInfo;
    OwnedLoadInfo.pData                  = DuplicateTextureData.data();
    OwnedLoadInfo.ReleaseData            = ReleaseTextureData;
    OwnedLoadInfo.pReleaseDataUserData   = &CacheHitRelease;

    RefCntAutoPtr<IRadientTextureAsset> pOwnedDuplicateTexture;
    EXPECT_EQ(pAssetManager->LoadTexture(OwnedLoadInfo, &pOwnedDuplicateTexture), RADIENT_STATUS_PENDING);
    ASSERT_NE(pOwnedDuplicateTexture, nullptr);
    EXPECT_NE(pOwnedDuplicateTexture.RawPtr(), pFirstTexture.RawPtr());
    ASSERT_NE(pOwnedDuplicateTexture->GetReference().URI, nullptr);
    EXPECT_STRNE(pOwnedDuplicateTexture->GetReference().URI, pFirstTexture->GetReference().URI);

    RadientTextureLoadInfo LinearLoadInfo = LoadInfo;
    LinearLoadInfo.IsSRGB                 = False;

    RefCntAutoPtr<IRadientTextureAsset> pLinearTexture;
    EXPECT_EQ(pAssetManager->LoadTexture(LinearLoadInfo, &pLinearTexture), RADIENT_STATUS_PENDING);
    ASSERT_NE(pLinearTexture, nullptr);
    EXPECT_NE(pLinearTexture.RawPtr(), pFirstTexture.RawPtr());

    // The async path must own a copy before the worker runs when no release
    // callback is provided.
    TextureData.fill(0);

    while (pThreadPool->GetQueueSize() != 0)
    {
        pThreadPool->ProcessTask(0, false);
    }

    const TexturePayloadImpl* pFirstPayload = RadientTextureAssetManager::GetTexturePayload(pFirstTexture);
    ASSERT_NE(pFirstPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pSecondTexture), pFirstPayload);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(pOwnedDuplicateTexture), pFirstPayload);
    EXPECT_NE(RadientTextureAssetManager::GetTexturePayload(pLinearTexture), pFirstPayload);

    EXPECT_EQ(CacheHitRelease.Count, 1u);
    EXPECT_EQ(CacheHitRelease.pData, DuplicateTextureData.data());
    EXPECT_EQ(CacheHitRelease.DataSize, DuplicateTextureData.size());

    pThreadPool->StopThreads();
}

TEST(RadientEngineTest, CreateScene)
{
    // Scene creation should preserve the public scene description.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    EXPECT_NE(pScene, nullptr);
}

TEST(RadientEngineTest, CreateSceneWriter)
{
    // A writer can be created for an existing scene and used for mutations.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = CreateTestSceneWriter(*pEngine, pScene);
    EXPECT_NE(pWriter, nullptr);
}

TEST(RadientEngineTest, CreateSceneImporter)
{
    // A scene importer can be created on top of a scene writer.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = CreateTestSceneWriter(*pEngine, pScene);
    ASSERT_NE(pWriter, nullptr);

    RefCntAutoPtr<IRadientSceneImporter> pImporter = CreateTestSceneImporter(*pEngine, pWriter);
    EXPECT_NE(pImporter, nullptr);
}

TEST(RadientSceneImporterTest, ImportScene)
{
    // Public ImportScene should reject invalid load info and then import a
    // simple valid glTF into a live scene root.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = CreateTestSceneWriter(*pEngine, pScene);
    ASSERT_NE(pWriter, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientSceneImporter> pImporter = CreateTestSceneImporter(*pEngine, pWriter);
    ASSERT_NE(pImporter, nullptr);

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Radient imported GLTF root";

    RefCntAutoPtr<IRadientSceneAsset> ImportedModel;
    RadientEntityID                   ImportedRoot = InvalidRadientEntityID;
    // Empty load info is invalid and should not instantiate anything.
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_EQ(pImporter->ImportScene({}, InstantiateInfo, &ImportedModel, ImportedRoot), RADIENT_STATUS_INVALID_ARGUMENT);
    }

    TempDirectory     ImportTempDir{"RadientSceneTest"};
    const std::string ImportScenePath = WriteBasicGLTFFile(ImportTempDir);

    RadientSceneLoadInfo SceneLoadInfo{};
    SceneLoadInfo.URI = ImportScenePath.c_str();

    RADIENT_STATUS ImportStatus = pImporter->ImportScene(SceneLoadInfo, InstantiateInfo, &ImportedModel, ImportedRoot);
    if (ImportStatus == RADIENT_STATUS_PENDING)
    {
        // Pending loads are completed explicitly in tests so the imported root
        // can be verified deterministically.
        ASSERT_EQ(ProcessTestGLTFLoad(*pAssetManager, ImportedModel), RADIENT_STATUS_OK);
        ImportStatus = pImporter->ProcessPendingImports();
    }
    EXPECT_EQ(ImportStatus, RADIENT_STATUS_OK);
    ASSERT_NE(ImportedModel, nullptr);
    EXPECT_NE(ImportedModel->GetReference().URI, nullptr);
    EXPECT_NE(ImportedModel->GetReference().Version, 0u);
    EXPECT_NE(ImportedRoot, InvalidRadientEntityID);
    EXPECT_EQ(pScene->IsEntityAlive(ImportedRoot), RADIENT_STATUS_OK);
}

TEST(RadientSceneWriterTest, CreateRenderableEntity)
{
    // A renderable scene entity requires mesh, mesh-renderer, and material
    // binding data to be accepted by the writer.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(pMaterial, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateTestMesh(*pAssetManager, pMaterial);
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = CreateTestSceneWriter(*pEngine, pScene);
    ASSERT_NE(pWriter, nullptr);

    const RadientEntityID Entity = CreateTestRenderableEntity(*pWriter, pMesh, pMaterial);
    EXPECT_NE(Entity, InvalidRadientEntityID);
}

TEST(RadientEngineTest, CreateRenderer)
{
    // Renderer creation should preserve the public renderer description.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientRenderer> pRenderer = CreateTestRenderer(*pEngine);
    EXPECT_NE(pRenderer, nullptr);
}

TEST(RadientRendererTest, CreateRenderTarget)
{
    // Render targets should preserve name and size metadata after creation.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientRenderer> pRenderer = CreateTestRenderer(*pEngine);
    ASSERT_NE(pRenderer, nullptr);

    RefCntAutoPtr<IRadientRenderTarget> pTarget = CreateTestRenderTarget(*pRenderer);
    EXPECT_NE(pTarget, nullptr);
}

TEST(RadientRendererTest, CreateView)
{
    // Views keep persistent scene/camera/target state that can be reused by
    // subsequent render calls.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientRenderer> pRenderer = CreateTestRenderer(*pEngine);
    ASSERT_NE(pRenderer, nullptr);

    RefCntAutoPtr<IRadientRenderTarget> pTarget = CreateTestRenderTarget(*pRenderer);
    ASSERT_NE(pTarget, nullptr);

    RefCntAutoPtr<IRadientView> pView = CreateTestView(*pRenderer, pScene, pTarget);
    ASSERT_NE(pView, nullptr);

    // View setters update the descriptor returned to callers.
    EXPECT_EQ(pView->SetCamera(42), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().Camera, 42);

    EXPECT_EQ(pView->SetScene(nullptr), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().pScene, nullptr);

    EXPECT_EQ(pView->SetRenderTarget(nullptr), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().pRenderTarget, nullptr);

    // Environment settings are view-local and retain the referenced texture asset.
    const RadientEnvironmentDesc& DefaultEnvironment = pView->GetDesc().Environment;
    const RadientFloat3           DefaultColor{1.f, 1.f, 1.f};
    EXPECT_EQ(DefaultEnvironment.pEnvironmentMap, nullptr);
    EXPECT_EQ(DefaultEnvironment.Color, DefaultColor);
    EXPECT_EQ(DefaultEnvironment.Intensity, 1.f);
    EXPECT_EQ(DefaultEnvironment.Exposure, 0.f);

    RefCntAutoPtr<IRadientTextureAsset> pEnvironmentMap = MakeTestTextureAsset("texture://environment", 7);

    RadientEnvironmentDesc Environment{};
    Environment.pEnvironmentMap = pEnvironmentMap;
    Environment.Color           = {0.25f, 0.5f, 1.f};
    Environment.Intensity       = 2.f;
    Environment.Exposure        = -1.f;

    EXPECT_EQ(pView->SetEnvironment(Environment), RADIENT_STATUS_OK);

    const IRadientTextureAsset* pExpectedEnvironmentMap = pEnvironmentMap;
    pEnvironmentMap.Release();

    const RadientEnvironmentDesc& StoredEnvironment = pView->GetDesc().Environment;
    ASSERT_NE(StoredEnvironment.pEnvironmentMap, nullptr);
    EXPECT_EQ(StoredEnvironment.pEnvironmentMap, pExpectedEnvironmentMap);
    EXPECT_STREQ(StoredEnvironment.pEnvironmentMap->GetReference().URI, "texture://environment");
    EXPECT_EQ(StoredEnvironment.pEnvironmentMap->GetReference().Version, 7u);
    EXPECT_EQ(StoredEnvironment.Color, Environment.Color);
    EXPECT_EQ(StoredEnvironment.Intensity, Environment.Intensity);
    EXPECT_EQ(StoredEnvironment.Exposure, Environment.Exposure);
    EXPECT_EQ(pView->SetEnvironment(StoredEnvironment), RADIENT_STATUS_NO_CHANGE);

    // Skybox settings are view-local and copy raw texture URI strings.
    std::string SkyboxURI = "texture://skybox";

    RefCntAutoPtr<IRadientTextureAsset> pSkyboxTexture = MakeTestTextureAsset(SkyboxURI.c_str(), 3);

    RadientSkyboxDesc Skybox{};
    Skybox.Source    = RADIENT_SKYBOX_SOURCE_TEXTURE;
    Skybox.pTexture  = pSkyboxTexture;
    Skybox.Color     = {0.25f, 0.5f, 1.f};
    Skybox.Intensity = 2.f;
    Skybox.Exposure  = -1.f;
    Skybox.MipLevel  = 1.f;

    EXPECT_EQ(pView->SetSkybox(Skybox), RADIENT_STATUS_OK);
    SkyboxURI[0] = 'X';

    const RadientSkyboxDesc& StoredSkybox = pView->GetDesc().Skybox;
    EXPECT_EQ(StoredSkybox.Source, RADIENT_SKYBOX_SOURCE_TEXTURE);
    ASSERT_NE(StoredSkybox.pTexture, nullptr);
    EXPECT_STREQ(StoredSkybox.pTexture->GetReference().URI, "texture://skybox");
    EXPECT_EQ(StoredSkybox.pTexture->GetReference().Version, 3u);
    EXPECT_EQ(StoredSkybox.Color.x, 0.25f);
    EXPECT_EQ(StoredSkybox.Color.y, 0.5f);
    EXPECT_EQ(StoredSkybox.Color.z, 1.f);
    EXPECT_EQ(StoredSkybox.Intensity, 2.f);
    EXPECT_EQ(StoredSkybox.Exposure, -1.f);
    EXPECT_EQ(StoredSkybox.MipLevel, 1.f);

    EXPECT_EQ(pView->SetSkybox(StoredSkybox), RADIENT_STATUS_NO_CHANGE);

    // Presentation settings are stored by the view independently of the
    // render technique that will consume them.
    const RadientToneMappingDesc& DefaultToneMapping = pView->GetDesc().ToneMapping;
    EXPECT_EQ(DefaultToneMapping.Mode, RADIENT_TONE_MAPPING_MODE_UNCHARTED2);
    EXPECT_EQ(DefaultToneMapping.AutoExposure, True);
    EXPECT_EQ(DefaultToneMapping.MiddleGray, 0.18f);
    EXPECT_EQ(DefaultToneMapping.LightAdaptation, True);
    EXPECT_EQ(DefaultToneMapping.WhitePoint, 3.f);
    EXPECT_EQ(DefaultToneMapping.LuminanceSaturation, 1.f);

    RadientToneMappingDesc ToneMapping{};
    ToneMapping.Mode            = RADIENT_TONE_MAPPING_MODE_AGX_CUSTOM;
    ToneMapping.AutoExposure    = False;
    ToneMapping.MiddleGray      = 0.2f;
    ToneMapping.LightAdaptation = False;
    ToneMapping.WhitePoint      = 4.f;
    ToneMapping.AgX.Saturation  = 1.2f;

    EXPECT_EQ(pView->SetToneMapping(ToneMapping), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().ToneMapping, ToneMapping);
    EXPECT_EQ(pView->SetToneMapping(ToneMapping), RADIENT_STATUS_NO_CHANGE);

    const RadientBloomDesc& DefaultBloom = pView->GetDesc().Bloom;
    EXPECT_EQ(DefaultBloom.Enabled, False);
    EXPECT_EQ(DefaultBloom.Intensity, 0.15f);
    EXPECT_EQ(DefaultBloom.Threshold, 1.f);
    EXPECT_EQ(DefaultBloom.SoftThreshold, 0.125f);
    EXPECT_EQ(DefaultBloom.Radius, 0.75f);

    RadientBloomDesc Bloom{};
    Bloom.Enabled       = True;
    Bloom.Intensity     = 0.25f;
    Bloom.Threshold     = 2.f;
    Bloom.SoftThreshold = 0.25f;
    Bloom.Radius        = 0.8f;

    EXPECT_EQ(pView->SetBloom(Bloom), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().Bloom, Bloom);
    EXPECT_EQ(pView->SetBloom(Bloom), RADIENT_STATUS_NO_CHANGE);

    const RadientTemporalAntiAliasingDesc& DefaultTemporalAntiAliasing = pView->GetDesc().TemporalAntiAliasing;
    EXPECT_EQ(DefaultTemporalAntiAliasing.Enabled, False);
    EXPECT_EQ(DefaultTemporalAntiAliasing.TemporalStabilityFactor, 0.9375f);

    RadientTemporalAntiAliasingDesc TemporalAntiAliasing{};
    TemporalAntiAliasing.Enabled                 = True;
    TemporalAntiAliasing.TemporalStabilityFactor = 0.9f;

    EXPECT_EQ(pView->SetTemporalAntiAliasing(TemporalAntiAliasing), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().TemporalAntiAliasing, TemporalAntiAliasing);
    EXPECT_EQ(pView->SetTemporalAntiAliasing(TemporalAntiAliasing), RADIENT_STATUS_NO_CHANGE);

    const RadientSSAODesc& DefaultSSAO = pView->GetDesc().SSAO;
    EXPECT_EQ(DefaultSSAO.Enabled, False);
    EXPECT_EQ(DefaultSSAO.Algorithm, RADIENT_SSAO_ALGORITHM_GTAO);
    EXPECT_EQ(DefaultSSAO.EffectRadius, 1.f);
    EXPECT_EQ(DefaultSSAO.EffectFalloffRange, 0.615f);
    EXPECT_EQ(DefaultSSAO.RadiusMultiplier, 1.457f);
    EXPECT_EQ(DefaultSSAO.DepthMIPSamplingOffset, 3.3f);
    EXPECT_EQ(DefaultSSAO.TemporalStabilityFactor, 0.9f);
    EXPECT_EQ(DefaultSSAO.SpatialReconstructionRadius, 4.f);
    EXPECT_EQ(DefaultSSAO.BitmaskThickness, 0.5f);

    RadientSSAODesc SSAO{};
    SSAO.Enabled                     = True;
    SSAO.Algorithm                   = RADIENT_SSAO_ALGORITHM_VBAO;
    SSAO.EffectRadius                = 2.f;
    SSAO.EffectFalloffRange          = 0.5f;
    SSAO.RadiusMultiplier            = 1.2f;
    SSAO.DepthMIPSamplingOffset      = 2.5f;
    SSAO.TemporalStabilityFactor     = 0.8f;
    SSAO.SpatialReconstructionRadius = 5.f;
    SSAO.BitmaskThickness            = 0.75f;

    EXPECT_EQ(pView->SetSSAO(SSAO), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().SSAO, SSAO);
    EXPECT_EQ(pView->SetSSAO(SSAO), RADIENT_STATUS_NO_CHANGE);

    const RadientSSRDesc& DefaultSSR = pView->GetDesc().SSR;
    EXPECT_EQ(DefaultSSR.Enabled, False);
    EXPECT_EQ(DefaultSSR.DepthBufferThickness, 0.025f);
    EXPECT_EQ(DefaultSSR.RoughnessThreshold, 0.2f);
    EXPECT_EQ(DefaultSSR.MostDetailedMip, 0u);
    EXPECT_EQ(DefaultSSR.MaxTraversalIntersections, 128u);
    EXPECT_EQ(DefaultSSR.GGXImportanceSampleBias, 0.3f);
    EXPECT_EQ(DefaultSSR.SpatialReconstructionRadius, 4.f);
    EXPECT_EQ(DefaultSSR.TemporalRadianceStabilityFactor, 1.f);
    EXPECT_EQ(DefaultSSR.TemporalVarianceStabilityFactor, 0.9f);
    EXPECT_EQ(DefaultSSR.BilateralCleanupSpatialSigmaFactor, 0.9f);

    RadientSSRDesc SSR{};
    SSR.Enabled                            = True;
    SSR.DepthBufferThickness               = 0.05f;
    SSR.RoughnessThreshold                 = 0.4f;
    SSR.MostDetailedMip                    = 1;
    SSR.MaxTraversalIntersections          = 64;
    SSR.GGXImportanceSampleBias            = 0.2f;
    SSR.SpatialReconstructionRadius        = 5.f;
    SSR.TemporalRadianceStabilityFactor    = 0.8f;
    SSR.TemporalVarianceStabilityFactor    = 0.7f;
    SSR.BilateralCleanupSpatialSigmaFactor = 1.2f;

    EXPECT_EQ(pView->SetSSR(SSR), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().SSR, SSR);
    EXPECT_EQ(pView->SetSSR(SSR), RADIENT_STATUS_NO_CHANGE);

    const RadientDepthOfFieldDesc& DefaultDepthOfField = pView->GetDesc().DepthOfField;
    EXPECT_EQ(DefaultDepthOfField.Enabled, False);
    EXPECT_EQ(DefaultDepthOfField.MaxCircleOfConfusion, 0.01f);
    EXPECT_EQ(DefaultDepthOfField.TemporalStabilityFactor, 0.9375f);
    EXPECT_EQ(DefaultDepthOfField.BokehKernelRingCount, 5u);
    EXPECT_EQ(DefaultDepthOfField.BokehKernelRingDensity, 7u);
    EXPECT_EQ(DefaultDepthOfField.TemporalSmoothing, True);
    EXPECT_EQ(DefaultDepthOfField.KarisInverse, True);

    RadientDepthOfFieldDesc DepthOfField{};
    DepthOfField.Enabled                 = True;
    DepthOfField.MaxCircleOfConfusion    = 0.015f;
    DepthOfField.TemporalStabilityFactor = 0.8f;
    DepthOfField.BokehKernelRingCount    = 4;
    DepthOfField.BokehKernelRingDensity  = 6;
    DepthOfField.TemporalSmoothing       = False;
    DepthOfField.KarisInverse            = False;

    EXPECT_EQ(pView->SetDepthOfField(DepthOfField), RADIENT_STATUS_OK);
    EXPECT_EQ(pView->GetDesc().DepthOfField, DepthOfField);
    EXPECT_EQ(pView->SetDepthOfField(DepthOfField), RADIENT_STATUS_NO_CHANGE);
}

TEST(RadientRendererTest, RejectsInvalidViewEnvironment)
{
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientRenderer> pRenderer = CreateTestRenderer(*pEngine);
    ASSERT_NE(pRenderer, nullptr);

    RadientViewDesc InvalidViewDesc{};
    InvalidViewDesc.Environment.Intensity = -1.f;

    RefCntAutoPtr<IRadientView> pView;
    EXPECT_EQ(pRenderer->CreateView(InvalidViewDesc, &pView), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pView, nullptr);

    RadientViewDesc ViewDesc{};
    ASSERT_EQ(pRenderer->CreateView(ViewDesc, &pView), RADIENT_STATUS_OK);
    ASSERT_NE(pView, nullptr);

    const RadientEnvironmentDesc StoredEnvironment        = pView->GetDesc().Environment;
    const auto                   ExpectInvalidEnvironment = [&](RadientEnvironmentDesc Environment) {
        EXPECT_EQ(pView->SetEnvironment(Environment), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pView->GetDesc().Environment, StoredEnvironment);
    };

    RadientEnvironmentDesc InvalidEnvironment = StoredEnvironment;
    InvalidEnvironment.Color.x                = -0.1f;
    ExpectInvalidEnvironment(InvalidEnvironment);

    InvalidEnvironment         = StoredEnvironment;
    InvalidEnvironment.Color.y = std::numeric_limits<Float32>::infinity();
    ExpectInvalidEnvironment(InvalidEnvironment);

    InvalidEnvironment           = StoredEnvironment;
    InvalidEnvironment.Intensity = -1.f;
    ExpectInvalidEnvironment(InvalidEnvironment);

    InvalidEnvironment           = StoredEnvironment;
    InvalidEnvironment.Intensity = std::numeric_limits<Float32>::infinity();
    ExpectInvalidEnvironment(InvalidEnvironment);

    InvalidEnvironment          = StoredEnvironment;
    InvalidEnvironment.Exposure = std::numeric_limits<Float32>::quiet_NaN();
    ExpectInvalidEnvironment(InvalidEnvironment);
}

TEST(RadientRendererTest, RejectsInvalidViewPresentationSettings)
{
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientRenderer> pRenderer = CreateTestRenderer(*pEngine);
    ASSERT_NE(pRenderer, nullptr);

    RadientViewDesc InvalidViewDesc{};
    InvalidViewDesc.Bloom.Radius = 2.f;

    RefCntAutoPtr<IRadientView> pView;
    EXPECT_EQ(pRenderer->CreateView(InvalidViewDesc, &pView), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pView, nullptr);

    InvalidViewDesc                                              = {};
    InvalidViewDesc.TemporalAntiAliasing.TemporalStabilityFactor = -0.1f;
    EXPECT_EQ(pRenderer->CreateView(InvalidViewDesc, &pView), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pView, nullptr);

    InvalidViewDesc                = {};
    InvalidViewDesc.SSAO.Algorithm = RADIENT_SSAO_ALGORITHM_COUNT;
    EXPECT_EQ(pRenderer->CreateView(InvalidViewDesc, &pView), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pView, nullptr);

    InvalidViewDesc                     = {};
    InvalidViewDesc.SSR.MostDetailedMip = 7;
    EXPECT_EQ(pRenderer->CreateView(InvalidViewDesc, &pView), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pView, nullptr);

    InvalidViewDesc                                   = {};
    InvalidViewDesc.DepthOfField.BokehKernelRingCount = 1;
    EXPECT_EQ(pRenderer->CreateView(InvalidViewDesc, &pView), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pView, nullptr);

    RadientViewDesc ViewDesc{};
    ASSERT_EQ(pRenderer->CreateView(ViewDesc, &pView), RADIENT_STATUS_OK);
    ASSERT_NE(pView, nullptr);

    const RadientToneMappingDesc StoredToneMapping        = pView->GetDesc().ToneMapping;
    const auto                   ExpectInvalidToneMapping = [&](RadientToneMappingDesc ToneMapping) {
        EXPECT_EQ(pView->SetToneMapping(ToneMapping), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pView->GetDesc().ToneMapping, StoredToneMapping);
    };

    RadientToneMappingDesc InvalidToneMapping = StoredToneMapping;
    InvalidToneMapping.Mode                   = RADIENT_TONE_MAPPING_MODE_COUNT;
    ExpectInvalidToneMapping(InvalidToneMapping);

    InvalidToneMapping            = StoredToneMapping;
    InvalidToneMapping.MiddleGray = 0.f;
    ExpectInvalidToneMapping(InvalidToneMapping);

    const RadientBloomDesc StoredBloom        = pView->GetDesc().Bloom;
    const auto             ExpectInvalidBloom = [&](RadientBloomDesc Bloom) {
        EXPECT_EQ(pView->SetBloom(Bloom), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pView->GetDesc().Bloom, StoredBloom);
    };

    RadientBloomDesc InvalidBloom = StoredBloom;
    InvalidBloom.Intensity        = std::numeric_limits<Float32>::quiet_NaN();
    ExpectInvalidBloom(InvalidBloom);

    InvalidBloom               = StoredBloom;
    InvalidBloom.SoftThreshold = 1.1f;
    ExpectInvalidBloom(InvalidBloom);

    const RadientTemporalAntiAliasingDesc StoredTemporalAntiAliasing  = pView->GetDesc().TemporalAntiAliasing;
    RadientTemporalAntiAliasingDesc       InvalidTemporalAntiAliasing = StoredTemporalAntiAliasing;
    InvalidTemporalAntiAliasing.TemporalStabilityFactor               = 1.1f;

    EXPECT_EQ(pView->SetTemporalAntiAliasing(InvalidTemporalAntiAliasing), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pView->GetDesc().TemporalAntiAliasing, StoredTemporalAntiAliasing);

    const RadientSSAODesc StoredSSAO        = pView->GetDesc().SSAO;
    const auto            ExpectInvalidSSAO = [&](RadientSSAODesc SSAO) {
        EXPECT_EQ(pView->SetSSAO(SSAO), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pView->GetDesc().SSAO, StoredSSAO);
    };

    RadientSSAODesc InvalidSSAO = StoredSSAO;
    InvalidSSAO.EffectRadius    = std::numeric_limits<Float32>::quiet_NaN();
    ExpectInvalidSSAO(InvalidSSAO);

    InvalidSSAO                         = StoredSSAO;
    InvalidSSAO.TemporalStabilityFactor = 1.1f;
    ExpectInvalidSSAO(InvalidSSAO);

    const RadientSSRDesc StoredSSR        = pView->GetDesc().SSR;
    const auto           ExpectInvalidSSR = [&](RadientSSRDesc SSR) {
        EXPECT_EQ(pView->SetSSR(SSR), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pView->GetDesc().SSR, StoredSSR);
    };

    RadientSSRDesc InvalidSSR     = StoredSSR;
    InvalidSSR.RoughnessThreshold = -0.1f;
    ExpectInvalidSSR(InvalidSSR);

    InvalidSSR                           = StoredSSR;
    InvalidSSR.MaxTraversalIntersections = 0;
    ExpectInvalidSSR(InvalidSSR);

    const RadientDepthOfFieldDesc StoredDepthOfField        = pView->GetDesc().DepthOfField;
    const auto                    ExpectInvalidDepthOfField = [&](RadientDepthOfFieldDesc DepthOfField) {
        EXPECT_EQ(pView->SetDepthOfField(DepthOfField), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pView->GetDesc().DepthOfField, StoredDepthOfField);
    };

    RadientDepthOfFieldDesc InvalidDepthOfField = StoredDepthOfField;
    InvalidDepthOfField.MaxCircleOfConfusion    = 0.f;
    ExpectInvalidDepthOfField(InvalidDepthOfField);

    InvalidDepthOfField                         = StoredDepthOfField;
    InvalidDepthOfField.TemporalStabilityFactor = 1.1f;
    ExpectInvalidDepthOfField(InvalidDepthOfField);

    InvalidDepthOfField                        = StoredDepthOfField;
    InvalidDepthOfField.BokehKernelRingDensity = 8;
    ExpectInvalidDepthOfField(InvalidDepthOfField);
}

TEST(RadientRendererTest, RenderHeadlessScene)
{
    // Builds a minimal material, mesh, scene, renderer, and target to verify
    // the headless render path can execute without a swap chain.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(pMaterial, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateTestMesh(*pAssetManager, pMaterial);
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = CreateTestSceneWriter(*pEngine, pScene);
    ASSERT_NE(pWriter, nullptr);

    const RadientEntityID Entity = CreateTestRenderableEntity(*pWriter, pMesh, pMaterial);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    RefCntAutoPtr<IRadientRenderer> pRenderer = CreateTestRenderer(*pEngine);
    ASSERT_NE(pRenderer, nullptr);

    RefCntAutoPtr<IRadientRenderTarget> pTarget = CreateTestRenderTarget(*pRenderer);
    ASSERT_NE(pTarget, nullptr);

    RefCntAutoPtr<IRadientView> pView = CreateTestView(*pRenderer, pScene, pTarget);
    ASSERT_NE(pView, nullptr);

    RadientRenderAttribs RenderAttribs{};
    RenderAttribs.pView = pView;

    // The test only checks successful submission; image correctness belongs to
    // renderer-specific tests.
    EXPECT_EQ(pRenderer->Render(RenderAttribs), RADIENT_STATUS_OK);
}

} // namespace
