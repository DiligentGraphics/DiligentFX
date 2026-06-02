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

#include "RadientMath.hpp"
#include "RadientSceneState.hpp"
#include "RadientTestAssetHelpers.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr float EPSILON = 1e-5f;

void ExpectTransformEq(const RadientTransform& Transform, const RadientTransform& Reference)
{
    EXPECT_EQ(Transform.Position.x, Reference.Position.x);
    EXPECT_EQ(Transform.Position.y, Reference.Position.y);
    EXPECT_EQ(Transform.Position.z, Reference.Position.z);

    EXPECT_EQ(Transform.Rotation.x, Reference.Rotation.x);
    EXPECT_EQ(Transform.Rotation.y, Reference.Rotation.y);
    EXPECT_EQ(Transform.Rotation.z, Reference.Rotation.z);
    EXPECT_EQ(Transform.Rotation.w, Reference.Rotation.w);

    EXPECT_EQ(Transform.Scale.x, Reference.Scale.x);
    EXPECT_EQ(Transform.Scale.y, Reference.Scale.y);
    EXPECT_EQ(Transform.Scale.z, Reference.Scale.z);
}

void ExpectMatrixNear(const RadientMatrix4x4& Matrix, const RadientMatrix4x4& Reference)
{
    for (Uint32 i = 0; i < 16; ++i)
        EXPECT_NEAR(Matrix.Data[i], Reference.Data[i], EPSILON) << "i = " << i;
}

void ExpectSceneRevisions(const RadientSceneRevisions& Revisions,
                          RadientRevision              Drawables,
                          RadientRevision              Lights,
                          RadientRevision              Transforms,
                          RadientRevision              Visibility,
                          RadientRevision              Cameras     = 0,
                          RadientRevision              Environment = 0)
{
    EXPECT_EQ(Revisions.Drawables, Drawables);
    EXPECT_EQ(Revisions.Lights, Lights);
    EXPECT_EQ(Revisions.Transforms, Transforms);
    EXPECT_EQ(Revisions.Visibility, Visibility);
    EXPECT_EQ(Revisions.Cameras, Cameras);
    EXPECT_EQ(Revisions.Environment, Environment);
}

RadientTransform MakeTranslation(float X, float Y, float Z)
{
    RadientTransform Transform;
    Transform.Position = {X, Y, Z};
    return Transform;
}

std::vector<RadientEntityID> CreateLinearChain(RadientSceneState& State, Uint32 NodeCount)
{
    std::vector<RadientEntityID> Entities(NodeCount, InvalidRadientEntityID);

    RadientEntityDesc Desc;
    for (Uint32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
    {
        Desc.Parent = NodeIndex > 0 ? Entities[NodeIndex - 1] : InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(Desc, Entities[NodeIndex]), RADIENT_STATUS_OK);
    }

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    return Entities;
}

void ExpectLinearChainWorldTranslation(RadientSceneState& State, RadientEntityID Entity, float X)
{
    RadientMatrix4x4 Matrix;
    EXPECT_EQ(State.GetWorldMatrix(Entity, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(MakeTranslation(X, 0.f, 0.f)));
}

struct CapturedRenderableMesh
{
    RadientEntityID  Entity = InvalidRadientEntityID;
    std::string      MeshURI;
    Uint64           MeshVersion      = 0;
    Uint64           VisibilityMask   = 0;
    Bool             EffectiveVisible = False;
    RadientMatrix4x4 WorldMatrix;
    bool             HasMaterialBindings    = false;
    Uint32           MaterialBindingCount   = 0;
    Uint32           MaterialPrimitiveIndex = 0;
    std::string      MaterialURI;
    Uint64           MaterialVersion = 0;
};

CapturedRenderableMesh CaptureRenderableMesh(const RadientSceneState::RenderableMesh& RenderableMesh)
{
    CapturedRenderableMesh Captured;
    Captured.Entity           = RenderableMesh.Entity;
    Captured.MeshURI          = RenderableMesh.Mesh.pMesh != nullptr && RenderableMesh.Mesh.pMesh->GetReference().URI != nullptr ? RenderableMesh.Mesh.pMesh->GetReference().URI : "";
    Captured.MeshVersion      = RenderableMesh.Mesh.pMesh != nullptr ? RenderableMesh.Mesh.pMesh->GetReference().Version : 0;
    Captured.VisibilityMask   = RenderableMesh.Renderer.VisibilityMask;
    Captured.EffectiveVisible = RenderableMesh.EffectiveVisible;
    Captured.WorldMatrix      = RenderableMesh.WorldMatrix;

    if (RenderableMesh.pMaterialBindings != nullptr)
    {
        Captured.HasMaterialBindings  = true;
        Captured.MaterialBindingCount = RenderableMesh.pMaterialBindings->BindingCount;

        if (RenderableMesh.pMaterialBindings->BindingCount != 0)
        {
            const RadientMaterialBinding& Binding = RenderableMesh.pMaterialBindings->pBindings[0];
            Captured.MaterialPrimitiveIndex       = Binding.PrimitiveIndex;
            Captured.MaterialURI                  = Binding.pMaterial != nullptr && Binding.pMaterial->GetReference().URI != nullptr ? Binding.pMaterial->GetReference().URI : "";
            Captured.MaterialVersion              = Binding.pMaterial != nullptr ? Binding.pMaterial->GetReference().Version : 0;
        }
    }

    return Captured;
}

const CapturedRenderableMesh* FindRenderableMesh(const std::vector<CapturedRenderableMesh>& RenderableMeshes, RadientEntityID Entity)
{
    const std::vector<CapturedRenderableMesh>::const_iterator It =
        std::find_if(RenderableMeshes.begin(), RenderableMeshes.end(),
                     [Entity](const CapturedRenderableMesh& Mesh) {
                         return Mesh.Entity == Entity;
                     });

    return It != RenderableMeshes.end() ? &*It : nullptr;
}

using RenderableMeshChangeType = RadientSceneState::RenderableMeshChangeType;

struct CapturedRenderableMeshChange
{
    RadientEntityID          Entity  = InvalidRadientEntityID;
    RenderableMeshChangeType Type    = RenderableMeshChangeType::Updated;
    bool                     HasMesh = false;
    CapturedRenderableMesh   Mesh;
};

std::vector<CapturedRenderableMeshChange> CaptureRenderableMeshChanges(const RadientSceneState& State)
{
    std::vector<CapturedRenderableMeshChange> Changes;
    EXPECT_EQ(State.EnumerateRenderableMeshChanges(
                  [&Changes](const RadientSceneState::RenderableMeshChange& Change,
                             const RadientSceneState::RenderableMesh*       pMesh) {
                      CapturedRenderableMeshChange Captured;
                      Captured.Entity  = Change.Entity;
                      Captured.Type    = Change.Type;
                      Captured.HasMesh = pMesh != nullptr;
                      if (pMesh != nullptr)
                          Captured.Mesh = CaptureRenderableMesh(*pMesh);

                      Changes.push_back(Captured);
                  }),
              RADIENT_STATUS_OK);
    return Changes;
}

const CapturedRenderableMeshChange* FindRenderableMeshChange(const std::vector<CapturedRenderableMeshChange>& Changes, RadientEntityID Entity)
{
    const std::vector<CapturedRenderableMeshChange>::const_iterator It =
        std::find_if(Changes.begin(), Changes.end(),
                     [Entity](const CapturedRenderableMeshChange& Change) {
                         return Change.Entity == Entity;
                     });

    return It != Changes.end() ? &*It : nullptr;
}

struct TestMeshComponent
{
    explicit TestMeshComponent(const char* URI, Uint64 Version = 1) :
        pMesh{MakeTestMeshAsset(URI, Version)}
    {
        Component.pMesh = pMesh;
    }

    operator const RadientMeshComponent&() const
    {
        return Component;
    }

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    RadientMeshComponent             Component;
};

TestMeshComponent MakeMeshComponent(const char* URI, Uint64 Version = 1)
{
    return TestMeshComponent{URI, Version};
}

struct TestMaterialBinding
{
    TestMaterialBinding(Uint32 PrimitiveIndex, const char* URI, Uint64 Version = 1) :
        pMaterial{MakeTestMaterialAsset(URI, Version)}
    {
        Binding.PrimitiveIndex = PrimitiveIndex;
        Binding.pMaterial      = pMaterial;
    }

    operator const RadientMaterialBinding&() const
    {
        return Binding;
    }

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    RadientMaterialBinding               Binding;
};

TestMaterialBinding MakeMaterialBinding(Uint32 PrimitiveIndex, const char* URI, Uint64 Version = 1)
{
    return TestMaterialBinding{PrimitiveIndex, URI, Version};
}

struct CapturedRenderableLight
{
    RadientEntityID       Entity = InvalidRadientEntityID;
    RadientLightComponent Light;
    RadientMatrix4x4      WorldMatrix;
    Bool                  EffectiveVisible = False;
};

CapturedRenderableLight CaptureRenderableLight(const RadientSceneState::RenderableLight& RenderableLight)
{
    CapturedRenderableLight Captured;
    Captured.Entity           = RenderableLight.Entity;
    Captured.Light            = RenderableLight.Light;
    Captured.WorldMatrix      = RenderableLight.WorldMatrix;
    Captured.EffectiveVisible = RenderableLight.EffectiveVisible;
    return Captured;
}

const CapturedRenderableLight* FindRenderableLight(const std::vector<CapturedRenderableLight>& RenderableLights, RadientEntityID Entity)
{
    const std::vector<CapturedRenderableLight>::const_iterator It =
        std::find_if(RenderableLights.begin(), RenderableLights.end(),
                     [Entity](const CapturedRenderableLight& Light) {
                         return Light.Entity == Entity;
                     });

    return It != RenderableLights.end() ? &*It : nullptr;
}

TEST(RadientSceneStateTest, GetDesc)
{
    // Verifies that the default scene has no name and that a provided desc
    // name is copied into scene-owned storage.
    RadientSceneState DefaultState;
    EXPECT_EQ(DefaultState.GetDesc().Name, nullptr);

    char Name[] = "Scene A";

    RadientSceneDesc Desc;
    Desc.Name = Name;

    RadientSceneState State{Desc};

    Name[0] = 'X';

    // Mutating the original buffer should not affect the stored description.
    EXPECT_STREQ(State.GetDesc().Name, "Scene A");
    EXPECT_NE(State.GetDesc().Name, Desc.Name);
}

TEST(RadientSceneStateTest, GetDescCopiesHeapName)
{
    // Verifies that the desc name remains valid after heap memory supplied by
    // the caller is released.
    std::unique_ptr<std::string> Name = std::make_unique<std::string>("Heap Scene");

    RadientSceneDesc Desc;
    Desc.Name = Name->c_str();

    RadientSceneState State{Desc};

    Name.reset();

    // The stored name should still be readable because SceneState made a copy.
    EXPECT_STREQ(State.GetDesc().Name, "Heap Scene");
}

TEST(RadientSceneStateTest, SetEnvironmentKeepsTextureAsset)
{
    // Environment is scene-level render state, so it is tracked outside the
    // entity/component hierarchy and retains the texture asset it references.
    RadientSceneState State;

    const RadientEnvironmentDesc& DefaultEnvironment = State.GetEnvironment();
    EXPECT_EQ(DefaultEnvironment.pEnvironmentMap, nullptr);
    EXPECT_EQ(DefaultEnvironment.Color.x, 1.f);
    EXPECT_EQ(DefaultEnvironment.Color.y, 1.f);
    EXPECT_EQ(DefaultEnvironment.Color.z, 1.f);
    EXPECT_EQ(DefaultEnvironment.Intensity, 1.f);
    EXPECT_EQ(DefaultEnvironment.Exposure, 0.f);

    RefCntAutoPtr<IRadientTextureAsset> pEnvironmentMap = MakeTestTextureAsset("texture://environment", 7);

    RadientEnvironmentDesc Environment{};
    Environment.pEnvironmentMap = pEnvironmentMap;
    Environment.Color           = {0.25f, 0.5f, 1.f};
    Environment.Intensity       = 2.f;
    Environment.Exposure        = -1.f;

    EXPECT_EQ(State.SetEnvironment(Environment), RADIENT_STATUS_OK);
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 0, 0, 0, 1);

    const IRadientTextureAsset* pExpectedEnvironmentMap = pEnvironmentMap;
    pEnvironmentMap.Release();

    const RadientEnvironmentDesc& StoredEnvironment = State.GetEnvironment();
    ASSERT_NE(StoredEnvironment.pEnvironmentMap, nullptr);
    EXPECT_EQ(StoredEnvironment.pEnvironmentMap, pExpectedEnvironmentMap);
    EXPECT_STREQ(StoredEnvironment.pEnvironmentMap->GetReference().URI, "texture://environment");
    EXPECT_EQ(StoredEnvironment.pEnvironmentMap->GetReference().Version, 7u);
    EXPECT_EQ(StoredEnvironment.Color.x, 0.25f);
    EXPECT_EQ(StoredEnvironment.Color.y, 0.5f);
    EXPECT_EQ(StoredEnvironment.Color.z, 1.f);
    EXPECT_EQ(StoredEnvironment.Intensity, 2.f);
    EXPECT_EQ(StoredEnvironment.Exposure, -1.f);

    EXPECT_EQ(State.SetEnvironment(StoredEnvironment), RADIENT_STATUS_NO_CHANGE);
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 0, 0, 0, 1);

    RadientEnvironmentDesc UpdatedEnvironment = StoredEnvironment;
    UpdatedEnvironment.Intensity              = 4.f;
    EXPECT_EQ(State.SetEnvironment(UpdatedEnvironment), RADIENT_STATUS_OK);
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 0, 0, 0, 2);
}

TEST(RadientSceneStateTest, IsEntityAlive)
{
    // Exercises entity lifetime queries for missing, created, and destroyed entities.
    RadientSceneState State;

    // Unknown entity IDs should report not found.
    EXPECT_EQ(State.IsEntityAlive(1), RADIENT_STATUS_NOT_FOUND);

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_NE(Entity, InvalidRadientEntityID);

    EXPECT_EQ(State.IsEntityAlive(Entity), RADIENT_STATUS_OK);

    // After destruction, the same public ID must no longer be alive.
    EXPECT_EQ(State.DestroyEntity(Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.IsEntityAlive(Entity), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientSceneStateTest, DestroyEntityHandlesDeepHierarchy)
{
    // Destroys a 1000-node chain to verify subtree destruction is robust for
    // deep imported hierarchies.
    static constexpr Uint32 NodeCount = 1000;

    RadientSceneState            State;
    std::vector<RadientEntityID> Entities = CreateLinearChain(State, NodeCount);

    EXPECT_EQ(State.DestroyEntity(Entities.front()), RADIENT_STATUS_OK);

    // Destroying the root should remove every descendant in the chain.
    for (const RadientEntityID Entity : Entities)
        EXPECT_EQ(State.IsEntityAlive(Entity), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientSceneStateTest, GetEntityFlags)
{
    // Validates entity flag creation, mutation, no-change handling, and error
    // behavior for invalid flags and missing entities.
    RadientSceneState State;

    const RADIENT_ENTITY_FLAGS InvalidEntityFlags =
        static_cast<RADIENT_ENTITY_FLAGS>(static_cast<Uint32>(RADIENT_ENTITY_FLAGS_ALL) << 1u);

    RADIENT_ENTITY_FLAGS Flags = RADIENT_ENTITY_FLAG_VISIBLE;
    // Missing entities reset the output to a safe default.
    EXPECT_EQ(State.GetEntityFlags(1, Flags), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_NONE);

    RadientEntityDesc InvalidEntityDesc;
    InvalidEntityDesc.Flags = InvalidEntityFlags;

    RadientEntityID InvalidEntity = 123;
    // Invalid flag bits should reject entity creation and clear the output ID.
    EXPECT_EQ(State.CreateEntity(InvalidEntityDesc, InvalidEntity), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(InvalidEntity, InvalidRadientEntityID);

    RadientEntityID DefaultEntity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, DefaultEntity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityFlags(DefaultEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

    // Failed flag updates must leave the existing flags unchanged.
    EXPECT_EQ(State.SetEntityFlags(DefaultEntity, InvalidEntityFlags), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(State.GetEntityFlags(DefaultEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

    RadientEntityDesc HiddenEntityDesc;
    HiddenEntityDesc.Flags = RADIENT_ENTITY_FLAG_NONE;

    RadientEntityID HiddenEntity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(HiddenEntityDesc, HiddenEntity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityFlags(HiddenEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_NONE);

    EXPECT_EQ(State.SetEntityFlags(HiddenEntity, RADIENT_ENTITY_FLAG_VISIBLE), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityFlags(HiddenEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

    // Setting the same value should report NO_CHANGE and preserve the value.
    EXPECT_EQ(State.SetEntityFlags(HiddenEntity, RADIENT_ENTITY_FLAG_VISIBLE), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetEntityFlags(HiddenEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

    EXPECT_EQ(State.GetEntityFlags(DefaultEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

    EXPECT_EQ(State.DestroyEntity(HiddenEntity), RADIENT_STATUS_OK);
    Flags = RADIENT_ENTITY_FLAG_VISIBLE;
    // Destroyed entities should report not found and reset the output flags.
    EXPECT_EQ(State.GetEntityFlags(HiddenEntity, Flags), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_NONE);
}

TEST(RadientSceneStateTest, GetEntityOwnVisibility)
{
    // Own visibility is derived directly from entity flags and should not be
    // affected by parent visibility.
    RadientSceneState State;

    Bool Visible = True;
    // Missing entities reset the output to false.
    EXPECT_EQ(State.GetEntityOwnVisibility(1, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityOwnVisibility(Entity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    RadientEntityDesc HiddenDesc;
    HiddenDesc.Flags = RADIENT_ENTITY_FLAG_NONE;

    RadientEntityID HiddenEntity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(HiddenDesc, HiddenEntity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetEntityOwnVisibility(HiddenEntity, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    // Reapplying the same visibility should be a no-op.
    EXPECT_EQ(State.SetEntityOwnVisibility(HiddenEntity, True), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.SetEntityOwnVisibility(HiddenEntity, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.DestroyEntity(HiddenEntity), RADIENT_STATUS_OK);
    Visible = True;
    // Destroyed entities should report not found and reset output visibility.
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, GetEntityEffectiveVisibility)
{
    // Effective visibility combines an entity's own visibility with all visible
    // ancestors, and supports both lazy queries and committed cached queries.
    RadientSceneState State;

    Bool Visible = True;
    // Missing entities reset the output to false.
    EXPECT_EQ(State.GetEntityEffectiveVisibility(1, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Root), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Root, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Root;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityOwnVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    // Hiding the root should make the child effectively invisible while its own
    // visibility remains true.
    EXPECT_EQ(State.SetEntityOwnVisibility(Root, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityOwnVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    // Cached access should report OUT_OF_DATE while global dirty visibility
    // still exists elsewhere in the scene.
    EXPECT_EQ(State.SetEntityOwnVisibility(Root, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetEntityOwnVisibility(Child, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetEntityOwnVisibility(Child, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    RadientEntityDesc GrandChildDesc;
    GrandChildDesc.Parent = Child;

    RadientEntityID GrandChild = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(GrandChildDesc, GrandChild), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.SetEntityOwnVisibility(Child, False), RADIENT_STATUS_OK);
    // A visible grandchild under a hidden child should still be effectively hidden.
    EXPECT_EQ(State.GetEntityOwnVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetEntityOwnVisibility(Child, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Root, False), RADIENT_STATUS_OK);
    // A hidden ancestor still hides the grandchild even when its direct parent
    // is visible.
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetParent(GrandChild, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    // Detaching the grandchild from the hidden root should make it visible again.
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.DestroyEntity(GrandChild), RADIENT_STATUS_OK);
    Visible = True;
    // Missing entities reset both lazy and cached visibility outputs.
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);
    Visible = True;
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, LazyEffectiveVisibilityUpdatePreservesCommitPropagation)
{
    // Querying one dirty branch lazily must not prevent CommitChanges from
    // propagating the same visibility change to sibling branches.
    RadientSceneState State;

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Root), RADIENT_STATUS_OK);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Root;

    RadientEntityID Child0 = InvalidRadientEntityID;
    RadientEntityID Child1 = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child0), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child1), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    EXPECT_EQ(State.SetEntityOwnVisibility(Root, False), RADIENT_STATUS_OK);

    // Lazily repairing Child0 should compute it as hidden.
    Bool Visible = True;
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child0, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    // Commit should still propagate the hidden root to Child1, which was not
    // queried before commit.
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child1, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, GetParent)
{
    // Verifies parent queries across root entities, reparenting, detaching,
    // and destruction of a parent subtree.
    RadientSceneState State;

    RadientEntityID Parent = 1;
    // Missing entities should return not found and clear the output parent.
    EXPECT_EQ(State.GetParent(1, Parent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Parent, InvalidRadientEntityID);

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Root), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Root, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, InvalidRadientEntityID);

    // A created child should report the requested parent.
    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Root;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);

    RadientEntityID NewParent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, NewParent), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetParent(Child, NewParent, True), RADIENT_STATUS_OK);
    // Reparenting should update the child to the new parent.
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, NewParent);

    EXPECT_EQ(State.SetParent(Child, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    // Detaching should make the child a root.
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, InvalidRadientEntityID);

    EXPECT_EQ(State.SetParent(Child, Root, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);

    EXPECT_EQ(State.DestroyEntity(Root), RADIENT_STATUS_OK);
    Parent = NewParent;
    // Destroying Root also destroys Child because it was reattached under Root.
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Parent, InvalidRadientEntityID);
}

TEST(RadientSceneStateTest, SetParentRejectsCycles)
{
    // Reparenting must reject self-parenting and ancestor/descendant cycles
    // without mutating the existing hierarchy.
    RadientSceneState State;

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Root), RADIENT_STATUS_OK);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Root;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);

    RadientEntityDesc GrandChildDesc;
    GrandChildDesc.Parent = Child;

    RadientEntityID GrandChild = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(GrandChildDesc, GrandChild), RADIENT_STATUS_OK);

    RadientEntityID Parent = InvalidRadientEntityID;
    // An entity cannot become its own parent.
    EXPECT_EQ(State.SetParent(Child, Child, True), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);

    // A root cannot be parented under one of its descendants.
    EXPECT_EQ(State.SetParent(Root, GrandChild, True), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State.GetParent(Root, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, InvalidRadientEntityID);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);
    EXPECT_EQ(State.GetParent(GrandChild, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Child);

    // A middle node also cannot be parented under its own child.
    EXPECT_EQ(State.SetParent(Child, GrandChild, True), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);
    EXPECT_EQ(State.GetParent(GrandChild, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Child);
}

TEST(RadientSceneStateTest, GetChildCountAndChildren)
{
    // Validates child-count queries, paged child enumeration, stable child
    // order, no-op reparenting, detaching, and child destruction.
    RadientSceneState State;

    Uint32 ChildCount = 1;
    // Missing parents should report not found and clear output counts.
    EXPECT_EQ(State.GetChildCount(1, ChildCount), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(ChildCount, 0u);

    Uint32          NumChildrenWritten = 1;
    RadientEntityID Children[4]        = {};
    EXPECT_EQ(State.GetChildren(1, 0, 1, Children, NumChildrenWritten), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(NumChildrenWritten, 0u);

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Root), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 0u);

    EXPECT_EQ(State.GetChildren(Root, 0, 0, nullptr, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 0u);

    // Asking to write children requires a non-null output buffer.
    EXPECT_EQ(State.GetChildren(Root, 0, 1, nullptr, NumChildrenWritten), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(NumChildrenWritten, 0u);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Root;

    RadientEntityID Child0 = InvalidRadientEntityID;
    RadientEntityID Child1 = InvalidRadientEntityID;
    RadientEntityID Child2 = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child0), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child1), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child2), RADIENT_STATUS_OK);

    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 3u);

    // Full enumeration should return all children in insertion order.
    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 3u);
    EXPECT_EQ(Children[0], Child0);
    EXPECT_EQ(Children[1], Child1);
    EXPECT_EQ(Children[2], Child2);

    Children[0] = InvalidRadientEntityID;
    Children[1] = InvalidRadientEntityID;
    // Offset enumeration should return the requested suffix.
    EXPECT_EQ(State.GetChildren(Root, 1, 2, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 2u);
    EXPECT_EQ(Children[0], Child1);
    EXPECT_EQ(Children[1], Child2);

    EXPECT_EQ(State.GetChildren(Root, 3, 1, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 0u);

    EXPECT_EQ(State.GetChildren(Root, 4, 1, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 0u);

    EXPECT_EQ(State.SetParent(Child2, Root, True), RADIENT_STATUS_NO_CHANGE);
    // Reparenting to the same parent should not duplicate the child entry.
    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 3u);

    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 3u);
    EXPECT_EQ(Children[0], Child0);
    EXPECT_EQ(Children[1], Child1);
    EXPECT_EQ(Children[2], Child2);

    EXPECT_EQ(State.SetParent(Child1, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    // Detaching a child should remove it from the old parent's list.
    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 2u);

    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 2u);
    EXPECT_EQ(Children[0], Child0);
    EXPECT_EQ(Children[1], Child2);

    EXPECT_EQ(State.DestroyEntity(Child0), RADIENT_STATUS_OK);
    // Destroying a child should also remove it from the parent's list.
    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 1u);

    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 1u);
    EXPECT_EQ(Children[0], Child2);
}

TEST(RadientSceneStateTest, GetLocalTransform)
{
    // Validates local transform creation, mutation, no-change detection, and
    // missing-entity output reset.
    RadientSceneState State;

    RadientTransform Transform;
    Transform.Position = {1.f, 2.f, 3.f};
    // Missing entities should return not found and reset the output transform.
    EXPECT_EQ(State.GetLocalTransform(1, Transform), RADIENT_STATUS_NOT_FOUND);
    ExpectTransformEq(Transform, RadientTransform{});

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, RadientTransform{});

    // Creation with an initial transform should preserve the exact TRS values.
    RadientTransform InitialTransform;
    InitialTransform.Position   = {1.f, 2.f, 3.f};
    InitialTransform.Rotation.z = 0.70710678f;
    InitialTransform.Rotation.w = 0.70710678f;
    InitialTransform.Scale      = {2.f, 3.f, 4.f};

    RadientEntityDesc EntityDesc;
    EntityDesc.Transform = InitialTransform;

    RadientEntityID TransformedEntity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(EntityDesc, TransformedEntity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetLocalTransform(TransformedEntity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, InitialTransform);

    RadientTransform UpdatedTransform;
    UpdatedTransform.Position   = {4.f, 5.f, 6.f};
    UpdatedTransform.Rotation.x = 0.70710678f;
    UpdatedTransform.Rotation.w = 0.70710678f;
    UpdatedTransform.Scale      = {5.f, 6.f, 7.f};

    EXPECT_EQ(State.SetLocalTransform(TransformedEntity, UpdatedTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetLocalTransform(TransformedEntity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, UpdatedTransform);

    // Setting the same transform again should report no change.
    EXPECT_EQ(State.SetLocalTransform(TransformedEntity, UpdatedTransform), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetLocalTransform(TransformedEntity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, UpdatedTransform);

    EXPECT_EQ(State.GetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, RadientTransform{});

    EXPECT_EQ(State.DestroyEntity(TransformedEntity), RADIENT_STATUS_OK);
    Transform = UpdatedTransform;
    // Destroyed entities should report not found and reset output transform.
    EXPECT_EQ(State.GetLocalTransform(TransformedEntity, Transform), RADIENT_STATUS_NOT_FOUND);
    ExpectTransformEq(Transform, RadientTransform{});
}

TEST(RadientSceneStateTest, GetWorldMatrix)
{
    // Exercises world matrix computation for roots, children, dirty cached
    // state, local changes, reparenting with KeepWorldTransform, and destruction.
    RadientSceneState State;

    RadientMatrix4x4 Matrix;
    Matrix.Data[0] = 2.f;
    // Missing entities should return not found and reset the output matrix.
    EXPECT_EQ(State.GetWorldMatrix(1, Matrix), RADIENT_STATUS_NOT_FOUND);
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Entity, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});

    RadientTransform RootTransform;
    RootTransform.Position = {1.f, 2.f, 3.f};
    RootTransform.Scale    = {2.f, 3.f, 4.f};

    RadientEntityDesc RootDesc;
    RootDesc.Transform = RootTransform;

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(RootDesc, Root), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Root, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(RootTransform));

    // After commit, cached and lazy world matrices should agree.
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Root, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(RootTransform));
    EXPECT_EQ(State.GetCachedWorldMatrix(Root, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(RootTransform));

    RadientTransform ChildTransform;
    ChildTransform.Position = {5.f, 6.f, 7.f};
    ChildTransform.Scale    = {1.f, 2.f, 3.f};

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent    = Root;
    ChildDesc.Transform = ChildTransform;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);

    RadientMatrix4x4 ExpectedChildWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(RootTransform));
    // Child world is local * parent world in Radient row-vector convention.
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    RadientMatrix4x4 CommittedChildWorld = ExpectedChildWorld;

    RootTransform.Position = {10.f, 20.f, 30.f};
    EXPECT_EQ(State.SetLocalTransform(Root, RootTransform), RADIENT_STATUS_OK);
    // Cached access should expose the previous value and report that it is stale.
    EXPECT_EQ(State.GetCachedWorldMatrix(Child, Matrix), RADIENT_STATUS_OUT_OF_DATE);
    ExpectMatrixNear(Matrix, CommittedChildWorld);

    ExpectedChildWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(RootTransform));
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);
    EXPECT_EQ(State.GetCachedWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);
    CommittedChildWorld = ExpectedChildWorld;

    ChildTransform.Position = {8.f, 9.f, 10.f};
    ChildTransform.Scale    = {4.f, 5.f, 6.f};
    EXPECT_EQ(State.SetLocalTransform(Child, ChildTransform), RADIENT_STATUS_OK);
    // Updating the child should recompute its world against the current parent.
    ExpectedChildWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(RootTransform));
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);
    CommittedChildWorld = ExpectedChildWorld;

    EXPECT_EQ(State.SetParent(Child, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    // KeepWorldTransform should preserve the committed world matrix while the
    // local transform is adjusted for the new parent.
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, CommittedChildWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    EXPECT_EQ(State.DestroyEntity(Child), RADIENT_STATUS_OK);
    Matrix = ExpectedChildWorld;
    // Destroyed entities should reset both lazy and cached world-matrix outputs.
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_NOT_FOUND);
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});
    Matrix = ExpectedChildWorld;
    EXPECT_EQ(State.GetCachedWorldMatrix(Child, Matrix), RADIENT_STATUS_NOT_FOUND);
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});
}

TEST(RadientSceneStateTest, LazyWorldMatrixUpdatePreservesCommitPropagation)
{
    // Lazily repairing one child after a parent transform change must not
    // prevent commit from propagating that change to sibling branches.
    RadientSceneState State;

    RadientTransform RootTransform = MakeTranslation(1.f, 2.f, 3.f);

    RadientEntityDesc RootDesc;
    RootDesc.Transform = RootTransform;

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(RootDesc, Root), RADIENT_STATUS_OK);

    RadientEntityDesc Child0Desc;
    Child0Desc.Parent    = Root;
    Child0Desc.Transform = MakeTranslation(4.f, 5.f, 6.f);

    RadientEntityDesc Child1Desc;
    Child1Desc.Parent    = Root;
    Child1Desc.Transform = MakeTranslation(7.f, 8.f, 9.f);

    RadientEntityID Child0 = InvalidRadientEntityID;
    RadientEntityID Child1 = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(Child0Desc, Child0), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CreateEntity(Child1Desc, Child1), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    RootTransform = MakeTranslation(10.f, 20.f, 30.f);
    EXPECT_EQ(State.SetLocalTransform(Root, RootTransform), RADIENT_STATUS_OK);

    RadientMatrix4x4 ExpectedWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(Child0Desc.Transform),
        RadientMath::TransformToMatrix(RootTransform));

    RadientMatrix4x4 Matrix;
    // Child0 is repaired by the direct query before commit.
    EXPECT_EQ(State.GetWorldMatrix(Child0, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    ExpectedWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(Child1Desc.Transform),
        RadientMath::TransformToMatrix(RootTransform));
    // Child1 was not queried before commit, so commit must still update it.
    EXPECT_EQ(State.GetWorldMatrix(Child1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);
}

TEST(RadientSceneStateTest, LazyWorldMatrixUpdateClearsGlobalDirtyFlags)
{
    // If the only dirty state is repaired lazily, unrelated cached getters
    // should stop reporting OUT_OF_DATE.
    RadientSceneState State;

    RadientEntityDesc Branch0Desc;
    Branch0Desc.Transform = MakeTranslation(1.f, 0.f, 0.f);

    RadientEntityDesc Branch1Desc;
    Branch1Desc.Transform = MakeTranslation(2.f, 0.f, 0.f);

    RadientEntityID Branch0 = InvalidRadientEntityID;
    RadientEntityID Branch1 = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(Branch0Desc, Branch0), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CreateEntity(Branch1Desc, Branch1), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    RadientMatrix4x4 Matrix;
    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));

    const RadientTransform UpdatedBranch0Transform = MakeTranslation(10.f, 0.f, 0.f);
    EXPECT_EQ(State.SetLocalTransform(Branch0, UpdatedBranch0Transform), RADIENT_STATUS_OK);

    // Branch1 data is unchanged, but global dirty state makes cached access
    // conservatively report that it may be stale.
    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OUT_OF_DATE);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));

    EXPECT_EQ(State.GetWorldMatrix(Branch0, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(UpdatedBranch0Transform));

    // Repairing Branch0 clears the global dirty flag because no other dirty
    // entities remain.
    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));
}

TEST(RadientSceneStateTest, DestroyEntityClearsGlobalDirtyFlags)
{
    // Destroying the only dirty branch should clear global dirty state so
    // cached getters on unaffected branches become valid again.
    RadientSceneState State;

    RadientEntityDesc Branch0Desc;
    Branch0Desc.Transform = MakeTranslation(1.f, 0.f, 0.f);

    RadientEntityDesc Branch1Desc;
    Branch1Desc.Transform = MakeTranslation(2.f, 0.f, 0.f);

    RadientEntityID Branch0 = InvalidRadientEntityID;
    RadientEntityID Branch1 = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(Branch0Desc, Branch0), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CreateEntity(Branch1Desc, Branch1), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    RadientMatrix4x4 Matrix;
    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));

    Bool Visible = False;
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Branch1, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.SetLocalTransform(Branch0, MakeTranslation(10.f, 0.f, 0.f)), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Branch0, False), RADIENT_STATUS_OK);

    // Dirty state in Branch0 makes unrelated cached data conservatively stale.
    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OUT_OF_DATE);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Branch1, Visible), RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.DestroyEntity(Branch0), RADIENT_STATUS_OK);

    // Removing the dirty branch should leave no pending dirty state.
    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Branch1, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
}

TEST(RadientSceneStateTest, CommitUpdatesDirtyParentBeforeDirtyChild)
{
    // Commit should update dirty ancestors before dirty descendants so child
    // world transforms and effective visibility use current parent state.
    RadientSceneState State;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Child), RADIENT_STATUS_OK);

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Parent), RADIENT_STATUS_OK);

    EXPECT_EQ(State.SetParent(Child, Parent, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    const RadientTransform ParentTransform = MakeTranslation(10.f, 20.f, 30.f);
    const RadientTransform ChildTransform  = MakeTranslation(1.f, 2.f, 3.f);

    EXPECT_EQ(State.SetLocalTransform(Parent, ParentTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetLocalTransform(Child, ChildTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    // Child world should include the parent's transform even though both were dirty.
    RadientMatrix4x4 ExpectedWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(ParentTransform));

    RadientMatrix4x4 Matrix;
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);

    const RadientTransform UpdatedChildTransform = MakeTranslation(4.f, 5.f, 6.f);
    EXPECT_EQ(State.SetEntityOwnVisibility(Parent, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetLocalTransform(Child, UpdatedChildTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    // Parent visibility should be applied before evaluating the child.
    Bool Visible = True;
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    ExpectedWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(UpdatedChildTransform),
        RadientMath::TransformToMatrix(ParentTransform));
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);
}

TEST(RadientSceneStateTest, CommitHandlesPathologicalDirtyLinearChains)
{
    // Exercises worst-case deep chains to ensure commit updates dirty subtrees
    // without quadratic path walks or missed descendants.
    static constexpr Uint32 NodeCount = 1000;

    {
        // Every node is dirty. Each world translation should accumulate all
        // ancestors up to that node.
        RadientSceneState            State;
        std::vector<RadientEntityID> Entities  = CreateLinearChain(State, NodeCount);
        const RadientTransform       Transform = MakeTranslation(1.f, 0.f, 0.f);

        for (const RadientEntityID Entity : Entities)
            EXPECT_EQ(State.SetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);

        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        for (Uint32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
        {
            SCOPED_TRACE(NodeIndex);
            ExpectLinearChainWorldTranslation(State, Entities[NodeIndex], static_cast<float>(NodeIndex + 1));
        }
    }

    {
        // Only root and leaf are dirty. Intermediate nodes should inherit the
        // root transform, while the leaf adds its own local transform.
        RadientSceneState            State;
        std::vector<RadientEntityID> Entities  = CreateLinearChain(State, NodeCount);
        const RadientTransform       Transform = MakeTranslation(1.f, 0.f, 0.f);

        EXPECT_EQ(State.SetLocalTransform(Entities.front(), Transform), RADIENT_STATUS_OK);
        EXPECT_EQ(State.SetLocalTransform(Entities.back(), Transform), RADIENT_STATUS_OK);

        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        for (Uint32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
        {
            SCOPED_TRACE(NodeIndex);
            const float ExpectedX = NodeIndex + 1 == NodeCount ? 2.f : 1.f;
            ExpectLinearChainWorldTranslation(State, Entities[NodeIndex], ExpectedX);
        }
    }

    {
        // Every third node is dirty. Each node should accumulate one unit of X
        // for each dirty ancestor on its path.
        RadientSceneState            State;
        std::vector<RadientEntityID> Entities  = CreateLinearChain(State, NodeCount);
        const RadientTransform       Transform = MakeTranslation(1.f, 0.f, 0.f);

        for (Uint32 NodeIndex = 0; NodeIndex < NodeCount; NodeIndex += 3)
            EXPECT_EQ(State.SetLocalTransform(Entities[NodeIndex], Transform), RADIENT_STATUS_OK);

        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        for (Uint32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
        {
            SCOPED_TRACE(NodeIndex);
            ExpectLinearChainWorldTranslation(State, Entities[NodeIndex], static_cast<float>(NodeIndex / 3 + 1));
        }
    }
}

TEST(RadientSceneStateTest, CommitCombinesParentAndChildDirtyFlags)
{
    // Parent and child may carry different dirty flags; commit must combine
    // inherited and local flags so transform and visibility are both correct.
    RadientSceneState State;

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Parent), RADIENT_STATUS_OK);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Parent;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    RadientTransform ParentTransform = MakeTranslation(10.f, 0.f, 0.f);
    RadientTransform ChildTransform  = MakeTranslation(1.f, 0.f, 0.f);

    EXPECT_EQ(State.SetLocalTransform(Parent, ParentTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Parent, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetLocalTransform(Child, ChildTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    // Parent transform and hidden visibility should both affect the child.
    RadientMatrix4x4 ExpectedWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(ParentTransform));

    RadientMatrix4x4 Matrix;
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);

    Bool Visible = True;
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    ParentTransform = MakeTranslation(20.f, 0.f, 0.f);

    EXPECT_EQ(State.SetLocalTransform(Parent, ParentTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Parent, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Child, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    // The updated parent transform should apply, while the child's own hidden
    // visibility keeps effective visibility false.
    ExpectedWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(ParentTransform));

    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, CommitUpdatesDirtyVisibilityParentBeforeDirtyTransformVisibilityChild)
{
    // Covers the case where a parent has visibility dirty and a child has both
    // transform and visibility dirty. Parent visibility must be applied first.
    RadientSceneState State;

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Parent), RADIENT_STATUS_OK);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Parent;
    ChildDesc.Flags  = RADIENT_ENTITY_FLAG_NONE;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    const RadientTransform ChildTransform = MakeTranslation(1.f, 2.f, 3.f);

    EXPECT_EQ(State.SetEntityOwnVisibility(Parent, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetLocalTransform(Child, ChildTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Child, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    // The child transform is local because the parent has identity transform.
    RadientMatrix4x4 Matrix;
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(ChildTransform));

    Bool Visible = True;
    // The parent remains hidden, so the child is still effectively invisible
    // even after its own visibility was set to true.
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, SetParentKeepWorldTransformUsesPendingTransforms)
{
    // Reparenting with KeepWorldTransform must account for pending dirty
    // transforms on the child, old parent, and new parent.
    {
        // Pending child transform should be used as the source world transform
        // before the child is attached to the new parent.
        RadientSceneState State;

        RadientEntityDesc OldParentDesc;
        OldParentDesc.Transform = MakeTranslation(10.f, 20.f, 30.f);

        RadientEntityID OldParent = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(OldParentDesc, OldParent), RADIENT_STATUS_OK);

        RadientEntityDesc ChildDesc;
        ChildDesc.Parent    = OldParent;
        ChildDesc.Transform = MakeTranslation(1.f, 2.f, 3.f);

        RadientEntityID Child = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);

        RadientEntityDesc NewParentDesc;
        NewParentDesc.Transform = MakeTranslation(100.f, 200.f, 300.f);

        RadientEntityID NewParent = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(NewParentDesc, NewParent), RADIENT_STATUS_OK);
        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        const RadientTransform PendingChildTransform = MakeTranslation(4.f, 5.f, 6.f);
        const RadientMatrix4x4 ExpectedWorld         = RadientMath::MultiplyMatrices(
            RadientMath::TransformToMatrix(PendingChildTransform),
            RadientMath::TransformToMatrix(OldParentDesc.Transform));

        EXPECT_EQ(State.SetLocalTransform(Child, PendingChildTransform), RADIENT_STATUS_OK);
        EXPECT_EQ(State.SetParent(Child, NewParent, True), RADIENT_STATUS_OK);
        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        // The committed world should match the child's pending local transform
        // under the old parent.
        RadientMatrix4x4 Matrix;
        EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
        ExpectMatrixNear(Matrix, ExpectedWorld);
    }

    {
        // Pending old-parent transform should be used when computing the
        // child's old world transform.
        RadientSceneState State;

        RadientEntityDesc OldParentDesc;
        OldParentDesc.Transform = MakeTranslation(10.f, 20.f, 30.f);

        RadientEntityID OldParent = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(OldParentDesc, OldParent), RADIENT_STATUS_OK);

        RadientEntityDesc ChildDesc;
        ChildDesc.Parent    = OldParent;
        ChildDesc.Transform = MakeTranslation(1.f, 2.f, 3.f);

        RadientEntityID Child = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);

        RadientEntityDesc NewParentDesc;
        NewParentDesc.Transform = MakeTranslation(100.f, 200.f, 300.f);

        RadientEntityID NewParent = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(NewParentDesc, NewParent), RADIENT_STATUS_OK);
        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        const RadientTransform PendingOldParentTransform = MakeTranslation(40.f, 50.f, 60.f);
        const RadientMatrix4x4 ExpectedWorld             = RadientMath::MultiplyMatrices(
            RadientMath::TransformToMatrix(ChildDesc.Transform),
            RadientMath::TransformToMatrix(PendingOldParentTransform));

        EXPECT_EQ(State.SetLocalTransform(OldParent, PendingOldParentTransform), RADIENT_STATUS_OK);
        EXPECT_EQ(State.SetParent(Child, NewParent, True), RADIENT_STATUS_OK);
        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        // The child should keep the world matrix it had under the pending old parent.
        RadientMatrix4x4 Matrix;
        EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
        ExpectMatrixNear(Matrix, ExpectedWorld);
    }

    {
        // Pending new-parent transform should be considered when computing the
        // local transform needed to preserve the child's world.
        RadientSceneState State;

        RadientEntityDesc OldParentDesc;
        OldParentDesc.Transform = MakeTranslation(10.f, 20.f, 30.f);

        RadientEntityID OldParent = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(OldParentDesc, OldParent), RADIENT_STATUS_OK);

        RadientEntityDesc ChildDesc;
        ChildDesc.Parent    = OldParent;
        ChildDesc.Transform = MakeTranslation(1.f, 2.f, 3.f);

        RadientEntityID Child = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);

        RadientEntityDesc NewParentDesc;
        NewParentDesc.Transform = MakeTranslation(100.f, 200.f, 300.f);

        RadientEntityID NewParent = InvalidRadientEntityID;
        EXPECT_EQ(State.CreateEntity(NewParentDesc, NewParent), RADIENT_STATUS_OK);
        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        const RadientTransform PendingNewParentTransform = MakeTranslation(400.f, 500.f, 600.f);
        const RadientMatrix4x4 ExpectedWorld             = RadientMath::MultiplyMatrices(
            RadientMath::TransformToMatrix(ChildDesc.Transform),
            RadientMath::TransformToMatrix(OldParentDesc.Transform));

        EXPECT_EQ(State.SetLocalTransform(NewParent, PendingNewParentTransform), RADIENT_STATUS_OK);
        EXPECT_EQ(State.SetParent(Child, NewParent, True), RADIENT_STATUS_OK);
        EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

        // The child's world should remain the old committed world after reparenting.
        RadientMatrix4x4 Matrix;
        EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
        ExpectMatrixNear(Matrix, ExpectedWorld);
    }
}

TEST(RadientSceneStateTest, EnumerateRenderableMeshes)
{
    // Enumerates mesh+renderer entities and verifies that incomplete entities
    // are skipped while renderable metadata, visibility, bindings, and world
    // matrices are captured.
    RadientSceneState State;

    RadientEntityDesc ParentDesc;
    ParentDesc.Transform = MakeTranslation(10.f, 20.f, 30.f);

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ParentDesc, Parent), RADIENT_STATUS_OK);

    RadientEntityID MeshOnly = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, MeshOnly), RADIENT_STATUS_OK);

    TestMeshComponent MeshOnlyComponent = MakeMeshComponent("mesh://mesh-only", 1);
    EXPECT_EQ(State.SetMesh(MeshOnly, MeshOnlyComponent), RADIENT_STATUS_OK);

    // Mesh without a renderer is not renderable.
    RadientEntityID RendererOnly = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, RendererOnly), RADIENT_STATUS_OK);

    RadientMeshRendererComponent RendererOnlyComponent;
    RendererOnlyComponent.VisibilityMask = 0x10;
    EXPECT_EQ(State.SetMeshRenderer(RendererOnly, RendererOnlyComponent), RADIENT_STATUS_OK);

    // This entity has both mesh and renderer components and should be enumerated.
    RadientEntityDesc DrawableDesc;
    DrawableDesc.Parent    = Parent;
    DrawableDesc.Transform = MakeTranslation(1.f, 2.f, 3.f);

    RadientEntityID Drawable = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(DrawableDesc, Drawable), RADIENT_STATUS_OK);

    TestMeshComponent DrawableMesh = MakeMeshComponent("mesh://cube", 7);
    EXPECT_EQ(State.SetMesh(Drawable, DrawableMesh), RADIENT_STATUS_OK);

    RadientMeshRendererComponent DrawableRenderer;
    DrawableRenderer.VisibilityMask = 0x55;
    EXPECT_EQ(State.SetMeshRenderer(Drawable, DrawableRenderer), RADIENT_STATUS_OK);

    TestMaterialBinding MaterialBinding = MakeMaterialBinding(2, "material://red", 3);

    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding.Binding;
    MaterialBindings.BindingCount = 1;
    EXPECT_EQ(State.SetMaterialBindings(Drawable, MaterialBindings), RADIENT_STATUS_OK);

    // Hidden renderables are still enumerated, but report effective visibility false.
    RadientEntityDesc HiddenDrawableDesc;
    HiddenDrawableDesc.Flags     = RADIENT_ENTITY_FLAG_NONE;
    HiddenDrawableDesc.Transform = MakeTranslation(4.f, 5.f, 6.f);

    RadientEntityID HiddenDrawable = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(HiddenDrawableDesc, HiddenDrawable), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(HiddenDrawable, DrawableMesh), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(HiddenDrawable, DrawableRenderer), RADIENT_STATUS_OK);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    std::vector<CapturedRenderableMesh> RenderableMeshes;
    const RADIENT_STATUS                Status = State.EnumerateRenderableMeshes(
        [&RenderableMeshes](const RadientSceneState::RenderableMesh& RenderableMesh) {
            RenderableMeshes.push_back(CaptureRenderableMesh(RenderableMesh));
        });

    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    // Only the complete visible/hidden drawables should be returned.
    ASSERT_EQ(RenderableMeshes.size(), 2u);

    // The visible drawable should preserve mesh, renderer, material binding,
    // visibility, and inherited transform data.
    const CapturedRenderableMesh* pDrawable = FindRenderableMesh(RenderableMeshes, Drawable);
    ASSERT_NE(pDrawable, nullptr);
    EXPECT_EQ(pDrawable->MeshURI, "mesh://cube");
    EXPECT_EQ(pDrawable->MeshVersion, 7u);
    EXPECT_EQ(pDrawable->VisibilityMask, 0x55u);
    EXPECT_EQ(pDrawable->EffectiveVisible, True);
    ASSERT_TRUE(pDrawable->HasMaterialBindings);
    EXPECT_EQ(pDrawable->MaterialBindingCount, 1u);
    EXPECT_EQ(pDrawable->MaterialPrimitiveIndex, 2u);
    EXPECT_EQ(pDrawable->MaterialURI, "material://red");
    EXPECT_EQ(pDrawable->MaterialVersion, 3u);

    const RadientMatrix4x4 ExpectedDrawableWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(DrawableDesc.Transform),
        RadientMath::TransformToMatrix(ParentDesc.Transform));
    ExpectMatrixNear(pDrawable->WorldMatrix, ExpectedDrawableWorld);

    const CapturedRenderableMesh* pHiddenDrawable = FindRenderableMesh(RenderableMeshes, HiddenDrawable);
    ASSERT_NE(pHiddenDrawable, nullptr);
    // Hidden entity is still a renderable, but render code can cull it by flag.
    EXPECT_EQ(pHiddenDrawable->EffectiveVisible, False);
    EXPECT_FALSE(pHiddenDrawable->HasMaterialBindings);
    ExpectMatrixNear(pHiddenDrawable->WorldMatrix, RadientMath::TransformToMatrix(HiddenDrawableDesc.Transform));
}

TEST(RadientSceneStateTest, EnumerateRenderableMeshesReportsOutOfDateDerivedState)
{
    // Renderable enumeration should still produce data when derived transforms
    // are dirty, but report OUT_OF_DATE so callers know cached values may be stale.
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    TestMeshComponent Mesh = MakeMeshComponent("mesh://cube");
    EXPECT_EQ(State.SetMesh(Entity, Mesh), RADIENT_STATUS_OK);

    RadientMeshRendererComponent Renderer;
    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    Uint32 RenderableMeshCount = 0;
    EXPECT_EQ(State.EnumerateRenderableMeshes(
                  [&RenderableMeshCount](const RadientSceneState::RenderableMesh&) {
                      ++RenderableMeshCount;
                  }),
              RADIENT_STATUS_OK);
    EXPECT_EQ(RenderableMeshCount, 1u);

    const RadientTransform Transform = MakeTranslation(1.f, 2.f, 3.f);
    EXPECT_EQ(State.SetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);

    // The entity is still enumerable, but the status tells the renderer to
    // commit or repair before trusting cached transforms.
    RenderableMeshCount = 0;
    EXPECT_EQ(State.EnumerateRenderableMeshes(
                  [&RenderableMeshCount](const RadientSceneState::RenderableMesh&) {
                      ++RenderableMeshCount;
                  }),
              RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(RenderableMeshCount, 1u);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    std::vector<CapturedRenderableMesh> RenderableMeshes;
    EXPECT_EQ(State.EnumerateRenderableMeshes(
                  [&RenderableMeshes](const RadientSceneState::RenderableMesh& RenderableMesh) {
                      RenderableMeshes.push_back(CaptureRenderableMesh(RenderableMesh));
                  }),
              RADIENT_STATUS_OK);

    ASSERT_EQ(RenderableMeshes.size(), 1u);
    // After commit, enumeration is up-to-date and returns the new world matrix.
    ExpectMatrixNear(RenderableMeshes[0].WorldMatrix, RadientMath::TransformToMatrix(Transform));
}

TEST(RadientSceneStateTest, TracksRenderableMeshChanges)
{
    // Tracks renderable mesh delta events as components are added, updated, and
    // removed from a single entity.
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());

    EXPECT_EQ(State.SetMesh(Entity, MakeMeshComponent("mesh://cube", 1)), RADIENT_STATUS_OK);
    // Mesh alone is not renderable, so no change should be emitted yet.
    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());

    RadientMeshRendererComponent Renderer;
    Renderer.VisibilityMask = 0x55;
    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);

    std::vector<CapturedRenderableMeshChange> Changes = CaptureRenderableMeshChanges(State);
    // Adding the renderer completes the renderable and should emit Added with
    // the current renderable mesh payload.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Added);
    ASSERT_TRUE(Changes[0].HasMesh);
    EXPECT_EQ(Changes[0].Mesh.Entity, Entity);
    EXPECT_EQ(Changes[0].Mesh.MeshURI, "mesh://cube");
    EXPECT_EQ(Changes[0].Mesh.MeshVersion, 1u);
    EXPECT_EQ(Changes[0].Mesh.VisibilityMask, 0x55u);
    State.ClearRenderableMeshChanges();
    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());

    std::vector<CapturedRenderableMesh> RenderableMeshes;
    // The renderable is visible to full enumeration even before committing
    // derived state; the status reports that transform data is out of date.
    EXPECT_EQ(State.EnumerateRenderableMeshes(
                  [&RenderableMeshes](const RadientSceneState::RenderableMesh& Mesh) {
                      RenderableMeshes.push_back(CaptureRenderableMesh(Mesh));
                  }),
              RADIENT_STATUS_OUT_OF_DATE);
    const CapturedRenderableMesh* pRenderable = FindRenderableMesh(RenderableMeshes, Entity);
    ASSERT_NE(pRenderable, nullptr);
    EXPECT_EQ(pRenderable->MeshURI, "mesh://cube");
    EXPECT_EQ(pRenderable->VisibilityMask, 0x55u);

    EXPECT_EQ(State.SetMesh(Entity, MakeMeshComponent("mesh://sphere", 2)), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // Changing the mesh reference on an existing renderable should emit Updated.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Updated);
    ASSERT_TRUE(Changes[0].HasMesh);
    EXPECT_EQ(Changes[0].Mesh.MeshURI, "mesh://sphere");
    EXPECT_EQ(Changes[0].Mesh.MeshVersion, 2u);
    State.ClearRenderableMeshChanges();

    TestMaterialBinding MaterialBinding = MakeMaterialBinding(1, "material://blue", 3);

    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding.Binding;
    MaterialBindings.BindingCount = 1;
    EXPECT_EQ(State.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // Adding material bindings changes renderer-visible data, so it is an update.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Updated);
    ASSERT_TRUE(Changes[0].HasMesh);
    EXPECT_TRUE(Changes[0].Mesh.HasMaterialBindings);
    EXPECT_EQ(Changes[0].Mesh.MaterialBindingCount, 1u);
    EXPECT_EQ(Changes[0].Mesh.MaterialPrimitiveIndex, 1u);
    EXPECT_EQ(Changes[0].Mesh.MaterialURI, "material://blue");
    EXPECT_EQ(Changes[0].Mesh.MaterialVersion, 3u);
    State.ClearRenderableMeshChanges();

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MATERIAL_BINDINGS), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // Removing optional material bindings keeps the entity renderable and emits Updated.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Updated);
    ASSERT_TRUE(Changes[0].HasMesh);
    EXPECT_FALSE(Changes[0].Mesh.HasMaterialBindings);
    State.ClearRenderableMeshChanges();

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MESH_RENDERER), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // Removing the renderer makes the entity non-renderable, so the change has
    // no renderable mesh payload.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Removed);
    EXPECT_FALSE(Changes[0].HasMesh);
    RenderableMeshes.clear();
    EXPECT_EQ(State.EnumerateRenderableMeshes(
                  [&RenderableMeshes](const RadientSceneState::RenderableMesh& Mesh) {
                      RenderableMeshes.push_back(CaptureRenderableMesh(Mesh));
                  }),
              RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(FindRenderableMesh(RenderableMeshes, Entity), nullptr);
    State.ClearRenderableMeshChanges();

    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // Re-adding the renderer recreates the renderable and emits Added.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Added);
    ASSERT_TRUE(Changes[0].HasMesh);
    EXPECT_EQ(Changes[0].Mesh.MeshURI, "mesh://sphere");
    State.ClearRenderableMeshChanges();

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MESH), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // Removing the mesh also removes the renderable from enumeration.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Removed);
    EXPECT_FALSE(Changes[0].HasMesh);
    RenderableMeshes.clear();
    EXPECT_EQ(State.EnumerateRenderableMeshes(
                  [&RenderableMeshes](const RadientSceneState::RenderableMesh& Mesh) {
                      RenderableMeshes.push_back(CaptureRenderableMesh(Mesh));
                  }),
              RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(FindRenderableMesh(RenderableMeshes, Entity), nullptr);
    State.ClearRenderableMeshChanges();
    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());
}

TEST(RadientSceneStateTest, TransformAndVisibilityDoNotEmitRenderableMeshChanges)
{
    // Renderable mesh changes describe immutable drawable structure. Transform
    // and visibility remain mutable per-frame state, so changing them should not
    // force the renderer to rebuild draw lists or primitive/material data.
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(Entity, MakeMeshComponent("mesh://cube")), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    State.ClearRenderableMeshChanges();

    EXPECT_EQ(State.SetLocalTransform(Entity, MakeTranslation(1.f, 2.f, 3.f)), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());

    EXPECT_EQ(State.SetEntityOwnVisibility(Entity, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());
}

TEST(RadientSceneStateTest, CoalescesRenderableMeshChanges)
{
    // Multiple edits before the renderer consumes changes should coalesce to
    // the final renderable state instead of emitting noisy intermediate events.
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    EXPECT_EQ(State.SetMesh(Entity, MakeMeshComponent("mesh://temporary", 1)), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(Entity, MakeMeshComponent("mesh://temporary", 2)), RADIENT_STATUS_OK);
    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MESH_RENDERER), RADIENT_STATUS_OK);
    // Added then removed before sync means no net renderable change.
    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());

    EXPECT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    std::vector<CapturedRenderableMeshChange> Changes = CaptureRenderableMeshChanges(State);
    // Re-adding the renderer after mesh edits emits one Added change with the
    // latest mesh version.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Added);
    ASSERT_TRUE(Changes[0].HasMesh);
    EXPECT_EQ(Changes[0].Mesh.MeshURI, "mesh://temporary");
    EXPECT_EQ(Changes[0].Mesh.MeshVersion, 2u);
    State.ClearRenderableMeshChanges();

    EXPECT_EQ(State.SetMesh(Entity, MakeMeshComponent("mesh://updated", 3)), RADIENT_STATUS_OK);
    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MESH_RENDERER), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // Updated, removed, and added again before consumption coalesces to Updated.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, Entity);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Updated);
    ASSERT_TRUE(Changes[0].HasMesh);
    EXPECT_EQ(Changes[0].Mesh.MeshURI, "mesh://updated");
    EXPECT_EQ(Changes[0].Mesh.MeshVersion, 3u);
    State.ClearRenderableMeshChanges();

    RadientEntityID Transient = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Transient), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(Transient, MakeMeshComponent("mesh://transient")), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(Transient, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.DestroyEntity(Transient), RADIENT_STATUS_OK);
    // A renderable created and destroyed before sync should leave no change.
    EXPECT_TRUE(CaptureRenderableMeshChanges(State).empty());

    RadientEntityID RemovedBeforeDestroy = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, RemovedBeforeDestroy), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(RemovedBeforeDestroy, MakeMeshComponent("mesh://removed-before-destroy")), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(RemovedBeforeDestroy, {}), RADIENT_STATUS_OK);
    State.ClearRenderableMeshChanges();

    EXPECT_EQ(State.RemoveComponent(RemovedBeforeDestroy, RADIENT_COMPONENT_TYPE_MESH_RENDERER), RADIENT_STATUS_OK);
    EXPECT_EQ(State.DestroyEntity(RemovedBeforeDestroy), RADIENT_STATUS_OK);

    Changes = CaptureRenderableMeshChanges(State);
    // If a previously synchronized renderable is removed before destruction,
    // the renderer should still receive exactly one Removed event.
    ASSERT_EQ(Changes.size(), 1u);
    EXPECT_EQ(Changes[0].Entity, RemovedBeforeDestroy);
    EXPECT_EQ(Changes[0].Type, RenderableMeshChangeType::Removed);
    EXPECT_FALSE(Changes[0].HasMesh);
}

TEST(RadientSceneStateTest, DestroyEntityRecordsRenderableMeshChangesForSubtree)
{
    // Destroying a parent subtree should emit Removed changes for every
    // renderable entity inside the subtree and leave unrelated siblings alone.
    RadientSceneState State;

    RadientEntityID Root = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Root), RADIENT_STATUS_OK);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Root;

    RadientEntityID Child = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);

    RadientEntityDesc GrandChildDesc;
    GrandChildDesc.Parent = Child;

    RadientEntityID GrandChild = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity(GrandChildDesc, GrandChild), RADIENT_STATUS_OK);

    RadientEntityID Sibling = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Sibling), RADIENT_STATUS_OK);

    EXPECT_EQ(State.SetMesh(Root, MakeMeshComponent("mesh://root")), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(Root, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(Child, MakeMeshComponent("mesh://child")), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(Child, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(GrandChild, MakeMeshComponent("mesh://grand-child")), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(GrandChild, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(Sibling, MakeMeshComponent("mesh://sibling")), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(Sibling, {}), RADIENT_STATUS_OK);

    State.ClearRenderableMeshChanges();
    EXPECT_EQ(State.DestroyEntity(Root), RADIENT_STATUS_OK);

    const std::vector<CapturedRenderableMeshChange> Changes = CaptureRenderableMeshChanges(State);
    // Root, child, and grandchild were renderable and should all be removed.
    ASSERT_EQ(Changes.size(), 3u);

    const CapturedRenderableMeshChange* pRootChange = FindRenderableMeshChange(Changes, Root);
    ASSERT_NE(pRootChange, nullptr);
    // Removed changes do not include a renderable payload because the entity is gone.
    EXPECT_EQ(pRootChange->Type, RenderableMeshChangeType::Removed);
    EXPECT_FALSE(pRootChange->HasMesh);

    const CapturedRenderableMeshChange* pChildChange = FindRenderableMeshChange(Changes, Child);
    ASSERT_NE(pChildChange, nullptr);
    EXPECT_EQ(pChildChange->Type, RenderableMeshChangeType::Removed);
    EXPECT_FALSE(pChildChange->HasMesh);

    const CapturedRenderableMeshChange* pGrandChildChange = FindRenderableMeshChange(Changes, GrandChild);
    ASSERT_NE(pGrandChildChange, nullptr);
    EXPECT_EQ(pGrandChildChange->Type, RenderableMeshChangeType::Removed);
    EXPECT_FALSE(pGrandChildChange->HasMesh);

    EXPECT_EQ(FindRenderableMeshChange(Changes, Sibling), nullptr);
    std::vector<CapturedRenderableMesh> RenderableMeshes;
    // The sibling was outside the destroyed subtree and should remain renderable.
    EXPECT_EQ(State.EnumerateRenderableMeshes(
                  [&RenderableMeshes](const RadientSceneState::RenderableMesh& Mesh) {
                      RenderableMeshes.push_back(CaptureRenderableMesh(Mesh));
                  }),
              RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_NE(FindRenderableMesh(RenderableMeshes, Sibling), nullptr);
}

TEST(RadientSceneStateTest, EnumerateRenderableLights)
{
    // Enumerates light components and verifies that unrelated mesh renderables
    // are ignored while light world transforms and visibility are reported.
    RadientSceneState State;

    RadientEntityDesc ParentDesc;
    ParentDesc.Transform = MakeTranslation(10.f, 0.f, 0.f);

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ParentDesc, Parent), RADIENT_STATUS_OK);

    RadientEntityDesc LightDesc;
    LightDesc.Parent    = Parent;
    LightDesc.Transform = MakeTranslation(1.f, 2.f, 3.f);

    RadientEntityID LightEntity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(LightDesc, LightEntity), RADIENT_STATUS_OK);

    RadientLightComponent Light;
    Light.Type      = RADIENT_LIGHT_TYPE_POINT;
    Light.Color     = {0.25f, 0.5f, 1.f};
    Light.Intensity = 7.f;
    EXPECT_EQ(State.SetLight(LightEntity, Light), RADIENT_STATUS_OK);

    RadientEntityDesc HiddenLightDesc;
    HiddenLightDesc.Flags     = RADIENT_ENTITY_FLAG_NONE;
    HiddenLightDesc.Transform = MakeTranslation(4.f, 5.f, 6.f);

    RadientEntityID HiddenLight = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(HiddenLightDesc, HiddenLight), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetLight(HiddenLight, {}), RADIENT_STATUS_OK);

    RadientEntityID MeshOnly = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, MeshOnly), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMesh(MeshOnly, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMeshRenderer(MeshOnly, {}), RADIENT_STATUS_OK);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    std::vector<CapturedRenderableLight> RenderableLights;
    const RADIENT_STATUS                 Status = State.EnumerateRenderableLights(
        [&RenderableLights](const RadientSceneState::RenderableLight& RenderableLight) {
            RenderableLights.push_back(CaptureRenderableLight(RenderableLight));
        });

    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    // Only light entities should be returned.
    ASSERT_EQ(RenderableLights.size(), 2u);

    const CapturedRenderableLight* pLight = FindRenderableLight(RenderableLights, LightEntity);
    ASSERT_NE(pLight, nullptr);
    // The visible light should preserve component data and inherited transform.
    EXPECT_TRUE(pLight->Light == Light);
    EXPECT_EQ(pLight->EffectiveVisible, True);

    const RadientMatrix4x4 ExpectedLightWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(LightDesc.Transform),
        RadientMath::TransformToMatrix(ParentDesc.Transform));
    ExpectMatrixNear(pLight->WorldMatrix, ExpectedLightWorld);

    const CapturedRenderableLight* pHiddenLight = FindRenderableLight(RenderableLights, HiddenLight);
    ASSERT_NE(pHiddenLight, nullptr);
    // Hidden lights are still enumerated and report effective visibility false.
    EXPECT_EQ(pHiddenLight->EffectiveVisible, False);
    ExpectMatrixNear(pHiddenLight->WorldMatrix, RadientMath::TransformToMatrix(HiddenLightDesc.Transform));
}

TEST(RadientSceneStateTest, EnumerateRenderableLightsReportsOutOfDateDerivedState)
{
    // Light enumeration mirrors mesh enumeration: data is returned while dirty,
    // but the status tells callers that cached derived values may be stale.
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetLight(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    Uint32 RenderableLightCount = 0;
    EXPECT_EQ(State.EnumerateRenderableLights(
                  [&RenderableLightCount](const RadientSceneState::RenderableLight&) {
                      ++RenderableLightCount;
                  }),
              RADIENT_STATUS_OK);
    EXPECT_EQ(RenderableLightCount, 1u);

    const RadientTransform Transform = MakeTranslation(1.f, 2.f, 3.f);
    EXPECT_EQ(State.SetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);

    // The light is still found, but the stale transform makes the status out of date.
    RenderableLightCount = 0;
    EXPECT_EQ(State.EnumerateRenderableLights(
                  [&RenderableLightCount](const RadientSceneState::RenderableLight&) {
                      ++RenderableLightCount;
                  }),
              RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(RenderableLightCount, 1u);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    std::vector<CapturedRenderableLight> RenderableLights;
    EXPECT_EQ(State.EnumerateRenderableLights(
                  [&RenderableLights](const RadientSceneState::RenderableLight& RenderableLight) {
                      RenderableLights.push_back(CaptureRenderableLight(RenderableLight));
                  }),
              RADIENT_STATUS_OK);

    ASSERT_EQ(RenderableLights.size(), 1u);
    // After commit, the enumerated light should contain the updated world matrix.
    ExpectMatrixNear(RenderableLights[0].WorldMatrix, RadientMath::TransformToMatrix(Transform));
}

TEST(RadientSceneStateTest, HasComponent)
{
    // Validates HasComponent for built-in, custom, removed, and mandatory
    // components, including invalid component IDs and missing entities.
    RadientSceneState State;

    Bool HasComponent = True;
    // Missing entities should report not found and reset the output flag.
    EXPECT_EQ(State.HasComponent(1, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(HasComponent, False);

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    HasComponent = True;
    // Invalid component type IDs are rejected and clear the output flag.
    EXPECT_EQ(State.HasComponent(Entity, InvalidRadientComponentTypeID, HasComponent), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(HasComponent, False);

    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    // Each optional built-in component should report absent before Set* and
    // present after Set*.
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
    EXPECT_EQ(State.SetCamera(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MESH, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
    EXPECT_EQ(State.SetMesh(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MESH, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MESH_RENDERER, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
    EXPECT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MESH_RENDERER, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    TestMaterialBinding              MaterialBinding = MakeMaterialBinding(0, "material://asset", 1);
    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding.Binding;
    MaterialBindings.BindingCount = 1;

    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MATERIAL_BINDINGS, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
    EXPECT_EQ(State.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MATERIAL_BINDINGS, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_LIGHT, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
    EXPECT_EQ(State.SetLight(Entity, {}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_LIGHT, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    static constexpr RadientComponentTypeID CustomComponentType = 100;
    EXPECT_EQ(State.HasComponent(Entity, CustomComponentType, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);

    // Custom component data should be tracked by its caller-provided type ID.
    RadientCustomComponentData CustomComponent;
    CustomComponent.ComponentType = CustomComponentType;
    EXPECT_EQ(State.SetCustomComponentData(Entity, CustomComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, CustomComponentType, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);
    // Removed optional components should return absent.
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);

    EXPECT_EQ(State.RemoveComponent(Entity, CustomComponentType), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, CustomComponentType, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM), RADIENT_STATUS_INVALID_OPERATION);
    // Transform is mandatory and should still be present after a failed remove.
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    EXPECT_EQ(State.DestroyEntity(Entity), RADIENT_STATUS_OK);
    HasComponent = True;
    // Destroyed entities should report not found and clear the output flag.
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(HasComponent, False);
}

TEST(RadientSceneStateTest, GetCamera)
{
    // Verifies camera component get/set/update/remove behavior and that
    // missing cameras reset the output component to defaults.
    RadientSceneState State;

    RadientCameraComponent Camera;
    Camera.FocalLength = 35.f;
    // Missing entities should report not found and reset output.
    EXPECT_EQ(State.GetCamera(1, Camera), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Camera, RadientCameraComponent{});

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    Camera.FocalLength = 35.f;
    // Existing entities without a camera should also report not found.
    EXPECT_EQ(State.GetCamera(Entity, Camera), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Camera, RadientCameraComponent{});

    RadientCameraComponent ExpectedCamera;
    ExpectedCamera.Projection         = RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC;
    ExpectedCamera.HorizontalAperture = 10.f;
    ExpectedCamera.VerticalAperture   = 5.f;
    ExpectedCamera.FocalLength        = 50.f;
    ExpectedCamera.ClippingRange      = {0.2f, 250.f};
    ExpectedCamera.FStop              = 8.f;
    ExpectedCamera.FocusDistance      = 12.f;

    EXPECT_EQ(State.SetCamera(Entity, ExpectedCamera), RADIENT_STATUS_OK);
    // The initial camera set should be retrieved exactly.
    EXPECT_EQ(State.GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera, ExpectedCamera);

    ExpectedCamera.FocalLength = 85.f;
    EXPECT_EQ(State.SetCamera(Entity, ExpectedCamera), RADIENT_STATUS_OK);
    // Updating camera data should replace the stored component.
    EXPECT_EQ(State.GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera, ExpectedCamera);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);
    // After removal, GetCamera should again return not found and defaults.
    EXPECT_EQ(State.GetCamera(Entity, Camera), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Camera, RadientCameraComponent{});
}

TEST(RadientSceneStateTest, SetBuiltInComponentReturnsNoChangeForSameValue)
{
    // Built-in component setters should report NO_CHANGE and avoid revision
    // bumps when the new value is equivalent to the stored value.
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    RadientCameraComponent Camera;
    Camera.FocalLength = 35.f;

    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_OK);
    RadientSceneRevisions Revisions = State.GetSceneRevisions();
    // Re-setting identical camera data should not change revisions.
    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);

    Camera.FocalLength = 50.f;
    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://asset", 7);

    RadientMeshComponent Mesh;
    Mesh.pMesh = pMesh;

    EXPECT_EQ(State.SetMesh(Entity, Mesh), RADIENT_STATUS_OK);
    Revisions = State.GetSceneRevisions();

    // Re-setting the same mesh asset should compare as no change.
    RadientMeshComponent SameMesh = Mesh;
    EXPECT_EQ(State.SetMesh(Entity, SameMesh), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);

    RefCntAutoPtr<IRadientMeshAsset> pUpdatedMesh = MakeTestMeshAsset("mesh://asset", 8);
    Mesh.pMesh                                    = pUpdatedMesh;
    EXPECT_EQ(State.SetMesh(Entity, Mesh), RADIENT_STATUS_OK);

    RadientMeshRendererComponent Renderer;
    Renderer.VisibilityMask = 0x0F;

    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);
    Revisions = State.GetSceneRevisions();
    // Same renderer flags should not bump drawable revisions.
    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);

    Renderer.VisibilityMask = 0xF0;
    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeTestMaterialAsset("material://asset", 2);

    RadientMaterialBinding MaterialBinding;
    MaterialBinding.PrimitiveIndex = 0;
    MaterialBinding.pMaterial      = pMaterial;

    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding;
    MaterialBindings.BindingCount = 1;

    EXPECT_EQ(State.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);
    Revisions = State.GetSceneRevisions();

    // Re-setting the same material asset should compare as no change.
    RadientMaterialBinding SameMaterialBinding = MaterialBinding;

    RadientMaterialBindingsComponent SameMaterialBindings;
    SameMaterialBindings.pBindings    = &SameMaterialBinding;
    SameMaterialBindings.BindingCount = 1;
    EXPECT_EQ(State.SetMaterialBindings(Entity, SameMaterialBindings), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);

    RefCntAutoPtr<IRadientMaterialAsset> pUpdatedMaterial = MakeTestMaterialAsset("material://asset", 3);
    MaterialBinding.pMaterial                             = pUpdatedMaterial;
    EXPECT_EQ(State.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);

    RadientLightComponent Light;
    Light.Intensity = 4.f;

    EXPECT_EQ(State.SetLight(Entity, Light), RADIENT_STATUS_OK);
    Revisions = State.GetSceneRevisions();
    // Same light values should not bump light revisions.
    EXPECT_EQ(State.SetLight(Entity, Light), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);

    Light.Intensity = 8.f;
    EXPECT_EQ(State.SetLight(Entity, Light), RADIENT_STATUS_OK);
}

TEST(RadientSceneStateTest, TracksSceneRevisions)
{
    // Verifies that each scene revision counter advances only for the category
    // affected by a mutation.
    RadientSceneState State;
    // A fresh state starts with all revision counters at zero.
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 0, 0);

    RadientEntityID Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    // Creating an entity affects transform and visibility derived data.
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 1, 1);

    const RadientSceneRevisions AfterCreate = State.GetSceneRevisions();
    EXPECT_EQ(State.SetLocalTransform(Entity, {}), RADIENT_STATUS_NO_CHANGE);
    // No-change transform updates should leave all counters unchanged.
    EXPECT_EQ(State.GetSceneRevisions(), AfterCreate);

    EXPECT_EQ(State.SetLocalTransform(Entity, MakeTranslation(1.f, 2.f, 3.f)), RADIENT_STATUS_OK);
    // Local transform changes bump transform revisions only.
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 2, 1);

    EXPECT_EQ(State.SetEntityOwnVisibility(Entity, False), RADIENT_STATUS_OK);
    // Own visibility changes bump visibility revisions only.
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 2, 2);

    TestMeshComponent Mesh = MakeMeshComponent("mesh://revision-test", 1);
    EXPECT_EQ(State.SetMesh(Entity, Mesh), RADIENT_STATUS_OK);
    // Mesh data affects drawable revisions.
    ExpectSceneRevisions(State.GetSceneRevisions(), 1, 0, 2, 2);

    EXPECT_EQ(State.SetMesh(Entity, Mesh), RADIENT_STATUS_NO_CHANGE);
    // Identical mesh data is a no-op.
    ExpectSceneRevisions(State.GetSceneRevisions(), 1, 0, 2, 2);

    RadientMeshRendererComponent Renderer;
    Renderer.VisibilityMask = 7;
    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);
    // Mesh renderer state also affects drawable revisions.
    ExpectSceneRevisions(State.GetSceneRevisions(), 2, 0, 2, 2);

    TestMaterialBinding MaterialBinding = MakeMaterialBinding(0, "material://revision-test", 1);

    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding.Binding;
    MaterialBindings.BindingCount = 1;
    EXPECT_EQ(State.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);
    // Material bindings are part of drawable data.
    ExpectSceneRevisions(State.GetSceneRevisions(), 3, 0, 2, 2);

    RadientLightComponent Light;
    Light.Intensity = 4.f;
    EXPECT_EQ(State.SetLight(Entity, Light), RADIENT_STATUS_OK);
    // Light components have their own revision counter.
    ExpectSceneRevisions(State.GetSceneRevisions(), 3, 1, 2, 2);

    RadientCameraComponent Camera;
    Camera.FocalLength = 35.f;
    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_OK);
    // Cameras are tracked separately from drawables and lights.
    ExpectSceneRevisions(State.GetSceneRevisions(), 3, 1, 2, 2, 1);

    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_NO_CHANGE);
    // Re-setting identical camera data should not advance revisions.
    ExpectSceneRevisions(State.GetSceneRevisions(), 3, 1, 2, 2, 1);

    Camera.FocalLength = 50.f;
    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_OK);
    ExpectSceneRevisions(State.GetSceneRevisions(), 3, 1, 2, 2, 2);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MESH_RENDERER), RADIENT_STATUS_OK);
    // Removing renderer data changes drawable membership.
    ExpectSceneRevisions(State.GetSceneRevisions(), 4, 1, 2, 2, 2);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_LIGHT), RADIENT_STATUS_OK);
    // Removing light data advances light revisions.
    ExpectSceneRevisions(State.GetSceneRevisions(), 4, 2, 2, 2, 2);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);
    // Removing camera data advances camera revisions.
    ExpectSceneRevisions(State.GetSceneRevisions(), 4, 2, 2, 2, 3);
}

TEST(RadientSceneStateTest, HierarchyChangesUpdateSceneRevisions)
{
    // Parent changes and subtree destruction affect transform and visibility
    // derived data, and destroying entities also removes renderable categories.
    RadientSceneState State;

    RadientEntityID Parent = InvalidRadientEntityID;
    RadientEntityID Child  = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Parent), RADIENT_STATUS_OK);
    ASSERT_EQ(State.CreateEntity({}, Child), RADIENT_STATUS_OK);
    // Two independent roots each add transform and visibility state.
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 2, 2);

    EXPECT_EQ(State.SetParent(Child, Parent, False), RADIENT_STATUS_OK);
    // Reparenting dirties child transform and visibility inheritance.
    ExpectSceneRevisions(State.GetSceneRevisions(), 0, 0, 3, 3);

    EXPECT_EQ(State.DestroyEntity(Parent), RADIENT_STATUS_OK);
    // Destroying the parent removes the child subtree and advances all affected
    // render data categories.
    ExpectSceneRevisions(State.GetSceneRevisions(), 1, 1, 4, 4, 1);
}

} // namespace
