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

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Scene/Components/RadientSkinComponentStorage.hpp"
#include "Scene/RadientSceneState.hpp"

#include "RadientSkinning.h"
#include "RadientTestAssetHelpers.hpp"

#include "RefCntAutoPtr.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <limits>
#include <utility>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

RadientTransform Translation(Float32 X, Float32 Y = 0.f, Float32 Z = 0.f)
{
    RadientTransform Transform;
    Transform.Position = {X, Y, Z};
    return Transform;
}

Float32 GetTranslationX(const RadientMatrix4x4& Matrix)
{
    return Matrix.Data[12];
}

RefCntAutoPtr<RadientAssetManagerImpl> CreateAssetManager()
{
    return RadientAssetManagerImpl::Create({});
}

RefCntAutoPtr<IRadientSkeletonAsset> CreateSkeleton(
    RadientAssetManagerImpl&        AssetManager,
    const RadientSkeletonJointDesc* pJoints,
    Uint32                          JointCount,
    const Char*                     Name = "Test skeleton")
{
    RadientSkeletonDesc Desc;
    Desc.Name       = Name;
    Desc.pJoints    = pJoints;
    Desc.JointCount = JointCount;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    EXPECT_EQ(AssetManager.CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    return pSkeleton;
}

RefCntAutoPtr<IRadientSkinAsset> CreateSkin(RadientAssetManagerImpl& AssetManager,
                                            IRadientSkeletonAsset*   pSkeleton,
                                            const Char*              Name = "Test skin")
{
    RadientSkinJointBindingDesc Joint;
    Joint.SkeletonJointIndex = 0;

    RadientSkinDesc Desc;
    Desc.Name       = Name;
    Desc.pSkeleton  = pSkeleton;
    Desc.pJoints    = &Joint;
    Desc.JointCount = 1;

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    EXPECT_EQ(AssetManager.CreateSkin(Desc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    return pSkin;
}

void ExpectInvalidAnimation(RadientAssetManagerImpl&            AssetManager,
                            const RadientSkeletonAnimationDesc& Desc,
                            const char*                         ExpectedError)
{
    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pAnimation;
    TestingEnvironment::ErrorScope                ExpectedErrors{ExpectedError};
    EXPECT_EQ(AssetManager.CreateSkeletonAnimation(Desc, pAnimation.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pAnimation);
}

void ExpectQuaternionNear(const RadientQuaternion& Value,
                          const RadientQuaternion& Expected)
{
    const Float32 Dot = Value.x * Expected.x + Value.y * Expected.y +
        Value.z * Expected.z + Value.w * Expected.w;
    const Float32 Sign = Dot < 0.f ? -1.f : 1.f;
    EXPECT_NEAR(Value.x * Sign, Expected.x, 1e-5f);
    EXPECT_NEAR(Value.y * Sign, Expected.y, 1e-5f);
    EXPECT_NEAR(Value.z * Sign, Expected.z, 1e-5f);
    EXPECT_NEAR(Value.w * Sign, Expected.w, 1e-5f);
}

} // namespace

TEST(RadientSkinningTest, SkinComponentStorageRetainsResourcesAndRepairsPointersAfterMove)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             Joint;
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &Joint, 1);
    RefCntAutoPtr<IRadientSkinAsset>     pSkin     = CreateSkin(*pAssetManager, pSkeleton);
    RefCntAutoPtr<IRadientSkeletonPose>  pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pSkin, nullptr);
    ASSERT_NE(pPose, nullptr);

    IRadientSkinAsset* const    pExpectedSkin = pSkin;
    IRadientSkeletonPose* const pExpectedPose = pPose;

    SkinComponentStorage Storage;
    Storage.Assign({pSkin, pPose});
    pSkin.Release();
    pPose.Release();
    pSkeleton.Release();

    EXPECT_EQ(Storage.Component.pSkin, pExpectedSkin);
    EXPECT_EQ(Storage.Component.pPose, pExpectedPose);

    SkinComponentStorage MoveConstructed{std::move(Storage)};
    EXPECT_EQ(Storage.Component.pSkin, nullptr);
    EXPECT_EQ(Storage.Component.pPose, nullptr);
    EXPECT_EQ(MoveConstructed.Component.pSkin, pExpectedSkin);
    EXPECT_EQ(MoveConstructed.Component.pPose, pExpectedPose);

    SkinComponentStorage MoveAssigned;
    MoveAssigned = std::move(MoveConstructed);
    EXPECT_EQ(MoveConstructed.Component.pSkin, nullptr);
    EXPECT_EQ(MoveConstructed.Component.pPose, nullptr);
    EXPECT_EQ(MoveAssigned.Component.pSkin, pExpectedSkin);
    EXPECT_EQ(MoveAssigned.Component.pPose, pExpectedPose);
}

TEST(RadientSkinningTest, SceneStoresSkinAndReportsIncrementalDrawableChanges)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             Joint;
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &Joint, 1);
    RefCntAutoPtr<IRadientSkinAsset>     pSkin     = CreateSkin(*pAssetManager, pSkeleton);
    RefCntAutoPtr<IRadientSkeletonPose>  pPose;
    RefCntAutoPtr<IRadientSkeletonPose>  pUpdatedPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pSkeleton->CreatePose(pUpdatedPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientSkeletonJointDesc             OtherJoint;
    RefCntAutoPtr<IRadientSkeletonAsset> pOtherSkeleton = CreateSkeleton(*pAssetManager, &OtherJoint, 1, "Other skeleton");
    RefCntAutoPtr<IRadientSkeletonPose>  pOtherPose;
    ASSERT_EQ(pOtherSkeleton->CreatePose(pOtherPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    EXPECT_EQ(State.SetSkin(InvalidRadientEntityID, {pSkin, pPose}), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(State.SetSkin(Entity, {}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(State.SetSkin(Entity, {pSkin, pOtherPose}), RADIENT_STATUS_INVALID_ARGUMENT);

    Bool HasSkin = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_SKIN, HasSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(HasSkin, False);

    const RadientSceneRevisions RevisionsBeforeSkin = State.GetSceneRevisions();
    ASSERT_EQ(State.SetSkin(Entity, {pSkin, pPose}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetSceneRevisions().Drawables, RevisionsBeforeSkin.Drawables + 1);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_SKIN, HasSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(HasSkin, True);

    Uint32 PendingChangeCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&PendingChangeCount](const RadientSceneState::RenderableMeshChange&,
                              const RadientSceneState::RenderableMesh*) {
            ++PendingChangeCount;
        });
    EXPECT_EQ(PendingChangeCount, 0u);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://skinned");
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.CommitChanges(), RADIENT_STATUS_OK);

    Uint32 AddedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++AddedCount;
            EXPECT_EQ(Change.Entity, Entity);
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Added);
            ASSERT_NE(pRenderable, nullptr);
            ASSERT_NE(pRenderable->pSkin, nullptr);
            EXPECT_EQ(pRenderable->pSkin->pSkin, pSkin.RawPtr());
            EXPECT_EQ(pRenderable->pSkin->pPose, pPose.RawPtr());
        });
    EXPECT_EQ(AddedCount, 1u);
    State.ClearRenderableMeshChanges();

    const RadientSceneRevisions RevisionsBeforeNoChange = State.GetSceneRevisions();
    EXPECT_EQ(State.SetSkin(Entity, {pSkin, pPose}), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), RevisionsBeforeNoChange);

    ASSERT_EQ(State.SetSkin(Entity, {pSkin, pUpdatedPose}), RADIENT_STATUS_OK);
    Uint32 UpdatedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++UpdatedCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            ASSERT_NE(pRenderable->pSkin, nullptr);
            EXPECT_EQ(pRenderable->pSkin->pPose, pUpdatedPose.RawPtr());
        });
    EXPECT_EQ(UpdatedCount, 1u);
    State.ClearRenderableMeshChanges();

    ASSERT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_SKIN), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_SKIN, HasSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(HasSkin, False);

    Uint32 RemovedSkinCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++RemovedSkinCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            EXPECT_EQ(pRenderable->pSkin, nullptr);
        });
    EXPECT_EQ(RemovedSkinCount, 1u);
    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_SKIN), RADIENT_STATUS_NO_CHANGE);
}

TEST(RadientSkinningTest, SkeletonCopiesHierarchyAndSupportsArbitraryJointOrder)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    char SkeletonName[] = "Skeleton";
    char ChildName[]    = "Child";
    char RootName[]     = "Root";

    std::array<RadientSkeletonJointDesc, 3> Joints{};
    Joints[0].Name                          = ChildName;
    Joints[0].ParentJointIndex              = 1;
    Joints[0].LocalRestTransform            = Translation(2.f);
    Joints[1].Name                          = RootName;
    Joints[1].ParentJointIndex              = InvalidRadientJointIndex;
    Joints[1].LocalRestTransform            = Translation(3.f);
    Joints[1].LocalRestTransform.Rotation.w = 2.f;
    Joints[2].Name                          = "Grandchild";
    Joints[2].ParentJointIndex              = 0;
    Joints[2].LocalRestTransform            = Translation(4.f);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, Joints.data(), static_cast<Uint32>(Joints.size()), SkeletonName);
    ASSERT_NE(pSkeleton, nullptr);

    SkeletonName[0]                         = 'X';
    ChildName[0]                            = 'X';
    RootName[0]                             = 'X';
    Joints[0].LocalRestTransform.Position.x = 100.f;

    const RadientSkeletonDesc& Desc = pSkeleton->GetDesc();
    EXPECT_STREQ(Desc.Name, "Skeleton");
    EXPECT_EQ(pSkeleton->GetType(), RADIENT_ASSET_TYPE_SKELETON);
    ASSERT_EQ(Desc.JointCount, 3u);
    ASSERT_NE(Desc.pJoints, nullptr);
    EXPECT_STREQ(Desc.pJoints[0].Name, "Child");
    EXPECT_STREQ(Desc.pJoints[1].Name, "Root");
    EXPECT_EQ(Desc.pJoints[0].ParentJointIndex, 1u);
    EXPECT_FLOAT_EQ(Desc.pJoints[0].LocalRestTransform.Position.x, 2.f);
    EXPECT_FLOAT_EQ(Desc.pJoints[1].LocalRestTransform.Rotation.w, 1.f);

    RefCntAutoPtr<IRadientSkeletonPose> pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pPose, nullptr);

    std::array<RadientMatrix4x4, 3> GlobalMatrices{};
    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 3, GlobalMatrices.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[0]), 5.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[1]), 3.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[2]), 9.f);
}

TEST(RadientSkinningTest, SkeletonRejectsInvalidHierarchyAndTransforms)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    RadientSkeletonDesc                  Desc;

    EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);

    {
        TestingEnvironment::ErrorScope ExpectedErrors{"at least one joint"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    Desc.JointCount = 1;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"joint data must not be null"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    std::array<RadientSkeletonJointDesc, 2> Joints{};
    Desc.pJoints    = Joints.data();
    Desc.JointCount = static_cast<Uint32>(Joints.size());

    Joints[0].ParentJointIndex = 2;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"invalid parent joint"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    Joints[0].ParentJointIndex = 1;
    Joints[1].ParentJointIndex = 0;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"contains a cycle"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    Joints[0].ParentJointIndex              = InvalidRadientJointIndex;
    Joints[1].ParentJointIndex              = InvalidRadientJointIndex;
    Joints[1].LocalRestTransform.Position.x = std::numeric_limits<Float32>::infinity();
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"non-finite local rest transform"};
        EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }
}

TEST(RadientSkinningTest, SkinCopiesMappingsAndRetainsSkeleton)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 2> SkeletonJoints{};
    SkeletonJoints[0].Name             = "Root";
    SkeletonJoints[1].Name             = "Child";
    SkeletonJoints[1].ParentJointIndex = 0;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, SkeletonJoints.data(), static_cast<Uint32>(SkeletonJoints.size()));
    ASSERT_NE(pSkeleton, nullptr);

    std::array<RadientSkinJointBindingDesc, 2> SkinJoints{};
    SkinJoints[0].SkeletonJointIndex         = 1;
    SkinJoints[0].InverseBindMatrix.Data[12] = -2.f;
    SkinJoints[1].SkeletonJointIndex         = 0;
    SkinJoints[1].InverseBindMatrix.Data[12] = -1.f;

    RadientSkinDesc Desc;
    Desc.Name       = "Test skin";
    Desc.pSkeleton  = pSkeleton;
    Desc.pJoints    = SkinJoints.data();
    Desc.JointCount = static_cast<Uint32>(SkinJoints.size());

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    ASSERT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pSkin, nullptr);

    SkinJoints[0].SkeletonJointIndex               = 0;
    SkinJoints[0].InverseBindMatrix.Data[12]       = 100.f;
    IRadientSkeletonAsset* const pExpectedSkeleton = pSkeleton;
    pSkeleton.Release();

    const RadientSkinDesc& StoredDesc = pSkin->GetDesc();
    EXPECT_EQ(pSkin->GetType(), RADIENT_ASSET_TYPE_SKIN);
    EXPECT_STREQ(StoredDesc.Name, "Test skin");
    EXPECT_EQ(StoredDesc.pSkeleton, pExpectedSkeleton);
    ASSERT_EQ(StoredDesc.JointCount, 2u);
    ASSERT_NE(StoredDesc.pJoints, nullptr);
    EXPECT_EQ(StoredDesc.pJoints[0].SkeletonJointIndex, 1u);
    EXPECT_FLOAT_EQ(StoredDesc.pJoints[0].InverseBindMatrix.Data[12], -2.f);
}

TEST(RadientSkinningTest, SkinRejectsInvalidMappings)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkinDesc                  Desc;
    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    EXPECT_EQ(pAssetManager->CreateSkin(Desc, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);

    {
        TestingEnvironment::ErrorScope ExpectedErrors{"must reference a skeleton"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    RadientSkeletonJointDesc             SkeletonJoint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &SkeletonJoint, 1);
    ASSERT_NE(pSkeleton, nullptr);
    Desc.pSkeleton = pSkeleton;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"at least one joint"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    RadientSkinJointBindingDesc SkinJoint{};
    Desc.JointCount = 1;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"joint data must not be null"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    SkinJoint.SkeletonJointIndex = 1;
    Desc.pJoints                 = &SkinJoint;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"invalid skeleton joint"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }

    SkinJoint.SkeletonJointIndex        = 0;
    SkinJoint.InverseBindMatrix.Data[0] = std::numeric_limits<Float32>::quiet_NaN();
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"non-finite inverse-bind matrix"};
        EXPECT_EQ(pAssetManager->CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
    }
}

TEST(RadientSkinningTest, PoseWriterCommitsVersionedPose)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 2> Joints{};
    Joints[0].LocalRestTransform = Translation(1.f);
    Joints[1].ParentJointIndex   = 0;
    Joints[1].LocalRestTransform = Translation(2.f);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, Joints.data(), static_cast<Uint32>(Joints.size()));
    ASSERT_NE(pSkeleton, nullptr);

    RefCntAutoPtr<IRadientSkeletonPose> pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pPose, nullptr);
    EXPECT_EQ(pPose->GetSkeleton(), pSkeleton.RawPtr());
    EXPECT_EQ(pPose->GetVersion(), 1u);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    std::array UpdatedTransforms{
        Translation(10.f),
        Translation(20.f),
    };
    UpdatedTransforms[0].Rotation.w = 2.f;
    EXPECT_EQ(pWriter->SetJointLocalTransforms(0, 1, &UpdatedTransforms[0]), RADIENT_STATUS_OK);

    std::array<RadientTransform, 2> LocalTransforms{};
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 2, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(LocalTransforms[0], Joints[0].LocalRestTransform);
    EXPECT_EQ(LocalTransforms[1], Joints[1].LocalRestTransform);
    EXPECT_EQ(pPose->GetVersion(), 1u);

    ASSERT_EQ(pWriter->Commit(False), RADIENT_STATUS_OK);
    EXPECT_EQ(pWriter->Commit(False), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pPose->GetVersion(), 1u);

    std::array<RadientMatrix4x4, 2> GlobalMatrices{};
    EXPECT_EQ(pPose->GetJointGlobalMatrices(0, 0, nullptr), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetJointGlobalMatrices(0, 2, GlobalMatrices.data()), RADIENT_STATUS_PENDING);

    EXPECT_EQ(pWriter->SetJointLocalTransforms(1, 1, &UpdatedTransforms[1]), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(False), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), 1u);

    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 2, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(LocalTransforms, UpdatedTransforms);
    EXPECT_EQ(pPose->GetJointGlobalMatrices(0, 2, GlobalMatrices.data()), RADIENT_STATUS_PENDING);

    ASSERT_EQ(pPose->UpdateGlobalTransforms(), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->UpdateGlobalTransforms(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pPose->GetVersion(), 2u);

    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 2, GlobalMatrices.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(LocalTransforms[0].Rotation.w, 2.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[0]), 10.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[1]), 30.f);

    ASSERT_EQ(pWriter->ResetToRestPose(), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), 3u);
    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 2, GlobalMatrices.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[0]), 1.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[1]), 3.f);
}

TEST(RadientSkinningTest, AnimationCopiesDescriptionAndRetainsSkeleton)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             Joint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &Joint, 1);
    ASSERT_NE(pSkeleton, nullptr);

    char                              Name[] = "Animation";
    std::array<Float32, 2>            Times  = {0.f, 1.f};
    std::array<RadientFloat3, 2>      Values = {RadientFloat3{1.f, 2.f, 3.f}, RadientFloat3{4.f, 5.f, 6.f}};
    RadientSkeletonAnimationTrackDesc Track;
    Track.SkeletonJointIndex        = 0;
    Track.Translation.Interpolation = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Track.Translation.pTimes        = Times.data();
    Track.Translation.pValues       = Values.data();
    Track.Translation.KeyframeCount = static_cast<Uint32>(Times.size());

    RadientSkeletonAnimationDesc AnimationDesc;
    AnimationDesc.Name       = Name;
    AnimationDesc.pSkeleton  = pSkeleton;
    AnimationDesc.pTracks    = &Track;
    AnimationDesc.TrackCount = 1;
    AnimationDesc.Duration   = 1.f;

    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pAnimation;
    ASSERT_EQ(pAssetManager->CreateSkeletonAnimation(AnimationDesc, pAnimation.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAnimation, nullptr);
    EXPECT_EQ(pAssetManager->WaitForAssetLoad(pAnimation), RADIENT_STATUS_OK);

    IRadientSkeletonAsset* const pStoredSkeleton = pSkeleton;
    Name[0]                                      = 'X';
    Times[1]                                     = 10.f;
    Values[0]                                    = {};
    Track.SkeletonJointIndex                     = InvalidRadientJointIndex;
    pSkeleton.Release();

    const RadientSkeletonAnimationDesc& StoredDesc = pAnimation->GetDesc();
    EXPECT_EQ(pAnimation->GetType(), RADIENT_ASSET_TYPE_SKELETON_ANIMATION);
    EXPECT_STREQ(StoredDesc.Name, "Animation");
    EXPECT_EQ(StoredDesc.pSkeleton, pStoredSkeleton);
    EXPECT_FLOAT_EQ(StoredDesc.Duration, 1.f);
    ASSERT_EQ(StoredDesc.TrackCount, 1u);
    ASSERT_NE(StoredDesc.pTracks, nullptr);
    EXPECT_EQ(StoredDesc.pTracks[0].SkeletonJointIndex, 0u);
    EXPECT_FLOAT_EQ(StoredDesc.pTracks[0].Translation.pTimes[1], 1.f);
    const auto* const pStoredValues =
        static_cast<const RadientFloat3*>(StoredDesc.pTracks[0].Translation.pValues);
    EXPECT_EQ(pStoredValues[0], (RadientFloat3{1.f, 2.f, 3.f}));
}

TEST(RadientSkinningTest, AnimationEvaluatesStepLinearAndCubicCurves)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 4> Joints{};
    Joints[0].LocalRestTransform       = Translation(10.f);
    Joints[1].ParentJointIndex         = 0;
    Joints[1].LocalRestTransform       = Translation(2.f);
    Joints[2].LocalRestTransform.Scale = {1.f, 1.f, 1.f};
    Joints[3].LocalRestTransform       = Translation(7.f);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, Joints.data(), static_cast<Uint32>(Joints.size()));
    ASSERT_NE(pSkeleton, nullptr);

    const std::array<Float32, 2>           Times        = {0.f, 2.f};
    const std::array<RadientFloat3, 2>     Translations = {RadientFloat3{1.f, 0.f, 0.f}, RadientFloat3{5.f, 0.f, 0.f}};
    const std::array<RadientQuaternion, 2> Rotations    = {RadientQuaternion{0.f, 0.f, 0.f, 1.f}, RadientQuaternion{0.f, 0.f, 1.f, 0.f}};
    const std::array<RadientFloat3, 6>     Scales       = {{
        {0.f, 0.f, 0.f},
        {1.f, 1.f, 1.f},
        {2.f, 0.f, 0.f},
        {0.f, 0.f, 0.f},
        {3.f, 1.f, 1.f},
        {0.f, 0.f, 0.f},
    }};

    std::array<RadientSkeletonAnimationTrackDesc, 3> Tracks{};
    Tracks[0].SkeletonJointIndex        = 0;
    Tracks[0].Translation.Interpolation = RADIENT_ANIMATION_INTERPOLATION_STEP;
    Tracks[0].Translation.pTimes        = Times.data();
    Tracks[0].Translation.pValues       = Translations.data();
    Tracks[0].Translation.KeyframeCount = 2;
    Tracks[1].SkeletonJointIndex        = 1;
    Tracks[1].Rotation.Interpolation    = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Tracks[1].Rotation.pTimes           = Times.data();
    Tracks[1].Rotation.pValues          = Rotations.data();
    Tracks[1].Rotation.KeyframeCount    = 2;
    Tracks[2].SkeletonJointIndex        = 2;
    Tracks[2].Scale.Interpolation       = RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE;
    Tracks[2].Scale.pTimes              = Times.data();
    Tracks[2].Scale.pValues             = Scales.data();
    Tracks[2].Scale.KeyframeCount       = 2;

    RadientSkeletonAnimationDesc AnimationDesc;
    AnimationDesc.Name       = "Mixed curves";
    AnimationDesc.pSkeleton  = pSkeleton;
    AnimationDesc.pTracks    = Tracks.data();
    AnimationDesc.TrackCount = static_cast<Uint32>(Tracks.size());
    AnimationDesc.Duration   = 2.f;

    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pAnimation;
    ASSERT_EQ(pAssetManager->CreateSkeletonAnimation(AnimationDesc, pAnimation.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPose> pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    RadientTransform ModifiedRest = Translation(99.f);
    ASSERT_EQ(pWriter->SetJointLocalTransforms(3, 1, &ModifiedRest), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), 2u);

    ASSERT_EQ(pAnimation->Evaluate(1.0, pPose, True), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), 3u);

    std::array<RadientTransform, 4> LocalTransforms{};
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 4, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(LocalTransforms[0].Position, (RadientFloat3{1.f, 0.f, 0.f}));
    EXPECT_EQ(LocalTransforms[1].Position, Joints[1].LocalRestTransform.Position);
    ExpectQuaternionNear(LocalTransforms[1].Rotation, RadientQuaternion{0.f, 0.f, 0.70710678f, 0.70710678f});
    EXPECT_NEAR(LocalTransforms[2].Scale.x, 2.5f, 1e-5f);
    EXPECT_FLOAT_EQ(LocalTransforms[2].Scale.y, 1.f);
    EXPECT_FLOAT_EQ(LocalTransforms[2].Scale.z, 1.f);
    EXPECT_EQ(LocalTransforms[3], Joints[3].LocalRestTransform);

    std::array<RadientMatrix4x4, 4> GlobalMatrices{};
    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 4, GlobalMatrices.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[0]), 1.f);
    EXPECT_FLOAT_EQ(GetTranslationX(GlobalMatrices[1]), 3.f);

    ASSERT_EQ(pAnimation->Evaluate(-10.0, pPose, True), RADIENT_STATUS_OK);
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 3, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(LocalTransforms[0].Position, Translations[0]);
    ExpectQuaternionNear(LocalTransforms[1].Rotation, Rotations[0]);
    EXPECT_EQ(LocalTransforms[2].Scale, (RadientFloat3{1.f, 1.f, 1.f}));

    ASSERT_EQ(pAnimation->Evaluate(10.0, pPose, True), RADIENT_STATUS_OK);
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 3, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_EQ(LocalTransforms[0].Position, Translations[1]);
    ExpectQuaternionNear(LocalTransforms[1].Rotation, Rotations[1]);
    EXPECT_EQ(LocalTransforms[2].Scale, (RadientFloat3{3.f, 1.f, 1.f}));

    EXPECT_EQ(pAnimation->Evaluate(0.0, nullptr, True), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pAnimation->Evaluate(std::numeric_limits<Float64>::quiet_NaN(), pPose, True), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientSkeletonJointDesc             OtherJoint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pOtherSkeleton = CreateSkeleton(*pAssetManager, &OtherJoint, 1, "Other");
    RefCntAutoPtr<IRadientSkeletonPose>  pOtherPose;
    ASSERT_EQ(pOtherSkeleton->CreatePose(pOtherPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"use different skeletons"};
        EXPECT_EQ(pAnimation->Evaluate(0.0, pOtherPose, True), RADIENT_STATUS_INVALID_ARGUMENT);
    }
}

TEST(RadientSkinningTest, AnimationOnlyWritesStartedAndActiveTracks)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 3> Joints{};
    Joints[0].LocalRestTransform = Translation(1.f);
    Joints[1].LocalRestTransform = Translation(2.f);
    Joints[2].LocalRestTransform = Translation(3.f);

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton =
        CreateSkeleton(*pAssetManager, Joints.data(), static_cast<Uint32>(Joints.size()));
    ASSERT_NE(pSkeleton, nullptr);

    const std::array<Float32, 2>       EarlyTimes  = {0.f, 1.f};
    const std::array<Float32, 2>       LateTimes   = {2.f, 3.f};
    const std::array<RadientFloat3, 2> EarlyValues = {
        RadientFloat3{10.f, 0.f, 0.f}, RadientFloat3{20.f, 0.f, 0.f}};
    const std::array<RadientFloat3, 2> LateValues = {
        RadientFloat3{30.f, 0.f, 0.f}, RadientFloat3{40.f, 0.f, 0.f}};

    std::array<RadientSkeletonAnimationTrackDesc, 2> Tracks{};
    Tracks[0].SkeletonJointIndex        = 0;
    Tracks[0].Translation.Interpolation = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Tracks[0].Translation.pTimes        = EarlyTimes.data();
    Tracks[0].Translation.pValues       = EarlyValues.data();
    Tracks[0].Translation.KeyframeCount = static_cast<Uint32>(EarlyTimes.size());
    Tracks[1].SkeletonJointIndex        = 1;
    Tracks[1].Translation.Interpolation = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Tracks[1].Translation.pTimes        = LateTimes.data();
    Tracks[1].Translation.pValues       = LateValues.data();
    Tracks[1].Translation.KeyframeCount = static_cast<Uint32>(LateTimes.size());

    RadientSkeletonAnimationDesc AnimationDesc;
    AnimationDesc.Name       = "Track intervals";
    AnimationDesc.pSkeleton  = pSkeleton;
    AnimationDesc.pTracks    = Tracks.data();
    AnimationDesc.TrackCount = static_cast<Uint32>(Tracks.size());
    AnimationDesc.Duration   = 4.f;

    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pAnimation;
    ASSERT_EQ(pAssetManager->CreateSkeletonAnimation(AnimationDesc, pAnimation.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPose> pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const std::array<RadientTransform, 3> ModifiedTransforms = {
        Translation(100.f), Translation(200.f), Translation(300.f)};
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, 3, ModifiedTransforms.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);

    std::array<RadientTransform, 3> LocalTransforms{};
    const Uint64                    InitialVersion = pPose->GetVersion();
    ASSERT_EQ(pAnimation->Evaluate(0.5, pPose, False), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), InitialVersion);
    RadientMatrix4x4 GlobalMatrix{};
    EXPECT_EQ(pPose->GetJointGlobalMatrices(0, 1, &GlobalMatrix), RADIENT_STATUS_PENDING);
    ASSERT_EQ(pPose->UpdateGlobalTransforms(), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), InitialVersion + 1);
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 3, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(LocalTransforms[0].Position.x, 15.f);
    EXPECT_EQ(LocalTransforms[1], Joints[1].LocalRestTransform);
    EXPECT_EQ(LocalTransforms[2], Joints[2].LocalRestTransform);

    ASSERT_EQ(pAnimation->Evaluate(1.0, pPose, True), RADIENT_STATUS_OK);
    ASSERT_EQ(pAnimation->Evaluate(2.5, pPose, True), RADIENT_STATUS_OK);
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 3, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(LocalTransforms[0].Position.x, 20.f);
    EXPECT_FLOAT_EQ(LocalTransforms[1].Position.x, 35.f);
    EXPECT_EQ(LocalTransforms[2], Joints[2].LocalRestTransform);

    ASSERT_EQ(pAnimation->Evaluate(3.5, pPose, True), RADIENT_STATUS_OK);
    ASSERT_EQ(pPose->GetJointLocalTransforms(0, 3, LocalTransforms.data()), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(LocalTransforms[0].Position.x, 20.f);
    EXPECT_FLOAT_EQ(LocalTransforms[1].Position.x, 35.f);
    EXPECT_EQ(LocalTransforms[2], Joints[2].LocalRestTransform);
}

TEST(RadientSkinningTest, AnimationRejectsInvalidDescriptions)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonAnimationDesc Desc;
    EXPECT_EQ(pAssetManager->CreateSkeletonAnimation(Desc, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    ExpectInvalidAnimation(*pAssetManager, Desc, "must reference a skeleton");

    RadientSkeletonJointDesc             Joint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &Joint, 1);
    ASSERT_NE(pSkeleton, nullptr);
    Desc.pSkeleton = pSkeleton;

    Desc.Duration = -1.f;
    ExpectInvalidAnimation(*pAssetManager, Desc, "duration must be finite and non-negative");
    Desc.Duration = std::numeric_limits<Float32>::infinity();
    ExpectInvalidAnimation(*pAssetManager, Desc, "duration must be finite and non-negative");
    Desc.Duration   = 1.f;
    Desc.TrackCount = 1;
    ExpectInvalidAnimation(*pAssetManager, Desc, "track data must not be null");

    RadientSkeletonAnimationTrackDesc Track;
    Desc.pTracks = &Track;
    ExpectInvalidAnimation(*pAssetManager, Desc, "invalid skeleton joint");
    Track.SkeletonJointIndex = 0;
    ExpectInvalidAnimation(*pAssetManager, Desc, "does not contain any curves");

    std::array<Float32, 2>       Times  = {0.f, 1.f};
    std::array<RadientFloat3, 2> Values = {RadientFloat3{0.f, 0.f, 0.f}, RadientFloat3{1.f, 1.f, 1.f}};
    Track.Translation.pTimes            = Times.data();
    Track.Translation.pValues           = Values.data();
    Track.Translation.KeyframeCount     = 2;

    std::array<RadientSkeletonAnimationTrackDesc, 2> DuplicateTracks = {Track, Track};
    Desc.pTracks                                                     = DuplicateTracks.data();
    Desc.TrackCount                                                  = 2;
    ExpectInvalidAnimation(*pAssetManager, Desc, "duplicate skeleton joint");
    Desc.pTracks    = &Track;
    Desc.TrackCount = 1;

    Track.Translation.Interpolation = RADIENT_ANIMATION_INTERPOLATION_COUNT;
    ExpectInvalidAnimation(*pAssetManager, Desc, "invalid interpolation mode");
    Track.Translation.Interpolation = RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE;
    Track.Translation.KeyframeCount = std::numeric_limits<Uint32>::max() / 3u + 1u;
    ExpectInvalidAnimation(*pAssetManager, Desc, "contains too many keyframes");
    Track.Translation.Interpolation = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Track.Translation.KeyframeCount = 2;

    Track.Translation.KeyframeCount = 0;
    Track.Translation.pValues       = nullptr;
    ExpectInvalidAnimation(*pAssetManager, Desc, "must not provide keyframe data");
    Track.Translation.KeyframeCount = 2;
    Track.Translation.pTimes        = nullptr;
    Track.Translation.pValues       = Values.data();
    ExpectInvalidAnimation(*pAssetManager, Desc, "keyframe times must not be null");
    Track.Translation.pTimes  = Times.data();
    Track.Translation.pValues = nullptr;
    ExpectInvalidAnimation(*pAssetManager, Desc, "curve values must not be null");
    Track.Translation.pValues = Values.data();

    Times[0] = -1.f;
    ExpectInvalidAnimation(*pAssetManager, Desc, "outside the clip duration");
    Times[0] = 0.f;
    Times[1] = std::numeric_limits<Float32>::quiet_NaN();
    ExpectInvalidAnimation(*pAssetManager, Desc, "outside the clip duration");
    Times[1] = 2.f;
    ExpectInvalidAnimation(*pAssetManager, Desc, "outside the clip duration");
    Times[1] = 0.f;
    ExpectInvalidAnimation(*pAssetManager, Desc, "strictly increasing");
    Times[1] = 1.f;

    Values[1].x = std::numeric_limits<Float32>::infinity();
    ExpectInvalidAnimation(*pAssetManager, Desc, "non-finite value");
    Values[1].x = 1.f;

    Track.Translation                          = RadientAnimationCurveDesc{};
    std::array<RadientQuaternion, 2> Rotations = {RadientQuaternion{0.f, 0.f, 0.f, 1.f}, RadientQuaternion{0.f, 0.f, 0.f, 2.f}};
    Track.Rotation.pTimes                      = Times.data();
    Track.Rotation.pValues                     = Rotations.data();
    Track.Rotation.KeyframeCount               = 2;
    ExpectInvalidAnimation(*pAssetManager, Desc, "is not a normalized rotation");

    std::array<RadientQuaternion, 6> CubicRotations = {{
        {0.f, 0.f, 0.f, 0.f},
        {0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.f, 0.f},
        {0.f, 0.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 0.f, 0.f},
    }};
    CubicRotations[2].x                             = std::numeric_limits<Float32>::quiet_NaN();
    Track.Rotation.Interpolation                    = RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE;
    Track.Rotation.pValues                          = CubicRotations.data();
    ExpectInvalidAnimation(*pAssetManager, Desc, "non-finite value");
}
