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
#include "RadientSceneImpl.hpp"

#include "ThreadPool.hpp"

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

RADIENT_STATUS ProcessTestGLTFLoad(IRadientAssetManager& AssetManager, const RadientAssetReference& Model)
{
    return AssetManager.WaitForAssetLoad(Model);
}

RadientAssetReference CreateTestMaterial(IRadientAssetManager& AssetManager)
{
    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.Name            = "Radient test material";
    MaterialCI.BaseColorFactor = {1.f, 0.f, 0.f, 1.f};

    RadientAssetReference Material{};
    EXPECT_EQ(AssetManager.CreateMaterial(MaterialCI, Material), RADIENT_STATUS_OK);
    EXPECT_NE(Material.URI, nullptr);
    EXPECT_NE(Material.Version, 0u);

    return Material;
}

RadientAssetReference CreateTestMesh(IRadientAssetManager& AssetManager, const RadientAssetReference& Material)
{
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

    RadientVertexBufferCreateInfo VertexBufferCI{};
    VertexBufferCI.Name          = "Radient test vertices";
    VertexBufferCI.pPositions    = Positions;
    VertexBufferCI.pColors0      = Colors;
    VertexBufferCI.pBoneIndices0 = BoneIndices;
    VertexBufferCI.pBoneWeights0 = BoneWeights;
    VertexBufferCI.VertexCount   = 3;

    RadientIndexBufferCreateInfo IndexBufferCI{};
    IndexBufferCI.pIndices   = Indices;
    IndexBufferCI.IndexCount = 3;
    IndexBufferCI.IndexType  = RADIENT_INDEX_TYPE_UINT32;

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.Name              = "Radient test primitive";
    PrimitiveCI.VertexBufferIndex = 0;
    PrimitiveCI.FirstIndex        = 0;
    PrimitiveCI.IndexCount        = 3;
    PrimitiveCI.Material          = Material;

    RadientMeshCreateInfo MeshCI{};
    MeshCI.Name              = "Radient test mesh";
    MeshCI.pVertexBuffers    = &VertexBufferCI;
    MeshCI.VertexBufferCount = 1;
    MeshCI.IndexBuffer       = IndexBufferCI;
    MeshCI.pPrimitives       = &PrimitiveCI;
    MeshCI.PrimitiveCount    = 1;

    RadientAssetReference Mesh{};
    EXPECT_EQ(AssetManager.CreateMesh(MeshCI, Mesh), RADIENT_STATUS_OK);
    EXPECT_NE(Mesh.URI, nullptr);
    EXPECT_NE(Mesh.Version, 0u);

    return Mesh;
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

RadientEntityID CreateTestRenderableEntity(IRadientSceneWriter&         Writer,
                                           const RadientAssetReference& Mesh,
                                           const RadientAssetReference& Material)
{
    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(Writer.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_NE(Entity, InvalidRadientEntityID);

    if (Entity == InvalidRadientEntityID)
        return Entity;

    RadientMeshComponent MeshComponent{};
    MeshComponent.Mesh = Mesh;
    EXPECT_EQ(Writer.SetMesh(Entity, MeshComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(Writer.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);

    RadientMaterialBinding MaterialBinding{};
    MaterialBinding.PrimitiveIndex = 0;
    MaterialBinding.Material       = Material;

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
    // Creating a material should allocate a stable asset URI and non-zero version.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    const RadientAssetReference Material = CreateTestMaterial(*pAssetManager);
    EXPECT_NE(Material.URI, nullptr);
    EXPECT_NE(Material.Version, 0u);
}

TEST(RadientAssetManagerTest, CreateMesh)
{
    // Creating a mesh should accept vertex/index buffers and primitive material
    // references, returning a usable asset reference.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    const RadientAssetReference Material = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(Material.URI, nullptr);

    const RadientAssetReference Mesh = CreateTestMesh(*pAssetManager, Material);
    EXPECT_NE(Mesh.URI, nullptr);
    EXPECT_NE(Mesh.Version, 0u);
}

TEST(RadientAssetManagerTest, LoadGLTF)
{
    // LoadGLTF should reject missing input, then accept a valid URI and finish
    // through WaitForAssetLoad.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    RadientGLTFLoadInfo   GLTFLoadInfo{};
    RadientAssetReference GLTFModel{};
    // A missing URI is invalid and should not create an asset reference.
    EXPECT_EQ(pAssetManager->LoadGLTF(GLTFLoadInfo, GLTFModel), RADIENT_STATUS_INVALID_ARGUMENT);

    TempDirectory     TempDir{"RadientAssetManagerTest"};
    const std::string GLTFPath = WriteBasicGLTFFile(TempDir);

    GLTFLoadInfo.URI                = GLTFPath.c_str();
    const RADIENT_STATUS LoadStatus = pAssetManager->LoadGLTF(GLTFLoadInfo, GLTFModel);
    // Depending on threading, the load may complete immediately or remain pending.
    EXPECT_TRUE(LoadStatus == RADIENT_STATUS_OK || LoadStatus == RADIENT_STATUS_PENDING);
    EXPECT_NE(GLTFModel.URI, nullptr);
    EXPECT_NE(GLTFModel.Version, 0u);
    EXPECT_EQ(ProcessTestGLTFLoad(*pAssetManager, GLTFModel), RADIENT_STATUS_OK);
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

    RadientAssetReference ImportedModel{};
    RadientEntityID       ImportedRoot = InvalidRadientEntityID;
    // Empty load info is invalid and should not instantiate anything.
    EXPECT_EQ(pImporter->ImportGLTF({}, InstantiateInfo, ImportedModel, ImportedRoot), RADIENT_STATUS_INVALID_ARGUMENT);

    TempDirectory     ImportTempDir{"RadientSceneTest"};
    const std::string ImportGLTFPath = WriteBasicGLTFFile(ImportTempDir);

    RadientGLTFLoadInfo GLTFLoadInfo{};
    GLTFLoadInfo.URI = ImportGLTFPath.c_str();

    RADIENT_STATUS ImportStatus = pImporter->ImportGLTF(GLTFLoadInfo, InstantiateInfo, ImportedModel, ImportedRoot);
    if (ImportStatus == RADIENT_STATUS_PENDING)
    {
        // Pending loads are completed explicitly in tests so the imported root
        // can be verified deterministically.
        ASSERT_EQ(ProcessTestGLTFLoad(*pAssetManager, ImportedModel), RADIENT_STATUS_OK);
        ImportStatus = pImporter->ProcessPendingImports();
    }
    EXPECT_EQ(ImportStatus, RADIENT_STATUS_OK);
    EXPECT_NE(ImportedModel.URI, nullptr);
    EXPECT_NE(ImportedModel.Version, 0u);
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

    const RadientAssetReference Material = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(Material.URI, nullptr);

    const RadientAssetReference Mesh = CreateTestMesh(*pAssetManager, Material);
    ASSERT_NE(Mesh.URI, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = CreateTestSceneWriter(*pEngine, pScene);
    ASSERT_NE(pWriter, nullptr);

    const RadientEntityID Entity = CreateTestRenderableEntity(*pWriter, Mesh, Material);
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
}

TEST(RadientRendererTest, RenderHeadlessScene)
{
    // Builds a minimal material, mesh, scene, renderer, and target to verify
    // the headless render path can execute without a swap chain.
    RefCntAutoPtr<IRadientEngine> pEngine = CreateTestEngine();
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager = GetTestAssetManager(*pEngine);
    ASSERT_NE(pAssetManager, nullptr);

    const RadientAssetReference Material = CreateTestMaterial(*pAssetManager);
    ASSERT_NE(Material.URI, nullptr);

    const RadientAssetReference Mesh = CreateTestMesh(*pAssetManager, Material);
    ASSERT_NE(Mesh.URI, nullptr);

    RefCntAutoPtr<IRadientScene> pScene = CreateTestScene(*pEngine);
    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = CreateTestSceneWriter(*pEngine, pScene);
    ASSERT_NE(pWriter, nullptr);

    const RadientEntityID Entity = CreateTestRenderableEntity(*pWriter, Mesh, Material);
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
