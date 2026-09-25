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

#include "gtest/gtest.h"
#include "TestingEnvironment.hpp"
#include "ThreadPool.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMeshAssetManager.hpp"
#include "Import/RadientOBJSceneAssetImporter.hpp"
#include "RadientMaterialTestHelpers.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RadientTestAssetHelpers.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

constexpr const char* Triangle = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

// Runs an optional callback after the next task is queued, before EnqueueTask
// returns. This deterministically reproduces a worker completing a texture load
// before LoadTexture reads the handle's status.
class ImportTestThreadPool final : public ObjectBase<IThreadPool>
{
public:
    using TBase = ObjectBase<IThreadPool>;
    explicit ImportTestThreadPool(IReferenceCounters* pRefCounters) :
        TBase{pRefCounters},
        m_pThreadPool{CreateThreadPool(ThreadPoolCreateInfo{0})}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_ThreadPool, TBase)

    bool DILIGENT_CALL_TYPE EnqueueTask(IAsyncTask* pTask, IAsyncTask** ppPrerequisites, Uint32 NumPrerequisites) override
    {
        const bool Enqueued = m_pThreadPool->EnqueueTask(pTask, ppPrerequisites, NumPrerequisites);
        if (Enqueued && OnNextEnqueue)
        {
            const std::function<void()> Callback = std::move(OnNextEnqueue);
            OnNextEnqueue                        = {};
            Callback();
        }
        return Enqueued;
    }

    bool DILIGENT_CALL_TYPE ReprioritizeTask(IAsyncTask* pTask) override
    {
        return m_pThreadPool->ReprioritizeTask(pTask);
    }
    void DILIGENT_CALL_TYPE ReprioritizeAllTasks() override
    {
        m_pThreadPool->ReprioritizeAllTasks();
    }
    bool DILIGENT_CALL_TYPE RemoveTask(IAsyncTask* pTask) override
    {
        return m_pThreadPool->RemoveTask(pTask);
    }
    void DILIGENT_CALL_TYPE WaitForAllTasks() override
    {
        m_pThreadPool->WaitForAllTasks();
    }
    Uint32 DILIGENT_CALL_TYPE GetQueueSize() override
    {
        return m_pThreadPool->GetQueueSize();
    }
    Uint32 DILIGENT_CALL_TYPE GetRunningTaskCount() const override
    {
        return m_pThreadPool->GetRunningTaskCount();
    }
    void DILIGENT_CALL_TYPE StopThreads() override
    {
        m_pThreadPool->StopThreads();
    }
    bool DILIGENT_CALL_TYPE ProcessTask(Uint32 ThreadId, bool WaitForTask) override
    {
        return m_pThreadPool->ProcessTask(ThreadId, WaitForTask);
    }

    std::function<void()> OnNextEnqueue;

private:
    RefCntAutoPtr<IThreadPool> m_pThreadPool;
};

class RadientOBJSceneAssetImporterTest : public testing::Test
{
protected:
    void SetUp() override
    {
        pThreadPool = MakeNewRCObj<ImportTestThreadPool>()();
        pResolver   = MakeNewRCObj<TestRadientAssetResolver>()();
        RadientAssetManagerImpl::CreateInfo CI;
        CI.pThreadPool           = pThreadPool;
        CI.Assets.pAssetResolver = pResolver;
        pManager                 = RadientAssetManagerImpl::Create(CI);
        ASSERT_NE(pManager, nullptr);
        pServices = pManager->CreateMeshImportServices();
        pImporter = CreateRadientOBJSceneAssetImporter();
        ASSERT_EQ(CreateStandardMaterialAsset(static_cast<IRadientAssetManager&>(*pManager), {}, &pDefaultMaterial), RADIENT_STATUS_OK);
    }

    void TearDown() override
    {
        for (Uint32 Iteration = 0; pThreadPool->GetQueueSize() != 0 && Iteration < 128; ++Iteration)
        {
            EXPECT_TRUE(pThreadPool->ProcessTask(0, false));
        }
        EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
        pThreadPool->StopThreads();
    }

    void AddText(const char* URI, const char* ResolvedURI, const std::string& Text)
    {
        pResolver->AddAsset(URI, ResolvedURI, {Text.begin(), Text.end()});
    }

    RADIENT_STATUS Import(const std::string& Text, RadientImport::ImportedDocument& Document)
    {
        RefCntAutoPtr<TestRadientAssetData> pSource{
            MakeNewRCObj<TestRadientAssetData>()(
                std::vector<Uint8>{Text.begin(), Text.end()}, "memory://models/model.obj", pSourceStats)};
        RadientSceneAssetImportContext Context;
        Context.pSourceData         = pSource;
        Context.pAssetResolver      = pResolver;
        Context.pAssetManager       = pManager;
        Context.pMeshImportServices = pServices;
        Context.pDefaultMaterial    = pDefaultMaterial;
        return pImporter->Import(Context, Document);
    }

    RefCntAutoPtr<IRadientSceneAsset> Load(const char* URI, const char* ImporterId = nullptr)
    {
        RadientSceneLoadInfo LoadInfo;
        LoadInfo.URI        = URI;
        LoadInfo.ImporterId = ImporterId;
        RefCntAutoPtr<IRadientSceneAsset> pScene;
        EXPECT_EQ(pManager->LoadScene(LoadInfo, &pScene), RADIENT_STATUS_PENDING);
        return pScene;
    }

    RefCntAutoPtr<ImportTestThreadPool>            pThreadPool;
    RefCntAutoPtr<TestRadientAssetResolver>        pResolver;
    RefCntAutoPtr<RadientAssetManagerImpl>         pManager;
    RefCntAutoPtr<IRadientMeshImportServices>      pServices;
    RefCntAutoPtr<IRadientSceneAssetImporter>      pImporter;
    RefCntAutoPtr<IRadientMaterialAsset>           pDefaultMaterial;
    std::shared_ptr<TestRadientAssetResolverStats> pSourceStats = std::make_shared<TestRadientAssetResolverStats>();
};

TEST_F(RadientOBJSceneAssetImporterTest, RecognizesOBJURIs)
{
    EXPECT_STREQ(pImporter->GetIdentifier(), "obj");
    EXPECT_TRUE(pImporter->CanImport("model.obj"));
    EXPECT_TRUE(pImporter->CanImport("memory://models/MODEL.OBJ?version=2#mesh"));
    EXPECT_FALSE(pImporter->CanImport("model.obj.png"));
    EXPECT_FALSE(pImporter->CanImport("model.gltf"));
}

TEST_F(RadientOBJSceneAssetImporterTest, BuiltInImporterLoadsAutomaticallyAndByIdentifier)
{
    AddText("triangle.obj", "memory://triangle.obj", Triangle);
    AddText("triangle.data", "memory://triangle.data", Triangle);
    for (const char* URI : {"triangle.obj", "triangle.data"})
    {
        const RefCntAutoPtr<IRadientSceneAsset> pScene = Load(URI, std::strcmp(URI, "triangle.data") == 0 ? "obj" : nullptr);
        ASSERT_NE(pScene, nullptr);
        ASSERT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
        const RadientImport::ImportedDocument* pDocument = RadientAssetManagerImpl::GetImportedScene(pScene);
        ASSERT_NE(pDocument, nullptr);
        ASSERT_EQ(pDocument->Meshes.size(), 1u);
        ASSERT_EQ(pDocument->Nodes.size(), 1u);
        ASSERT_EQ(pDocument->Scenes.size(), 1u);
        EXPECT_EQ(pDocument->Scenes[0].RootNodes, (std::vector<Uint32>{0}));
        EXPECT_EQ(pDocument->Nodes[0].pMesh, pDocument->Meshes[0]);
        const RadientMeshAssetDesc& Desc = pDocument->Meshes[0]->GetDesc();
        ASSERT_EQ(Desc.GeometryCount, 1u);
        EXPECT_EQ(Desc.pGeometries[0].VertexCount, 3u);
        EXPECT_EQ(Desc.pGeometries[0].IndexCount, 3u);
    }
}

TEST_F(RadientOBJSceneAssetImporterTest, SharesGeometryAndRetainsParsedBuffersAfterSourceIsDestroyed)
{
    // Both object views reference one vertex/index domain. Processing starts
    // only after Import returns and the source text has been destroyed.
    RadientImport::ImportedDocument Document;
    ASSERT_EQ(Import("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 1 1 0\ns 1\n"
                     "o First\nf 1 2 3\no Second\nf 2 4 3\n",
                     Document),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);
    EXPECT_EQ(pSourceStats->AssetDataDestroyCount, 1u);
    ASSERT_EQ(Document.Meshes.size(), 2u);
    ASSERT_EQ(Document.Nodes.size(), 2u);
    EXPECT_EQ(Document.Nodes[0].Name, "First");
    EXPECT_EQ(Document.Nodes[1].Name, "Second");
    EXPECT_GT(pThreadPool->GetQueueSize(), 0u);
    for (const RefCntAutoPtr<IRadientMeshAsset>& pMesh : Document.Meshes)
    {
        ASSERT_EQ(pManager->WaitForAssetLoad(pMesh), RADIENT_STATUS_OK);
    }
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(Document.Meshes[0], 0),
              RadientMeshAssetManager::GetMeshVertexDataPayload(Document.Meshes[1], 0));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(Document.Meshes[0], 0),
              RadientMeshAssetManager::GetMeshIndexDataPayload(Document.Meshes[1], 0));
    for (Uint32 MeshIndex = 0; MeshIndex < 2; ++MeshIndex)
    {
        const RadientMeshAssetDesc& Desc = Document.Meshes[MeshIndex]->GetDesc();
        ASSERT_EQ(Desc.GeometryCount, 1u);
        EXPECT_EQ(Desc.pGeometries[0].VertexCount, 4u);
        EXPECT_EQ(Desc.pGeometries[0].IndexCount, 6u);
        ASSERT_EQ(Desc.PrimitiveCount, 1u);
        EXPECT_EQ(Desc.pPrimitives[0].FirstElement, MeshIndex * 3);
        EXPECT_EQ(Desc.pPrimitives[0].ElementCount, 3u);
        EXPECT_EQ(Desc.pPrimitives[0].pMaterial, pDefaultMaterial);
    }
}

TEST_F(RadientOBJSceneAssetImporterTest, ConvertsMTLFactorsAndMaterialRanges)
{
    AddText("materials/surface.mtl", "memory://materials/surface.mtl",
            "newmtl Paint\nKd 0.2 0.4 0.6\nKs 0.1 0.2 0.3\nKe 0.4 0.3 0.2\nNs 30\nd 0.75\n"
            "newmtl Flat\nillum 0\nKd 0.7 0.8 0.9\n");
    RadientImport::ImportedDocument Document;
    ASSERT_EQ(Import(std::string{"mtllib materials/surface.mtl\nusemtl Paint\n"} + Triangle +
                         "usemtl Flat\nf 1 2 3\n",
                     Document),
              RADIENT_STATUS_OK);
    EXPECT_EQ(pResolver->GetStats().LastBaseURI, "memory://models/model.obj");
    ASSERT_EQ(Document.Meshes.size(), 1u);
    ASSERT_EQ(pManager->WaitForAssetLoad(Document.Meshes[0]), RADIENT_STATUS_OK);
    const RadientMeshAssetDesc& Desc = Document.Meshes[0]->GetDesc();
    ASSERT_EQ(Desc.PrimitiveCount, 2u);
    IRadientMaterialAsset* pPaint = Desc.pPrimitives[0].pMaterial;
    IRadientMaterialAsset* pFlat  = Desc.pPrimitives[1].pMaterial;
    ASSERT_NE(pPaint, nullptr);
    ASSERT_NE(pFlat, nullptr);
    EXPECT_NE(pPaint, pFlat);
    const RadientFloat4 Diffuse = GetMaterialParameter<RadientFloat4>(*pPaint, RadientStandardMaterialDiffuseFactorName);
    EXPECT_FLOAT_EQ(Diffuse.x, 0.2f);
    EXPECT_FLOAT_EQ(Diffuse.y, 0.4f);
    EXPECT_FLOAT_EQ(Diffuse.z, 0.6f);
    EXPECT_FLOAT_EQ(Diffuse.w, 0.75f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat3>(*pPaint, RadientStandardMaterialSpecularFactorName).z, 0.3f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat3>(*pPaint, RadientStandardMaterialEmissiveFactorName).x, 0.4f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pPaint, RadientStandardMaterialGlossinessFactorName), 0.75f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat4>(*pFlat, RadientStandardMaterialBaseColorFactorName).y, 0.8f);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurface{pPaint, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurface, nullptr);
    EXPECT_EQ(pSurface->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT);
}

TEST_F(RadientOBJSceneAssetImporterTest, ResolvesTexturesRelativeToMTLAndPreservesMapping)
{
    AddText("surface.mtl", "memory://materials/surface.mtl",
            "newmtl Paint\nmap_Kd -s 2 3 -o 0.1 0.2 -clamp on ../images/white.png\n");
    pResolver->AddAsset("../images/white.png", "memory://images/white.png", {WhitePng.begin(), WhitePng.end()});
    RadientImport::ImportedDocument Document;
    ASSERT_EQ(Import(std::string{"mtllib surface.mtl\nusemtl Paint\n"} + Triangle, Document), RADIENT_STATUS_OK);
    ASSERT_EQ(Document.Textures.size(), 1u);
    ASSERT_EQ(Document.Meshes.size(), 1u);
    ASSERT_EQ(pManager->WaitForAssetLoad(Document.Meshes[0]), RADIENT_STATUS_OK);
    const RadientMeshAssetDesc& Desc = Document.Meshes[0]->GetDesc();
    ASSERT_EQ(Desc.PrimitiveCount, 1u);
    IRadientMaterialAsset& Material = *Desc.pPrimitives[0].pMaterial;
    const RadientFloat2    Bias     = GetMaterialParameter<RadientFloat2>(Material, "DiffuseTextureUVBias");
    EXPECT_FLOAT_EQ(Bias.x, 0.1f);
    EXPECT_FLOAT_EQ(Bias.y, 0.2f);
    const float2x2 Scale = GetMaterialParameter<float2x2>(Material, "DiffuseTextureUVScaleAndRotation");
    EXPECT_FLOAT_EQ(Scale._11, 2.f);
    EXPECT_FLOAT_EQ(Scale._22, 3.f);
    EXPECT_EQ(GetMaterialParameter<Uint32>(Material, "DiffuseTextureWrapU"), static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP));
    ASSERT_EQ(pManager->WaitForAssetLoad(Document.Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(pResolver->GetStats().LastBaseURI, "memory://materials/surface.mtl");
    EXPECT_EQ(Document.Textures[0]->GetDesc().Format, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM_SRGB);
}

TEST_F(RadientOBJSceneAssetImporterTest, RetainsFailedTexturesRegardlessOfCompletionTiming)
{
    Uint32 CaseIndex = 0;
    for (RADIENT_STATUS Failure : {RADIENT_STATUS_CANCELLED, RADIENT_STATUS_INVALID_OPERATION})
    {
        for (bool CompleteBeforeReturn : {false, true})
        {
            SCOPED_TRACE(Failure);
            SCOPED_TRACE(CompleteBeforeReturn);
            // Distinct URIs prevent a previous failed texture payload from being reused.
            const std::string Stem        = "surface" + std::to_string(CaseIndex++);
            const std::string MaterialURI = Stem + ".mtl";
            const std::string TextureURI  = Stem + ".png";
            AddText(MaterialURI.c_str(), ("memory://materials/" + MaterialURI).c_str(),
                    "newmtl Paint\nmap_Kd -o 0.1 0.2 " + TextureURI + "\n");
            pResolver->AddAsset(TextureURI, "memory://images/" + TextureURI, {WhitePng.begin(), WhitePng.end()});

            bool       TextureTaskRan  = false;
            const auto FailTextureLoad = [&]() {
                // Only the texture task sees this failure; the MTL has already loaded.
                pResolver->SetOpenAssetStatus(Failure);
                EXPECT_TRUE(pThreadPool->ProcessTask(0, false));
                pResolver->SetOpenAssetStatus(RADIENT_STATUS_OK);
                TextureTaskRan = true;
            };
            if (CompleteBeforeReturn)
                pThreadPool->OnNextEnqueue = FailTextureLoad;

            RadientImport::ImportedDocument Document;
            ASSERT_EQ(Import("mtllib " + MaterialURI + "\nusemtl Paint\n" + Triangle, Document), RADIENT_STATUS_OK);
            EXPECT_EQ(TextureTaskRan, CompleteBeforeReturn);
            ASSERT_EQ(Document.Textures.size(), 1u);
            ASSERT_NE(Document.Textures[0], nullptr);
            EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Document.Textures[0]),
                      CompleteBeforeReturn ? Failure : RADIENT_STATUS_PENDING);
            if (!CompleteBeforeReturn)
                FailTextureLoad();
            EXPECT_EQ(pManager->WaitForAssetLoad(Document.Textures[0]), Failure);

            // Keep the requested handle and mapping intact. Material dependencies
            // decide whether a failed source can be replaced by a default texture.
            ASSERT_EQ(Document.Materials.size(), 1u);
            ASSERT_NE(Document.Materials[0], nullptr);
            IRadientMaterialAsset&         Material = *Document.Materials[0];
            RadientMaterialParameterHandle TextureHandle;
            ASSERT_EQ(Material.GetDefinition()->FindParameter(RadientStandardMaterialDiffuseTextureName, &TextureHandle), RADIENT_STATUS_OK);
            RefCntAutoPtr<IRadientTextureAsset> pBoundTexture;
            ASSERT_EQ(Material.GetTexture(TextureHandle, 0, pBoundTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
            EXPECT_EQ(pBoundTexture, Document.Textures[0]);
            EXPECT_EQ(GetMaterialParameter<Int32>(Material, RadientStandardMaterialDiffuseTextureUVSelectorName), 0);
            const RadientFloat2 Bias = GetMaterialParameter<RadientFloat2>(Material, RadientStandardMaterialDiffuseTextureUVBiasName);
            EXPECT_FLOAT_EQ(Bias.x, 0.1f);
            EXPECT_FLOAT_EQ(Bias.y, 0.2f);

            ASSERT_EQ(Document.Meshes.size(), 1u);
            while (pThreadPool->GetQueueSize() != 0)
            {
                ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
            }
            // This CPU-only manager has no default textures, so dependency
            // resolution reports the same source failure in both timing cases.
            EXPECT_EQ(pManager->WaitForAssetLoad(Document.Meshes[0]), Failure);
        }
    }
}

TEST_F(RadientOBJSceneAssetImporterTest, MissingMaterialLibraryUsesDefaultMaterial)
{
    RadientImport::ImportedDocument Document;
    ASSERT_EQ(Import(std::string{"mtllib missing.mtl\nusemtl Missing\n"} + Triangle, Document), RADIENT_STATUS_OK);
    ASSERT_EQ(Document.Meshes.size(), 1u);
    ASSERT_EQ(pManager->WaitForAssetLoad(Document.Meshes[0]), RADIENT_STATUS_OK);
    ASSERT_EQ(Document.Meshes[0]->GetDesc().PrimitiveCount, 1u);
    EXPECT_EQ(Document.Meshes[0]->GetDesc().pPrimitives[0].pMaterial, pDefaultMaterial);
}

TEST_F(RadientOBJSceneAssetImporterTest, InvalidGeometryDoesNotPublishScene)
{
    AddText("invalid.obj", "memory://invalid.obj", "v 0 0 0\nf 1 2 3\n");
    const RefCntAutoPtr<IRadientSceneAsset> pScene = Load("invalid.obj");
    ASSERT_NE(pScene, nullptr);
    TestingEnvironment::ErrorScope ExpectedError{"Failed to parse OBJ source"};
    EXPECT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pScene), nullptr);
}

TEST_F(RadientOBJSceneAssetImporterTest, PropagatesResolverCancellation)
{
    AddText("surface.mtl", "memory://materials/surface.mtl", "newmtl Paint\n");
    pResolver->SetOpenAssetStatus(RADIENT_STATUS_CANCELLED);
    RadientImport::ImportedDocument Document;
    EXPECT_EQ(Import(std::string{"mtllib surface.mtl\n"} + Triangle, Document), RADIENT_STATUS_CANCELLED);
    EXPECT_TRUE(Document.Meshes.empty());
}

} // namespace
