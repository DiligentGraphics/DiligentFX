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
#include "gtest/gtest.h"

#include "RadientEngine.h"
#include "RadientSceneImpl.hpp"

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

TEST(RadientSceneTest, Create)
{
    RefCntAutoPtr<IRadientScene> pScene = RadientSceneImpl::Create();
    EXPECT_NE(pScene, nullptr);
}

TEST(RadientEngineTest, CreateObjects)
{
    RadientEngineCreateInfo EngineCI{};

    RefCntAutoPtr<IRadientEngine> pEngine;
    EXPECT_EQ(CreateRadientEngine(EngineCI, &pEngine), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientBackend> pBackend;
    EXPECT_EQ(pEngine->GetBackend(&pBackend), RADIENT_STATUS_OK);
    ASSERT_NE(pBackend, nullptr);
    EXPECT_EQ(pBackend->GetDesc().Type, RADIENT_BACKEND_TYPE_LOCAL);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    EXPECT_EQ(pEngine->GetAssetManager(&pAssetManager), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetManager, nullptr);

    RadientMaterialCreateInfo MaterialCI{};
    MaterialCI.Name            = "Radient test material";
    MaterialCI.BaseColorFactor = {1.f, 0.f, 0.f, 1.f};

    RadientAssetReference Material{};
    EXPECT_EQ(pAssetManager->CreateMaterial(MaterialCI, Material), RADIENT_STATUS_OK);
    ASSERT_NE(Material.URI, nullptr);
    EXPECT_NE(Material.Version, 0u);

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
    EXPECT_EQ(pAssetManager->CreateMesh(MeshCI, Mesh), RADIENT_STATUS_OK);
    ASSERT_NE(Mesh.URI, nullptr);
    EXPECT_NE(Mesh.Version, 0u);

    RadientGLTFLoadInfo   GLTFLoadInfo{};
    RadientAssetReference GLTFModel{};
    EXPECT_EQ(pAssetManager->LoadGLTF(GLTFLoadInfo, GLTFModel), RADIENT_STATUS_INVALID_ARGUMENT);

    GLTFLoadInfo.URI = "memory://radient/test-model.gltf";
    EXPECT_EQ(pAssetManager->LoadGLTF(GLTFLoadInfo, GLTFModel), RADIENT_STATUS_OK);
    ASSERT_NE(GLTFModel.URI, nullptr);
    EXPECT_NE(GLTFModel.Version, 0u);

    RadientSceneDesc SceneDesc{};
    SceneDesc.Name = "Radient test scene";

    RefCntAutoPtr<IRadientScene> pScene;
    EXPECT_EQ(pEngine->CreateScene(SceneDesc, &pScene), RADIENT_STATUS_OK);
    ASSERT_NE(pScene, nullptr);
    EXPECT_STREQ(pScene->GetDesc().Name, SceneDesc.Name);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    EXPECT_EQ(pEngine->CreateSceneWriter(pScene, &pWriter), RADIENT_STATUS_OK);
    ASSERT_NE(pWriter, nullptr);

    RefCntAutoPtr<IRadientSceneImporter> pImporter;
    EXPECT_EQ(pEngine->CreateSceneImporter(pWriter, &pImporter), RADIENT_STATUS_OK);
    ASSERT_NE(pImporter, nullptr);

    RadientGLTFInstantiateInfo InstantiateInfo{};
    InstantiateInfo.Name = "Radient imported GLTF root";

    RadientAssetReference ImportedModel{};
    RadientEntityID       ImportedRoot = InvalidRadientEntityID;
    EXPECT_EQ(pImporter->ImportGLTF({}, InstantiateInfo, ImportedModel, ImportedRoot), RADIENT_STATUS_INVALID_ARGUMENT);

    EXPECT_EQ(pImporter->ImportGLTF(GLTFLoadInfo, InstantiateInfo, ImportedModel, ImportedRoot), RADIENT_STATUS_OK);
    ASSERT_NE(ImportedModel.URI, nullptr);
    EXPECT_NE(ImportedModel.Version, 0u);
    EXPECT_NE(ImportedRoot, InvalidRadientEntityID);
    EXPECT_EQ(pScene->IsEntityAlive(ImportedRoot), RADIENT_STATUS_OK);

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(pWriter->CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    RadientMeshComponent MeshComponent{};
    MeshComponent.Mesh = Mesh;
    EXPECT_EQ(pWriter->SetMesh(Entity, MeshComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);

    RadientMaterialBinding MaterialBinding{};
    MaterialBinding.PrimitiveIndex = 0;
    MaterialBinding.Material       = Material;

    RadientMaterialBindingsComponent MaterialBindings{};
    MaterialBindings.pBindings    = &MaterialBinding;
    MaterialBindings.BindingCount = 1;
    EXPECT_EQ(pWriter->SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);

    RadientRendererDesc RendererDesc{};
    RendererDesc.Name = "Radient test renderer";

    RefCntAutoPtr<IRadientRenderer> pRenderer;
    EXPECT_EQ(pEngine->CreateRenderer(RendererDesc, &pRenderer), RADIENT_STATUS_OK);
    ASSERT_NE(pRenderer, nullptr);
    EXPECT_STREQ(pRenderer->GetDesc().Name, RendererDesc.Name);

    RadientRenderTargetDesc TargetDesc{};
    TargetDesc.Name = "Radient test target";
    TargetDesc.Size = {640, 480};

    RefCntAutoPtr<IRadientRenderTarget> pTarget;
    EXPECT_EQ(pRenderer->CreateRenderTarget(TargetDesc, &pTarget), RADIENT_STATUS_OK);
    ASSERT_NE(pTarget, nullptr);
    EXPECT_STREQ(pTarget->GetDesc().Name, TargetDesc.Name);
    EXPECT_EQ(pTarget->GetDesc().Size, TargetDesc.Size);

    RadientRenderAttribs RenderAttribs{};
    RenderAttribs.pScene        = pScene;
    RenderAttribs.pRenderTarget = pTarget;

    EXPECT_EQ(pRenderer->Render(RenderAttribs), RADIENT_STATUS_OK);
}

} // namespace
