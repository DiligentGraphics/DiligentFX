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

#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientMorphTargetData.hpp"
#include "Scene/Components/RadientMorphComponentStorage.hpp"
#include "Scene/RadientSceneState.hpp"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <string>

using namespace Diligent;

namespace
{

struct MorphMeshData
{
    std::array<RadientFloat3, 3> Positions{};
    std::array<Uint32, 3>        Indices{0, 1, 2};
    std::array<Float32, 9>       PositionDeltas{
        0.f, 0.f, 0.f,
        0.1f, 0.2f, 0.3f,
        -0.1f, -0.2f, -0.3f};
    std::array<Float32, 6> CustomDeltas{
        1.f, 2.f,
        3.f, 4.f,
        5.f, 6.f};
    std::array<RadientMorphTargetAttributeDesc, 2>       Attributes{};
    std::array<RadientMorphTargetAttributeCreateInfo, 2> AttributeData{};
    std::array<RadientMorphTargetCreateInfo, 2>          Targets{};
    RadientMeshPrimitiveCreateInfo                       Primitive{};

    MorphMeshData()
    {
        Attributes[0].Semantic       = RadientMorphTargetPositionSemantic;
        Attributes[0].ComponentCount = 3;
        Attributes[1].Semantic       = "CUSTOM";
        Attributes[1].ComponentCount = 2;
        AttributeData[0].pDeltas     = PositionDeltas.data();
        AttributeData[1].pDeltas     = CustomDeltas.data();

        Targets[0].Desc.Name           = "Smile";
        Targets[0].Desc.pAttributes    = Attributes.data();
        Targets[0].Desc.AttributeCount = static_cast<Uint32>(Attributes.size());
        Targets[0].Desc.DefaultWeight  = 0.25f;
        Targets[0].pAttributeData      = AttributeData.data();
        Targets[1].Desc.Name           = nullptr;
        Targets[1].Desc.DefaultWeight  = -0.5f;

        Primitive.IndexCount = static_cast<Uint32>(Indices.size());
    }

    RadientMeshCreateInfo MakeCreateInfo() const
    {
        RadientMeshCreateInfo CI{};
        CI.pPositions       = Positions.data();
        CI.VertexCount      = static_cast<Uint32>(Positions.size());
        CI.pMorphTargets    = Targets.data();
        CI.MorphTargetCount = static_cast<Uint32>(Targets.size());
        CI.pIndices         = Indices.data();
        CI.IndexCount       = static_cast<Uint32>(Indices.size());
        CI.IndexType        = RADIENT_INDEX_TYPE_UINT32;
        CI.pPrimitives      = &Primitive;
        CI.PrimitiveCount   = 1;
        return CI;
    }
};

RefCntAutoPtr<IRadientMeshAsset> CreateMesh(const RadientMeshCreateInfo& CI)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    EXPECT_NE(pThreadPool, nullptr);
    if (!pThreadPool)
        return {};

    RadientMeshAssetManagerSharedPtr pManager = RadientMeshAssetManager::Create({});
    EXPECT_NE(pManager, nullptr);
    if (!pManager)
        return {};

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    const RADIENT_STATUS             Status = pManager->CreateMesh(*pThreadPool, CI, pMesh.GetAddressOfEmpty());
    EXPECT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING);
    EXPECT_NE(pMesh, nullptr);
    pThreadPool->WaitForAllTasks();
    pThreadPool->StopThreads();
    return pMesh;
}

RefCntAutoPtr<IRadientMorphTargetWeights> CreateWeights(IRadientMeshAsset& Mesh)
{
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    EXPECT_EQ(Mesh.CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_NE(pWeights, nullptr);
    return pWeights;
}

} // namespace

TEST(RadientMorphTargetsTest, PackedDataCopiesTargetsAttributesAndDeltas)
{
    MorphMeshData Source;
    std::string   TargetName      = "Expression";
    std::string   Semantic        = "CUSTOM_DATA";
    Source.Targets[0].Desc.Name   = TargetName.c_str();
    Source.Attributes[1].Semantic = Semantic.c_str();

    RadientMorphTargetData Data{Source.Targets.data(),
                                static_cast<Uint32>(Source.Targets.size()),
                                static_cast<Uint32>(Source.Positions.size())};

    TargetName.assign("changed");
    Semantic.assign("changed");
    Source.PositionDeltas.fill(42.f);
    Source.CustomDeltas.fill(24.f);

    const RadientMeshAssetDesc& Desc = Data.GetDesc();
    ASSERT_EQ(Desc.MorphTargetCount, 2u);
    ASSERT_NE(Desc.pMorphTargets, nullptr);
    EXPECT_STREQ(Desc.pMorphTargets[0].Name, "Expression");
    EXPECT_FLOAT_EQ(Desc.pMorphTargets[0].DefaultWeight, 0.25f);
    EXPECT_STREQ(Desc.pMorphTargets[1].Name, "");
    EXPECT_FLOAT_EQ(Desc.pMorphTargets[1].DefaultWeight, -0.5f);

    const RadientMorphTargetDesc* pTargets = Desc.pMorphTargets;
    ASSERT_NE(pTargets, nullptr);
    ASSERT_EQ(pTargets[0].AttributeCount, 2u);
    EXPECT_STREQ(pTargets[0].pAttributes[0].Semantic, RadientMorphTargetPositionSemantic);
    EXPECT_STREQ(pTargets[0].pAttributes[1].Semantic, "CUSTOM_DATA");
    EXPECT_EQ(pTargets[0].pAttributes[0].ComponentCount, 3u);
    EXPECT_EQ(pTargets[0].pAttributes[1].ComponentCount, 2u);

    const std::array<Float32, 9> ExpectedPositionDeltas{
        0.f, 0.f, 0.f,
        0.1f, 0.2f, 0.3f,
        -0.1f, -0.2f, -0.3f};
    const std::array<Float32, 6> ExpectedCustomDeltas{1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
    EXPECT_EQ(std::memcmp(Data.GetDeltas(0, 0),
                          ExpectedPositionDeltas.data(),
                          sizeof(ExpectedPositionDeltas)),
              0);
    EXPECT_EQ(std::memcmp(Data.GetDeltas(0, 1),
                          ExpectedCustomDeltas.data(),
                          sizeof(ExpectedCustomDeltas)),
              0);
}

TEST(RadientMorphTargetsTest, MeshCreatesMutableWeightsFromDefaults)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.MorphTargetCount, 2u);
    ASSERT_NE(Desc.pMorphTargets, nullptr);
    EXPECT_STREQ(Desc.pMorphTargets[0].Name, "Smile");
    EXPECT_STREQ(Desc.pMorphTargets[1].Name, "");

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWeights, nullptr);
    EXPECT_EQ(pWeights->GetMesh(), pMesh.RawPtr());
    EXPECT_EQ(pWeights->GetVersion(), 1u);
    ASSERT_EQ(pWeights->GetWeightCount(), 2u);
    ASSERT_NE(pWeights->GetWeights(), nullptr);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], -0.5f);
}

TEST(RadientMorphTargetsTest, WeightsRetainTheirMesh)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    IRadientMeshAsset* const pExpectedMesh = pMesh;
    pMesh.Release();

    ASSERT_EQ(pWeights->GetMesh(), pExpectedMesh);
    EXPECT_EQ(pWeights->GetMesh()->GetDesc().MorphTargetCount, 2u);
}

TEST(RadientMorphTargetsTest, WeightUpdatesAdvanceVersionAndPreserveOtherValues)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const Float32 NewWeight = 1.5f;
    EXPECT_EQ(pWeights->SetWeights(1, 1, &NewWeight), RADIENT_STATUS_OK);
    EXPECT_EQ(pWeights->GetVersion(), 2u);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], 1.5f);

    EXPECT_EQ(pWeights->SetWeights(1, 1, &NewWeight), RADIENT_STATUS_OK);
    EXPECT_EQ(pWeights->GetVersion(), 3u);
}

TEST(RadientMorphTargetsTest, WeightUpdatesValidateRangesWithoutChangingState)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const Float32 Weight = 1.f;
    EXPECT_EQ(pWeights->SetWeights(3, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWeights->SetWeights(1, 2, &Weight), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWeights->SetWeights(0, 1, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWeights->GetVersion(), 1u);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], -0.5f);

    EXPECT_EQ(pWeights->SetWeights(2, 0, nullptr), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWeights->GetVersion(), 1u);
}

TEST(RadientMorphTargetsTest, ResetRestoresDefaultsAndAdvancesVersion)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array<Float32, 2> Values{-2.f, 3.f};
    ASSERT_EQ(pWeights->SetWeights(0, 2, Values.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWeights->ResetToDefaults(), RADIENT_STATUS_OK);
    EXPECT_EQ(pWeights->GetVersion(), 3u);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], -0.5f);
}

TEST(RadientMorphTargetsTest, MeshWithoutTargetsCreatesEmptyWeights)
{
    MorphMeshData         Source;
    RadientMeshCreateInfo CI               = Source.MakeCreateInfo();
    CI.pMorphTargets                       = nullptr;
    CI.MorphTargetCount                    = 0;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(CI);
    ASSERT_NE(pMesh, nullptr);
    EXPECT_EQ(pMesh->GetDesc().MorphTargetCount, 0u);
    EXPECT_EQ(pMesh->GetDesc().pMorphTargets, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWeights, nullptr);
    EXPECT_EQ(pWeights->GetWeightCount(), 0u);
    EXPECT_EQ(pWeights->GetWeights(), nullptr);
    EXPECT_EQ(pWeights->SetWeights(0, 0, nullptr), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWeights->ResetToDefaults(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWeights->GetVersion(), 1u);
}

TEST(RadientMorphTargetsTest, MorphComponentStorageRetainsWeightsAndRepairsPointerAfterMove)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);
    IRadientMorphTargetWeights* const pExpectedWeights = pWeights;

    MorphComponentStorage Storage;
    Storage.Assign({pWeights});
    pWeights.Release();
    pMesh.Release();

    ASSERT_EQ(Storage.Component.pWeights, pExpectedWeights);
    EXPECT_EQ(Storage.Component.pWeights->GetWeightCount(), 2u);

    MorphComponentStorage MoveConstructed{std::move(Storage)};
    EXPECT_EQ(Storage.Component.pWeights, nullptr);
    EXPECT_EQ(MoveConstructed.Component.pWeights, pExpectedWeights);

    MorphComponentStorage MoveAssigned;
    MoveAssigned = std::move(MoveConstructed);
    EXPECT_EQ(MoveConstructed.Component.pWeights, nullptr);
    EXPECT_EQ(MoveAssigned.Component.pWeights, pExpectedWeights);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRejectsUnknownEntity)
{
    RadientSceneState State;
    EXPECT_EQ(State.SetMorph(InvalidRadientEntityID, {}), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRejectsNullWeights)
{
    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    const RadientSceneRevisions Revisions = State.GetSceneRevisions();
    EXPECT_EQ(State.SetMorph(Entity, {}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRequiresMesh)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_INVALID_OPERATION);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRejectsWeightsForDifferentMesh)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh      = CreateMesh(Source.MakeCreateInfo());
    RefCntAutoPtr<IRadientMeshAsset> pOtherMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    ASSERT_NE(pOtherMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pOtherWeights = CreateWeights(*pOtherMesh);
    ASSERT_NE(pOtherWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);

    const RadientSceneRevisions Revisions = State.GetSceneRevisions();
    EXPECT_EQ(State.SetMorph(Entity, {pOtherWeights}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);

    Bool HasMorph = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
}

TEST(RadientMorphTargetsTest, SceneStoresMorphAndReportsDrawableChanges)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);
    IRadientMorphTargetWeights* const pExpectedWeights = pWeights;

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    State.ClearRenderableMeshChanges();

    RadientMorphComponent StoredMorph{pWeights};
    EXPECT_EQ(State.GetMorph(InvalidRadientEntityID, StoredMorph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredMorph, RadientMorphComponent{});
    StoredMorph = {pWeights};
    EXPECT_EQ(State.GetMorph(Entity, StoredMorph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredMorph, RadientMorphComponent{});

    const RadientSceneRevisions RevisionsBeforeMorph = State.GetSceneRevisions();
    ASSERT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetSceneRevisions().Drawables, RevisionsBeforeMorph.Drawables + 1);

    pWeights.Release();
    pMesh.Release();

    Bool HasMorph = False;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, True);
    EXPECT_EQ(State.GetMorph(Entity, StoredMorph), RADIENT_STATUS_OK);
    ASSERT_EQ(StoredMorph.pWeights, pExpectedWeights);
    EXPECT_EQ(StoredMorph.pWeights->GetWeightCount(), 2u);

    Uint32 UpdatedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++UpdatedCount;
            EXPECT_EQ(Change.Entity, Entity);
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            ASSERT_NE(pRenderable->pMorph, nullptr);
            EXPECT_EQ(pRenderable->pMorph->pWeights, pExpectedWeights);
        });
    EXPECT_EQ(UpdatedCount, 1u);
    State.ClearRenderableMeshChanges();

    const RadientSceneRevisions RevisionsBeforeNoChange = State.GetSceneRevisions();
    EXPECT_EQ(State.SetMorph(Entity, StoredMorph), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), RevisionsBeforeNoChange);
    Uint32 NoChangeCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&NoChangeCount](const RadientSceneState::RenderableMeshChange&,
                         const RadientSceneState::RenderableMesh*) {
            ++NoChangeCount;
        });
    EXPECT_EQ(NoChangeCount, 0u);

    ASSERT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
    EXPECT_EQ(State.GetMorph(Entity, StoredMorph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredMorph, RadientMorphComponent{});

    Uint32 RemovedMorphCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++RemovedMorphCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            EXPECT_EQ(pRenderable->pMorph, nullptr);
        });
    EXPECT_EQ(RemovedMorphCount, 1u);
    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH), RADIENT_STATUS_NO_CHANGE);
}

TEST(RadientMorphTargetsTest, ReplacingMeshRemovesIncompatibleMorph)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh      = CreateMesh(Source.MakeCreateInfo());
    RefCntAutoPtr<IRadientMeshAsset> pOtherMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    ASSERT_NE(pOtherMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_OK);
    State.ClearRenderableMeshChanges();

    ASSERT_EQ(State.SetMesh(Entity, {pOtherMesh}), RADIENT_STATUS_OK);

    Bool HasMorph = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
    RadientMorphComponent Morph{pWeights};
    EXPECT_EQ(State.GetMorph(Entity, Morph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Morph, RadientMorphComponent{});

    Uint32 UpdatedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++UpdatedCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            EXPECT_EQ(pRenderable->Mesh.pMesh, pOtherMesh.RawPtr());
            EXPECT_EQ(pRenderable->pMorph, nullptr);
        });
    EXPECT_EQ(UpdatedCount, 1u);
}

TEST(RadientMorphTargetsTest, RemovingMeshRemovesMorph)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_OK);
    State.ClearRenderableMeshChanges();

    ASSERT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MESH), RADIENT_STATUS_OK);

    Bool HasMorph = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
    RadientMorphComponent Morph{pWeights};
    EXPECT_EQ(State.GetMorph(Entity, Morph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Morph, RadientMorphComponent{});

    Uint32 RemovedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++RemovedCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Removed);
            EXPECT_EQ(pRenderable, nullptr);
        });
    EXPECT_EQ(RemovedCount, 1u);
}
