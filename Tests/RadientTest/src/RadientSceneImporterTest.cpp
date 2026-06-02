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
#include "RadientAssetManagerImpl.hpp"
#include "RadientSceneImpl.hpp"
#include "RadientSceneState.hpp"

#include "Cast.hpp"
#include "ThreadPool.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr float EPSILON = 1e-5f;

std::string WriteGLTFFile(const TempDirectory& TempDir, const char* FileName, const char* Contents)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << Contents;

    return Path;
}

void WriteBinaryFile(const TempDirectory& TempDir, const char* FileName, const std::vector<Uint8>& Data)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File.write(reinterpret_cast<const char*>(Data.data()), static_cast<std::streamsize>(Data.size()));
}

struct ImportFixture
{
    RefCntAutoPtr<IRadientEngine>        pEngine;
    RefCntAutoPtr<IRadientAssetManager>  pAssetManager;
    RefCntAutoPtr<IRadientScene>         pScene;
    RefCntAutoPtr<IRadientSceneWriter>   pWriter;
    RefCntAutoPtr<IRadientSceneImporter> pImporter;
    RefCntAutoPtr<IThreadPool>           pThreadPool;
};

ImportFixture CreateImportFixture(IThreadPool* pThreadPool = nullptr)
{
    ImportFixture Fixture;

    RadientEngineCreateInfo EngineCI{};
    Fixture.pThreadPool  = pThreadPool != nullptr ? pThreadPool : CreateThreadPool(ThreadPoolCreateInfo{0});
    EngineCI.pThreadPool = Fixture.pThreadPool;
    EXPECT_EQ(CreateRadientEngine(EngineCI, &Fixture.pEngine), RADIENT_STATUS_OK);
    EXPECT_NE(Fixture.pEngine, nullptr);

    if (Fixture.pEngine != nullptr)
    {
        EXPECT_EQ(Fixture.pEngine->GetAssetManager(&Fixture.pAssetManager), RADIENT_STATUS_OK);
        EXPECT_NE(Fixture.pAssetManager, nullptr);

        EXPECT_EQ(Fixture.pEngine->CreateScene({}, &Fixture.pScene), RADIENT_STATUS_OK);
        EXPECT_NE(Fixture.pScene, nullptr);

        EXPECT_EQ(Fixture.pEngine->CreateSceneWriter(Fixture.pScene, &Fixture.pWriter), RADIENT_STATUS_OK);
        EXPECT_NE(Fixture.pWriter, nullptr);

        EXPECT_EQ(Fixture.pEngine->CreateSceneImporter(Fixture.pWriter, &Fixture.pImporter), RADIENT_STATUS_OK);
        EXPECT_NE(Fixture.pImporter, nullptr);
    }

    return Fixture;
}

RADIENT_STATUS ProcessGLTFLoad(ImportFixture& Fixture, IRadientSceneAsset* pModel)
{
    if (Fixture.pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    return Fixture.pAssetManager->WaitForAssetLoad(pModel);
}

RADIENT_STATUS FinishPendingGLTFImports(ImportFixture& Fixture, IRadientSceneAsset* pModel)
{
    const RADIENT_STATUS LoadStatus = ProcessGLTFLoad(Fixture, pModel);
    if (RADIENT_FAILED(LoadStatus) || LoadStatus == RADIENT_STATUS_PENDING)
        return LoadStatus;

    return Fixture.pImporter != nullptr ? Fixture.pImporter->ProcessPendingImports() : RADIENT_STATUS_INVALID_OPERATION;
}

RADIENT_STATUS ImportGLTFAndFinishPending(ImportFixture&                     Fixture,
                                          const RadientGLTFLoadInfo&         LoadInfo,
                                          const RadientGLTFInstantiateInfo&  InstantiateInfo,
                                          RefCntAutoPtr<IRadientSceneAsset>& pModel,
                                          RadientEntityID&                   RootEntity)
{
    RADIENT_STATUS Status = Fixture.pImporter->ImportGLTF(LoadInfo, InstantiateInfo, &pModel, RootEntity);
    if (Status == RADIENT_STATUS_PENDING)
        Status = FinishPendingGLTFImports(Fixture, pModel);

    return Status;
}

std::vector<RadientEntityID> GetChildren(IRadientScene& Scene, RadientEntityID Entity)
{
    Uint32 ChildCount = 0;
    EXPECT_EQ(Scene.GetChildCount(Entity, ChildCount), RADIENT_STATUS_OK);

    std::vector<RadientEntityID> Children(ChildCount, InvalidRadientEntityID);
    if (ChildCount != 0)
    {
        Uint32 NumChildrenWritten = 0;
        EXPECT_EQ(Scene.GetChildren(Entity, 0, ChildCount, Children.data(), NumChildrenWritten), RADIENT_STATUS_OK);
        EXPECT_EQ(NumChildrenWritten, ChildCount);
    }

    return Children;
}

void ExpectFloat3Near(const RadientFloat3& Value, const RadientFloat3& Reference)
{
    EXPECT_NEAR(Value.x, Reference.x, EPSILON);
    EXPECT_NEAR(Value.y, Reference.y, EPSILON);
    EXPECT_NEAR(Value.z, Reference.z, EPSILON);
}

void ExpectFloat2Near(const RadientFloat2& Value, const RadientFloat2& Reference)
{
    EXPECT_NEAR(Value.x, Reference.x, EPSILON);
    EXPECT_NEAR(Value.y, Reference.y, EPSILON);
}

struct CapturedRenderableLight
{
    RadientEntityID       Entity = InvalidRadientEntityID;
    RadientLightComponent Light;
    Bool                  EffectiveVisible = False;
};

const CapturedRenderableLight* FindLight(const std::vector<CapturedRenderableLight>& Lights, RadientEntityID Entity)
{
    const std::vector<CapturedRenderableLight>::const_iterator It =
        std::find_if(Lights.begin(), Lights.end(),
                     [Entity](const CapturedRenderableLight& Light) {
                         return Light.Entity == Entity;
                     });

    return It != Lights.end() ? &*It : nullptr;
}

TEST(RadientSceneImporterTest, ImportsNodeHierarchy)
{
    // Imports a small glTF hierarchy and verifies that Radient creates the
    // matching parent/child structure with local transforms preserved.
    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFFile(TempDir, "hierarchy.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [
        {"name": "RootNode", "translation": [1, 2, 3], "children": [1, 2]},
        {"name": "ChildA", "translation": [4, 5, 6]},
        {"name": "ChildB", "scale": [2, 3, 4]}
    ]
})GLTF");

    ImportFixture Fixture = CreateImportFixture();
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported hierarchy";

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    RadientEntityID                   ImportedRoot = InvalidRadientEntityID;
    EXPECT_EQ(ImportGLTFAndFinishPending(Fixture, LoadInfo, InstantiateInfo, pModel, ImportedRoot), RADIENT_STATUS_OK);
    EXPECT_NE(ImportedRoot, InvalidRadientEntityID);

    // The importer creates an explicit Radient root, with the glTF scene root
    // as its only child.
    const std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, ImportedRoot);
    ASSERT_EQ(RootChildren.size(), 1u);

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pScene->GetParent(RootChildren[0], Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, ImportedRoot);

    RadientTransform Transform{};
    EXPECT_EQ(Fixture.pScene->GetLocalTransform(RootChildren[0], Transform), RADIENT_STATUS_OK);
    ExpectFloat3Near(Transform.Position, {1.f, 2.f, 3.f});
    ExpectFloat3Near(Transform.Scale, {1.f, 1.f, 1.f});

    const std::vector<RadientEntityID> Children = GetChildren(*Fixture.pScene, RootChildren[0]);
    ASSERT_EQ(Children.size(), 2u);

    // Both glTF child nodes should be attached below the imported scene root.
    EXPECT_EQ(Fixture.pScene->GetParent(Children[0], Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, RootChildren[0]);
    EXPECT_EQ(Fixture.pScene->GetParent(Children[1], Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, RootChildren[0]);

    EXPECT_EQ(Fixture.pScene->GetLocalTransform(Children[0], Transform), RADIENT_STATUS_OK);
    ExpectFloat3Near(Transform.Position, {4.f, 5.f, 6.f});

    EXPECT_EQ(Fixture.pScene->GetLocalTransform(Children[1], Transform), RADIENT_STATUS_OK);
    ExpectFloat3Near(Transform.Scale, {2.f, 3.f, 4.f});
}

TEST(RadientSceneImporterTest, UsesExplicitSceneIndex)
{
    // Verifies that import uses glTF's default scene unless the caller
    // explicitly requests a scene index.
    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFFile(TempDir, "scene_index.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 1,
    "scenes": [
        {"nodes": [0]},
        {"nodes": [1]}
    ],
    "nodes": [
        {"name": "Scene0Node", "translation": [1, 0, 0]},
        {"name": "DefaultSceneNode", "translation": [2, 0, 0]}
    ]
})GLTF");

    ImportFixture Fixture = CreateImportFixture();
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Default scene";

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    RadientEntityID                   ImportedRoot = InvalidRadientEntityID;
    EXPECT_EQ(ImportGLTFAndFinishPending(Fixture, LoadInfo, InstantiateInfo, pModel, ImportedRoot), RADIENT_STATUS_OK);

    std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, ImportedRoot);
    ASSERT_EQ(RootChildren.size(), 1u);

    RadientTransform Transform{};
    EXPECT_EQ(Fixture.pScene->GetLocalTransform(RootChildren[0], Transform), RADIENT_STATUS_OK);
    // No explicit index means the glTF default scene, scene 1, is imported.
    ExpectFloat3Near(Transform.Position, {2.f, 0.f, 0.f});

    InstantiateInfo.Name       = "Explicit scene 0";
    InstantiateInfo.SceneIndex = 0;

    EXPECT_EQ(ImportGLTFAndFinishPending(Fixture, LoadInfo, InstantiateInfo, pModel, ImportedRoot), RADIENT_STATUS_OK);

    RootChildren = GetChildren(*Fixture.pScene, ImportedRoot);
    ASSERT_EQ(RootChildren.size(), 1u);

    EXPECT_EQ(Fixture.pScene->GetLocalTransform(RootChildren[0], Transform), RADIENT_STATUS_OK);
    // Requesting scene 0 should instantiate the alternate root node.
    ExpectFloat3Near(Transform.Position, {1.f, 0.f, 0.f});

    InstantiateInfo.Name       = "Invalid scene";
    InstantiateInfo.SceneIndex = 2;
    ImportedRoot               = InvalidRadientEntityID;

    // Invalid scene indices should fail without returning a created root.
    EXPECT_EQ(Fixture.pImporter->InstantiateGLTF(pModel, InstantiateInfo, ImportedRoot), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(ImportedRoot, InvalidRadientEntityID);
}

TEST(RadientSceneImporterTest, InstantiateGLTFUsesCachedModel)
{
    // Loads a glTF once, removes the file, then instantiates from the cached
    // in-memory model to prove instantiation does not re-read the source URI.
    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFFile(TempDir, "cached_model.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "CachedNode"}]
})GLTF");

    ImportFixture Fixture = CreateImportFixture();
    ASSERT_NE(Fixture.pAssetManager, nullptr);
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    const RADIENT_STATUS              LoadStatus = Fixture.pAssetManager->LoadGLTF(LoadInfo, &pModel);
    ASSERT_TRUE(LoadStatus == RADIENT_STATUS_OK || LoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pModel, nullptr);
    ASSERT_NE(pModel->GetReference().URI, nullptr);
    ASSERT_EQ(ProcessGLTFLoad(Fixture, pModel), RADIENT_STATUS_OK);

    EXPECT_EQ(std::remove(GLTFPath.c_str()), 0);

    // The cached model should still be available after the source file is gone.
    RadientEntityID RootEntity = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pImporter->InstantiateGLTF(pModel, {}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_NE(RootEntity, InvalidRadientEntityID);

    const std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, RootEntity);
    ASSERT_EQ(RootChildren.size(), 1u);
}

TEST(RadientSceneImporterTest, InstantiateGLTFReportsPendingWhileModelLoads)
{
    // Uses a stopped thread pool so LoadGLTF can enter the pending state and
    // InstantiateGLTF can create a placeholder root without blocking.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFFile(TempDir, "pending.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "Pending Child"}]
})GLTF");

    ImportFixture Fixture = CreateImportFixture(pThreadPool);
    ASSERT_NE(Fixture.pAssetManager, nullptr);
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    EXPECT_EQ(Fixture.pAssetManager->LoadGLTF(LoadInfo, &pModel), RADIENT_STATUS_PENDING);
    ASSERT_NE(pModel, nullptr);
    ASSERT_NE(pModel->GetReference().URI, nullptr);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pImporter->InstantiateGLTF(pModel, {}, RootEntity), RADIENT_STATUS_PENDING);
    ASSERT_NE(RootEntity, InvalidRadientEntityID);

    // Pending imports should expose an empty root until the model load completes.
    std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, RootEntity);
    EXPECT_TRUE(RootChildren.empty());

    // Once the asset is loaded and pending imports are processed, the root
    // should be populated with the glTF scene graph.
    ASSERT_EQ(ProcessGLTFLoad(Fixture, pModel), RADIENT_STATUS_OK);
    EXPECT_EQ(Fixture.pImporter->ProcessPendingImports(), RADIENT_STATUS_OK);

    RootChildren = GetChildren(*Fixture.pScene, RootEntity);
    ASSERT_EQ(RootChildren.size(), 1u);

    pThreadPool->StopThreads();
}

TEST(RadientSceneImporterTest, PendingGLTFImportDestroysRootWhenSceneIndexIsInvalid)
{
    // If a pending import later resolves to an invalid scene index, the
    // placeholder root must be removed instead of leaving a dead import shell.
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFFile(TempDir, "pending_invalid_scene.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "Pending Child"}]
})GLTF");

    ImportFixture Fixture = CreateImportFixture(pThreadPool);
    ASSERT_NE(Fixture.pAssetManager, nullptr);
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    EXPECT_EQ(Fixture.pAssetManager->LoadGLTF(LoadInfo, &pModel), RADIENT_STATUS_PENDING);
    ASSERT_NE(pModel, nullptr);
    ASSERT_NE(pModel->GetReference().URI, nullptr);

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.SceneIndex = 1;

    RadientEntityID RootEntity = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pImporter->InstantiateGLTF(pModel, InstantiateInfo, RootEntity), RADIENT_STATUS_PENDING);
    ASSERT_NE(RootEntity, InvalidRadientEntityID);
    EXPECT_EQ(Fixture.pScene->IsEntityAlive(RootEntity), RADIENT_STATUS_OK);

    // Processing the completed load should fail and destroy the placeholder.
    ASSERT_EQ(ProcessGLTFLoad(Fixture, pModel), RADIENT_STATUS_OK);
    EXPECT_EQ(Fixture.pImporter->ProcessPendingImports(), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Fixture.pScene->IsEntityAlive(RootEntity), RADIENT_STATUS_NOT_FOUND);

    pThreadPool->StopThreads();
}

TEST(RadientSceneImporterTest, ImportsCameras)
{
    // Imports perspective and orthographic glTF cameras and verifies their
    // Radient camera parameters are converted correctly.
    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFFile(TempDir, "camera.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "cameras": [
        {"name": "Perspective", "type": "perspective", "perspective": {"aspectRatio": 1.5, "yfov": 0.7, "znear": 0.2, "zfar": 250.0}},
        {"name": "Orthographic", "type": "orthographic", "orthographic": {"xmag": 4.0, "ymag": 3.0, "znear": 0.5, "zfar": 500.0}}
    ],
    "nodes": [
        {"name": "PerspectiveNode", "camera": 0},
        {"name": "OrthographicNode", "camera": 1}
    ]
})GLTF");

    ImportFixture Fixture = CreateImportFixture();
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported cameras";

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    RadientEntityID                   ImportedRoot = InvalidRadientEntityID;
    EXPECT_EQ(ImportGLTFAndFinishPending(Fixture, LoadInfo, InstantiateInfo, pModel, ImportedRoot), RADIENT_STATUS_OK);

    const std::vector<RadientEntityID> CameraNodes = GetChildren(*Fixture.pScene, ImportedRoot);
    ASSERT_EQ(CameraNodes.size(), 2u);

    RadientCameraComponent Camera{};
    // Perspective yfov/aspect should be converted into Radient aperture and
    // focal-length values while preserving clipping range.
    EXPECT_EQ(Fixture.pScene->GetCamera(CameraNodes[0], Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera.Projection, RADIENT_CAMERA_PROJECTION_PERSPECTIVE);
    ExpectFloat2Near(Camera.ClippingRange, {0.2f, 250.f});
    EXPECT_NEAR(Camera.HorizontalAperture, Camera.VerticalAperture * 1.5f, EPSILON);
    EXPECT_NEAR(Camera.FocalLength, Camera.VerticalAperture / (2.f * std::tan(0.7f * 0.5f)), EPSILON);

    // Orthographic xmag/ymag map directly to Radient aperture values.
    EXPECT_EQ(Fixture.pScene->GetCamera(CameraNodes[1], Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera.Projection, RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC);
    EXPECT_NEAR(Camera.HorizontalAperture, 4.f, EPSILON);
    EXPECT_NEAR(Camera.VerticalAperture, 3.f, EPSILON);
    ExpectFloat2Near(Camera.ClippingRange, {0.5f, 500.f});
}

TEST(RadientSceneImporterTest, ImportsMeshNodeMetadataWithoutDevice)
{
    // Imports mesh nodes without creating GPU resources. The scene should still
    // get Radient mesh references and mesh-renderer components for each glTF node.
    TempDirectory TempDir{"RadientSceneImporterTest"};

    const float  Positions[] = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    const Uint16 Indices[]   = {0u, 1u, 2u};

    std::vector<Uint8> Buffer(sizeof(Positions) + sizeof(Indices));
    std::memcpy(Buffer.data(), Positions, sizeof(Positions));
    std::memcpy(Buffer.data() + sizeof(Positions), Indices, sizeof(Indices));
    WriteBinaryFile(TempDir, "mesh.bin", Buffer);

    const std::string GLTFPath = WriteGLTFFile(TempDir, "mesh_node.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "buffers": [{"uri": "mesh.bin", "byteLength": 42}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": 0, "byteLength": 36},
        {"buffer": 0, "byteOffset": 36, "byteLength": 6}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "meshes": [{"name": "Triangle", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
    "nodes": [
        {"name": "MeshNodeA", "mesh": 0},
        {"name": "MeshNodeB", "mesh": 0, "translation": [1, 0, 0]}
    ]
})GLTF");

    ImportFixture Fixture = CreateImportFixture();
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported mesh metadata";

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    RadientEntityID                   ImportedRoot = InvalidRadientEntityID;
    EXPECT_EQ(ImportGLTFAndFinishPending(Fixture, LoadInfo, InstantiateInfo, pModel, ImportedRoot), RADIENT_STATUS_OK);

    const std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, ImportedRoot);
    ASSERT_EQ(RootChildren.size(), 2u);

    // The synthetic import root is only a grouping node and should not be renderable.
    Bool HasComponent = True;
    EXPECT_EQ(Fixture.pScene->HasComponent(ImportedRoot, RADIENT_COMPONENT_TYPE_MESH, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
    EXPECT_EQ(Fixture.pScene->HasComponent(ImportedRoot, RADIENT_COMPONENT_TYPE_MESH_RENDERER, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);

    for (const RadientEntityID MeshNode : RootChildren)
    {
        // Each glTF mesh node should receive both mesh data and a renderer component.
        EXPECT_EQ(Fixture.pScene->HasComponent(MeshNode, RADIENT_COMPONENT_TYPE_MESH, HasComponent), RADIENT_STATUS_OK);
        EXPECT_EQ(HasComponent, True);
        EXPECT_EQ(Fixture.pScene->HasComponent(MeshNode, RADIENT_COMPONENT_TYPE_MESH_RENDERER, HasComponent), RADIENT_STATUS_OK);
        EXPECT_EQ(HasComponent, True);
    }

    ASSERT_NE(Fixture.pWriter, nullptr);
    EXPECT_EQ(Fixture.pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(Fixture.pScene.RawPtr());
    ASSERT_NE(pSceneImpl, nullptr);

    std::vector<RadientEntityID>    RenderableEntities;
    std::vector<IRadientMeshAsset*> RenderableMeshes;
    // Renderable enumeration should expose the imported mesh nodes, not the
    // grouping root.
    EXPECT_EQ(pSceneImpl->GetState().EnumerateRenderableMeshes(
                  [&RenderableEntities, &RenderableMeshes](const RadientSceneState::RenderableMesh& Mesh) {
                      RenderableEntities.push_back(Mesh.Entity);
                      RenderableMeshes.push_back(Mesh.Mesh.pMesh);
                  }),
              RADIENT_STATUS_OK);
    ASSERT_EQ(RenderableEntities.size(), 2u);
    EXPECT_NE(std::find(RenderableEntities.begin(), RenderableEntities.end(), RootChildren[0]), RenderableEntities.end());
    EXPECT_NE(std::find(RenderableEntities.begin(), RenderableEntities.end(), RootChildren[1]), RenderableEntities.end());
    ASSERT_NE(RenderableMeshes[0], nullptr);
    ASSERT_NE(RenderableMeshes[0]->GetReference().URI, nullptr);
    EXPECT_NE(std::string{RenderableMeshes[0]->GetReference().URI}, std::string{pModel->GetReference().URI});
    // Both nodes reference the same converted Radient mesh asset.
    EXPECT_EQ(RenderableMeshes[0], RenderableMeshes[1]);

    const RadientAssetManagerImpl* pAssetManagerImpl = ClassPtrCast<RadientAssetManagerImpl>(Fixture.pAssetManager.RawPtr());
    ASSERT_NE(pAssetManagerImpl, nullptr);

    RefCntAutoPtr<IRadientSceneAsset> pSourceModel;
    Uint32                            SourceMeshIndex = ~0u;
    for (IRadientMeshAsset* pMesh : RenderableMeshes)
    {
        // The asset manager should remember which glTF model and mesh index
        // produced each converted Radient mesh.
        EXPECT_EQ(pAssetManagerImpl->GetMeshGLTFSource(pMesh, &pSourceModel, SourceMeshIndex), RADIENT_STATUS_OK);
        EXPECT_EQ(pSourceModel, pModel);
        EXPECT_EQ(SourceMeshIndex, 0u);
    }
}

TEST(RadientSceneImporterTest, ImportsLights)
{
    // Imports KHR_lights_punctual lights and verifies that Radient light
    // components preserve type, color, intensity, range, and cone angles.
    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFFile(TempDir, "lights.gltf",
                                               R"GLTF({
    "asset": {"version": "2.0"},
    "extensionsUsed": ["KHR_lights_punctual"],
    "extensions": {
        "KHR_lights_punctual": {
            "lights": [
                {"name": "Sun", "type": "directional", "color": [1.0, 0.8, 0.6], "intensity": 2.0},
                {"name": "Point", "type": "point", "color": [0.2, 0.3, 1.0], "intensity": 3.0, "range": 10.0},
                {"name": "Spot", "type": "spot", "color": [1.0, 1.0, 1.0], "intensity": 4.0,
                 "spot": {"innerConeAngle": 0.1, "outerConeAngle": 0.4}}
            ]
        }
    },
    "scene": 0,
    "scenes": [{"nodes": [0, 1, 2]}],
    "nodes": [
        {"name": "DirectionalNode", "extensions": {"KHR_lights_punctual": {"light": 0}}},
        {"name": "PointNode", "extensions": {"KHR_lights_punctual": {"light": 1}}},
        {"name": "SpotNode", "extensions": {"KHR_lights_punctual": {"light": 2}}}
    ]
})GLTF");

    ImportFixture Fixture = CreateImportFixture();
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);
    ASSERT_NE(Fixture.pWriter, nullptr);

    RadientGLTFLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported lights";

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    RadientEntityID                   ImportedRoot = InvalidRadientEntityID;
    EXPECT_EQ(ImportGLTFAndFinishPending(Fixture, LoadInfo, InstantiateInfo, pModel, ImportedRoot), RADIENT_STATUS_OK);
    EXPECT_EQ(Fixture.pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::vector<RadientEntityID> LightNodes = GetChildren(*Fixture.pScene, ImportedRoot);
    ASSERT_EQ(LightNodes.size(), 3u);

    const RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(Fixture.pScene.RawPtr());
    ASSERT_NE(pSceneImpl, nullptr);

    std::vector<CapturedRenderableLight> Lights;
    EXPECT_EQ(pSceneImpl->GetState().EnumerateRenderableLights(
                  [&Lights](const RadientSceneState::RenderableLight& Light) {
                      Lights.push_back({Light.Entity, Light.Light, Light.EffectiveVisible});
                  }),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Lights.size(), 3u);

    // Directional lights should keep color/intensity and become visible render lights.
    const CapturedRenderableLight* pDirectional = FindLight(Lights, LightNodes[0]);
    ASSERT_NE(pDirectional, nullptr);
    EXPECT_EQ(pDirectional->Light.Type, RADIENT_LIGHT_TYPE_DIRECTIONAL);
    ExpectFloat3Near(pDirectional->Light.Color, {1.f, 0.8f, 0.6f});
    EXPECT_NEAR(pDirectional->Light.Intensity, 2.f, EPSILON);
    EXPECT_EQ(pDirectional->EffectiveVisible, True);

    // Point lights should also carry the glTF range value.
    const CapturedRenderableLight* pPoint = FindLight(Lights, LightNodes[1]);
    ASSERT_NE(pPoint, nullptr);
    EXPECT_EQ(pPoint->Light.Type, RADIENT_LIGHT_TYPE_POINT);
    ExpectFloat3Near(pPoint->Light.Color, {0.2f, 0.3f, 1.f});
    EXPECT_NEAR(pPoint->Light.Intensity, 3.f, EPSILON);
    EXPECT_NEAR(pPoint->Light.Range, 10.f, EPSILON);
    EXPECT_EQ(pPoint->EffectiveVisible, True);

    // Spot light cone angles use glTF radians and should be copied directly.
    const CapturedRenderableLight* pSpot = FindLight(Lights, LightNodes[2]);
    ASSERT_NE(pSpot, nullptr);
    EXPECT_EQ(pSpot->Light.Type, RADIENT_LIGHT_TYPE_SPOT);
    ExpectFloat3Near(pSpot->Light.Color, {1.f, 1.f, 1.f});
    EXPECT_NEAR(pSpot->Light.Intensity, 4.f, EPSILON);
    EXPECT_NEAR(pSpot->Light.InnerConeAngle, 0.1f, EPSILON);
    EXPECT_NEAR(pSpot->Light.OuterConeAngle, 0.4f, EPSILON);
    EXPECT_EQ(pSpot->EffectiveVisible, True);
}

} // namespace
