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

static constexpr std::array<Uint8, 67> TransparentPng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
    0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82};

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

TEST(RadientAssetManagerTest, CreateMaterial)
{
    // Creating a material should allocate a stable asset reference and build the
    // internal GLTF material representation used by the renderer.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientTextureAsset> pBaseColorTexture = MakeTestTextureAsset("texture://base-color", 1);
    RefCntAutoPtr<IRadientTextureAsset> pNormalTexture    = MakeTestTextureAsset("texture://normal", 2);

    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.Name              = "Radient test material";
    MaterialCI.BaseColorFactor   = {0.25f, 0.5f, 0.75f, 0.8f};
    MaterialCI.MetallicFactor    = 0.2f;
    MaterialCI.RoughnessFactor   = 0.7f;
    MaterialCI.EmissiveFactor    = {1.f, 2.f, 3.f};
    MaterialCI.AlphaCutoff       = 0.33f;
    MaterialCI.DoubleSided       = True;
    MaterialCI.pBaseColorTexture = pBaseColorTexture;
    MaterialCI.pNormalTexture    = pNormalTexture;

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    ASSERT_EQ(pAssetManager->CreateMaterial(MaterialCI, &pMaterial), RADIENT_STATUS_OK);
    ASSERT_NE(pMaterial, nullptr);
    EXPECT_NE(pMaterial->GetReference().URI, nullptr);
    EXPECT_NE(pMaterial->GetReference().Version, 0u);

    const GLTF::Material* pGLTFMaterial = RadientAssetManagerImpl::GetMaterial(pMaterial);
    ASSERT_NE(pGLTFMaterial, nullptr);

    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.x, MaterialCI.BaseColorFactor.x);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.y, MaterialCI.BaseColorFactor.y);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.z, MaterialCI.BaseColorFactor.z);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.BaseColorFactor.w, MaterialCI.BaseColorFactor.w);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.MetallicFactor, MaterialCI.MetallicFactor);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.RoughnessFactor, MaterialCI.RoughnessFactor);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.EmissiveFactor.x, MaterialCI.EmissiveFactor.x);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.EmissiveFactor.y, MaterialCI.EmissiveFactor.y);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.EmissiveFactor.z, MaterialCI.EmissiveFactor.z);
    EXPECT_FLOAT_EQ(pGLTFMaterial->Attribs.AlphaCutoff, MaterialCI.AlphaCutoff);
    EXPECT_TRUE(pGLTFMaterial->DoubleSided);

    EXPECT_EQ(pGLTFMaterial->GetTextureId(GLTF::DefaultBaseColorTextureAttribId), 0);
    EXPECT_EQ(pGLTFMaterial->GetTextureAttrib(GLTF::DefaultBaseColorTextureAttribId).GetUVSelector(), 0);
    EXPECT_EQ(pGLTFMaterial->GetTextureId(GLTF::DefaultNormalTextureAttribId), 1);
    EXPECT_EQ(pGLTFMaterial->GetTextureAttrib(GLTF::DefaultNormalTextureAttribId).GetUVSelector(), 0);
    EXPECT_EQ(pGLTFMaterial->GetTextureId(GLTF::DefaultMetallicRoughnessTextureAttribId), -1);
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
    EXPECT_TRUE(MeshStatus0 == RADIENT_STATUS_OK || MeshStatus0 == RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_TRUE(MeshStatus1 == RADIENT_STATUS_OK || MeshStatus1 == RADIENT_STATUS_INVALID_OPERATION);
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
    EXPECT_TRUE(MeshStatus0 == RADIENT_STATUS_OK || MeshStatus0 == RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_TRUE(MeshStatus1 == RADIENT_STATUS_OK || MeshStatus1 == RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_NE(pMesh0.RawPtr(), pMesh1.RawPtr());

    const MeshPayloadImpl* pMeshPayload0 = RadientMeshAssetManager::GetMeshPayload(pMesh0);
    ASSERT_NE(pMeshPayload0, nullptr);
    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(pMesh1), pMeshPayload0);
}

TEST(RadientAssetManagerTest, LoadGLTF)
{
    // LoadGLTF should reject missing input, then accept a valid URI and finish
    // through WaitForAssetLoad.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RadientGLTFLoadInfo               GLTFLoadInfo{};
    RefCntAutoPtr<IRadientSceneAsset> pGLTFModel;
    // A missing URI is invalid and should not create an asset reference.
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_EQ(pAssetManager->LoadGLTF(GLTFLoadInfo, &pGLTFModel), RADIENT_STATUS_INVALID_ARGUMENT);
    }

    TempDirectory     TempDir{"RadientAssetManagerTest"};
    const std::string GLTFPath = WriteBasicGLTFFile(TempDir);

    GLTFLoadInfo.URI                = GLTFPath.c_str();
    const RADIENT_STATUS LoadStatus = pAssetManager->LoadGLTF(GLTFLoadInfo, &pGLTFModel);
    // Depending on threading, the load may complete immediately or remain pending.
    EXPECT_TRUE(LoadStatus == RADIENT_STATUS_OK || LoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pGLTFModel, nullptr);
    EXPECT_NE(pGLTFModel->GetReference().URI, nullptr);
    EXPECT_STREQ(pGLTFModel->GetReference().URI, GLTFLoadInfo.URI);
    EXPECT_NE(pGLTFModel->GetReference().Version, 0u);
    EXPECT_EQ(ProcessTestGLTFLoad(*pAssetManager, pGLTFModel), RADIENT_STATUS_OK);
}

TEST(RadientAssetManagerTest, RejectsLoadsWithoutThreadPool)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RadientGLTFLoadInfo GLTFLoadInfo{};
    GLTFLoadInfo.URI = "no_thread_pool.gltf";

    RefCntAutoPtr<IRadientSceneAsset> pGLTFModel;
    EXPECT_EQ(pAssetManager->LoadGLTF(GLTFLoadInfo, &pGLTFModel), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pGLTFModel, nullptr);

    std::array<Uint8, 4> TextureData{1, 2, 3, 4};

    RadientTextureLoadInfo TextureLoadInfo{};
    TextureLoadInfo.pData    = TextureData.data();
    TextureLoadInfo.DataSize = static_cast<Uint64>(TextureData.size());

    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    EXPECT_EQ(pAssetManager->LoadTexture(TextureLoadInfo, &pTexture), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pTexture, nullptr);
}

TEST(RadientAssetManagerTest, DeduplicatesGLTFLoads)
{
    // Loading the same glTF URI twice should create distinct lightweight asset
    // handles that resolve to the same cached model payload.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    TempDirectory     TempDir{"RadientAssetManagerTest"};
    const std::string GLTFPath = WriteBasicGLTFFile(TempDir);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pFirstModel;
    const RADIENT_STATUS              FirstLoadStatus = pAssetManager->LoadGLTF(LoadInfo, &pFirstModel);
    ASSERT_TRUE(FirstLoadStatus == RADIENT_STATUS_OK || FirstLoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pFirstModel, nullptr);
    ASSERT_EQ(ProcessTestGLTFLoad(*pAssetManager, pFirstModel), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSceneAsset> pSecondModel;
    const RADIENT_STATUS              SecondLoadStatus = pAssetManager->LoadGLTF(LoadInfo, &pSecondModel);
    EXPECT_TRUE(SecondLoadStatus == RADIENT_STATUS_OK || SecondLoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pSecondModel, nullptr);
    ASSERT_EQ(ProcessTestGLTFLoad(*pAssetManager, pSecondModel), RADIENT_STATUS_OK);
    EXPECT_NE(pSecondModel.RawPtr(), pFirstModel.RawPtr());
    ASSERT_NE(pSecondModel->GetReference().URI, nullptr);
    ASSERT_NE(pFirstModel->GetReference().URI, nullptr);
    EXPECT_STREQ(pFirstModel->GetReference().URI, LoadInfo.URI);
    EXPECT_STREQ(pSecondModel->GetReference().URI, LoadInfo.URI);
    EXPECT_STREQ(pSecondModel->GetReference().URI, pFirstModel->GetReference().URI);
    EXPECT_EQ(RadientAssetManagerImpl::GetGLTFModel(pSecondModel), RadientAssetManagerImpl::GetGLTFModel(pFirstModel));
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

TEST(RadientSceneImporterTest, ImportGLTF)
{
    // Public ImportGLTF should reject invalid load info and then import a
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

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Radient imported GLTF root";

    RefCntAutoPtr<IRadientSceneAsset> ImportedModel;
    RadientEntityID                   ImportedRoot = InvalidRadientEntityID;
    // Empty load info is invalid and should not instantiate anything.
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_EQ(pImporter->ImportGLTF({}, InstantiateInfo, &ImportedModel, ImportedRoot), RADIENT_STATUS_INVALID_ARGUMENT);
    }

    TempDirectory     ImportTempDir{"RadientSceneTest"};
    const std::string ImportGLTFPath = WriteBasicGLTFFile(ImportTempDir);

    RadientGLTFLoadInfo GLTFLoadInfo{};
    GLTFLoadInfo.URI = ImportGLTFPath.c_str();

    RADIENT_STATUS ImportStatus = pImporter->ImportGLTF(GLTFLoadInfo, InstantiateInfo, &ImportedModel, ImportedRoot);
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
