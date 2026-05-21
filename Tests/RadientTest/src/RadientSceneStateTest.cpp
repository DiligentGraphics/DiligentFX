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

TEST(RadientSceneStateTest, GetEntityFlags)
{
    RadientSceneState State;

    RADIENT_ENTITY_FLAGS Flags = RADIENT_ENTITY_FLAG_VISIBLE;
    EXPECT_EQ(State.GetEntityFlags(1, Flags), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Flags, RADIENT_ENTITY_FLAG_NONE);

    RadientEntityID DefaultEntity = InvalidRadientEntityID;
    EXPECT_EQ(State.CreateEntity({}, DefaultEntity), RADIENT_STATUS_OK);
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

    EXPECT_EQ(State.SetEntityOwnVisibility(Root, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Child, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetEntityOwnVisibility(Child, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(Child, Visible), RADIENT_STATUS_OK);
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

    EXPECT_EQ(State.SetEntityOwnVisibility(Child, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetEntityOwnVisibility(Root, False), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    EXPECT_EQ(State.SetParent(GrandChild, InvalidRadientEntityID, True), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);

    EXPECT_EQ(State.DestroyEntity(GrandChild), RADIENT_STATUS_OK);
    Visible = True;
    EXPECT_EQ(State.GetEntityEffectiveVisibility(GrandChild, Visible), RADIENT_STATUS_NOT_FOUND);
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

    EXPECT_EQ(State.GetChildren(Root, 3, 1, Children, NumChildrenWritten), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(NumChildrenWritten, 0u);

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
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Root, Matrix), RADIENT_STATUS_OK);
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
    ExpectMatrixNear(Matrix, RadientMatrix4x4{});

    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);

    RadientMatrix4x4 CommittedChildWorld = ExpectedChildWorld;

    RootTransform.Position = {10.f, 20.f, 30.f};
    EXPECT_EQ(State.SetLocalTransform(Root, RootTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, CommittedChildWorld);

    ExpectedChildWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(RootTransform));
    EXPECT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, ExpectedChildWorld);
    CommittedChildWorld = ExpectedChildWorld;

    ChildTransform.Position = {8.f, 9.f, 10.f};
    ChildTransform.Scale    = {4.f, 5.f, 6.f};
    EXPECT_EQ(State.SetLocalTransform(Child, ChildTransform), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetWorldMatrix(Child, Matrix), RADIENT_STATUS_OK);
    ExpectMatrixNear(Matrix, CommittedChildWorld);

    ExpectedChildWorld = RadientMath::MultiplyMatrices(
        RadientMath::TransformToMatrix(ChildTransform),
        RadientMath::TransformToMatrix(RootTransform));
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

} // namespace
