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
    Captured.MeshURI          = RenderableMesh.Mesh.Mesh.URI != nullptr ? RenderableMesh.Mesh.Mesh.URI : "";
    Captured.MeshVersion      = RenderableMesh.Mesh.Mesh.Version;
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
            Captured.MaterialURI                  = Binding.Material.URI != nullptr ? Binding.Material.URI : "";
            Captured.MaterialVersion              = Binding.Material.Version;
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

TEST(RadientSceneStateTest, GetDesc)
{
    RadientSceneState DefaultState;
    EXPECT_EQ(DefaultState.GetDesc().Name, nullptr);

    char Name[] = "Scene A";

    RadientSceneDesc Desc;
    Desc.Name = Name;

    RadientSceneState State{Desc};

    Name[0] = 'X';

    EXPECT_STREQ(State.GetDesc().Name, "Scene A");
    EXPECT_NE(State.GetDesc().Name, Desc.Name);
}

TEST(RadientSceneStateTest, GetDescCopiesHeapName)
{
    std::unique_ptr<std::string> Name = std::make_unique<std::string>("Heap Scene");

    RadientSceneDesc Desc;
    Desc.Name = Name->c_str();

    RadientSceneState State{Desc};

    Name.reset();

    EXPECT_STREQ(State.GetDesc().Name, "Heap Scene");
}

TEST(RadientSceneStateTest, IsEntityAlive)
{
    RadientSceneState State;

    EXPECT_EQ(State.IsEntityAlive(1), RADIENT_STATUS_NOT_FOUND);

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_NE(Entity, InvalidRadientEntityID);

    EXPECT_EQ(State.IsEntityAlive(Entity), RADIENT_STATUS_OK);

    EXPECT_EQ(State.DestroyEntity(Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.IsEntityAlive(Entity), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientSceneStateTest, DestroyEntityHandlesDeepHierarchy)
{
    static constexpr Uint32 NodeCount = 1000;

    RadientSceneState            State;
    std::vector<RadientEntityID> Entities = CreateLinearChain(State, NodeCount);

    EXPECT_EQ(State.DestroyEntity(Entities.front()), RADIENT_STATUS_OK);

    for (const RadientEntityID Entity : Entities)
        EXPECT_EQ(State.IsEntityAlive(Entity), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientSceneStateTest, GetEntityFlags)
{
    RadientSceneState State;

    const RADIENT_ENTITY_FLAGS InvalidEntityFlags =
        static_cast<RADIENT_ENTITY_FLAGS>(static_cast<Uint32>(RADIENT_ENTITY_FLAGS_ALL) << 1u);

    RADIENT_ENTITY_FLAGS Flags = RADIENT_ENTITY_FLAG_VISIBLE;
    EXPECT_EQ(State.GetEntityFlags(1, Flags), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_NONE);

    RadientEntityDesc InvalidEntityDesc;
    InvalidEntityDesc.Flags = InvalidEntityFlags;

    RadientEntityID InvalidEntity = 123;
    EXPECT_EQ(State.CreateEntity(InvalidEntityDesc, InvalidEntity), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(InvalidEntity, InvalidRadientEntityID);

    RadientEntityID DefaultEntity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, DefaultEntity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityFlags(DefaultEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

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

    EXPECT_EQ(State.SetEntityFlags(HiddenEntity, RADIENT_ENTITY_FLAG_VISIBLE), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetEntityFlags(HiddenEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

    EXPECT_EQ(State.GetEntityFlags(DefaultEntity, Flags), RADIENT_STATUS_OK);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_VISIBLE);

    EXPECT_EQ(State.DestroyEntity(HiddenEntity), RADIENT_STATUS_OK);
    Flags = RADIENT_ENTITY_FLAG_VISIBLE;
    EXPECT_EQ(State.GetEntityFlags(HiddenEntity, Flags), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_NONE);
}

TEST(RadientSceneStateTest, GetEntityOwnVisibility)
{
    RadientSceneState State;

    Bool Visible = True;
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

    EXPECT_EQ(State.SetEntityOwnVisibility(HiddenEntity, True), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.SetEntityOwnVisibility(HiddenEntity, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.DestroyEntity(HiddenEntity), RADIENT_STATUS_OK);
    Visible = True;
    EXPECT_EQ(State.GetEntityOwnVisibility(HiddenEntity, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, GetEntityEffectiveVisibility)
{
    RadientSceneState State;

    Bool Visible = True;
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
    EXPECT_EQ(State.GetEntityOwnVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetEntityOwnVisibility(Child, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Root, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetParent(GrandChild, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.DestroyEntity(GrandChild), RADIENT_STATUS_OK);
    Visible = True;
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);
    Visible = True;
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, LazyEffectiveVisibilityUpdatePreservesCommitPropagation)
{
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

    Bool Visible = True;
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child0, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child1, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, GetParent)
{
    RadientSceneState State;

    RadientEntityID Parent = 1;
    EXPECT_EQ(State.GetParent(1, Parent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Parent, InvalidRadientEntityID);

    RadientEntityID Root = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Root), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Root, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, InvalidRadientEntityID);

    RadientEntityDesc ChildDesc;
    ChildDesc.Parent = Root;

    RadientEntityID Child = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ChildDesc, Child), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);

    RadientEntityID NewParent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, NewParent), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetParent(Child, NewParent, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, NewParent);

    EXPECT_EQ(State.SetParent(Child, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, InvalidRadientEntityID);

    EXPECT_EQ(State.SetParent(Child, Root, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);

    EXPECT_EQ(State.DestroyEntity(Root), RADIENT_STATUS_OK);
    Parent = NewParent;
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Parent, InvalidRadientEntityID);
}

TEST(RadientSceneStateTest, SetParentRejectsCycles)
{
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
    EXPECT_EQ(State.SetParent(Child, Child, True), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);

    EXPECT_EQ(State.SetParent(Root, GrandChild, True), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State.GetParent(Root, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, InvalidRadientEntityID);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);
    EXPECT_EQ(State.GetParent(GrandChild, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Child);

    EXPECT_EQ(State.SetParent(Child, GrandChild, True), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State.GetParent(Child, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Root);
    EXPECT_EQ(State.GetParent(GrandChild, Parent), RADIENT_STATUS_OK);
    EXPECT_EQ(Parent, Child);
}

TEST(RadientSceneStateTest, GetChildCountAndChildren)
{
    RadientSceneState State;

    Uint32 ChildCount = 1;
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

    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 3u);
    EXPECT_EQ(Children[0], Child0);
    EXPECT_EQ(Children[1], Child1);
    EXPECT_EQ(Children[2], Child2);

    Children[0] = InvalidRadientEntityID;
    Children[1] = InvalidRadientEntityID;
    EXPECT_EQ(State.GetChildren(Root, 1, 2, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 2u);
    EXPECT_EQ(Children[0], Child1);
    EXPECT_EQ(Children[1], Child2);

    EXPECT_EQ(State.GetChildren(Root, 3, 1, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 0u);

    EXPECT_EQ(State.GetChildren(Root, 4, 1, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 0u);

    EXPECT_EQ(State.SetParent(Child2, Root, True), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 3u);

    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 3u);
    EXPECT_EQ(Children[0], Child0);
    EXPECT_EQ(Children[1], Child1);
    EXPECT_EQ(Children[2], Child2);

    EXPECT_EQ(State.SetParent(Child1, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 2u);

    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 2u);
    EXPECT_EQ(Children[0], Child0);
    EXPECT_EQ(Children[1], Child2);

    EXPECT_EQ(State.DestroyEntity(Child0), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetChildCount(Root, ChildCount), RADIENT_STATUS_OK);
    EXPECT_EQ(ChildCount, 1u);

    EXPECT_EQ(State.GetChildren(Root, 0, 4, Children, NumChildrenWritten), RADIENT_STATUS_OK);
    EXPECT_EQ(NumChildrenWritten, 1u);
    EXPECT_EQ(Children[0], Child2);
}

TEST(RadientSceneStateTest, GetLocalTransform)
{
    RadientSceneState State;

    RadientTransform Transform;
    Transform.Position = {1.f, 2.f, 3.f};
    EXPECT_EQ(State.GetLocalTransform(1, Transform), RADIENT_STATUS_NOT_FOUND);
    ExpectTransformEq(Transform, RadientTransform{});

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, RadientTransform{});

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

    EXPECT_EQ(State.SetLocalTransform(TransformedEntity, UpdatedTransform), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetLocalTransform(TransformedEntity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, UpdatedTransform);

    EXPECT_EQ(State.GetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);
    ExpectTransformEq(Transform, RadientTransform{});

    EXPECT_EQ(State.DestroyEntity(TransformedEntity), RADIENT_STATUS_OK);
    Transform = UpdatedTransform;
    EXPECT_EQ(State.GetLocalTransform(TransformedEntity, Transform), RADIENT_STATUS_NOT_FOUND);
    ExpectTransformEq(Transform, RadientTransform{});
}

TEST(RadientSceneStateTest, GetWorldMatrix)
{
    RadientSceneState State;

    RadientMatrix4x4 Matrix;
    Matrix.Data[0] = 2.f;
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
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    RadientMatrix4x4 CommittedChildWorld = ExpectedChildWorld;

    RootTransform.Position = {10.f, 20.f, 30.f};
    EXPECT_EQ(State.SetLocalTransform(Root, RootTransform), RADIENT_STATUS_OK);
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
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, CommittedChildWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    EXPECT_EQ(State.DestroyEntity(Child), RADIENT_STATUS_OK);
    Matrix = ExpectedChildWorld;
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_NOT_FOUND);
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});
    Matrix = ExpectedChildWorld;
    EXPECT_EQ(State.GetCachedWorldMatrix(Child, Matrix), RADIENT_STATUS_NOT_FOUND);
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});
}

TEST(RadientSceneStateTest, LazyWorldMatrixUpdatePreservesCommitPropagation)
{
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
    EXPECT_EQ(State.GetWorldMatrix(Child0, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    ExpectedWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(Child1Desc.Transform),
        RadientMath::TransformToMatrix(RootTransform));
    EXPECT_EQ(State.GetWorldMatrix(Child1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedWorld);
}

TEST(RadientSceneStateTest, LazyWorldMatrixUpdateClearsGlobalDirtyFlags)
{
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

    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OUT_OF_DATE);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));

    EXPECT_EQ(State.GetWorldMatrix(Branch0, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(UpdatedBranch0Transform));

    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));
}

TEST(RadientSceneStateTest, DestroyEntityClearsGlobalDirtyFlags)
{
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

    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OUT_OF_DATE);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Branch1, Visible), RADIENT_STATUS_OUT_OF_DATE);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.DestroyEntity(Branch0), RADIENT_STATUS_OK);

    EXPECT_EQ(State.GetCachedWorldMatrix(Branch1, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(Branch1Desc.Transform));
    EXPECT_EQ(State.GetCachedEntityEffectiveVisibility(Branch1, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
}

TEST(RadientSceneStateTest, CommitUpdatesDirtyParentBeforeDirtyChild)
{
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
    static constexpr Uint32 NodeCount = 1000;

    {
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

    RadientMatrix4x4 Matrix;
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, RadientMath::TransformToMatrix(ChildTransform));

    Bool Visible = True;
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
}

TEST(RadientSceneStateTest, SetParentKeepWorldTransformUsesPendingTransforms)
{
    {
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

        RadientMatrix4x4 Matrix;
        EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
        ExpectMatrixNear(Matrix, ExpectedWorld);
    }

    {
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

        RadientMatrix4x4 Matrix;
        EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
        ExpectMatrixNear(Matrix, ExpectedWorld);
    }

    {
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

        RadientMatrix4x4 Matrix;
        EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
        ExpectMatrixNear(Matrix, ExpectedWorld);
    }
}

TEST(RadientSceneStateTest, EnumerateRenderableMeshes)
{
    RadientSceneState State;

    RadientEntityDesc ParentDesc;
    ParentDesc.Transform = MakeTranslation(10.f, 20.f, 30.f);

    RadientEntityID Parent = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(ParentDesc, Parent), RADIENT_STATUS_OK);

    RadientEntityID MeshOnly = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, MeshOnly), RADIENT_STATUS_OK);

    RadientMeshComponent MeshOnlyComponent;
    MeshOnlyComponent.Mesh.URI     = "mesh://mesh-only";
    MeshOnlyComponent.Mesh.Version = 1;
    EXPECT_EQ(State.SetMesh(MeshOnly, MeshOnlyComponent), RADIENT_STATUS_OK);

    RadientEntityID RendererOnly = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, RendererOnly), RADIENT_STATUS_OK);

    RadientMeshRendererComponent RendererOnlyComponent;
    RendererOnlyComponent.VisibilityMask = 0x10;
    EXPECT_EQ(State.SetMeshRenderer(RendererOnly, RendererOnlyComponent), RADIENT_STATUS_OK);

    RadientEntityDesc DrawableDesc;
    DrawableDesc.Parent    = Parent;
    DrawableDesc.Transform = MakeTranslation(1.f, 2.f, 3.f);

    RadientEntityID Drawable = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity(DrawableDesc, Drawable), RADIENT_STATUS_OK);

    RadientMeshComponent DrawableMesh;
    DrawableMesh.Mesh.URI     = "mesh://cube";
    DrawableMesh.Mesh.Version = 7;
    EXPECT_EQ(State.SetMesh(Drawable, DrawableMesh), RADIENT_STATUS_OK);

    RadientMeshRendererComponent DrawableRenderer;
    DrawableRenderer.VisibilityMask = 0x55;
    EXPECT_EQ(State.SetMeshRenderer(Drawable, DrawableRenderer), RADIENT_STATUS_OK);

    RadientMaterialBinding MaterialBinding;
    MaterialBinding.PrimitiveIndex   = 2;
    MaterialBinding.Material.URI     = "material://red";
    MaterialBinding.Material.Version = 3;

    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding;
    MaterialBindings.BindingCount = 1;
    EXPECT_EQ(State.SetMaterialBindings(Drawable, MaterialBindings), RADIENT_STATUS_OK);

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
    ASSERT_EQ(RenderableMeshes.size(), 2u);

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
    EXPECT_EQ(pHiddenDrawable->EffectiveVisible, False);
    EXPECT_FALSE(pHiddenDrawable->HasMaterialBindings);
    ExpectMatrixNear(pHiddenDrawable->WorldMatrix, RadientMath::TransformToMatrix(HiddenDrawableDesc.Transform));
}

TEST(RadientSceneStateTest, EnumerateRenderableMeshesReportsOutOfDateDerivedState)
{
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    RadientMeshComponent Mesh;
    Mesh.Mesh.URI = "mesh://cube";
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
    ExpectMatrixNear(RenderableMeshes[0].WorldMatrix, RadientMath::TransformToMatrix(Transform));
}

TEST(RadientSceneStateTest, HasComponent)
{
    RadientSceneState State;

    Bool HasComponent = True;
    EXPECT_EQ(State.HasComponent(1, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(HasComponent, False);

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    HasComponent = True;
    EXPECT_EQ(State.HasComponent(Entity, InvalidRadientComponentTypeID, HasComponent), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(HasComponent, False);

    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

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

    RadientMaterialBinding MaterialBinding;
    MaterialBinding.PrimitiveIndex   = 0;
    MaterialBinding.Material.URI     = "material://asset";
    MaterialBinding.Material.Version = 1;
    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding;
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

    RadientCustomComponentData CustomComponent;
    CustomComponent.ComponentType = CustomComponentType;
    EXPECT_EQ(State.SetCustomComponentData(Entity, CustomComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, CustomComponentType, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);

    EXPECT_EQ(State.RemoveComponent(Entity, CustomComponentType), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, CustomComponentType, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);

    EXPECT_EQ(State.DestroyEntity(Entity), RADIENT_STATUS_OK);
    HasComponent = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_TRANSFORM, HasComponent), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(HasComponent, False);
}

TEST(RadientSceneStateTest, GetCamera)
{
    RadientSceneState State;

    RadientCameraComponent Camera;
    Camera.FocalLength = 35.f;
    EXPECT_EQ(State.GetCamera(1, Camera), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Camera, RadientCameraComponent{});

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    Camera.FocalLength = 35.f;
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
    EXPECT_EQ(State.GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera, ExpectedCamera);

    ExpectedCamera.FocalLength = 85.f;
    EXPECT_EQ(State.SetCamera(Entity, ExpectedCamera), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_EQ(Camera, ExpectedCamera);

    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetCamera(Entity, Camera), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Camera, RadientCameraComponent{});
}

TEST(RadientSceneStateTest, SetBuiltInComponentReturnsNoChangeForSameValue)
{
    RadientSceneState State;

    RadientEntityID Entity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    RadientCameraComponent Camera;
    Camera.FocalLength = 35.f;

    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_OK);
    RadientRevision Revision = State.GetRevision();
    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetRevision(), Revision);

    Camera.FocalLength = 50.f;
    EXPECT_EQ(State.SetCamera(Entity, Camera), RADIENT_STATUS_OK);

    char MeshURI0[] = "mesh://asset";
    char MeshURI1[] = "mesh://asset";

    RadientMeshComponent Mesh;
    Mesh.Mesh.URI     = MeshURI0;
    Mesh.Mesh.Version = 7;

    EXPECT_EQ(State.SetMesh(Entity, Mesh), RADIENT_STATUS_OK);
    Revision = State.GetRevision();

    RadientMeshComponent SameMesh = Mesh;
    SameMesh.Mesh.URI             = MeshURI1;
    EXPECT_EQ(State.SetMesh(Entity, SameMesh), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetRevision(), Revision);

    Mesh.Mesh.Version = 8;
    EXPECT_EQ(State.SetMesh(Entity, Mesh), RADIENT_STATUS_OK);

    RadientMeshRendererComponent Renderer;
    Renderer.VisibilityMask = 0x0F;

    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);
    Revision = State.GetRevision();
    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetRevision(), Revision);

    Renderer.VisibilityMask = 0xF0;
    EXPECT_EQ(State.SetMeshRenderer(Entity, Renderer), RADIENT_STATUS_OK);

    char MaterialURI0[] = "material://asset";
    char MaterialURI1[] = "material://asset";

    RadientMaterialBinding MaterialBinding;
    MaterialBinding.PrimitiveIndex   = 0;
    MaterialBinding.Material.URI     = MaterialURI0;
    MaterialBinding.Material.Version = 2;

    RadientMaterialBindingsComponent MaterialBindings;
    MaterialBindings.pBindings    = &MaterialBinding;
    MaterialBindings.BindingCount = 1;

    EXPECT_EQ(State.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);
    Revision = State.GetRevision();

    RadientMaterialBinding SameMaterialBinding = MaterialBinding;
    SameMaterialBinding.Material.URI           = MaterialURI1;

    RadientMaterialBindingsComponent SameMaterialBindings;
    SameMaterialBindings.pBindings    = &SameMaterialBinding;
    SameMaterialBindings.BindingCount = 1;
    EXPECT_EQ(State.SetMaterialBindings(Entity, SameMaterialBindings), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetRevision(), Revision);

    MaterialBinding.Material.Version = 3;
    EXPECT_EQ(State.SetMaterialBindings(Entity, MaterialBindings), RADIENT_STATUS_OK);

    RadientLightComponent Light;
    Light.Intensity = 4.f;

    EXPECT_EQ(State.SetLight(Entity, Light), RADIENT_STATUS_OK);
    Revision = State.GetRevision();
    EXPECT_EQ(State.SetLight(Entity, Light), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetRevision(), Revision);

    Light.Intensity = 8.f;
    EXPECT_EQ(State.SetLight(Entity, Light), RADIENT_STATUS_OK);
}

} // namespace
