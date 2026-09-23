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

#include "RadientSceneAssetImporter.hpp"
#include "RadientTypesX.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "TestingEnvironment.hpp"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

// Uses only the public importer context and asset APIs, as an external plugin would.
class TestSceneAssetImporter final : public ObjectBase<IRadientSceneAssetImporter>
{
public:
    using TBase = ObjectBase<IRadientSceneAssetImporter>;

    TestSceneAssetImporter(IReferenceCounters* pRefCounters, std::string Identifier) :
        TBase{pRefCounters}, m_Identifier{std::move(Identifier)}
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSceneAssetImporter, TBase)

    const Char* DILIGENT_CALL_TYPE GetIdentifier() const override
    {
        if (ThrowOnIdentifier)
            throw std::runtime_error{"test identifier failure"};
        return m_Identifier.c_str();
    }

    Bool DILIGENT_CALL_TYPE CanImport(const Char* URI) const override
    {
        ++ProbeCount;
        if (ThrowOnProbe)
            throw std::runtime_error{"test probe failure"};
        if (OnProbe)
            OnProbe();
        const std::string Path = URI != nullptr ? URI : "";
        return Matches && Path.size() >= 5 && Path.compare(Path.size() - 5, 5, ".fake") == 0 ? True : False;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE Import(const RadientSceneAssetImportContext& Context,
                                             RadientImport::ImportedDocument&      Document) override
    {
        if (OnImport)
            return OnImport(Context, Document);

        ++ImportCount;
        EXPECT_NE(Context.pSourceData, nullptr);
        EXPECT_NE(Context.pAssetResolver, nullptr);
        EXPECT_NE(Context.pAssetManager, nullptr);
        EXPECT_NE(Context.pMeshImportServices, nullptr);
        EXPECT_NE(Context.pDefaultMaterial, nullptr);
        if (Context.pSourceData == nullptr || Context.pAssetResolver == nullptr ||
            Context.pAssetManager == nullptr || Context.pMeshImportServices == nullptr ||
            Context.pDefaultMaterial == nullptr)
            return RADIENT_STATUS_FAILED;

        SourceURI = Context.pSourceData->GetResolvedURI();
        SourceBytes.assign(static_cast<const char*>(Context.pSourceData->GetData()), Context.pSourceData->GetSize());
        WeakSource   = Context.pSourceData;
        WeakServices = Context.pMeshImportServices;

        // Failed and throwing imports deliberately leave partially written output.
        // The asset manager must never publish this document.
        Document.Nodes.emplace_back();
        Document.Nodes.back().Name = m_Identifier;
        Document.Scenes.emplace_back();
        Document.Scenes.back().RootNodes.push_back(0);
        if (ThrowOnImport)
            throw std::runtime_error{"test importer failure"};
        if (ReturnStatus != RADIENT_STATUS_OK)
            return ReturnStatus;
        if (!CreateTriangle)
            return RADIENT_STATUS_OK;

        const std::array<RadientFloat3, 3> Positions{{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}};
        const std::array<Uint8, 3>         Indices{0, 1, 2};
        RadientDataBlobCreateInfo          BlobCI;
        BlobCI.pData = Positions.data();
        BlobCI.Size  = sizeof(Positions);
        RefCntAutoPtr<IRadientDataBlob> pVertexBlob;
        RADIENT_STATUS                  Status = CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pVertexBlob);
        if (Status != RADIENT_STATUS_OK)
            return Status;
        BlobCI.pData = Indices.data();
        BlobCI.Size  = sizeof(Indices);
        RefCntAutoPtr<IRadientDataBlob> pIndexBlob;
        Status = CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &pIndexBlob);
        if (Status != RADIENT_STATUS_OK)
            return Status;

        RadientVertexLayoutDescX Layout;
        Layout.AddBuffer().AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3);
        IRadientDataBlob* const               pBuffer = pVertexBlob;
        const RadientMeshVertexData           VertexData{Layout, &pBuffer, static_cast<Uint32>(Positions.size())};
        RefCntAutoPtr<IRadientMeshVertexData> pVertices;
        VertexStatus = Context.pMeshImportServices->CreateMeshVertexData(VertexData, &pVertices);
        if (RADIENT_FAILED(VertexStatus))
            return VertexStatus;

        const RadientMeshIndexData           IndexData{pIndexBlob, static_cast<Uint32>(Indices.size()), RADIENT_INDEX_TYPE_UINT8};
        RefCntAutoPtr<IRadientMeshIndexData> pIndices;
        IndexStatus = Context.pMeshImportServices->CreateMeshIndexData(IndexData, &pIndices);
        if (RADIENT_FAILED(IndexStatus))
            return IndexStatus;

        RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
        Status = Context.pAssetManager->CreateStandardMaterialDefinition({}, &pDefinition);
        if (RADIENT_FAILED(Status))
            return Status;
        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
        Status = Context.pAssetManager->CreateMaterial(pDefinition, &pMaterial);
        if (RADIENT_FAILED(Status))
            return Status;

        const RadientMeshGeometryData  Geometry{pVertices, pIndices};
        RadientMeshPrimitiveCreateInfo Primitive;
        Primitive.Name       = "Imported triangle";
        Primitive.IndexCount = static_cast<Uint32>(Indices.size());
        Primitive.pMaterial  = pMaterial;
        RadientMeshViewCreateInfo ViewCI;
        ViewCI.pPrimitives    = &Primitive;
        ViewCI.PrimitiveCount = 1;
        ViewCI.pGeometryData  = &Geometry;
        ViewCI.GeometryCount  = 1;
        RefCntAutoPtr<IRadientMeshAsset> pMesh;
        MeshStatus = Context.pMeshImportServices->CreateMeshView(ViewCI, &pMesh);
        if (RADIENT_FAILED(MeshStatus))
            return MeshStatus;
        Document.Materials.push_back(pMaterial);
        Document.Meshes.push_back(pMesh);
        Document.Nodes[0].pMesh = pMesh;
        return RADIENT_STATUS_OK;
    }

    Bool                                      Matches           = True;
    bool                                      CreateTriangle    = false;
    bool                                      ThrowOnImport     = false;
    bool                                      ThrowOnIdentifier = false;
    bool                                      ThrowOnProbe      = false;
    RADIENT_STATUS                            ReturnStatus      = RADIENT_STATUS_OK;
    RADIENT_STATUS                            VertexStatus      = RADIENT_STATUS_FAILED;
    RADIENT_STATUS                            IndexStatus       = RADIENT_STATUS_FAILED;
    RADIENT_STATUS                            MeshStatus        = RADIENT_STATUS_FAILED;
    mutable std::atomic<Uint32>               ProbeCount{0};
    Uint32                                    ImportCount = 0;
    std::function<void()>                     OnProbe;
    std::string                               SourceURI;
    std::string                               SourceBytes;
    RefCntWeakPtr<IRadientAssetData>          WeakSource;
    RefCntWeakPtr<IRadientMeshImportServices> WeakServices;

    std::function<RADIENT_STATUS(const RadientSceneAssetImportContext&, RadientImport::ImportedDocument&)> OnImport;

private:
    const std::string m_Identifier;
};

class RadientSceneAssetImporterTest : public testing::Test
{
protected:
    void SetUp() override
    {
        pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
        pResolver   = MakeNewRCObj<TestRadientAssetResolver>()();
        pResolver->AddAsset("first.fake", "memory://resolved/shared.fake", {'a', 'b', 'c'});
        pResolver->AddAsset("alias.fake", "memory://resolved/shared.fake", {'a', 'b', 'c'});
        RadientAssetManagerImpl::CreateInfo CI;
        CI.pThreadPool           = pThreadPool;
        CI.Assets.pAssetResolver = pResolver;
        pManager                 = RadientAssetManagerImpl::Create(CI);
        ASSERT_NE(pManager, nullptr);
    }

    void TearDown() override
    {
        for (Uint32 Iteration = 0; pThreadPool->GetQueueSize() != 0 && Iteration < 64; ++Iteration)
            EXPECT_TRUE(pThreadPool->ProcessTask(0, false));
        EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
        pThreadPool->StopThreads();
    }

    RefCntAutoPtr<TestSceneAssetImporter> Register(const char* Identifier)
    {
        RefCntAutoPtr<TestSceneAssetImporter> pImporter{MakeNewRCObj<TestSceneAssetImporter>()(Identifier)};
        EXPECT_EQ(pManager->RegisterSceneAssetImporter(pImporter), RADIENT_STATUS_OK);
        return pImporter;
    }

    RefCntAutoPtr<IRadientSceneAsset> Load(const char* URI = "first.fake", const char* ImporterId = nullptr)
    {
        RadientSceneLoadInfo LoadInfo;
        LoadInfo.URI        = URI;
        LoadInfo.ImporterId = ImporterId;
        RefCntAutoPtr<IRadientSceneAsset> pScene;
        EXPECT_EQ(pManager->LoadScene(LoadInfo, &pScene), RADIENT_STATUS_PENDING);
        return pScene;
    }

    RefCntAutoPtr<IThreadPool>              pThreadPool;
    RefCntAutoPtr<TestRadientAssetResolver> pResolver;
    RefCntAutoPtr<IRadientAssetManager>     pManager;
};

TEST_F(RadientSceneAssetImporterTest, ImportsThroughPublicServicesAndWaitsForPendingMeshDependencies)
{
    RefCntAutoPtr<TestSceneAssetImporter> pImporter = Register("triangle");
    pImporter->CreateTriangle                       = true;
    RefCntAutoPtr<IRadientSceneAsset> pScene        = Load();
    ASSERT_NE(pScene, nullptr);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);
    EXPECT_EQ(pImporter->ImportCount, 0u);
    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(pImporter->ImportCount, 1u);
    EXPECT_EQ(pImporter->VertexStatus, RADIENT_STATUS_PENDING);
    EXPECT_EQ(pImporter->IndexStatus, RADIENT_STATUS_PENDING);
    EXPECT_EQ(pImporter->MeshStatus, RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pScene), RADIENT_STATUS_PENDING);

    ASSERT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
    const RadientImport::ImportedDocument* pDocument = RadientAssetManagerImpl::GetImportedScene(pScene);
    ASSERT_NE(pDocument, nullptr);
    ASSERT_EQ(pDocument->Nodes.size(), 1u);
    ASSERT_EQ(pDocument->Meshes.size(), 1u);
    ASSERT_EQ(pDocument->Materials.size(), 1u);
    ASSERT_EQ(pDocument->Scenes.size(), 1u);
    EXPECT_EQ(pDocument->Scenes[0].RootNodes, std::vector<Uint32>{0});
    EXPECT_EQ(pDocument->Nodes[0].Name, "triangle");
    EXPECT_EQ(pDocument->Nodes[0].pMesh, pDocument->Meshes[0]);
    const RadientMeshAssetDesc& Desc = pDocument->Meshes[0]->GetDesc();
    ASSERT_EQ(Desc.GeometryCount, 1u);
    EXPECT_EQ(Desc.pGeometries[0].VertexCount, 3u);
    EXPECT_EQ(Desc.pGeometries[0].IndexCount, 3u);
    ASSERT_EQ(Desc.PrimitiveCount, 1u);
    EXPECT_EQ(Desc.pPrimitives[0].pMaterial, pDocument->Materials[0]);
    EXPECT_EQ(pImporter->SourceURI, "memory://resolved/shared.fake");
    EXPECT_EQ(pImporter->SourceBytes, "abc");
    EXPECT_EQ(pImporter->WeakSource.Lock(), nullptr);
    EXPECT_EQ(pImporter->WeakServices.Lock(), nullptr);
}

TEST_F(RadientSceneAssetImporterTest, SelectsFirstMatchAndAllowsExplicitImporterOverride)
{
    RefCntAutoPtr<TestSceneAssetImporter> pFirst     = Register("first");
    RefCntAutoPtr<TestSceneAssetImporter> pSecond    = Register("second");
    RefCntAutoPtr<IRadientSceneAsset>     pAutomatic = Load();
    ASSERT_EQ(pManager->WaitForAssetLoad(pAutomatic), RADIENT_STATUS_OK);
    EXPECT_EQ(pFirst->ProbeCount.load(), 1u);
    EXPECT_EQ(pSecond->ProbeCount.load(), 1u);
    EXPECT_EQ(pFirst->ImportCount, 1u);
    EXPECT_EQ(pSecond->ImportCount, 0u);

    pSecond->Matches = False;
    RadientSceneLoadInfo LoadInfo;
    LoadInfo.URI        = "first.fake";
    LoadInfo.Format     = RADIENT_SCENE_FORMAT_GLTF;
    LoadInfo.ImporterId = "second";
    RefCntAutoPtr<IRadientSceneAsset> pExplicit;
    ASSERT_EQ(pManager->LoadScene(LoadInfo, &pExplicit), RADIENT_STATUS_PENDING);
    ASSERT_EQ(pManager->WaitForAssetLoad(pExplicit), RADIENT_STATUS_OK);
    EXPECT_EQ(pFirst->ProbeCount.load(), 1u);
    EXPECT_EQ(pSecond->ProbeCount.load(), 1u);
    EXPECT_EQ(pFirst->ImportCount, 1u);
    EXPECT_EQ(pSecond->ImportCount, 1u);
    EXPECT_NE(RadientAssetManagerImpl::GetImportedScene(pAutomatic), RadientAssetManagerImpl::GetImportedScene(pExplicit));
    EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pExplicit)->Nodes[0].Name, "second");
}

TEST_F(RadientSceneAssetImporterTest, DeduplicatesResolvedAliasesWithoutReopeningSources)
{
    RefCntAutoPtr<TestSceneAssetImporter> pImporter = Register("alias-importer");
    RefCntAutoPtr<IRadientSceneAsset>     pFirst    = Load();
    ASSERT_EQ(pManager->WaitForAssetLoad(pFirst), RADIENT_STATUS_OK);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 1u);
    pResolver->SetOpenAssetStatus(RADIENT_STATUS_NOT_FOUND);
    RefCntAutoPtr<IRadientSceneAsset> pAlias = Load("alias.fake", "alias-importer");
    ASSERT_EQ(pManager->WaitForAssetLoad(pAlias), RADIENT_STATUS_OK);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 1u);
    EXPECT_EQ(pImporter->ImportCount, 1u);
    EXPECT_NE(pFirst, pAlias);
    EXPECT_STREQ(pFirst->GetReference().URI, "first.fake");
    EXPECT_STREQ(pAlias->GetReference().URI, "alias.fake");
    EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pFirst), RadientAssetManagerImpl::GetImportedScene(pAlias));
}

TEST_F(RadientSceneAssetImporterTest, RetainsImportersAndReleasesManagerAfterCompletedImport)
{
    RefCntAutoPtr<TestSceneAssetImporter>     pImporter = Register("retained");
    RefCntWeakPtr<IRadientSceneAssetImporter> WeakImporter{pImporter.RawPtr()};
    RefCntWeakPtr<IRadientAssetManager>       WeakManager{pManager.RawPtr()};
    pImporter.Release();
    EXPECT_NE(WeakImporter.Lock(), nullptr);
    RefCntAutoPtr<IRadientSceneAsset> pScene = Load();
    ASSERT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
    pManager.Release();
    EXPECT_EQ(WeakManager.Lock(), nullptr);
    EXPECT_EQ(WeakImporter.Lock(), nullptr);
    // A live scene payload must not require either its importer or its manager.
    const RadientImport::ImportedDocument* pDocument = RadientAssetManagerImpl::GetImportedScene(pScene);
    ASSERT_NE(pDocument, nullptr);
    ASSERT_EQ(pDocument->Nodes.size(), 1u);
    EXPECT_EQ(pDocument->Nodes[0].Name, "retained");
}

TEST_F(RadientSceneAssetImporterTest, RejectsInvalidRegistrationAndRegistrationAfterStop)
{
    EXPECT_EQ(pManager->RegisterSceneAssetImporter(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<TestSceneAssetImporter> pEmpty{MakeNewRCObj<TestSceneAssetImporter>()("")};
    EXPECT_EQ(pManager->RegisterSceneAssetImporter(pEmpty), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<TestSceneAssetImporter> pBuiltinDuplicate{MakeNewRCObj<TestSceneAssetImporter>()("gltf")};
    EXPECT_EQ(pManager->RegisterSceneAssetImporter(pBuiltinDuplicate), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<TestSceneAssetImporter> pFirst = Register("unique");
    RefCntAutoPtr<TestSceneAssetImporter> pDuplicate{MakeNewRCObj<TestSceneAssetImporter>()("unique")};
    EXPECT_EQ(pManager->RegisterSceneAssetImporter(pDuplicate), RADIENT_STATUS_INVALID_ARGUMENT);
    RefCntAutoPtr<TestSceneAssetImporter> pCaseDistinct = Register("Unique");
    EXPECT_EQ(pManager->Stop(nullptr), RADIENT_STATUS_OK);
    RefCntAutoPtr<TestSceneAssetImporter> pAfterStop{MakeNewRCObj<TestSceneAssetImporter>()("after-stop")};
    EXPECT_EQ(pManager->RegisterSceneAssetImporter(pAfterStop), RADIENT_STATUS_INVALID_OPERATION);
}

TEST_F(RadientSceneAssetImporterTest, RejectsUnknownSelectionWithoutOpeningSource)
{
    RefCntAutoPtr<IRadientSceneAsset> pScene;
    RadientSceneLoadInfo              LoadInfo;
    LoadInfo.URI = "first.fake";
    {
        TestingEnvironment::ErrorScope ExpectedError{"No Radient scene importer supports URI"};
        EXPECT_EQ(pManager->LoadScene(LoadInfo, &pScene), RADIENT_STATUS_INVALID_ARGUMENT);
    }
    EXPECT_EQ(pScene, nullptr);
    LoadInfo.ImporterId = "missing";
    {
        TestingEnvironment::ErrorScope ExpectedError{"not registered"};
        EXPECT_EQ(pManager->LoadScene(LoadInfo, &pScene), RADIENT_STATUS_UNSUPPORTED);
    }
    EXPECT_EQ(pScene, nullptr);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
}

TEST_F(RadientSceneAssetImporterTest, PreservesRegistrationOrderAndFindsEveryRegisteredIdentifier)
{
    // Exercise selection with more importers than fit in the inline snapshot.
    std::array<RefCntAutoPtr<TestSceneAssetImporter>, 16> Importers;
    std::vector<Uint32>                                   ProbeOrder;
    std::vector<Uint32>                                   ExpectedProbeOrder;
    for (Uint32 Index = 0; Index < Importers.size(); ++Index)
    {
        const std::string Identifier = "importer-" + std::to_string(Index);
        Importers[Index]             = Register(Identifier.c_str());
        Importers[Index]->OnProbe    = [&ProbeOrder, Index]() {
            ProbeOrder.push_back(Index);
        };
        ExpectedProbeOrder.push_back(Index);
    }
    RefCntAutoPtr<IRadientSceneAsset> pAutomatic = Load();
    ASSERT_EQ(pManager->WaitForAssetLoad(pAutomatic), RADIENT_STATUS_OK);
    EXPECT_EQ(ProbeOrder, ExpectedProbeOrder);
    for (Uint32 Index = 0; Index < Importers.size(); ++Index)
    {
        EXPECT_EQ(Importers[Index]->ProbeCount.load(), 1u);
        EXPECT_EQ(Importers[Index]->ImportCount, Index == 0 ? 1u : 0u);
    }

    // Explicit selection bypasses probes and keeps each importer's cache distinct.
    for (const RefCntAutoPtr<TestSceneAssetImporter>& pImporter : Importers)
    {
        RefCntAutoPtr<IRadientSceneAsset> pExplicit = Load("first.fake", pImporter->GetIdentifier());
        ASSERT_EQ(pManager->WaitForAssetLoad(pExplicit), RADIENT_STATUS_OK);
        const RadientImport::ImportedDocument* pDocument = RadientAssetManagerImpl::GetImportedScene(pExplicit);
        ASSERT_NE(pDocument, nullptr);
        ASSERT_EQ(pDocument->Nodes.size(), 1u);
        EXPECT_EQ(pDocument->Nodes[0].Name, pImporter->GetIdentifier());
    }
    EXPECT_EQ(ProbeOrder, ExpectedProbeOrder);
    for (const RefCntAutoPtr<TestSceneAssetImporter>& pImporter : Importers)
    {
        EXPECT_EQ(pImporter->ProbeCount.load(), 1u);
        EXPECT_EQ(pImporter->ImportCount, 1u);
        pImporter->OnProbe = {};
    }
    EXPECT_EQ(pResolver->GetStats().OpenCount, Importers.size());
}

TEST_F(RadientSceneAssetImporterTest, ReentrantRegistrationDoesNotChangeActiveSelectionSnapshot)
{
    RefCntAutoPtr<TestSceneAssetImporter> pProbe = Register("probe");
    pProbe->Matches                              = False;
    RefCntAutoPtr<TestSceneAssetImporter> pLate{MakeNewRCObj<TestSceneAssetImporter>()("late")};
    pProbe->OnProbe = [&]() {
        EXPECT_EQ(pManager->RegisterSceneAssetImporter(pLate), RADIENT_STATUS_OK);
    };
    RadientSceneLoadInfo LoadInfo;
    LoadInfo.URI = "first.fake";
    RefCntAutoPtr<IRadientSceneAsset> pScene;
    {
        TestingEnvironment::ErrorScope ExpectedError{"No Radient scene importer supports URI"};
        EXPECT_EQ(pManager->LoadScene(LoadInfo, &pScene), RADIENT_STATUS_INVALID_ARGUMENT);
    }
    pProbe->OnProbe = {};
    EXPECT_EQ(pScene, nullptr);
    EXPECT_EQ(pLate->ProbeCount.load(), 0u);
    EXPECT_EQ(pLate->ImportCount, 0u);

    // The registration is visible to the next request, after the original
    // request finishes probing its retained list of importers.
    pScene = Load();
    ASSERT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(pProbe->ProbeCount.load(), 2u);
    EXPECT_EQ(pLate->ProbeCount.load(), 1u);
    EXPECT_EQ(pLate->ImportCount, 1u);
}

TEST_F(RadientSceneAssetImporterTest, ConcurrentRegistrationRejectsDuplicateIdentifiers)
{
    RefCntAutoPtr<TestSceneAssetImporter> pFirst{MakeNewRCObj<TestSceneAssetImporter>()("duplicate")};
    RefCntAutoPtr<TestSceneAssetImporter> pSecond{MakeNewRCObj<TestSceneAssetImporter>()("duplicate")};
    std::promise<void>                    Start;
    const std::shared_future<void>        StartFuture          = Start.get_future().share();
    const auto                            RegisterConcurrently = [&](IRadientSceneAssetImporter* pImporter) {
        StartFuture.wait();
        return pManager->RegisterSceneAssetImporter(pImporter);
    };
    std::future<RADIENT_STATUS> First  = std::async(std::launch::async, RegisterConcurrently, pFirst.RawPtr());
    std::future<RADIENT_STATUS> Second = std::async(std::launch::async, RegisterConcurrently, pSecond.RawPtr());
    Start.set_value();
    const RADIENT_STATUS FirstStatus  = First.get();
    const RADIENT_STATUS SecondStatus = Second.get();
    ASSERT_TRUE((FirstStatus == RADIENT_STATUS_OK && SecondStatus == RADIENT_STATUS_INVALID_ARGUMENT) ||
                (FirstStatus == RADIENT_STATUS_INVALID_ARGUMENT && SecondStatus == RADIENT_STATUS_OK));

    RefCntAutoPtr<IRadientSceneAsset> pScene = Load("first.fake", "duplicate");
    ASSERT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(pFirst->ImportCount, FirstStatus == RADIENT_STATUS_OK ? 1u : 0u);
    EXPECT_EQ(pSecond->ImportCount, SecondStatus == RADIENT_STATUS_OK ? 1u : 0u);
}

TEST_F(RadientSceneAssetImporterTest, AllowsConcurrentImporterProbes)
{
    RefCntAutoPtr<TestSceneAssetImporter> pImporter = Register("concurrent");
    pImporter->Matches                              = False;
    std::promise<void>             BothEntered;
    std::future<void>              BothEnteredFuture = BothEntered.get_future();
    std::promise<void>             Release;
    const std::shared_future<void> ReleaseFuture = Release.get_future().share();
    std::atomic<Uint32>            Entered{0};
    std::atomic<bool>              ProbeTimedOut{false};
    pImporter->OnProbe = [&]() {
        if (Entered.fetch_add(1) == 1)
            BothEntered.set_value();
        if (ReleaseFuture.wait_for(std::chrono::seconds{10}) != std::future_status::ready)
            ProbeTimedOut.store(true);
    };

    const RadientSceneLoadInfo                       LoadInfo{"first.fake"};
    std::array<RefCntAutoPtr<IRadientSceneAsset>, 2> Scenes;
    const auto                                       LoadConcurrently = [&](Uint32 Index) {
        return pManager->LoadScene(LoadInfo, &Scenes[Index]);
    };
    TestingEnvironment::ErrorScope ExpectedErrors{
        "No Radient scene importer supports URI",
        "No Radient scene importer supports URI"};
    std::future<RADIENT_STATUS> First         = std::async(std::launch::async, LoadConcurrently, 0u);
    std::future<RADIENT_STATUS> Second        = std::async(std::launch::async, LoadConcurrently, 1u);
    const bool                  ProbesOverlap = BothEnteredFuture.wait_for(std::chrono::seconds{5}) == std::future_status::ready;

    // Always release and join the probes before asserting, including when an
    // exclusive lookup lock would have prevented the second probe from entering.
    Release.set_value();
    const RADIENT_STATUS FirstStatus  = First.get();
    const RADIENT_STATUS SecondStatus = Second.get();
    pImporter->OnProbe                = {};
    EXPECT_TRUE(ProbesOverlap);
    EXPECT_FALSE(ProbeTimedOut.load());
    EXPECT_EQ(FirstStatus, RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(SecondStatus, RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Scenes[0], nullptr);
    EXPECT_EQ(Scenes[1], nullptr);
    EXPECT_EQ(pImporter->ProbeCount.load(), 2u);
    EXPECT_EQ(pImporter->ImportCount, 0u);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
}

TEST_F(RadientSceneAssetImporterTest, DoesNotPublishFailedOrIncompleteDocuments)
{
    const std::array<RADIENT_STATUS, 3> Results{RADIENT_STATUS_INVALID_DATA, RADIENT_STATUS_PENDING, RADIENT_STATUS_NO_CHANGE};
    for (Uint32 Index = 0; Index < Results.size(); ++Index)
    {
        const std::string                     Identifier = "failure-" + std::to_string(Index);
        RefCntAutoPtr<TestSceneAssetImporter> pImporter  = Register(Identifier.c_str());
        pImporter->ReturnStatus                          = Results[Index];
        RefCntAutoPtr<IRadientSceneAsset> pScene         = Load("first.fake", Identifier.c_str());
        if (RADIENT_FAILED(Results[Index]))
        {
            EXPECT_EQ(pManager->WaitForAssetLoad(pScene), Results[Index]);
        }
        else
        {
            TestingEnvironment::ErrorScope ExpectedError{"must finish document conversion synchronously"};
            EXPECT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_FAILED);
        }
        EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pScene), nullptr);
        EXPECT_EQ(pImporter->WeakSource.Lock(), nullptr);
        EXPECT_EQ(pImporter->WeakServices.Lock(), nullptr);
    }
    RefCntAutoPtr<TestSceneAssetImporter> pThrowing = Register("throwing");
    pThrowing->ThrowOnImport                        = true;
    RefCntAutoPtr<IRadientSceneAsset> pScene        = Load("first.fake", "throwing");
    {
        TestingEnvironment::ErrorScope ExpectedError{"test importer failure"};
        EXPECT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_FAILED);
    }
    EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pScene), nullptr);
    EXPECT_EQ(pThrowing->WeakSource.Lock(), nullptr);
    EXPECT_EQ(pThrowing->WeakServices.Lock(), nullptr);
}

TEST_F(RadientSceneAssetImporterTest, DoesNotTryAnotherMatchingImporterAfterImportFailure)
{
    RefCntAutoPtr<TestSceneAssetImporter> pFirst  = Register("first");
    RefCntAutoPtr<TestSceneAssetImporter> pSecond = Register("second");
    pFirst->ReturnStatus                          = RADIENT_STATUS_UNSUPPORTED;
    pSecond->ReturnStatus                         = RADIENT_STATUS_UNSUPPORTED;
    RefCntAutoPtr<IRadientSceneAsset> pScene      = Load();
    EXPECT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(pFirst->ImportCount, 1u);
    EXPECT_EQ(pSecond->ImportCount, 0u);
    EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pScene), nullptr);
}

TEST_F(RadientSceneAssetImporterTest, ContainsRegistrationAndProbeExceptionsAndRemainsUsable)
{
    RefCntAutoPtr<TestSceneAssetImporter> pImporter{MakeNewRCObj<TestSceneAssetImporter>()("throws")};
    pImporter->ThrowOnIdentifier = true;
    {
        TestingEnvironment::ErrorScope ExpectedError{"test identifier failure"};
        EXPECT_EQ(pManager->RegisterSceneAssetImporter(pImporter), RADIENT_STATUS_FAILED);
    }
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);

    // A failed registration does not reserve the identifier or poison the registry.
    pImporter->ThrowOnIdentifier = false;
    ASSERT_EQ(pManager->RegisterSceneAssetImporter(pImporter), RADIENT_STATUS_OK);
    pImporter->ThrowOnProbe = true;
    RadientSceneLoadInfo LoadInfo;
    LoadInfo.URI = "first.fake";
    RefCntAutoPtr<IRadientSceneAsset> pScene;
    {
        TestingEnvironment::ErrorScope ExpectedError{"test probe failure"};
        EXPECT_EQ(pManager->LoadScene(LoadInfo, &pScene), RADIENT_STATUS_FAILED);
    }
    EXPECT_EQ(pScene, nullptr);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);
    EXPECT_EQ(pImporter->ImportCount, 0u);

    pImporter->ThrowOnProbe = false;
    pScene                  = Load();
    ASSERT_EQ(pManager->WaitForAssetLoad(pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(pImporter->ImportCount, 1u);
}

TEST_F(RadientSceneAssetImporterTest, QueuedImportCancelsWhenManagerIsReleased)
{
    RefCntAutoPtr<TestSceneAssetImporter> pImporter = Register("cancelled");
    RefCntWeakPtr<IRadientAssetManager>   WeakManager{pManager.RawPtr()};
    RefCntAutoPtr<IRadientSceneAsset>     pScene = Load();
    ASSERT_NE(pScene, nullptr);
    EXPECT_EQ(pImporter->ImportCount, 0u);

    // The queued load holds only a weak reference to its manager.
    pManager.Release();
    EXPECT_EQ(WeakManager.Lock(), nullptr);

    ASSERT_TRUE(pThreadPool->ProcessTask(0, false));
    EXPECT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pScene), RADIENT_STATUS_CANCELLED);
    EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pScene), nullptr);
    EXPECT_EQ(pImporter->ImportCount, 0u);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);
    EXPECT_EQ(pThreadPool->GetQueueSize(), 0u);
}

// Immutable source objects and atomic counters allow worker threads to resolve
// and open sources concurrently without racing the single-threaded test helper.
class ConcurrentSceneAssetResolver final : public ObjectBase<IRadientAssetResolver>
{
public:
    using TBase = ObjectBase<IRadientAssetResolver>;

    explicit ConcurrentSceneAssetResolver(IReferenceCounters* pRefCounters) :
        TBase{pRefCounters}
    {
        m_Data[0] = MakeNewRCObj<TestRadientAssetData>()(
            std::vector<Uint8>{'a', 'b', 'c'}, "memory://resolved/shared.fake", std::make_shared<TestRadientAssetResolverStats>());
        m_Data[1] = MakeNewRCObj<TestRadientAssetData>()(
            std::vector<Uint8>{'x', 'y', 'z'}, "memory://resolved/second.fake", std::make_shared<TestRadientAssetResolverStats>());
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetResolver, TBase)

    RADIENT_STATUS DILIGENT_CALL_TYPE CheckAsset(IRadientAssetLocation* pLocation) override
    {
        return FindData(pLocation) != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_NOT_FOUND;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE ResolveAssetLocation(const RadientAssetResolveInfo& ResolveInfo,
                                                           IRadientAssetLocation**        ppLocation) override
    {
        if (ppLocation == nullptr || ResolveInfo.URI == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppLocation = nullptr;

        Uint32 SourceIndex = 0;
        if (std::strcmp(ResolveInfo.URI, "second.fake") == 0)
            SourceIndex = 1;
        else if (std::strcmp(ResolveInfo.URI, "first.fake") != 0 && std::strcmp(ResolveInfo.URI, "alias.fake") != 0)
            return RADIENT_STATUS_NOT_FOUND;

        RefCntAutoPtr<TestRadientAssetLocation> pLocation{
            MakeNewRCObj<TestRadientAssetLocation>()(m_Data[SourceIndex]->GetResolvedURI())};
        pLocation->QueryInterface(IID_RadientAssetLocation, ppLocation);
        const Uint32 Count = ResolveCount.fetch_add(1) + 1;
        if (OnResolve)
            OnResolve(Count);
        return RADIENT_STATUS_OK;
    }

    RADIENT_STATUS DILIGENT_CALL_TYPE OpenAsset(IRadientAssetLocation* pLocation, IRadientAssetData** ppData) override
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppData                  = nullptr;
        IRadientAssetData* pData = FindData(pLocation);
        if (pData == nullptr)
            return RADIENT_STATUS_NOT_FOUND;
        ++OpenCount;
        pData->QueryInterface(IID_RadientAssetData, ppData);
        return RADIENT_STATUS_OK;
    }

    std::atomic<Uint32>         ResolveCount{0};
    std::atomic<Uint32>         OpenCount{0};
    std::function<void(Uint32)> OnResolve;

private:
    IRadientAssetData* FindData(IRadientAssetLocation* pLocation) const
    {
        if (pLocation != nullptr)
        {
            for (const RefCntAutoPtr<TestRadientAssetData>& pData : m_Data)
            {
                if (std::strcmp(pLocation->GetLocation(), pData->GetResolvedURI()) == 0)
                    return pData;
            }
        }
        return nullptr;
    }

    std::array<RefCntAutoPtr<TestRadientAssetData>, 2> m_Data;
};

class RadientConcurrentSceneAssetImporterTest : public testing::Test
{
protected:
    void SetUp() override
    {
        pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{2});
        pResolver   = MakeNewRCObj<ConcurrentSceneAssetResolver>()();
        RadientAssetManagerImpl::CreateInfo CI;
        CI.pThreadPool           = pThreadPool;
        CI.Assets.pAssetResolver = pResolver;
        pManager                 = RadientAssetManagerImpl::Create(CI);
        ASSERT_NE(pManager, nullptr);
        pImporter = MakeNewRCObj<TestSceneAssetImporter>()("concurrent");
        ASSERT_EQ(pManager->RegisterSceneAssetImporter(pImporter), RADIENT_STATUS_OK);
    }

    void TearDown() override
    {
        pThreadPool->StopThreads();
    }

    RefCntAutoPtr<IRadientSceneAsset> Load(const char* URI)
    {
        RadientSceneLoadInfo LoadInfo;
        LoadInfo.URI = URI;
        RefCntAutoPtr<IRadientSceneAsset> pScene;
        const RADIENT_STATUS              Status = pManager->LoadScene(LoadInfo, &pScene);
        // A worker may attach the payload before LoadScene returns.
        EXPECT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING) << Status;
        return pScene;
    }

    RefCntAutoPtr<IThreadPool>                  pThreadPool;
    RefCntAutoPtr<ConcurrentSceneAssetResolver> pResolver;
    RefCntAutoPtr<IRadientAssetManager>         pManager;
    RefCntAutoPtr<TestSceneAssetImporter>       pImporter;
};

TEST_F(RadientConcurrentSceneAssetImporterTest, ImportsDistinctSourcesConcurrentlyOnWorkers)
{
    std::promise<void>                                       BothEntered;
    std::future<void>                                        BothEnteredFuture = BothEntered.get_future();
    std::promise<void>                                       Release;
    const std::shared_future<void>                           ReleaseFuture = Release.get_future().share();
    std::atomic<Uint32>                                      ImportCount{0};
    std::atomic<bool>                                        TimedOut{false};
    std::array<std::thread::id, 2>                           ImportThreads;
    std::array<RefCntWeakPtr<IRadientMeshImportServices>, 2> Services;

    // The same importer receives both calls. Keep each call's output local,
    // and hold both imports until the test has observed their overlap.
    pImporter->OnImport = [&](const RadientSceneAssetImportContext& Context, RadientImport::ImportedDocument& Document) {
        const Uint32 Index   = std::strcmp(Context.pSourceData->GetResolvedURI(), "memory://resolved/shared.fake") == 0 ? 0u : 1u;
        ImportThreads[Index] = std::this_thread::get_id();
        Services[Index]      = Context.pMeshImportServices;
        if (ImportCount.fetch_add(1) == 1)
            BothEntered.set_value();
        if (ReleaseFuture.wait_for(std::chrono::seconds{10}) != std::future_status::ready)
            TimedOut.store(true);

        RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
        RADIENT_STATUS                                 Status = Context.pAssetManager->CreateStandardMaterialDefinition({}, &pDefinition);
        if (RADIENT_FAILED(Status))
            return Status;
        RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
        Status = Context.pAssetManager->CreateMaterial(pDefinition, &pMaterial);
        if (RADIENT_FAILED(Status))
            return Status;
        Document.Materials.push_back(pMaterial);
        Document.Nodes.emplace_back();
        Document.Nodes.back().Name.assign(static_cast<const char*>(Context.pSourceData->GetData()), Context.pSourceData->GetSize());
        Document.Scenes.emplace_back();
        Document.Scenes.back().RootNodes.push_back(0);
        return RADIENT_STATUS_OK;
    };

    RefCntAutoPtr<IRadientSceneAsset> pFirst         = Load("first.fake");
    RefCntAutoPtr<IRadientSceneAsset> pSecond        = Load("second.fake");
    const bool                        ImportsOverlap = BothEnteredFuture.wait_for(std::chrono::seconds{5}) == std::future_status::ready;
    if (ImportsOverlap)
    {
        EXPECT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pFirst), RADIENT_STATUS_PENDING);
        EXPECT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pSecond), RADIENT_STATUS_PENDING);
        const RefCntAutoPtr<IRadientMeshImportServices> pFirstServices  = Services[0].Lock();
        const RefCntAutoPtr<IRadientMeshImportServices> pSecondServices = Services[1].Lock();
        EXPECT_NE(pFirstServices, nullptr);
        EXPECT_NE(pSecondServices, nullptr);
        EXPECT_NE(pFirstServices, pSecondServices);
    }

    // Release and drain before fatal assertions so a failure cannot leave
    // workers blocked or callbacks referencing destroyed local state.
    Release.set_value();
    pThreadPool->WaitForAllTasks();
    pImporter->OnImport = {};
    ASSERT_TRUE(ImportsOverlap);
    EXPECT_FALSE(TimedOut.load());
    EXPECT_EQ(ImportCount.load(), 2u);
    EXPECT_EQ(pResolver->ResolveCount.load(), 2u);
    EXPECT_EQ(pResolver->OpenCount.load(), 2u);
    EXPECT_NE(ImportThreads[0], std::this_thread::get_id());
    EXPECT_NE(ImportThreads[1], std::this_thread::get_id());
    EXPECT_NE(ImportThreads[0], ImportThreads[1]);
    EXPECT_EQ(Services[0].Lock(), nullptr);
    EXPECT_EQ(Services[1].Lock(), nullptr);
    ASSERT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pFirst), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pSecond), RADIENT_STATUS_OK);
    const RadientImport::ImportedDocument* pFirstDocument  = RadientAssetManagerImpl::GetImportedScene(pFirst);
    const RadientImport::ImportedDocument* pSecondDocument = RadientAssetManagerImpl::GetImportedScene(pSecond);
    ASSERT_NE(pFirstDocument, nullptr);
    ASSERT_NE(pSecondDocument, nullptr);
    EXPECT_NE(pFirstDocument, pSecondDocument);
    ASSERT_EQ(pFirstDocument->Nodes.size(), 1u);
    ASSERT_EQ(pSecondDocument->Nodes.size(), 1u);
    EXPECT_EQ(pFirstDocument->Nodes[0].Name, "abc");
    EXPECT_EQ(pSecondDocument->Nodes[0].Name, "xyz");
    ASSERT_EQ(pFirstDocument->Materials.size(), 1u);
    ASSERT_EQ(pSecondDocument->Materials.size(), 1u);
    EXPECT_NE(pFirstDocument->Materials[0], nullptr);
    EXPECT_NE(pSecondDocument->Materials[0], nullptr);
}

TEST_F(RadientConcurrentSceneAssetImporterTest, SimultaneousAliasesSharePendingImport)
{
    std::promise<void>             BothResolved;
    std::future<void>              BothResolvedFuture = BothResolved.get_future();
    std::promise<void>             ReleaseResolution;
    const std::shared_future<void> ResolveFuture = ReleaseResolution.get_future().share();
    std::promise<void>             ImportEntered;
    std::future<void>              ImportEnteredFuture = ImportEntered.get_future();
    std::promise<void>             ReleaseImport;
    const std::shared_future<void> ImportFuture = ReleaseImport.get_future().share();
    std::atomic<Uint32>            ImportCount{0};
    std::atomic<bool>              TimedOut{false};

    // Let both workers resolve the aliases before either looks up the cache.
    pResolver->OnResolve = [&](Uint32 Count) {
        if (Count == 2)
            BothResolved.set_value();
        if (ResolveFuture.wait_for(std::chrono::seconds{10}) != std::future_status::ready)
            TimedOut.store(true);
    };
    pImporter->OnImport = [&](const RadientSceneAssetImportContext& Context, RadientImport::ImportedDocument& Document) {
        if (ImportCount.fetch_add(1) == 0)
            ImportEntered.set_value();
        if (ImportFuture.wait_for(std::chrono::seconds{10}) != std::future_status::ready)
            TimedOut.store(true);
        Document.Nodes.emplace_back();
        Document.Nodes.back().Name.assign(static_cast<const char*>(Context.pSourceData->GetData()), Context.pSourceData->GetSize());
        Document.Scenes.emplace_back();
        Document.Scenes.back().RootNodes.push_back(0);
        return RADIENT_STATUS_OK;
    };

    RefCntAutoPtr<IRadientSceneAsset> pFirst             = Load("first.fake");
    RefCntAutoPtr<IRadientSceneAsset> pAlias             = Load("alias.fake");
    const bool                        ResolutionsOverlap = BothResolvedFuture.wait_for(std::chrono::seconds{5}) == std::future_status::ready;
    ReleaseResolution.set_value();
    const bool ImportStarted = ImportEnteredFuture.wait_for(std::chrono::seconds{5}) == std::future_status::ready;

    // One worker remains inside Import. Only the worker that finishes the
    // pending alias lookup can execute this marker. Do not process tasks on
    // the calling thread, which would invalidate that check.
    std::promise<void>        AliasTaskFinished;
    std::future<void>         AliasTaskFinishedFuture = AliasTaskFinished.get_future();
    RefCntAutoPtr<IAsyncTask> pMarker                 = CreateAsyncWorkTask([&](Uint32) {
        AliasTaskFinished.set_value();
        return ASYNC_TASK_STATUS_COMPLETE;
    });
    pThreadPool->EnqueueTask(pMarker);
    const bool AliasFinishedWhilePending = AliasTaskFinishedFuture.wait_for(std::chrono::seconds{5}) == std::future_status::ready;
    if (ResolutionsOverlap && ImportStarted && AliasFinishedWhilePending)
    {
        EXPECT_EQ(pResolver->OpenCount.load(), 1u);
        EXPECT_EQ(ImportCount.load(), 1u);
        EXPECT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pFirst), RADIENT_STATUS_PENDING);
        EXPECT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pAlias), RADIENT_STATUS_PENDING);
        EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pFirst), nullptr);
        EXPECT_EQ(RadientAssetManagerImpl::GetImportedScene(pAlias), nullptr);
    }

    ReleaseImport.set_value();
    pThreadPool->WaitForAllTasks();
    pImporter->OnImport  = {};
    pResolver->OnResolve = {};
    ASSERT_TRUE(ResolutionsOverlap);
    ASSERT_TRUE(ImportStarted);
    ASSERT_TRUE(AliasFinishedWhilePending);
    EXPECT_FALSE(TimedOut.load());
    EXPECT_EQ(pResolver->ResolveCount.load(), 2u);
    EXPECT_EQ(pResolver->OpenCount.load(), 1u);
    EXPECT_EQ(ImportCount.load(), 1u);
    ASSERT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pFirst), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientAssetManagerImpl::GetSceneLoadStatus(pAlias), RADIENT_STATUS_OK);
    EXPECT_NE(pFirst, pAlias);
    EXPECT_STREQ(pFirst->GetReference().URI, "first.fake");
    EXPECT_STREQ(pAlias->GetReference().URI, "alias.fake");
    const RadientImport::ImportedDocument* pDocument = RadientAssetManagerImpl::GetImportedScene(pFirst);
    ASSERT_NE(pDocument, nullptr);
    EXPECT_EQ(pDocument, RadientAssetManagerImpl::GetImportedScene(pAlias));
    ASSERT_EQ(pDocument->Nodes.size(), 1u);
    EXPECT_EQ(pDocument->Nodes[0].Name, "abc");
}

} // namespace
