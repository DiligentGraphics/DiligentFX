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
#include "RadientSkinning.h"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Scene/RadientSceneImpl.hpp"
#include "Scene/RadientSceneState.hpp"

#include "Cast.hpp"
#include "ThreadPool.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
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

template <typename ValueType, size_t Size>
size_t AppendBytes(std::vector<Uint8>& Buffer, const std::array<ValueType, Size>& Values)
{
    const size_t Offset = Buffer.size();
    Buffer.resize(Offset + sizeof(ValueType) * Values.size());
    std::memcpy(Buffer.data() + Offset, Values.data(), sizeof(ValueType) * Values.size());
    return Offset;
}

std::string WriteGLTFSkinFile(const TempDirectory& TempDir)
{
    const std::array<Float32, 9> Positions{
        -0.5f, 0.f, 0.f,
        +0.5f, 0.f, 0.f,
        0.f, 1.f, 0.f};
    const std::array<Uint16, 12> Joints{
        0, 1, 0, 0,
        0, 1, 0, 0,
        0, 1, 0, 0};
    const std::array<Float32, 12> Weights{
        1.f, 0.f, 0.f, 0.f,
        0.5f, 0.5f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f};
    const std::array<Float32, 32> InverseBindMatrices{
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
        2.f, 0.f, 0.f, 0.f,
        0.f, 3.f, 0.f, 0.f,
        0.f, 0.f, 4.f, 0.f,
        0.f, 0.f, 0.f, 1.f};
    const std::array<Uint16, 3>  Indices{0, 1, 2};
    const std::array<Float32, 2> AnimationTimes{1.f, 3.f};
    const std::array<Float32, 6> AnimationTranslations{
        0.f, 0.f, 3.f,
        2.f, 0.f, 3.f};

    std::vector<Uint8> Buffer;
    const size_t       PositionOffset             = AppendBytes(Buffer, Positions);
    const size_t       JointsOffset               = AppendBytes(Buffer, Joints);
    const size_t       WeightsOffset              = AppendBytes(Buffer, Weights);
    const size_t       InverseBindOffset          = AppendBytes(Buffer, InverseBindMatrices);
    const size_t       IndexOffset                = AppendBytes(Buffer, Indices);
    const size_t       AnimationTimeOffset        = AppendBytes(Buffer, AnimationTimes);
    const size_t       AnimationTranslationOffset = AppendBytes(Buffer, AnimationTranslations);
    WriteBinaryFile(TempDir, "skin.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 4, 5]}],
    "nodes": [
        {"name": "Root", "translation": [1, 0, 0], "children": [1]},
        {"name": "NonJointAncestor", "translation": [0, 2, 0], "children": [2]},
        {"name": "JointA", "translation": [0, 0, 3], "children": [3]},
        {"name": "JointB", "scale": [2, 3, 4]},
        {"name": "SkinnedMeshA", "mesh": 0, "skin": 0, "translation": [-1, 0, 0]},
        {"name": "SkinnedMeshB", "mesh": 0, "skin": 0, "translation": [1, 0, 0]}
    ],
    "skins": [{"name": "Character", "skeleton": 2, "joints": [2, 3], "inverseBindMatrices": 3}],
    "animations": [{
        "name": "Joint motion",
        "samplers": [{"input": 5, "output": 6, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": 2, "path": "translation"}}]
    }],
    "meshes": [{"primitives": [{
        "attributes": {"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2},
        "indices": 4
    }]}],
    "buffers": [{"uri": "skin.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionOffset << R"GLTF(, "byteLength": )GLTF" << sizeof(Positions) << R"GLTF(, "target": 34962},
        {"buffer": 0, "byteOffset": )GLTF"
         << JointsOffset << R"GLTF(, "byteLength": )GLTF" << sizeof(Joints) << R"GLTF(, "target": 34962},
        {"buffer": 0, "byteOffset": )GLTF"
         << WeightsOffset << R"GLTF(, "byteLength": )GLTF" << sizeof(Weights) << R"GLTF(, "target": 34962},
        {"buffer": 0, "byteOffset": )GLTF"
         << InverseBindOffset << R"GLTF(, "byteLength": )GLTF" << sizeof(InverseBindMatrices) << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexOffset << R"GLTF(, "byteLength": )GLTF" << sizeof(Indices) << R"GLTF(, "target": 34963},
        {"buffer": 0, "byteOffset": )GLTF"
         << AnimationTimeOffset << R"GLTF(, "byteLength": )GLTF" << sizeof(AnimationTimes) << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << AnimationTranslationOffset << R"GLTF(, "byteLength": )GLTF" << sizeof(AnimationTranslations) << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [-0.5, 0, 0], "max": [0.5, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "VEC4"},
        {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"},
        {"bufferView": 3, "componentType": 5126, "count": 2, "type": "MAT4"},
        {"bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR"},
        {"bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR", "min": [1], "max": [3]},
        {"bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3"}
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "skin.gltf", GLTF.str().c_str());
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

RADIENT_STATUS ProcessSceneLoad(ImportFixture& Fixture, IRadientSceneAsset* pModel)
{
    if (Fixture.pAssetManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    return Fixture.pAssetManager->WaitForAssetLoad(pModel);
}

RADIENT_STATUS FinishPendingSceneImports(ImportFixture& Fixture, IRadientSceneAsset* pModel)
{
    const RADIENT_STATUS LoadStatus = ProcessSceneLoad(Fixture, pModel);
    if (RADIENT_FAILED(LoadStatus) || LoadStatus == RADIENT_STATUS_PENDING)
        return LoadStatus;

    return Fixture.pImporter != nullptr ? Fixture.pImporter->ProcessPendingImports() : RADIENT_STATUS_INVALID_OPERATION;
}

struct ImportSceneResult
{
    RADIENT_STATUS                    Status = RADIENT_STATUS_INVALID_OPERATION;
    RefCntAutoPtr<IRadientSceneAsset> pModel;
    RadientEntityID                   RootEntity = InvalidRadientEntityID;
};

ImportSceneResult ImportSceneAndFinishPending(ImportFixture&                     Fixture,
                                              const RadientSceneLoadInfo&        LoadInfo,
                                              const RadientSceneInstantiateInfo& InstantiateInfo)
{
    ImportSceneResult Result;
    Result.Status = Fixture.pImporter->ImportScene(LoadInfo, InstantiateInfo, &Result.pModel, Result.RootEntity);
    if (Result.Status == RADIENT_STATUS_PENDING)
        Result.Status = FinishPendingSceneImports(Fixture, Result.pModel);

    return Result;
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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported hierarchy";

    const ImportSceneResult ImportResult = ImportSceneAndFinishPending(Fixture, LoadInfo, InstantiateInfo);
    EXPECT_EQ(ImportResult.Status, RADIENT_STATUS_OK);
    EXPECT_NE(ImportResult.RootEntity, InvalidRadientEntityID);

    // The importer creates an explicit Radient root, with the glTF scene root
    // as its only child.
    const std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, ImportResult.RootEntity);
    ASSERT_EQ(RootChildren.size(), 1u);

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pScene->GetParent(RootChildren[0], Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, ImportResult.RootEntity);

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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Default scene";

    const ImportSceneResult DefaultImport = ImportSceneAndFinishPending(Fixture, LoadInfo, InstantiateInfo);
    EXPECT_EQ(DefaultImport.Status, RADIENT_STATUS_OK);

    std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, DefaultImport.RootEntity);
    ASSERT_EQ(RootChildren.size(), 1u);

    RadientTransform Transform{};
    EXPECT_EQ(Fixture.pScene->GetLocalTransform(RootChildren[0], Transform), RADIENT_STATUS_OK);
    // No explicit index means the glTF default scene, scene 1, is imported.
    ExpectFloat3Near(Transform.Position, {2.f, 0.f, 0.f});

    InstantiateInfo.Name       = "Explicit scene 0";
    InstantiateInfo.SceneIndex = 0;

    const ImportSceneResult ExplicitImport = ImportSceneAndFinishPending(Fixture, LoadInfo, InstantiateInfo);
    EXPECT_EQ(ExplicitImport.Status, RADIENT_STATUS_OK);

    RootChildren = GetChildren(*Fixture.pScene, ExplicitImport.RootEntity);
    ASSERT_EQ(RootChildren.size(), 1u);

    EXPECT_EQ(Fixture.pScene->GetLocalTransform(RootChildren[0], Transform), RADIENT_STATUS_OK);
    // Requesting scene 0 should instantiate the alternate root node.
    ExpectFloat3Near(Transform.Position, {1.f, 0.f, 0.f});

    InstantiateInfo.Name         = "Invalid scene";
    InstantiateInfo.SceneIndex   = 2;
    RadientEntityID ImportedRoot = InvalidRadientEntityID;

    // Invalid scene indices should fail without returning a created root.
    EXPECT_EQ(Fixture.pImporter->InstantiateScene(DefaultImport.pModel, InstantiateInfo, ImportedRoot), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(ImportedRoot, InvalidRadientEntityID);
}

TEST(RadientSceneImporterTest, InstantiateSceneUsesCachedModel)
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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    const RADIENT_STATUS              LoadStatus = Fixture.pAssetManager->LoadScene(LoadInfo, &pModel);
    ASSERT_TRUE(LoadStatus == RADIENT_STATUS_OK || LoadStatus == RADIENT_STATUS_PENDING);
    ASSERT_NE(pModel, nullptr);
    ASSERT_NE(pModel->GetReference().URI, nullptr);
    ASSERT_EQ(ProcessSceneLoad(Fixture, pModel), RADIENT_STATUS_OK);

    EXPECT_EQ(std::remove(GLTFPath.c_str()), 0);

    // The cached model should still be available after the source file is gone.
    RadientEntityID RootEntity = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pImporter->InstantiateScene(pModel, {}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_NE(RootEntity, InvalidRadientEntityID);

    const std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, RootEntity);
    ASSERT_EQ(RootChildren.size(), 1u);
}

TEST(RadientSceneImporterTest, InstantiateSceneReportsPendingWhileModelLoads)
{
    // Uses a stopped thread pool so LoadScene can enter the pending state and
    // InstantiateScene can create a placeholder root without blocking.
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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    EXPECT_EQ(Fixture.pAssetManager->LoadScene(LoadInfo, &pModel), RADIENT_STATUS_PENDING);
    ASSERT_NE(pModel, nullptr);
    ASSERT_NE(pModel->GetReference().URI, nullptr);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pImporter->InstantiateScene(pModel, {}, RootEntity), RADIENT_STATUS_PENDING);
    ASSERT_NE(RootEntity, InvalidRadientEntityID);

    // Pending imports should expose an empty root until the model load completes.
    std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, RootEntity);
    EXPECT_TRUE(RootChildren.empty());

    // Once the asset is loaded and pending imports are processed, the root
    // should be populated with the glTF scene graph.
    ASSERT_EQ(ProcessSceneLoad(Fixture, pModel), RADIENT_STATUS_OK);
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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RefCntAutoPtr<IRadientSceneAsset> pModel;
    EXPECT_EQ(Fixture.pAssetManager->LoadScene(LoadInfo, &pModel), RADIENT_STATUS_PENDING);
    ASSERT_NE(pModel, nullptr);
    ASSERT_NE(pModel->GetReference().URI, nullptr);

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.SceneIndex = 1;

    RadientEntityID RootEntity = InvalidRadientEntityID;
    EXPECT_EQ(Fixture.pImporter->InstantiateScene(pModel, InstantiateInfo, RootEntity), RADIENT_STATUS_PENDING);
    ASSERT_NE(RootEntity, InvalidRadientEntityID);
    EXPECT_EQ(Fixture.pScene->IsEntityAlive(RootEntity), RADIENT_STATUS_OK);

    // Processing the completed load should fail and destroy the placeholder.
    ASSERT_EQ(ProcessSceneLoad(Fixture, pModel), RADIENT_STATUS_OK);
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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported cameras";

    const ImportSceneResult ImportResult = ImportSceneAndFinishPending(Fixture, LoadInfo, InstantiateInfo);
    EXPECT_EQ(ImportResult.Status, RADIENT_STATUS_OK);

    const std::vector<RadientEntityID> CameraNodes = GetChildren(*Fixture.pScene, ImportResult.RootEntity);
    ASSERT_EQ(CameraNodes.size(), 2u);

    RadientCameraComponent Camera{};
    // Perspective yfov/aspect should be converted into Radient aperture and
    // focal-length values while preserving clipping range.
    EXPECT_EQ(Fixture.pScene->GetCamera(CameraNodes[0], Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera.Projection, RADIENT_CAMERA_PROJECTION_PERSPECTIVE);
    ExpectFloat2Near(Camera.ClippingRange, {0.2f, 250.f});
    EXPECT_NEAR(Camera.HorizontalAperture, Camera.VerticalAperture * 1.5f, EPSILON);
    EXPECT_NEAR(Camera.FocalLength, Camera.VerticalAperture / (2.f * std::tan(0.7f * 0.5f)), EPSILON);

    // Orthographic xmag/ymag are glTF half-extents; Radient stores full view size.
    EXPECT_EQ(Fixture.pScene->GetCamera(CameraNodes[1], Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera.Projection, RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC);
    EXPECT_NEAR(Camera.HorizontalAperture, 8.f, EPSILON);
    EXPECT_NEAR(Camera.VerticalAperture, 6.f, EPSILON);
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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported mesh metadata";

    const ImportSceneResult ImportResult = ImportSceneAndFinishPending(Fixture, LoadInfo, InstantiateInfo);
    EXPECT_EQ(ImportResult.Status, RADIENT_STATUS_OK);
    ASSERT_NE(ImportResult.pModel, nullptr);

    const std::vector<RadientEntityID> RootChildren = GetChildren(*Fixture.pScene, ImportResult.RootEntity);
    ASSERT_EQ(RootChildren.size(), 2u);

    // The synthetic import root is only a grouping node and should not be renderable.
    Bool HasComponent = True;
    EXPECT_EQ(Fixture.pScene->HasComponent(ImportResult.RootEntity, RADIENT_COMPONENT_TYPE_MESH, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
    EXPECT_EQ(Fixture.pScene->HasComponent(ImportResult.RootEntity, RADIENT_COMPONENT_TYPE_MESH_RENDERER, HasComponent), RADIENT_STATUS_OK);
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
    EXPECT_NE(std::string{RenderableMeshes[0]->GetReference().URI}, std::string{ImportResult.pModel->GetReference().URI});
    // Both nodes reference the same converted Radient mesh asset.
    EXPECT_EQ(RenderableMeshes[0], RenderableMeshes[1]);

    for (IRadientMeshAsset* pMesh : RenderableMeshes)
    {
        // The asset manager should expose mesh metadata even without a GPU
        // device. GPU buffer fields stay empty until a GPU-backed manager is
        // used.
        const RadientDrawableMeshResolveResult Result =
            RadientAssetManagerImpl::GetDrawableMesh(pMesh, false);
        EXPECT_EQ(Result.Status, RADIENT_STATUS_OK);
        ASSERT_NE(Result.pMesh, nullptr);
        ASSERT_EQ(Result.pMesh->Geometries.size(), 1u);
        EXPECT_EQ(Result.pMesh->Geometries[0].pVertexPool, nullptr);
        EXPECT_EQ(Result.pMesh->Geometries[0].VertexAttribFlags, PBR_Renderer::PSO_FLAG_NONE);
        EXPECT_EQ(Result.pMesh->Geometries[0].FirstIndexLocation, 0u);
        EXPECT_EQ(Result.pMesh->Geometries[0].BaseVertex, 0u);

        ASSERT_EQ(Result.pMesh->Primitives.size(), 1u);
        const RadientDrawableMeshPrimitive& ResolvedPrimitive = Result.pMesh->Primitives[0];
        EXPECT_NE(ResolvedPrimitive.pMaterialAsset, nullptr);
        EXPECT_EQ(ResolvedPrimitive.GeometryIndex, 0u);
        EXPECT_EQ(ResolvedPrimitive.IsIndexed, true);
        EXPECT_EQ(ResolvedPrimitive.FirstElement, 0u);
        EXPECT_EQ(ResolvedPrimitive.ElementCount, 3u);
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

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Imported lights";

    const ImportSceneResult ImportResult = ImportSceneAndFinishPending(Fixture, LoadInfo, InstantiateInfo);
    EXPECT_EQ(ImportResult.Status, RADIENT_STATUS_OK);
    EXPECT_EQ(Fixture.pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::vector<RadientEntityID> LightNodes = GetChildren(*Fixture.pScene, ImportResult.RootEntity);
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

TEST(RadientSceneImporterTest, ImportsSkinsAndCreatesPosesPerSceneInstance)
{
    TempDirectory     TempDir{"RadientSceneImporterTest"};
    const std::string GLTFPath = WriteGLTFSkinFile(TempDir);

    ImportFixture Fixture = CreateImportFixture();
    ASSERT_NE(Fixture.pImporter, nullptr);
    ASSERT_NE(Fixture.pScene, nullptr);
    ASSERT_NE(Fixture.pWriter, nullptr);

    RadientSceneLoadInfo LoadInfo{};
    LoadInfo.URI = GLTFPath.c_str();

    RadientSceneInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "First skinned instance";

    const ImportSceneResult ImportResult = ImportSceneAndFinishPending(Fixture, LoadInfo, InstantiateInfo);
    ASSERT_EQ(ImportResult.Status, RADIENT_STATUS_OK);
    ASSERT_NE(ImportResult.pModel, nullptr);

    const RadientSceneAssetDesc& SceneAssetDesc = ImportResult.pModel->GetDesc();
    ASSERT_EQ(SceneAssetDesc.AnimationCount, 1u);
    ASSERT_NE(SceneAssetDesc.pAnimations, nullptr);
    const RadientSceneAnimationDesc& SceneAnimation = SceneAssetDesc.pAnimations[0];
    EXPECT_STREQ(SceneAnimation.Name, "Joint motion");
    EXPECT_FLOAT_EQ(SceneAnimation.Duration, 2.f);
    ASSERT_EQ(SceneAnimation.SkeletonAnimationCount, 1u);
    ASSERT_NE(SceneAnimation.pSkeletonAnimations, nullptr);
    ASSERT_NE(SceneAnimation.pSkeletonAnimations[0].pAnimation, nullptr);
    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pImportedAnimation{
        SceneAnimation.pSkeletonAnimations[0].pAnimation};

    ASSERT_EQ(Fixture.pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const RadientSceneImpl* pSceneImpl = ClassPtrCast<RadientSceneImpl>(Fixture.pScene.RawPtr());
    ASSERT_NE(pSceneImpl, nullptr);

    struct CapturedSkin
    {
        RefCntAutoPtr<IRadientSkinAsset>    pSkin;
        RefCntAutoPtr<IRadientSkeletonPose> pPose;
    };

    const auto CaptureSkins = [pSceneImpl]() {
        std::vector<CapturedSkin> Skins;
        EXPECT_EQ(pSceneImpl->GetState().EnumerateRenderableMeshes(
                      [&Skins](const RadientSceneState::RenderableMesh& Mesh) {
                          EXPECT_NE(Mesh.pSkin, nullptr);
                          if (Mesh.pSkin != nullptr)
                          {
                              CapturedSkin Skin;
                              Skin.pSkin = Mesh.pSkin->pSkin;
                              Skin.pPose = Mesh.pSkin->pPose;
                              Skins.emplace_back(std::move(Skin));
                          }
                      }),
                  RADIENT_STATUS_OK);
        return Skins;
    };

    std::vector<CapturedSkin> Skins = CaptureSkins();
    ASSERT_EQ(Skins.size(), 2u);
    ASSERT_NE(Skins[0].pSkin, nullptr);
    ASSERT_NE(Skins[0].pPose, nullptr);
    EXPECT_EQ(Skins[1].pSkin, Skins[0].pSkin);
    EXPECT_EQ(Skins[1].pPose, Skins[0].pPose);

    const RadientSkinDesc& SkinDesc = Skins[0].pSkin->GetDesc();
    ASSERT_NE(SkinDesc.pSkeleton, nullptr);
    EXPECT_EQ(pImportedAnimation->GetDesc().pSkeleton, SkinDesc.pSkeleton);
    ASSERT_EQ(SkinDesc.JointCount, 2u);
    EXPECT_EQ(SkinDesc.pJoints[0].SkeletonJointIndex, 2u);
    EXPECT_EQ(SkinDesc.pJoints[1].SkeletonJointIndex, 3u);
    EXPECT_EQ(SkinDesc.pJoints[0].InverseBindMatrix, RadientMatrix4x4{});
    const RadientMatrix4x4 ScaledInverseBind{
        2.f, 0.f, 0.f, 0.f,
        0.f, 3.f, 0.f, 0.f,
        0.f, 0.f, 4.f, 0.f,
        0.f, 0.f, 0.f, 1.f};
    EXPECT_EQ(SkinDesc.pJoints[1].InverseBindMatrix, ScaledInverseBind);

    const RadientSkeletonDesc& SkeletonDesc = SkinDesc.pSkeleton->GetDesc();
    ASSERT_EQ(SkeletonDesc.JointCount, 4u);
    EXPECT_STREQ(SkeletonDesc.pJoints[0].Name, "Root");
    EXPECT_EQ(SkeletonDesc.pJoints[0].ParentJointIndex, InvalidRadientJointIndex);
    EXPECT_STREQ(SkeletonDesc.pJoints[1].Name, "NonJointAncestor");
    EXPECT_EQ(SkeletonDesc.pJoints[1].ParentJointIndex, 0u);
    EXPECT_STREQ(SkeletonDesc.pJoints[2].Name, "JointA");
    EXPECT_EQ(SkeletonDesc.pJoints[2].ParentJointIndex, 1u);
    EXPECT_STREQ(SkeletonDesc.pJoints[3].Name, "JointB");
    EXPECT_EQ(SkeletonDesc.pJoints[3].ParentJointIndex, 2u);

    std::array<RadientTransform, 4> LocalTransforms{};
    ASSERT_EQ(Skins[0].pPose->GetJointLocalTransforms(0, 4, LocalTransforms.data()), RADIENT_STATUS_OK);
    ExpectFloat3Near(LocalTransforms[0].Position, {1.f, 0.f, 0.f});
    ExpectFloat3Near(LocalTransforms[1].Position, {0.f, 2.f, 0.f});
    ExpectFloat3Near(LocalTransforms[2].Position, {0.f, 0.f, 3.f});
    ExpectFloat3Near(LocalTransforms[3].Scale, {2.f, 3.f, 4.f});

    RefCntAutoPtr<IRadientSkeletonPose> pFirstInstancePose = Skins[0].pPose;
    ASSERT_EQ(pImportedAnimation->Evaluate(1.0, Skins[0].pPose, True), RADIENT_STATUS_OK);
    ASSERT_EQ(Skins[0].pPose->GetJointLocalTransforms(0, 4, LocalTransforms.data()), RADIENT_STATUS_OK);
    ExpectFloat3Near(LocalTransforms[2].Position, {1.f, 0.f, 3.f});

    RadientSceneInstantiateInfo SecondInstantiateInfo{};
    SecondInstantiateInfo.Name = "Second skinned instance";
    RadientEntityID SecondRoot = InvalidRadientEntityID;
    ASSERT_EQ(Fixture.pImporter->InstantiateScene(ImportResult.pModel, SecondInstantiateInfo, SecondRoot),
              RADIENT_STATUS_OK);
    ASSERT_NE(SecondRoot, InvalidRadientEntityID);
    ASSERT_EQ(Fixture.pWriter->CommitChanges(), RADIENT_STATUS_OK);

    Skins = CaptureSkins();
    ASSERT_EQ(Skins.size(), 4u);

    IRadientSkeletonPose* pSecondPose        = nullptr;
    Uint32                FirstPoseUseCount  = 0;
    Uint32                SecondPoseUseCount = 0;
    for (const CapturedSkin& Skin : Skins)
    {
        EXPECT_EQ(Skin.pSkin, Skins[0].pSkin);
        if (Skin.pPose == pFirstInstancePose)
        {
            ++FirstPoseUseCount;
        }
        else
        {
            if (pSecondPose == nullptr)
                pSecondPose = Skin.pPose;
            EXPECT_EQ(Skin.pPose, pSecondPose);
            ++SecondPoseUseCount;
        }
    }

    EXPECT_NE(pSecondPose, nullptr);
    EXPECT_NE(pSecondPose, pFirstInstancePose);
    EXPECT_EQ(FirstPoseUseCount, 2u);
    EXPECT_EQ(SecondPoseUseCount, 2u);

    ASSERT_EQ(pSecondPose->GetJointLocalTransforms(0, 4, LocalTransforms.data()), RADIENT_STATUS_OK);
    ExpectFloat3Near(LocalTransforms[2].Position, {0.f, 0.f, 3.f});
}

} // namespace
