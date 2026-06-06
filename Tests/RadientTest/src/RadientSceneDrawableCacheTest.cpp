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

#include "Render/RadientSceneDrawableCache.hpp"
#include "Scene/RadientSceneImpl.hpp"
#include "Scene/RadientSceneWriterImpl.hpp"
#include "Math/RadientMath.hpp"
#include "RadientTestAssetHelpers.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr float EPSILON = 1e-5f;

class TestDrawableMeshProvider final : public IRadientDrawableMeshProvider
{
public:
    struct MeshData
    {
        GLTF::Model*              pModel             = nullptr;
        RadientDrawableMeshStatus Status             = RadientDrawableMeshStatus::Failed;
        PBR_Renderer::PSO_FLAGS   VertexAttribFlags  = PBR_Renderer::PSO_FLAG_NONE;
        Uint32                    FirstIndexLocation = 7;
        Uint32                    BaseVertex         = 3;
    };

    void RegisterMesh(IRadientMeshAsset*        pMesh,
                      GLTF::Model&              Model,
                      RadientDrawableMeshStatus Status,
                      PBR_Renderer::PSO_FLAGS   VertexAttribFlags  = PBR_Renderer::PSO_FLAG_NONE,
                      Uint32                    FirstIndexLocation = 7,
                      Uint32                    BaseVertex         = 3)
    {
        Meshes[pMesh] = MeshData{
            &Model,
            Status,
            VertexAttribFlags,
            FirstIndexLocation,
            BaseVertex};
    }

    void SetMeshStatus(IRadientMeshAsset* pMesh, RadientDrawableMeshStatus Status)
    {
        auto MeshIt = Meshes.find(pMesh);
        ASSERT_NE(MeshIt, Meshes.end());
        if (MeshIt != Meshes.end())
            MeshIt->second.Status = Status;
    }

    RadientDrawableMeshStatus GetDrawableMesh(IRadientMeshAsset*   pMesh,
                                              RadientDrawableMesh& Mesh) override final
    {
        ++NumCalls;
        pLastMesh = pMesh;

        const auto MeshIt = Meshes.find(pMesh);
        if (MeshIt == Meshes.end())
        {
            ADD_FAILURE() << "Mesh asset must be registered with the test mesh provider";
            return RadientDrawableMeshStatus::Failed;
        }

        const MeshData& MeshData = MeshIt->second;
        if (MeshData.Status != RadientDrawableMeshStatus::Ready)
            return MeshData.Status;

        if (MeshData.pModel == nullptr || MeshData.pModel->Meshes.empty())
        {
            ADD_FAILURE() << "Registered mesh data must reference a valid model";
            return RadientDrawableMeshStatus::Failed;
        }

        Mesh.pModel             = MeshData.pModel;
        Mesh.pMesh              = &MeshData.pModel->Meshes[0];
        Mesh.VertexAttribFlags  = MeshData.VertexAttribFlags;
        Mesh.FirstIndexLocation = MeshData.FirstIndexLocation;
        Mesh.BaseVertex         = MeshData.BaseVertex;
        return RadientDrawableMeshStatus::Ready;
    }

    std::unordered_map<IRadientMeshAsset*, MeshData> Meshes;
    IRadientMeshAsset*                               pLastMesh = nullptr;
    Uint32                                           NumCalls  = 0;
};

GLTF::Primitive MakePrimitive(Uint32 FirstIndex,
                              Uint32 IndexCount,
                              Uint32 FirstVertex,
                              Uint32 VertexCount,
                              Uint32 MaterialId)
{
    return GLTF::Primitive{
        FirstIndex,
        IndexCount,
        FirstVertex,
        VertexCount,
        MaterialId,
        float3{-1.f, -1.f, -1.f},
        float3{+1.f, +1.f, +1.f}};
}

void InitTestModel(GLTF::Model& Model)
{
    Model.Materials.resize(3);
    Model.Materials[0].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_OPAQUE;
    Model.Materials[1].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_MASK;
    Model.Materials[2].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_BLEND;

    Model.Meshes.resize(1);
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 3, 0, 0, 0));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 0, 12, 4, 0));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(3, 6, 0, 0, 1));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 0, 16, 5, 1));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(9, 12, 0, 0, 2));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 0, 21, 6, 2));
}

void InitAlternateTestModel(GLTF::Model& Model)
{
    Model.Materials.resize(3);
    Model.Materials[0].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_OPAQUE;
    Model.Materials[1].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_MASK;
    Model.Materials[2].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_BLEND;

    Model.Meshes.resize(1);
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(40, 5, 0, 0, 0));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 0, 60, 7, 1));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(45, 8, 0, 0, 2));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 0, 67, 9, 2));
}

void InitFilteredPrimitiveTestModel(GLTF::Model& Model)
{
    Model.Materials.resize(3);
    Model.Materials[0].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_OPAQUE;
    Model.Materials[1].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_MASK;
    Model.Materials[2].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_BLEND;

    Model.Meshes.resize(1);
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(30, 4, 0, 0, 0));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 0, 50, 6, 1));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(34, 0, 0, 0, 2));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 0, 56, 0, 2));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(40, 5, 0, 0, 9));
}

RadientEntityID AddRenderableEntity(IRadientSceneWriter& Writer,
                                    IRadientMeshAsset*   pMesh,
                                    RadientEntityID      Parent = InvalidRadientEntityID)
{
    RadientEntityID Entity = InvalidRadientEntityID;

    RadientEntityDesc Desc;
    Desc.Parent = Parent;

    EXPECT_EQ(Writer.CreateEntity(Desc, Entity), RADIENT_STATUS_OK);
    EXPECT_NE(Entity, InvalidRadientEntityID);

    RadientMeshComponent Mesh{};
    Mesh.pMesh = pMesh;
    EXPECT_EQ(Writer.SetMesh(Entity, Mesh), RADIENT_STATUS_OK);
    EXPECT_EQ(Writer.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(Writer.CommitChanges(), RADIENT_STATUS_OK);

    return Entity;
}

RadientTransform MakeTranslation(float X, float Y, float Z)
{
    RadientTransform Transform;
    Transform.Position = {X, Y, Z};
    return Transform;
}

void ExpectMatrixNear(const RadientMatrix4x4& Matrix,
                      const RadientMatrix4x4& Reference)
{
    for (Uint32 i = 0; i < 16; ++i)
        EXPECT_NEAR(Matrix.Data[i], Reference.Data[i], EPSILON) << "i = " << i;
}

void ExpectDrawableChangeCounts(const RadientSceneDrawableCache& Cache,
                                size_t                           ExpectedAdded,
                                size_t                           ExpectedRemoved,
                                size_t                           ExpectedUpdated)
{
    size_t Added   = 0;
    size_t Removed = 0;
    size_t Updated = 0;

    for (const RadientDrawableChange& Change : Cache.GetDrawableChanges())
    {
        switch (Change.Type)
        {
            case RadientDrawableChangeType::Added:
                ++Added;
                break;

            case RadientDrawableChangeType::Removed:
                ++Removed;
                break;

            case RadientDrawableChangeType::Updated:
                ++Updated;
                break;
        }
    }

    EXPECT_EQ(Added, ExpectedAdded);
    EXPECT_EQ(Removed, ExpectedRemoved);
    EXPECT_EQ(Updated, ExpectedUpdated);
}

void ExpectLightChangeCounts(const RadientSceneDrawableCache& Cache,
                             size_t                           ExpectedAdded,
                             size_t                           ExpectedRemoved,
                             size_t                           ExpectedUpdated)
{
    size_t Added   = 0;
    size_t Removed = 0;
    size_t Updated = 0;

    for (const RadientLightChange& Change : Cache.GetLightChanges())
    {
        switch (Change.Change)
        {
            case RadientLightChangeType::Added:
                ++Added;
                break;

            case RadientLightChangeType::Removed:
                ++Removed;
                break;

            case RadientLightChangeType::Updated:
                ++Updated;
                break;
        }
    }

    EXPECT_EQ(Added, ExpectedAdded);
    EXPECT_EQ(Removed, ExpectedRemoved);
    EXPECT_EQ(Updated, ExpectedUpdated);
}

RadientEntityID AddLightEntity(IRadientSceneWriter&         Writer,
                               const RadientLightComponent& Light,
                               RadientEntityID              Parent = InvalidRadientEntityID)
{
    RadientEntityID Entity = InvalidRadientEntityID;

    RadientEntityDesc Desc;
    Desc.Parent = Parent;

    EXPECT_EQ(Writer.CreateEntity(Desc, Entity), RADIENT_STATUS_OK);
    EXPECT_NE(Entity, InvalidRadientEntityID);
    EXPECT_EQ(Writer.SetLight(Entity, Light), RADIENT_STATUS_OK);
    EXPECT_EQ(Writer.CommitChanges(), RADIENT_STATUS_OK);

    return Entity;
}

const RadientLightItem* FindLightItem(const RadientLightList& LightList,
                                      RadientEntityID         Entity)
{
    const RadientLightList::ItemListType& Items = LightList.GetItems();

    const auto It = std::find_if(Items.begin(), Items.end(),
                                 [Entity](const RadientLightItem& Item) {
                                     return Item.Entity == Entity;
                                 });
    return It != Items.end() ? &*It : nullptr;
}

RadientEntityID AddReadyRenderableEntity(TestDrawableMeshProvider&  MeshProvider,
                                         RadientSceneDrawableCache& DrawableCache,
                                         RadientSceneImpl&          Scene,
                                         IRadientSceneWriter&       Writer,
                                         IRadientMeshAsset*         pMesh,
                                         RadientEntityID            Parent = InvalidRadientEntityID)
{
    const RadientEntityID Entity = AddRenderableEntity(Writer, pMesh, Parent);
    EXPECT_NE(Entity, InvalidRadientEntityID);

    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Ready);
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(Scene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    ExpectDrawableChangeCounts(DrawableCache, 6u, 0u, 0u);
    Scene.ClearPendingRenderChanges();

    return Entity;
}

bool DrawableSlotMatchesPrimitive(const RadientDrawableSlot& Slot,
                                  const GLTF::Model&         Model,
                                  const GLTF::Primitive&     Primitive,
                                  RadientEntityID            Entity,
                                  GLTF::Material::ALPHA_MODE AlphaMode,
                                  PBR_Renderer::PSO_FLAGS    VertexAttribFlags  = PBR_Renderer::PSO_FLAG_NONE,
                                  Uint32                     FirstIndexLocation = 7,
                                  Uint32                     BaseVertex         = 3)
{
    const GLTF::Material& Material  = Model.Materials[Primitive.MaterialId];
    const bool            IsIndexed = Primitive.HasIndices();

    return Slot.Entity == Entity &&
        Slot.IsValid() &&
        Slot.IsInDrawList() &&
        Slot.IsIndexed == IsIndexed &&
        Slot.AlphaMode == static_cast<Uint8>(AlphaMode) &&
        Slot.pMaterial == &Material &&
        Slot.VertexAttribFlags == VertexAttribFlags &&
        Slot.FirstIndexLocation == FirstIndexLocation &&
        Slot.BaseVertex == BaseVertex &&
        Slot.FirstElement == (IsIndexed ? Primitive.FirstIndex : Primitive.FirstVertex) &&
        Slot.ElementCount == (IsIndexed ? Primitive.IndexCount : Primitive.VertexCount);
}

void ExpectDrawListMatchesPrimitives(const RadientSceneDrawableCache&    Cache,
                                     const GLTF::Model&                  Model,
                                     const std::vector<RadientEntityID>& Entities,
                                     GLTF::Material::ALPHA_MODE          AlphaMode,
                                     PBR_Renderer::PSO_FLAGS             VertexAttribFlags  = PBR_Renderer::PSO_FLAG_NONE,
                                     Uint32                              FirstIndexLocation = 7,
                                     Uint32                              BaseVertex         = 3)
{
    const RadientDrawList::ItemListType& Items = Cache.GetDrawList(AlphaMode).GetItems();

    struct ExpectedDrawable
    {
        RadientEntityID        Entity     = InvalidRadientEntityID;
        const GLTF::Primitive* pPrimitive = nullptr;
    };

    std::vector<ExpectedDrawable> ExpectedDrawables;
    for (const RadientEntityID Entity : Entities)
    {
        for (const GLTF::Primitive& Primitive : Model.Meshes[0].Primitives)
        {
            if (Primitive.MaterialId >= Model.Materials.size())
                continue;

            const bool   IsIndexed    = Primitive.HasIndices();
            const Uint32 ElementCount = IsIndexed ? Primitive.IndexCount : Primitive.VertexCount;
            if (ElementCount == 0)
                continue;

            const GLTF::Material& Material = Model.Materials[Primitive.MaterialId];
            if (Material.Attribs.AlphaMode != AlphaMode)
                continue;

            ExpectedDrawables.push_back({Entity, &Primitive});
        }
    }

    EXPECT_EQ(Items.size(), ExpectedDrawables.size());

    for (size_t ItemIndex = 0; ItemIndex < Items.size(); ++ItemIndex)
    {
        const RadientDrawableSlot* pSlot = Cache.GetDrawableSlot(Items[ItemIndex].DrawableID);
        ASSERT_NE(pSlot, nullptr);
        EXPECT_EQ(pSlot->DrawListIndex, ItemIndex);

        const auto ExpectedIt = std::find_if(ExpectedDrawables.begin(), ExpectedDrawables.end(),
                                             [&](const ExpectedDrawable& Expected) {
                                                 return Expected.pPrimitive != nullptr &&
                                                     DrawableSlotMatchesPrimitive(*pSlot, Model, *Expected.pPrimitive, Expected.Entity, AlphaMode, VertexAttribFlags, FirstIndexLocation, BaseVertex);
                                             });
        EXPECT_NE(ExpectedIt, ExpectedDrawables.end());
        if (ExpectedIt != ExpectedDrawables.end())
            ExpectedDrawables.erase(ExpectedIt);
    }

    EXPECT_TRUE(ExpectedDrawables.empty());
}

void ExpectDrawListsForEntities(const RadientSceneDrawableCache&    Cache,
                                const GLTF::Model&                  Model,
                                const std::vector<RadientEntityID>& Entities,
                                PBR_Renderer::PSO_FLAGS             VertexAttribFlags  = PBR_Renderer::PSO_FLAG_NONE,
                                Uint32                              FirstIndexLocation = 7,
                                Uint32                              BaseVertex         = 3)
{
    ExpectDrawListMatchesPrimitives(Cache, Model, Entities, GLTF::Material::ALPHA_MODE_OPAQUE, VertexAttribFlags, FirstIndexLocation, BaseVertex);
    ExpectDrawListMatchesPrimitives(Cache, Model, Entities, GLTF::Material::ALPHA_MODE_MASK, VertexAttribFlags, FirstIndexLocation, BaseVertex);
    ExpectDrawListMatchesPrimitives(Cache, Model, Entities, GLTF::Material::ALPHA_MODE_BLEND, VertexAttribFlags, FirstIndexLocation, BaseVertex);
}

const RadientDrawableSlot* GetFirstDrawableSlot(const RadientSceneDrawableCache& Cache)
{
    const RadientDrawList::ItemListType& Items = Cache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItems();
    EXPECT_FALSE(Items.empty());
    if (Items.empty())
        return nullptr;

    return Cache.GetDrawableSlot(Items.front().DrawableID);
}

} // namespace

TEST(RadientSceneDrawableCacheTest, SyncEmptyScene)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    // Empty scenes have no renderable meshes, so synchronization should not ask
    // the mesh provider for drawable data.
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);

    // The drawable cache should remain completely empty.
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetLightList().IsEmpty());

    // The cache revision marker should match the empty scene.
    EXPECT_EQ(DrawableCache.GetSceneRevisions(), pScene->GetSceneRevisions());

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-test", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Pending);
    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // The first scene sync sees the renderable, but the provider reports that
    // mesh data is still pending. The cache should only queue retry work.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // The next sync has no new scene changes, but pending mesh resolution now
    // succeeds and expands the renderable into one drawable per primitive.
    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Ready);
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    EXPECT_EQ(DrawableCache.GetDrawableChanges().size(), 6u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItemCount(), 2u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).GetItemCount(), 2u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).GetItemCount(), 2u);
    EXPECT_FALSE(DrawableCache.GetDrawLists().IsEmpty());
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity}, GLTF::Material::ALPHA_MODE_OPAQUE);
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity}, GLTF::Material::ALPHA_MODE_MASK);
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity}, GLTF::Material::ALPHA_MODE_BLEND);

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
    {
        EXPECT_EQ(Change.Type, RadientDrawableChangeType::Added);

        const RadientDrawableSlot* pSlot = DrawableCache.GetDrawableSlot(Change.DrawableID);
        ASSERT_NE(pSlot, nullptr);
        EXPECT_EQ(pSlot->Entity, Entity);
        EXPECT_TRUE(pSlot->IsValid());
        EXPECT_TRUE(pSlot->IsInDrawList());
    }

    // Removing the scene renderable should remove all drawables from every list.
    EXPECT_EQ(pWriter->DestroyEntity(Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(DrawableCache.GetDrawableChanges().size(), 6u);
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).IsEmpty());

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
        EXPECT_EQ(Change.Type, RadientDrawableChangeType::Removed);

    pScene->ClearPendingRenderChanges();
}

TEST(RadientSceneDrawableCacheTest, DetectsClearedRenderableMeshChangesBeforeSync)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-cleared-mesh-changes", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_NO_CHANGE);

    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Clearing pending changes before the drawable cache consumes them makes
    // incremental sync unsafe: the scene revision changed, but the delta log is gone.
    pScene->ClearPendingRenderChanges();

    {
        TestingEnvironment::ErrorScope ExpectedErrors{
            "Failed to sync Radient drawable cache: renderable mesh changes were cleared before the cache consumed them"};
        EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
}

TEST(RadientSceneDrawableCacheTest, DetectsClearedRenderableLightChangesBeforeSync)
{
    RadientSceneDrawableCache       DrawableCache;
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_NO_CHANGE);

    RadientLightComponent Light;
    Light.Type      = RADIENT_LIGHT_TYPE_POINT;
    Light.Intensity = 2.f;

    const RadientEntityID Entity = AddLightEntity(*pWriter, Light);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Clearing pending changes before the drawable cache consumes them makes
    // incremental light-list sync unsafe.
    pScene->ClearPendingRenderChanges();

    {
        TestingEnvironment::ErrorScope ExpectedErrors{
            "Failed to sync Radient drawable cache: renderable light changes were cleared before the cache consumed them"};
        EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_INVALID_OPERATION);
    }
    EXPECT_TRUE(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetLightChanges().empty());
}

TEST(RadientSceneDrawableCacheTest, InvalidAlphaModeDefaultsToOpaque)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    Model.Materials.resize(2);
    Model.Materials[0].Attribs.AlphaMode = GLTF::Material::ALPHA_MODE_NUM_MODES;
    Model.Materials[1].Attribs.AlphaMode = -1;
    Model.Meshes.resize(1);
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(0, 3, 0, 0, 0));
    Model.Meshes[0].Primitives.emplace_back(MakePrimitive(3, 3, 0, 0, 1));

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-invalid-alpha-mode", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);
    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Invalid material alpha modes must be clamped to opaque before they are
    // used as draw-list indices.
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectDrawableChangeCounts(DrawableCache, 2u, 0u, 0u);

    const RadientDrawList::ItemListType& OpaqueItems = DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItems();
    ASSERT_EQ(OpaqueItems.size(), 2u);
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).IsEmpty());

    for (const RadientDrawItem& Item : OpaqueItems)
    {
        const RadientDrawableSlot* pSlot = DrawableCache.GetDrawableSlot(Item.DrawableID);
        ASSERT_NE(pSlot, nullptr);
        EXPECT_EQ(pSlot->Entity, Entity);
        EXPECT_EQ(pSlot->AlphaMode, GLTF::Material::ALPHA_MODE_OPAQUE);
    }
}

TEST(RadientSceneDrawableCacheTest, PendingRenderableMeshCanFail)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-failed-mesh-test", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Pending);
    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // The first sync sees the renderable, but mesh resolution is still pending.
    // No drawable should be created until the mesh provider returns ready.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // The retry reports failure. The cache should drop the pending retry and
    // keep draw lists empty.
    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Failed);
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).IsEmpty());

    // Failed mesh resolution is not retried without a new scene-side change.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());

    // A later scene-side renderable change should attempt resolution again.
    // If the mesh is ready by then, the renderable expands normally.
    RadientMeshRendererComponent Renderer;
    Renderer.VisibilityMask = 0xA5A5u;
    EXPECT_EQ(pWriter->SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Ready);
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    ExpectDrawableChangeCounts(DrawableCache, 6u, 0u, 0u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity});

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
    {
        const RadientDrawableSlot* pSlot = DrawableCache.GetDrawableSlot(Change.DrawableID);
        ASSERT_NE(pSlot, nullptr);
        ASSERT_NE(pSlot->pRenderer, nullptr);
        EXPECT_EQ(pSlot->pRenderer->VisibilityMask, Renderer.VisibilityMask);
    }
}

TEST(RadientSceneDrawableCacheTest, PendingRenderableMeshCanBeRemoved)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-pending-remove-test", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Pending);
    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // The renderable enters pending mesh resolution and should not produce any
    // drawable slots yet.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // Removing the entity while mesh resolution is pending should remove the
    // renderable record and leave no pending work behind.
    EXPECT_EQ(pWriter->DestroyEntity(Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // Even if the provider later becomes ready, the removed renderable should
    // not be retried or expanded into drawables.
    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Ready);
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
}

TEST(RadientSceneDrawableCacheTest, SharedPendingMeshExpandsForMultipleEntities)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-shared-mesh-test", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Pending);

    const RadientEntityID Entity0 = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity0, InvalidRadientEntityID);

    // The first entity starts pending mesh resolution.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    const RadientEntityID Entity1 = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity1, InvalidRadientEntityID);

    // The second entity references the same mesh asset while the first one is
    // still pending. Neither entity should produce drawables yet.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // Once the shared mesh becomes ready, both entities expand into drawable
    // slots. Each material alpha mode has two primitives per entity.
    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Ready);
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 2u);
    EXPECT_EQ(DrawableCache.GetDrawableChanges().size(), 12u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItemCount(), 4u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).GetItemCount(), 4u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).GetItemCount(), 4u);

    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity0, Entity1}, GLTF::Material::ALPHA_MODE_OPAQUE);
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity0, Entity1}, GLTF::Material::ALPHA_MODE_MASK);
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity0, Entity1}, GLTF::Material::ALPHA_MODE_BLEND);

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
        EXPECT_EQ(Change.Type, RadientDrawableChangeType::Added);

    // Removing one entity should remove only its six drawable slots. The other
    // entity keeps two drawables in each alpha-mode list.
    EXPECT_EQ(pWriter->DestroyEntity(Entity0), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(DrawableCache.GetDrawableChanges().size(), 6u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItemCount(), 2u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).GetItemCount(), 2u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).GetItemCount(), 2u);
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity1}, GLTF::Material::ALPHA_MODE_OPAQUE);
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity1}, GLTF::Material::ALPHA_MODE_MASK);
    ExpectDrawListMatchesPrimitives(DrawableCache, Model, {Entity1}, GLTF::Material::ALPHA_MODE_BLEND);

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
        EXPECT_EQ(Change.Type, RadientDrawableChangeType::Removed);

    pScene->ClearPendingRenderChanges();

    // Removing the second entity should empty every draw list.
    EXPECT_EQ(pWriter->DestroyEntity(Entity1), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(DrawableCache.GetDrawableChanges().size(), 6u);
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).IsEmpty());
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).IsEmpty());

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
        EXPECT_EQ(Change.Type, RadientDrawableChangeType::Removed);

    pScene->ClearPendingRenderChanges();
}

TEST(RadientSceneDrawableCacheTest, PendingRenderableMeshUsesLatestRendererWhenReady)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-pending-renderer-change", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Pending);

    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Initial sync records the renderable, but cannot expand it yet because
    // the mesh data is still pending.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // Update renderer metadata before the mesh becomes ready. When expansion
    // finally happens, new slots must point at the updated renderer component.
    RadientMeshRendererComponent Renderer;
    Renderer.VisibilityMask = 0x4321u;
    EXPECT_EQ(pWriter->SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Ready);
    MeshProvider.NumCalls  = 0;
    MeshProvider.pLastMesh = nullptr;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh);
    ExpectDrawableChangeCounts(DrawableCache, 6u, 0u, 0u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity});

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
    {
        const RadientDrawableSlot* pSlot = DrawableCache.GetDrawableSlot(Change.DrawableID);
        ASSERT_NE(pSlot, nullptr);
        ASSERT_NE(pSlot->pRenderer, nullptr);
        EXPECT_EQ(pSlot->pRenderer->VisibilityMask, Renderer.VisibilityMask);
    }
}

TEST(RadientSceneDrawableCacheTest, PendingRenderableMeshAcceptsMaterialBindingsBeforeReady)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>     pMesh     = MakeTestMeshAsset("mesh://drawable-cache-pending-bindings-change", 1);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeTestMaterialAsset("material://drawable-cache-pending-bindings-change", 1);
    RefCntAutoPtr<IRadientSceneWriter>   pWriter   = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Pending);

    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Initial sync records the renderable but keeps drawable creation pending.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // Material bindings can change while the mesh is pending. This should not
    // create drawables early or lose the pending renderable.
    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex = 2;
    Binding.pMaterial      = pMaterial;

    RadientMaterialBindingsComponent Bindings;
    Bindings.pBindings    = &Binding;
    Bindings.BindingCount = 1;
    EXPECT_EQ(pWriter->SetMaterialBindings(Entity, Bindings), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.SetMeshStatus(pMesh, RadientDrawableMeshStatus::Ready);
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    ExpectDrawableChangeCounts(DrawableCache, 6u, 0u, 0u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity});
}

TEST(RadientSceneDrawableCacheTest, PendingRenderableMeshChangeExpandsNewMeshOnly)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    GLTF::Model AlternateModel;
    InitAlternateTestModel(AlternateModel);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh0  = MakeTestMeshAsset("mesh://drawable-cache-pending-mesh-change-0", 1);
    RefCntAutoPtr<IRadientMeshAsset>   pMesh1  = MakeTestMeshAsset("mesh://drawable-cache-pending-mesh-change-1", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh0, Model, RadientDrawableMeshStatus::Pending);

    const PBR_Renderer::PSO_FLAGS NewVertexAttribFlags  = PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS | PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
    const Uint32                  NewFirstIndexLocation = 123;
    const Uint32                  NewBaseVertex         = 29;
    MeshProvider.RegisterMesh(pMesh1,
                              AlternateModel,
                              RadientDrawableMeshStatus::Ready,
                              NewVertexAttribFlags,
                              NewFirstIndexLocation,
                              NewBaseVertex);

    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh0);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Queue pending resolution for the first mesh, but do not create drawables.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_GT(MeshProvider.NumCalls, 0u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh0);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
    pScene->ClearPendingRenderChanges();

    // Swap the renderable to a different mesh while the old mesh is still
    // pending. Expansion must use only the new mesh data; the stale pending
    // queue entry for pMesh0 must not create old drawables.
    RadientMeshComponent Mesh;
    Mesh.pMesh = pMesh1;
    EXPECT_EQ(pWriter->SetMesh(Entity, Mesh), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls  = 0;
    MeshProvider.pLastMesh = nullptr;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh1);
    ExpectDrawableChangeCounts(DrawableCache, 4u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItemCount(), 1u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).GetItemCount(), 1u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).GetItemCount(), 2u);
    ExpectDrawListsForEntities(DrawableCache, AlternateModel, {Entity}, NewVertexAttribFlags, NewFirstIndexLocation, NewBaseVertex);
}

TEST(RadientSceneDrawableCacheTest, MeshChangeRebuildsDrawablePrimitives)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    GLTF::Model AlternateModel;
    InitAlternateTestModel(AlternateModel);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh0  = MakeTestMeshAsset("mesh://drawable-cache-mesh-change-0", 1);
    RefCntAutoPtr<IRadientMeshAsset>   pMesh1  = MakeTestMeshAsset("mesh://drawable-cache-mesh-change-1", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh0, Model, RadientDrawableMeshStatus::Ready);
    const RadientEntityID Entity = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh0);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const PBR_Renderer::PSO_FLAGS NewVertexAttribFlags  = PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS | PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
    const Uint32                  NewFirstIndexLocation = 101;
    const Uint32                  NewBaseVertex         = 17;
    MeshProvider.RegisterMesh(pMesh1,
                              AlternateModel,
                              RadientDrawableMeshStatus::Ready,
                              NewVertexAttribFlags,
                              NewFirstIndexLocation,
                              NewBaseVertex);

    // Changing the mesh asset invalidates the existing primitive expansion. The
    // cache should remove the old drawable slots and expand the new mesh once.
    // The new mock mesh deliberately has a different primitive layout and
    // buffer offsets, so stale slots from the old mesh cannot satisfy the test.
    RadientMeshComponent Mesh;
    Mesh.pMesh = pMesh1;
    EXPECT_EQ(pWriter->SetMesh(Entity, Mesh), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls  = 0;
    MeshProvider.pLastMesh = nullptr;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    EXPECT_EQ(MeshProvider.pLastMesh, pMesh1);
    ExpectDrawableChangeCounts(DrawableCache, 4u, 6u, 0u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItemCount(), 1u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).GetItemCount(), 1u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).GetItemCount(), 2u);
    ExpectDrawListsForEntities(DrawableCache, AlternateModel, {Entity}, NewVertexAttribFlags, NewFirstIndexLocation, NewBaseVertex);
}

TEST(RadientSceneDrawableCacheTest, MeshExpansionSkipsInvalidPrimitives)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitFilteredPrimitiveTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-filtered-primitives", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);

    const RadientEntityID Entity = AddRenderableEntity(*pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // The model contains two valid primitives, two zero-count primitives, and
    // one primitive with an invalid material index. Only valid primitives should
    // become drawable slots.
    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 1u);
    ExpectDrawableChangeCounts(DrawableCache, 2u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItemCount(), 1u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).GetItemCount(), 1u);
    EXPECT_TRUE(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).IsEmpty());
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity});
}

TEST(RadientSceneDrawableCacheTest, RendererChangeUpdatesExistingDrawableSlots)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-renderer-change", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);
    const RadientEntityID Entity = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Renderer data is per-renderable metadata. Existing primitive slots should
    // update their renderer reference without asking the mesh provider again.
    RadientMeshRendererComponent Renderer;
    Renderer.VisibilityMask = 0x1234u;
    EXPECT_EQ(pWriter->SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    ExpectDrawableChangeCounts(DrawableCache, 0u, 0u, 6u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity});

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
    {
        const RadientDrawableSlot* pSlot = DrawableCache.GetDrawableSlot(Change.DrawableID);
        ASSERT_NE(pSlot, nullptr);
        ASSERT_NE(pSlot->pRenderer, nullptr);
        EXPECT_EQ(pSlot->pRenderer->VisibilityMask, Renderer.VisibilityMask);
    }
}

TEST(RadientSceneDrawableCacheTest, RemovingMiddleRenderableRepairsDrawListIndices)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-middle-removal", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);

    const RadientEntityID Entity0 = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    const RadientEntityID Entity1 = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    const RadientEntityID Entity2 = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    ASSERT_NE(Entity0, InvalidRadientEntityID);
    ASSERT_NE(Entity1, InvalidRadientEntityID);
    ASSERT_NE(Entity2, InvalidRadientEntityID);

    // Removing the middle entity forces draw-list swap-erase to move other
    // items. The primitive matcher also verifies every moved slot's
    // DrawListIndex points back to its new list position.
    EXPECT_EQ(pWriter->DestroyEntity(Entity1), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    ExpectDrawableChangeCounts(DrawableCache, 0u, 6u, 0u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_OPAQUE).GetItemCount(), 4u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_MASK).GetItemCount(), 4u);
    EXPECT_EQ(DrawableCache.GetDrawList(GLTF::Material::ALPHA_MODE_BLEND).GetItemCount(), 4u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity0, Entity2});
}

TEST(RadientSceneDrawableCacheTest, MaterialBindingsChangeUpdatesExistingDrawableSlots)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>     pMesh     = MakeTestMeshAsset("mesh://drawable-cache-material-bindings-change", 1);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeTestMaterialAsset("material://drawable-cache-material-bindings-change", 1);
    RefCntAutoPtr<IRadientSceneWriter>   pWriter   = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);
    const RadientEntityID Entity = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    // Material bindings are renderable metadata. They should mark existing
    // drawables as updated, but not force mesh resolution or primitive rebuilds.
    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex = 1;
    Binding.pMaterial      = pMaterial;

    RadientMaterialBindingsComponent Bindings;
    Bindings.pBindings    = &Binding;
    Bindings.BindingCount = 1;

    EXPECT_EQ(pWriter->SetMaterialBindings(Entity, Bindings), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    ExpectDrawableChangeCounts(DrawableCache, 0u, 0u, 6u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity});
}

TEST(RadientSceneDrawableCacheTest, ComponentRemovalUpdatesRenderableDrawables)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>     pMesh     = MakeTestMeshAsset("mesh://drawable-cache-component-removal", 1);
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeTestMaterialAsset("material://drawable-cache-component-removal", 1);
    RefCntAutoPtr<IRadientSceneWriter>   pWriter   = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);

    const RadientEntityID Entity0 = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    const RadientEntityID Entity1 = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    ASSERT_NE(Entity0, InvalidRadientEntityID);
    ASSERT_NE(Entity1, InvalidRadientEntityID);

    // Add and then remove material bindings. Bindings are renderable metadata,
    // so both operations update existing drawables rather than rebuilding them.
    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex = 1;
    Binding.pMaterial      = pMaterial;

    RadientMaterialBindingsComponent Bindings;
    Bindings.pBindings    = &Binding;
    Bindings.BindingCount = 1;
    EXPECT_EQ(pWriter->SetMaterialBindings(Entity0, Bindings), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    ExpectDrawableChangeCounts(DrawableCache, 0u, 0u, 6u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity0, Entity1});
    pScene->ClearPendingRenderChanges();

    EXPECT_EQ(pWriter->RemoveComponent(Entity0, RADIENT_COMPONENT_TYPE_MATERIAL_BINDINGS), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    ExpectDrawableChangeCounts(DrawableCache, 0u, 0u, 6u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity0, Entity1});
    pScene->ClearPendingRenderChanges();

    // Removing the mesh renderer makes only Entity0 non-renderable.
    EXPECT_EQ(pWriter->RemoveComponent(Entity0, RADIENT_COMPONENT_TYPE_MESH_RENDERER), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    ExpectDrawableChangeCounts(DrawableCache, 0u, 6u, 0u);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity1});
    pScene->ClearPendingRenderChanges();

    // Removing the mesh component removes the remaining renderable.
    EXPECT_EQ(pWriter->RemoveComponent(Entity1, RADIENT_COMPONENT_TYPE_MESH), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    ExpectDrawableChangeCounts(DrawableCache, 0u, 6u, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawLists().IsEmpty());
}

TEST(RadientSceneDrawableCacheTest, WorldMatrixPointerTracksHierarchyWithoutDrawableUpdate)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-world-matrix-change", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);
    const RadientEntityID Entity = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const RadientDrawableSlot* pSlot = GetFirstDrawableSlot(DrawableCache);
    ASSERT_NE(pSlot, nullptr);
    ASSERT_NE(pSlot->pWorldMatrix, nullptr);

    const RadientMatrix4x4* const pWorldMatrix = pSlot->pWorldMatrix;
    ExpectMatrixNear(*pWorldMatrix, RadientMath::TransformToMatrix(MakeTranslation(0.f, 0.f, 0.f)));

    // Reparenting under a transformed parent changes the world matrix contents,
    // but drawable slots keep a stable pointer to the entity's world matrix.
    RadientEntityDesc ParentDesc;
    ParentDesc.Transform = MakeTranslation(10.f, 0.f, 0.f);

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(pWriter->CreateEntity(ParentDesc, Parent), RADIENT_STATUS_OK);
    ASSERT_NE(Parent, InvalidRadientEntityID);
    EXPECT_EQ(pWriter->SetParent(Entity, Parent, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_EQ(pSlot->pWorldMatrix, pWorldMatrix);
    ExpectMatrixNear(*pWorldMatrix, RadientMath::TransformToMatrix(MakeTranslation(10.f, 0.f, 0.f)));

    // Direct transform edits are mutable frame data as well. They must update
    // through the same pointer without causing a renderable-list update.
    EXPECT_EQ(pWriter->SetLocalTransform(Entity, MakeTranslation(2.f, 0.f, 0.f)), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_EQ(pSlot->pWorldMatrix, pWorldMatrix);
    ExpectMatrixNear(*pWorldMatrix, RadientMath::TransformToMatrix(MakeTranslation(12.f, 0.f, 0.f)));
}

TEST(RadientSceneDrawableCacheTest, VisibilityPointerTracksHierarchyWithoutDrawableUpdate)
{
    TestDrawableMeshProvider        MeshProvider;
    RadientSceneDrawableCache       DrawableCache{&MeshProvider};
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    GLTF::Model Model;
    InitTestModel(Model);

    RefCntAutoPtr<IRadientMeshAsset>   pMesh   = MakeTestMeshAsset("mesh://drawable-cache-visibility-change", 1);
    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);
    MeshProvider.RegisterMesh(pMesh, Model, RadientDrawableMeshStatus::Ready);
    const RadientEntityID Entity = AddReadyRenderableEntity(MeshProvider, DrawableCache, *pScene, *pWriter, pMesh);
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const RadientDrawableSlot* pSlot = GetFirstDrawableSlot(DrawableCache);
    ASSERT_NE(pSlot, nullptr);
    ASSERT_NE(pSlot->pEffectiveVisible, nullptr);

    const Bool* const pEffectiveVisible = pSlot->pEffectiveVisible;
    EXPECT_TRUE(*pEffectiveVisible);

    // Parenting under a hidden entity changes effective visibility through the
    // cached visibility pointer, without rebuilding drawables.
    RadientEntityDesc HiddenParentDesc;
    HiddenParentDesc.Flags = RADIENT_ENTITY_FLAG_NONE;

    RadientEntityID HiddenParent = InvalidRadientEntityID;
    EXPECT_EQ(pWriter->CreateEntity(HiddenParentDesc, HiddenParent), RADIENT_STATUS_OK);
    ASSERT_NE(HiddenParent, InvalidRadientEntityID);
    EXPECT_EQ(pWriter->SetParent(Entity, HiddenParent, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_EQ(pSlot->pEffectiveVisible, pEffectiveVisible);
    EXPECT_FALSE(*pEffectiveVisible);

    // Removing the parent restores effective visibility through the same
    // pointer and still does not require a drawable-list update.
    EXPECT_EQ(pWriter->SetParent(Entity, InvalidRadientEntityID, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_EQ(pSlot->pEffectiveVisible, pEffectiveVisible);
    EXPECT_TRUE(*pEffectiveVisible);

    // Own visibility is mutable per-frame state too. Draw lists stay intact and
    // render passes can skip invisible slots by reading the cached pointer.
    EXPECT_EQ(pWriter->SetEntityOwnVisibility(Entity, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    MeshProvider.NumCalls = 0;
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    EXPECT_EQ(MeshProvider.NumCalls, 0u);
    EXPECT_TRUE(DrawableCache.GetDrawableChanges().empty());
    EXPECT_EQ(pSlot->pEffectiveVisible, pEffectiveVisible);
    EXPECT_FALSE(*pEffectiveVisible);
    ExpectDrawListsForEntities(DrawableCache, Model, {Entity});
}

TEST(RadientSceneDrawableCacheTest, LightListsUpdateIncrementallyByType)
{
    RadientSceneDrawableCache       DrawableCache;
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);

    RadientLightComponent DirectionalLight;
    DirectionalLight.Type      = RADIENT_LIGHT_TYPE_DIRECTIONAL;
    DirectionalLight.Intensity = 2.f;

    RadientLightComponent PointLight;
    PointLight.Type      = RADIENT_LIGHT_TYPE_POINT;
    PointLight.Intensity = 3.f;

    RadientLightComponent SpotLight;
    SpotLight.Type      = RADIENT_LIGHT_TYPE_SPOT;
    SpotLight.Intensity = 4.f;

    // Initial sync adds one light to each type-specific list.
    const RadientEntityID DirectionalEntity = AddLightEntity(*pWriter, DirectionalLight);
    const RadientEntityID PointEntity       = AddLightEntity(*pWriter, PointLight);
    const RadientEntityID SpotEntity        = AddLightEntity(*pWriter, SpotLight);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 3u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_DIRECTIONAL).GetItemCount(), 1u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 1u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT).GetItemCount(), 1u);
    EXPECT_NE(FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_DIRECTIONAL), DirectionalEntity), nullptr);
    EXPECT_NE(FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), PointEntity), nullptr);
    EXPECT_NE(FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT), SpotEntity), nullptr);
    pScene->ClearPendingRenderChanges();

    // Updating component data without changing type stays in the same list and
    // is reported as an update.
    PointLight.Intensity = 6.f;
    EXPECT_EQ(pWriter->SetLight(PointEntity, PointLight), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 0u, 0u, 1u);
    const RadientLightItem* pUpdatedPoint = FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), PointEntity);
    ASSERT_NE(pUpdatedPoint, nullptr);
    ASSERT_NE(pUpdatedPoint->pLight, nullptr);
    EXPECT_EQ(pUpdatedPoint->pLight->Intensity, 6.f);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 1u);
    pScene->ClearPendingRenderChanges();

    // Changing the type moves the light between lists: remove from the old type
    // and add to the new type.
    PointLight.Type = RADIENT_LIGHT_TYPE_SPOT;
    EXPECT_EQ(pWriter->SetLight(PointEntity, PointLight), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 1u, 1u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT).GetItemCount(), 2u);
    EXPECT_NE(FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT), PointEntity), nullptr);
    EXPECT_NE(FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT), SpotEntity), nullptr);
    pScene->ClearPendingRenderChanges();

    // Removing a light removes it from the type-specific list that currently
    // owns it.
    EXPECT_EQ(pWriter->RemoveComponent(SpotEntity, RADIENT_COMPONENT_TYPE_LIGHT), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 0u, 1u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT).GetItemCount(), 1u);
    EXPECT_EQ(FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT), SpotEntity), nullptr);
    EXPECT_NE(FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_SPOT), PointEntity), nullptr);
}

TEST(RadientSceneDrawableCacheTest, LightWorldMatrixPointerTracksHierarchyWithoutLightUpdate)
{
    RadientSceneDrawableCache       DrawableCache;
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);

    RadientEntityDesc RootDesc;

    RadientEntityID Root0 = InvalidRadientEntityID;
    RadientEntityID Root1 = InvalidRadientEntityID;
    EXPECT_EQ(pWriter->CreateEntity(RootDesc, Root0), RADIENT_STATUS_OK);

    RootDesc.Transform = MakeTranslation(10.f, 0.f, 0.f);
    EXPECT_EQ(pWriter->CreateEntity(RootDesc, Root1), RADIENT_STATUS_OK);
    ASSERT_NE(Root0, InvalidRadientEntityID);
    ASSERT_NE(Root1, InvalidRadientEntityID);

    RadientEntityDesc BranchDesc;
    BranchDesc.Parent = Root0;

    RadientEntityID Branch = InvalidRadientEntityID;
    EXPECT_EQ(pWriter->CreateEntity(BranchDesc, Branch), RADIENT_STATUS_OK);
    ASSERT_NE(Branch, InvalidRadientEntityID);

    RadientLightComponent PointLight;
    PointLight.Type = RADIENT_LIGHT_TYPE_POINT;

    const RadientEntityID LightEntity = AddLightEntity(*pWriter, PointLight, Branch);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 1u, 0u, 0u);

    const RadientLightItem* pLightItem = FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), LightEntity);
    ASSERT_NE(pLightItem, nullptr);
    ASSERT_NE(pLightItem->pWorldMatrix, nullptr);

    const RadientMatrix4x4* const pWorldMatrix = pLightItem->pWorldMatrix;
    ExpectMatrixNear(*pWorldMatrix, RadientMath::TransformToMatrix(MakeTranslation(0.f, 0.f, 0.f)));
    pScene->ClearPendingRenderChanges();

    // Reparenting an ancestor changes the light's world matrix through the
    // cached pointer, but does not change the light list.
    EXPECT_EQ(pWriter->SetParent(Branch, Root1, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 0u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 1u);
    pLightItem = FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), LightEntity);
    ASSERT_NE(pLightItem, nullptr);
    EXPECT_EQ(pLightItem->pWorldMatrix, pWorldMatrix);
    ExpectMatrixNear(*pWorldMatrix, RadientMath::TransformToMatrix(MakeTranslation(10.f, 0.f, 0.f)));
    pScene->ClearPendingRenderChanges();

    // Reparenting the light entity itself is still mutable transform state, so
    // the cache should not emit a light update.
    EXPECT_EQ(pWriter->SetParent(LightEntity, Root0, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 0u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 1u);
    pLightItem = FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), LightEntity);
    ASSERT_NE(pLightItem, nullptr);
    EXPECT_EQ(pLightItem->pWorldMatrix, pWorldMatrix);
    ExpectMatrixNear(*pWorldMatrix, RadientMath::TransformToMatrix(MakeTranslation(0.f, 0.f, 0.f)));
}

TEST(RadientSceneDrawableCacheTest, LightVisibilityIsSkippedByGeometryPass)
{
    RadientSceneDrawableCache       DrawableCache;
    RefCntAutoPtr<RadientSceneImpl> pScene = RadientSceneImpl::Create();

    ASSERT_NE(pScene, nullptr);

    RefCntAutoPtr<IRadientSceneWriter> pWriter = RadientSceneWriterImpl::Create(pScene);

    RadientLightComponent PointLight;
    PointLight.Type = RADIENT_LIGHT_TYPE_POINT;

    RadientEntityDesc ParentDesc;
    RadientEntityID   Parent = InvalidRadientEntityID;
    EXPECT_EQ(pWriter->CreateEntity(ParentDesc, Parent), RADIENT_STATUS_OK);
    ASSERT_NE(Parent, InvalidRadientEntityID);

    // The visible child light is added to the point-light list.
    const RadientEntityID LightEntity = AddLightEntity(*pWriter, PointLight, Parent);
    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 1u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 1u);
    const RadientLightItem* pVisibleLight = FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), LightEntity);
    ASSERT_NE(pVisibleLight, nullptr);
    ASSERT_NE(pVisibleLight->pEffectiveVisible, nullptr);
    EXPECT_TRUE(*pVisibleLight->pEffectiveVisible);
    pScene->ClearPendingRenderChanges();

    // Hiding the parent only changes effective visibility. The light component
    // itself is unchanged, so the cache keeps the light in the typed list and
    // lets the geometry pass skip it through the visibility pointer.
    EXPECT_EQ(pWriter->SetEntityOwnVisibility(Parent, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 0u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 1u);
    const RadientLightItem* pHiddenLight = FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), LightEntity);
    ASSERT_NE(pHiddenLight, nullptr);
    ASSERT_NE(pHiddenLight->pEffectiveVisible, nullptr);
    EXPECT_FALSE(*pHiddenLight->pEffectiveVisible);
    pScene->ClearPendingRenderChanges();

    // Restoring parent visibility flips the same pointer back without emitting
    // a light-list delta.
    EXPECT_EQ(pWriter->SetEntityOwnVisibility(Parent, True), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(DrawableCache.SyncScene(*pScene), RADIENT_STATUS_OK);
    ExpectLightChangeCounts(DrawableCache, 0u, 0u, 0u);
    EXPECT_EQ(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT).GetItemCount(), 1u);
    const RadientLightItem* pShownLight = FindLightItem(DrawableCache.GetLightList(RADIENT_LIGHT_TYPE_POINT), LightEntity);
    ASSERT_NE(pShownLight, nullptr);
    ASSERT_NE(pShownLight->pEffectiveVisible, nullptr);
    EXPECT_TRUE(*pShownLight->pEffectiveVisible);
}
