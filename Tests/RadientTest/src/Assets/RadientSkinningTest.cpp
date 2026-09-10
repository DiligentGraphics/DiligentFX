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
#include "Math/RadientMath.hpp"
#include "Scene/Components/RadientSkinComponentStorage.hpp"
#include "Scene/RadientSceneState.hpp"

#include "RadientSkinning.h"
#include "RadientMathTestHelpers.hpp"
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

RadientMatrix4x4 TranslationMatrix(Float32 X)
{
    return RadientMath::TransformToMatrix(Translation(X));
}

std::array<RadientMatrix4x4, 2> MakeSentinelMatrices()
{
    return {{
        RadientMatrix4x4{
            1.f, 2.f, 3.f, 4.f,
            5.f, 6.f, 7.f, 8.f,
            9.f, 10.f, 11.f, 12.f,
            13.f, 14.f, 15.f, 16.f},
        RadientMatrix4x4{
            17.f, 18.f, 19.f, 20.f,
            21.f, 22.f, 23.f, 24.f,
            25.f, 26.f, 27.f, 28.f,
            29.f, 30.f, 31.f, 32.f},
    }};
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

void ExpectInvalidSkeleton(RadientAssetManagerImpl&   AssetManager,
                           const RadientSkeletonDesc& Desc,
                           const char*                ExpectedError)
{
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    TestingEnvironment::ErrorScope       ExpectedErrors{ExpectedError};
    EXPECT_EQ(AssetManager.CreateSkeleton(Desc, pSkeleton.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pSkeleton);
}

void ExpectInvalidSkin(RadientAssetManagerImpl& AssetManager,
                       const RadientSkinDesc&   Desc,
                       const char*              ExpectedError)
{
    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    TestingEnvironment::ErrorScope   ExpectedErrors{ExpectedError};
    EXPECT_EQ(AssetManager.CreateSkin(Desc, pSkin.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pSkin);
}

struct PoseSkinningTestObjects
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager;
    RefCntAutoPtr<IRadientSkeletonAsset>   pSkeleton;
    RefCntAutoPtr<IRadientSkinAsset>       pSkin;
    RefCntAutoPtr<IRadientSkeletonPose>    pPose;
};

PoseSkinningTestObjects CreatePoseSkinningTestObjects()
{
    PoseSkinningTestObjects Objects;
    Objects.pAssetManager = CreateAssetManager();
    EXPECT_NE(Objects.pAssetManager, nullptr);
    if (!Objects.pAssetManager)
        return Objects;

    std::array<RadientSkeletonJointDesc, 2> Joints{};
    Joints[0].LocalRestTransform = Translation(1.f);
    Joints[1].ParentJointIndex   = 0;
    Joints[1].LocalRestTransform = Translation(2.f);
    Objects.pSkeleton            = CreateSkeleton(
        *Objects.pAssetManager, Joints.data(), static_cast<Uint32>(Joints.size()));
    EXPECT_NE(Objects.pSkeleton, nullptr);
    if (!Objects.pSkeleton)
        return Objects;

    std::array<RadientSkinJointBindingDesc, 2> SkinJoints{};
    SkinJoints[0].SkeletonJointIndex         = 1;
    SkinJoints[0].InverseBindMatrix.Data[12] = -2.f;
    SkinJoints[1].SkeletonJointIndex         = 0;
    SkinJoints[1].InverseBindMatrix.Data[12] = -4.f;

    RadientSkinDesc SkinDesc;
    SkinDesc.pSkeleton  = Objects.pSkeleton;
    SkinDesc.pJoints    = SkinJoints.data();
    SkinDesc.JointCount = static_cast<Uint32>(SkinJoints.size());
    EXPECT_EQ(Objects.pAssetManager->CreateSkin(SkinDesc, Objects.pSkin.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    if (!Objects.pSkin)
        return Objects;

    EXPECT_EQ(Objects.pSkeleton->CreatePose(Objects.pPose.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    return Objects;
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

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    Bool HasSkin = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_SKIN, HasSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(HasSkin, False);

    RadientSkinComponent StoredSkin{pSkin, pPose};
    EXPECT_EQ(State.GetSkin(InvalidRadientEntityID, StoredSkin), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredSkin, RadientSkinComponent{});
    StoredSkin = {pSkin, pPose};
    EXPECT_EQ(State.GetSkin(Entity, StoredSkin), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredSkin, RadientSkinComponent{});

    const RadientSceneRevisions RevisionsBeforeSkin = State.GetSceneRevisions();
    ASSERT_EQ(State.SetSkin(Entity, {pSkin, pPose}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetSceneRevisions().Drawables, RevisionsBeforeSkin.Drawables + 1);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_SKIN, HasSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(HasSkin, True);
    EXPECT_EQ(State.GetSkin(Entity, StoredSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(StoredSkin, (RadientSkinComponent{pSkin, pPose}));

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

    RadientSkinComponent UpdatedSkin{pSkin, pPose};
    UpdatedSkin.SkeletonToMeshTransform = TranslationMatrix(5.f);
    ASSERT_EQ(State.SetSkin(Entity, UpdatedSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetSkin(Entity, StoredSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(StoredSkin, UpdatedSkin);

    Uint32 TransformUpdatedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++TransformUpdatedCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            ASSERT_NE(pRenderable->pSkin, nullptr);
            EXPECT_EQ(pRenderable->pSkin->SkeletonToMeshTransform, UpdatedSkin.SkeletonToMeshTransform);
        });
    EXPECT_EQ(TransformUpdatedCount, 1u);
    State.ClearRenderableMeshChanges();

    UpdatedSkin.pPose = pUpdatedPose;
    ASSERT_EQ(State.SetSkin(Entity, UpdatedSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetSkin(Entity, StoredSkin), RADIENT_STATUS_OK);
    EXPECT_EQ(StoredSkin, UpdatedSkin);
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
    EXPECT_EQ(State.GetSkin(Entity, StoredSkin), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredSkin, RadientSkinComponent{});

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

TEST(RadientSkinningTest, SceneSetSkinRejectsUnknownEntity)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             Joint;
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &Joint, 1);
    RefCntAutoPtr<IRadientSkinAsset>     pSkin     = CreateSkin(*pAssetManager, pSkeleton);
    RefCntAutoPtr<IRadientSkeletonPose>  pPose;
    ASSERT_EQ(pSkeleton->CreatePose(pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientSceneState State;
    EXPECT_EQ(State.SetSkin(InvalidRadientEntityID, {pSkin, pPose}), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientSkinningTest, SceneSetSkinRejectsNullResources)
{
    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    EXPECT_EQ(State.SetSkin(Entity, {}), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientSkinningTest, SceneSetSkinRejectsPoseFromDifferentSkeleton)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             Joint;
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &Joint, 1);
    RefCntAutoPtr<IRadientSkinAsset>     pSkin     = CreateSkin(*pAssetManager, pSkeleton);

    RadientSkeletonJointDesc             OtherJoint;
    RefCntAutoPtr<IRadientSkeletonAsset> pOtherSkeleton =
        CreateSkeleton(*pAssetManager, &OtherJoint, 1, "Other skeleton");
    RefCntAutoPtr<IRadientSkeletonPose> pOtherPose;
    ASSERT_EQ(pOtherSkeleton->CreatePose(pOtherPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetSkin(Entity, {pSkin, pOtherPose}), RADIENT_STATUS_INVALID_ARGUMENT);
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
    ExpectMatrixNear(GlobalMatrices[0], TranslationMatrix(5.f));
    ExpectMatrixNear(GlobalMatrices[1], TranslationMatrix(3.f));
    ExpectMatrixNear(GlobalMatrices[2], TranslationMatrix(9.f));
}

TEST(RadientSkinningTest, CreateSkeletonRejectsNullOutput)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonDesc Desc;
    EXPECT_EQ(pAssetManager->CreateSkeleton(Desc, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientSkinningTest, CreateSkeletonRejectsEmptyJointArray)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonDesc Desc;
    ExpectInvalidSkeleton(*pAssetManager, Desc, "at least one joint");
}

TEST(RadientSkinningTest, CreateSkeletonRejectsNullJointData)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonDesc Desc;
    Desc.JointCount = 1;
    ExpectInvalidSkeleton(*pAssetManager, Desc, "joint data must not be null");
}

TEST(RadientSkinningTest, CreateSkeletonRejectsInvalidParentJoint)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 2> Joints{};
    Joints[0].ParentJointIndex = 2;

    RadientSkeletonDesc Desc;
    Desc.pJoints    = Joints.data();
    Desc.JointCount = static_cast<Uint32>(Joints.size());
    ExpectInvalidSkeleton(*pAssetManager, Desc, "invalid parent joint");
}

TEST(RadientSkinningTest, CreateSkeletonRejectsHierarchyCycle)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    std::array<RadientSkeletonJointDesc, 2> Joints{};
    Joints[0].ParentJointIndex = 1;
    Joints[1].ParentJointIndex = 0;

    RadientSkeletonDesc Desc;
    Desc.pJoints    = Joints.data();
    Desc.JointCount = static_cast<Uint32>(Joints.size());
    ExpectInvalidSkeleton(*pAssetManager, Desc, "contains a cycle");
}

TEST(RadientSkinningTest, CreateSkeletonRejectsNonFiniteRestTransform)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc Joint;
    Joint.LocalRestTransform.Position.x = std::numeric_limits<Float32>::infinity();

    RadientSkeletonDesc Desc;
    Desc.pJoints    = &Joint;
    Desc.JointCount = 1;
    ExpectInvalidSkeleton(*pAssetManager, Desc, "non-finite local rest transform");
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
    const std::array ExpectedSkinJoints      = SkinJoints;

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
    for (Uint32 JointIndex = 0; JointIndex < StoredDesc.JointCount; ++JointIndex)
    {
        EXPECT_EQ(StoredDesc.pJoints[JointIndex].SkeletonJointIndex,
                  ExpectedSkinJoints[JointIndex].SkeletonJointIndex);
        EXPECT_EQ(StoredDesc.pJoints[JointIndex].InverseBindMatrix,
                  ExpectedSkinJoints[JointIndex].InverseBindMatrix);
    }
}

TEST(RadientSkinningTest, CreateSkinRejectsNullOutput)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkinDesc Desc;
    EXPECT_EQ(pAssetManager->CreateSkin(Desc, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientSkinningTest, CreateSkinRejectsNullSkeleton)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkinDesc Desc;
    ExpectInvalidSkin(*pAssetManager, Desc, "must reference a skeleton");
}

TEST(RadientSkinningTest, CreateSkinRejectsEmptyJointArray)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             SkeletonJoint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &SkeletonJoint, 1);
    ASSERT_NE(pSkeleton, nullptr);

    RadientSkinDesc Desc;
    Desc.pSkeleton = pSkeleton;
    ExpectInvalidSkin(*pAssetManager, Desc, "at least one joint");
}

TEST(RadientSkinningTest, CreateSkinRejectsNullJointData)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             SkeletonJoint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &SkeletonJoint, 1);
    ASSERT_NE(pSkeleton, nullptr);

    RadientSkinDesc Desc;
    Desc.pSkeleton  = pSkeleton;
    Desc.JointCount = 1;
    ExpectInvalidSkin(*pAssetManager, Desc, "joint data must not be null");
}

TEST(RadientSkinningTest, CreateSkinRejectsInvalidSkeletonJoint)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             SkeletonJoint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &SkeletonJoint, 1);
    ASSERT_NE(pSkeleton, nullptr);

    RadientSkinJointBindingDesc SkinJoint{};
    SkinJoint.SkeletonJointIndex = 1;

    RadientSkinDesc Desc;
    Desc.pSkeleton  = pSkeleton;
    Desc.pJoints    = &SkinJoint;
    Desc.JointCount = 1;
    ExpectInvalidSkin(*pAssetManager, Desc, "invalid skeleton joint");
}

TEST(RadientSkinningTest, CreateSkinRejectsNonFiniteInverseBindMatrix)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = CreateAssetManager();
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc             SkeletonJoint{};
    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton = CreateSkeleton(*pAssetManager, &SkeletonJoint, 1);
    ASSERT_NE(pSkeleton, nullptr);

    RadientSkinJointBindingDesc SkinJoint{};
    SkinJoint.SkeletonJointIndex        = 0;
    SkinJoint.InverseBindMatrix.Data[0] = std::numeric_limits<Float32>::quiet_NaN();

    RadientSkinDesc Desc;
    Desc.pSkeleton  = pSkeleton;
    Desc.pJoints    = &SkinJoint;
    Desc.JointCount = 1;
    ExpectInvalidSkin(*pAssetManager, Desc, "non-finite inverse-bind matrix");
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
    ExpectMatrixNear(GlobalMatrices[0], TranslationMatrix(10.f));
    ExpectMatrixNear(GlobalMatrices[1], TranslationMatrix(30.f));

    ASSERT_EQ(pWriter->ResetToRestPose(), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(True), RADIENT_STATUS_OK);
    EXPECT_EQ(pPose->GetVersion(), 3u);
    ASSERT_EQ(pPose->GetJointGlobalMatrices(0, 2, GlobalMatrices.data()), RADIENT_STATUS_OK);
    ExpectMatrixNear(GlobalMatrices[0], TranslationMatrix(1.f));
    ExpectMatrixNear(GlobalMatrices[1], TranslationMatrix(3.f));
}

TEST(RadientSkinningTest, PoseComputesSkinningMatrices)
{
    PoseSkinningTestObjects Objects = CreatePoseSkinningTestObjects();
    ASSERT_NE(Objects.pPose, nullptr);

    std::array<RadientMatrix4x4, 2> SkinningMatrices{};
    ASSERT_EQ(Objects.pPose->ComputeSkinningMatrices(Objects.pSkin, SkinningMatrices.data()),
              RADIENT_STATUS_OK);
    ExpectMatrixNear(SkinningMatrices[0], TranslationMatrix(1.f));
    ExpectMatrixNear(SkinningMatrices[1], TranslationMatrix(-3.f));

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(Objects.pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const std::array UpdatedTransforms{
        Translation(10.f),
        Translation(20.f),
    };
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, 2, UpdatedTransforms.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(False), RADIENT_STATUS_OK);

    ASSERT_EQ(Objects.pPose->ComputeSkinningMatrices(Objects.pSkin, SkinningMatrices.data()),
              RADIENT_STATUS_OK);
    EXPECT_EQ(Objects.pPose->GetVersion(), 2u);
    ExpectMatrixNear(SkinningMatrices[0], TranslationMatrix(28.f));
    ExpectMatrixNear(SkinningMatrices[1], TranslationMatrix(6.f));
    EXPECT_EQ(Objects.pPose->UpdateGlobalTransforms(), RADIENT_STATUS_NO_CHANGE);
}

TEST(RadientSkinningTest, PoseComputesTransposedSkinningMatrices)
{
    PoseSkinningTestObjects Objects = CreatePoseSkinningTestObjects();
    ASSERT_NE(Objects.pPose, nullptr);

    std::array<RadientMatrix4x4, 2> SkinningMatrices{};
    ASSERT_EQ(Objects.pPose->ComputeSkinningMatrices(
                  Objects.pSkin,
                  SkinningMatrices.data(),
                  True),
              RADIENT_STATUS_OK);
    ExpectMatrixNear(SkinningMatrices[0], RadientMath::TransposeMatrix(TranslationMatrix(1.f)));
    ExpectMatrixNear(SkinningMatrices[1], RadientMath::TransposeMatrix(TranslationMatrix(-3.f)));
}

TEST(RadientSkinningTest, ComputeSkinningMatricesRejectsNullSkin)
{
    PoseSkinningTestObjects Objects = CreatePoseSkinningTestObjects();
    ASSERT_NE(Objects.pPose, nullptr);

    std::array<RadientMatrix4x4, 2> SkinningMatrices{};
    EXPECT_EQ(Objects.pPose->ComputeSkinningMatrices(nullptr, SkinningMatrices.data()),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientSkinningTest, ComputeSkinningMatricesRejectsNullOutput)
{
    PoseSkinningTestObjects Objects = CreatePoseSkinningTestObjects();
    ASSERT_NE(Objects.pPose, nullptr);

    EXPECT_EQ(Objects.pPose->ComputeSkinningMatrices(Objects.pSkin, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientSkinningTest, ComputeSkinningMatricesRejectsSkinFromDifferentSkeleton)
{
    PoseSkinningTestObjects Objects = CreatePoseSkinningTestObjects();
    ASSERT_NE(Objects.pPose, nullptr);

    RadientSkeletonJointDesc             OtherJoint;
    RefCntAutoPtr<IRadientSkeletonAsset> pOtherSkeleton =
        CreateSkeleton(*Objects.pAssetManager, &OtherJoint, 1, "Other skeleton");
    RefCntAutoPtr<IRadientSkinAsset> pOtherSkin =
        CreateSkin(*Objects.pAssetManager, pOtherSkeleton, "Other skin");
    ASSERT_NE(pOtherSkin, nullptr);

    const std::array InitialMatrices  = MakeSentinelMatrices();
    std::array       SkinningMatrices = InitialMatrices;
    EXPECT_EQ(Objects.pPose->ComputeSkinningMatrices(pOtherSkin, SkinningMatrices.data()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(SkinningMatrices, InitialMatrices);
}

TEST(RadientSkinningTest, ComputeSkinningMatricesDefersDirtyGlobalTransformsWhenUpdateDisabled)
{
    PoseSkinningTestObjects Objects = CreatePoseSkinningTestObjects();
    ASSERT_NE(Objects.pPose, nullptr);

    RefCntAutoPtr<IRadientSkeletonPoseWriter> pWriter;
    ASSERT_EQ(Objects.pPose->CreateWriter(pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    const RadientTransform UpdatedTransform = Translation(10.f);
    ASSERT_EQ(pWriter->SetJointLocalTransforms(0, 1, &UpdatedTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->Commit(False), RADIENT_STATUS_OK);

    const std::array InitialMatrices  = MakeSentinelMatrices();
    std::array       SkinningMatrices = InitialMatrices;
    EXPECT_EQ(Objects.pPose->ComputeSkinningMatrices(Objects.pSkin, SkinningMatrices.data(), False, False),
              RADIENT_STATUS_PENDING);
    EXPECT_EQ(Objects.pPose->GetVersion(), 1u);
    EXPECT_EQ(SkinningMatrices, InitialMatrices);
}
