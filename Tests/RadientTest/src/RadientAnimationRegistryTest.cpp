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

#include "RadientEngine.h"
#include "RefCntAutoPtr.hpp"

#include <array>

using namespace Diligent;

namespace
{

const RadientAnimationRegistryEntry* FindEntry(const RadientAnimationRegistryState& State,
                                               IRadientSkeletonAnimationAsset*      pAnimation)
{
    for (Uint32 EntryIndex = 0; EntryIndex < State.EntryCount; ++EntryIndex)
    {
        if (State.pEntries[EntryIndex].pAnimation == pAnimation)
            return &State.pEntries[EntryIndex];
    }
    return nullptr;
}

const RadientAnimationTarget* FindTarget(const RadientAnimationRegistryEntry& Entry,
                                         RadientEntityID                      Entity)
{
    for (Uint32 TargetIndex = 0; TargetIndex < Entry.TargetCount; ++TargetIndex)
    {
        if (Entry.pTargets[TargetIndex].Entity == Entity)
            return &Entry.pTargets[TargetIndex];
    }
    return nullptr;
}

class RadientAnimationRegistryTest : public testing::Test
{
protected:
    struct Rig
    {
        RefCntAutoPtr<IRadientSkeletonAsset>          pSkeleton;
        RefCntAutoPtr<IRadientSkinAsset>              pSkin;
        RefCntAutoPtr<IRadientSkeletonPose>           pPose;
        RefCntAutoPtr<IRadientSkeletonAnimationAsset> pAnimation;
    };

    void SetUp() override
    {
        RadientEngineCreateInfo EngineCI{};
        EngineCI.WorkerThreadCount = 1;
        ASSERT_EQ(CreateRadientEngine(EngineCI, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(pEngine, nullptr);

        ASSERT_EQ(pEngine->GetAssetManager(pAssetManager.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(pAssetManager, nullptr);
        ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(pScene, nullptr);
        ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(pWriter, nullptr);
        ASSERT_EQ(pEngine->CreateAnimationRegistry(pScene, pRegistry.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(pRegistry, nullptr);
    }

    RefCntAutoPtr<IRadientSkeletonAnimationAsset> CreateAnimation(IRadientSkeletonAsset* pSkeleton,
                                                                  const Char*            Name)
    {
        RadientSkeletonAnimationDesc AnimationDesc{};
        AnimationDesc.Name      = Name;
        AnimationDesc.pSkeleton = pSkeleton;
        AnimationDesc.Duration  = 1.f;

        RefCntAutoPtr<IRadientSkeletonAnimationAsset> pAnimation;
        EXPECT_EQ(pAssetManager->CreateSkeletonAnimation(AnimationDesc, pAnimation.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        return pAnimation;
    }

    Rig CreateRig(const Char* Name)
    {
        Rig Result;

        RadientSkeletonJointDesc Joint{};
        Joint.Name = "Root";

        RadientSkeletonDesc SkeletonDesc{};
        SkeletonDesc.Name       = Name;
        SkeletonDesc.pJoints    = &Joint;
        SkeletonDesc.JointCount = 1;
        EXPECT_EQ(pAssetManager->CreateSkeleton(SkeletonDesc, Result.pSkeleton.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        if (Result.pSkeleton == nullptr)
            return Result;

        RadientSkinJointBindingDesc JointBinding{};
        JointBinding.SkeletonJointIndex = 0;

        RadientSkinDesc SkinDesc{};
        SkinDesc.Name       = Name;
        SkinDesc.pSkeleton  = Result.pSkeleton;
        SkinDesc.pJoints    = &JointBinding;
        SkinDesc.JointCount = 1;
        EXPECT_EQ(pAssetManager->CreateSkin(SkinDesc, Result.pSkin.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        EXPECT_EQ(Result.pSkeleton->CreatePose(Result.pPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        Result.pAnimation = CreateAnimation(Result.pSkeleton, Name);
        return Result;
    }

    RadientEntityID CreateSkinnedEntity(const Rig&            RigData,
                                        IRadientSkeletonPose* pPose = nullptr)
    {
        RadientEntityID Entity = InvalidRadientEntityID;
        EXPECT_EQ(pWriter->CreateEntity({}, Entity), RADIENT_STATUS_OK);
        if (Entity == InvalidRadientEntityID)
            return Entity;

        RadientSkinComponent Skin{};
        Skin.pSkin = RigData.pSkin;
        Skin.pPose = pPose != nullptr ? pPose : RigData.pPose.RawPtr();
        EXPECT_EQ(pWriter->SetSkin(Entity, Skin), RADIENT_STATUS_OK);
        return Entity;
    }

protected:
    RefCntAutoPtr<IRadientEngine>            pEngine;
    RefCntAutoPtr<IRadientAssetManager>      pAssetManager;
    RefCntAutoPtr<IRadientScene>             pScene;
    RefCntAutoPtr<IRadientSceneWriter>       pWriter;
    RefCntAutoPtr<IRadientAnimationRegistry> pRegistry;
};

TEST_F(RadientAnimationRegistryTest, CreatesEmptyRegistryAndRetainsScene)
{
    RefCntAutoPtr<IRadientAnimationRegistry> pInvalidRegistry;
    EXPECT_EQ(pEngine->CreateAnimationRegistry(nullptr, pInvalidRegistry.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pEngine->CreateAnimationRegistry(pScene, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    const RadientAnimationRegistryState& State = pRegistry->GetState();
    EXPECT_EQ(State.Revision, 0u);
    EXPECT_EQ(State.pEntries, nullptr);
    EXPECT_EQ(State.EntryCount, 0u);

    IRadientScene* const pRetainedScene = pScene;
    EXPECT_EQ(pRegistry->GetScene(), pRetainedScene);
    pWriter.Release();
    pScene.Release();

    EXPECT_EQ(pRegistry->GetScene(), pRetainedScene);
    RefCntAutoPtr<IRadientSceneWriter> pRetainedSceneWriter;
    EXPECT_EQ(pEngine->CreateSceneWriter(pRegistry->GetScene(), pRetainedSceneWriter.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    EXPECT_NE(pRetainedSceneWriter, nullptr);
}

TEST_F(RadientAnimationRegistryTest, AddsUniqueTargetsAndRetainsAnimationData)
{
    Rig RigData = CreateRig("Retained animation");
    ASSERT_NE(RigData.pAnimation, nullptr);
    ASSERT_NE(RigData.pPose, nullptr);

    const RadientEntityID FirstEntity  = CreateSkinnedEntity(RigData);
    const RadientEntityID SecondEntity = CreateSkinnedEntity(RigData);
    ASSERT_NE(FirstEntity, InvalidRadientEntityID);
    ASSERT_NE(SecondEntity, InvalidRadientEntityID);

    const std::array<RadientEntityID, 3> Entities = {FirstEntity, SecondEntity, FirstEntity};
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, Entities.data(), static_cast<Uint32>(Entities.size())),
              RADIENT_STATUS_OK);

    const RadientAnimationRegistryState& State = pRegistry->GetState();
    ASSERT_EQ(State.Revision, 1u);
    ASSERT_EQ(State.EntryCount, 1u);
    const RadientAnimationRegistryEntry* pEntry = FindEntry(State, RigData.pAnimation);
    ASSERT_NE(pEntry, nullptr);
    ASSERT_EQ(pEntry->TargetCount, 2u);
    ASSERT_NE(FindTarget(*pEntry, FirstEntity), nullptr);
    ASSERT_NE(FindTarget(*pEntry, SecondEntity), nullptr);
    EXPECT_EQ(FindTarget(*pEntry, FirstEntity)->pPose, RigData.pPose);
    EXPECT_EQ(FindTarget(*pEntry, SecondEntity)->pPose, RigData.pPose);

    EXPECT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, Entities.data(), static_cast<Uint32>(Entities.size())),
              RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, nullptr, 0), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.Revision, 1u);

    IRadientSkeletonAnimationAsset* const pRetainedAnimation = RigData.pAnimation;
    IRadientSkeletonPose* const           pRetainedPose      = RigData.pPose;
    ASSERT_EQ(pWriter->RemoveComponent(FirstEntity, RADIENT_COMPONENT_TYPE_SKIN), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->RemoveComponent(SecondEntity, RADIENT_COMPONENT_TYPE_SKIN), RADIENT_STATUS_OK);
    RigData.pAnimation.Release();
    RigData.pPose.Release();

    ASSERT_EQ(State.pEntries[0].pAnimation, pRetainedAnimation);
    EXPECT_EQ(State.pEntries[0].pAnimation->GetDesc().Duration, 1.f);
    ASSERT_NE(FindTarget(State.pEntries[0], FirstEntity), nullptr);
    EXPECT_EQ(FindTarget(State.pEntries[0], FirstEntity)->pPose, pRetainedPose);
    EXPECT_EQ(pRetainedPose->GetSkeleton(), RigData.pSkeleton);
}

TEST_F(RadientAnimationRegistryTest, RejectsInvalidBatchAtomically)
{
    Rig RigData = CreateRig("Atomic validation");
    ASSERT_NE(RigData.pAnimation, nullptr);

    const RadientEntityID ExistingEntity = CreateSkinnedEntity(RigData);
    const RadientEntityID NewEntity      = CreateSkinnedEntity(RigData);
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, &ExistingEntity, 1), RADIENT_STATUS_OK);

    RadientEntityID EmptyEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity({}, EmptyEntity), RADIENT_STATUS_OK);

    EXPECT_EQ(pRegistry->AddAnimatedEntities(nullptr, &NewEntity, 1), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, nullptr, 1), RADIENT_STATUS_INVALID_ARGUMENT);

    const std::array<RadientEntityID, 2> Entities = {NewEntity, EmptyEntity};
    EXPECT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, Entities.data(), static_cast<Uint32>(Entities.size())),
              RADIENT_STATUS_NOT_FOUND);

    const RadientAnimationRegistryState& State = pRegistry->GetState();
    EXPECT_EQ(State.Revision, 1u);
    ASSERT_EQ(State.EntryCount, 1u);
    const RadientAnimationRegistryEntry* pEntry = FindEntry(State, RigData.pAnimation);
    ASSERT_NE(pEntry, nullptr);
    ASSERT_EQ(pEntry->TargetCount, 1u);
    EXPECT_EQ(pEntry->pTargets[0].Entity, ExistingEntity);
}

TEST_F(RadientAnimationRegistryTest, RejectsSkeletonMismatchAtomically)
{
    Rig FirstRig  = CreateRig("First skeleton");
    Rig SecondRig = CreateRig("Second skeleton");
    ASSERT_NE(FirstRig.pAnimation, nullptr);
    ASSERT_NE(SecondRig.pAnimation, nullptr);

    const std::array<RadientEntityID, 2> Entities = {
        CreateSkinnedEntity(FirstRig),
        CreateSkinnedEntity(SecondRig),
    };
    ASSERT_NE(Entities[0], InvalidRadientEntityID);
    ASSERT_NE(Entities[1], InvalidRadientEntityID);

    EXPECT_EQ(pRegistry->AddAnimatedEntities(FirstRig.pAnimation, Entities.data(), static_cast<Uint32>(Entities.size())),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pRegistry->GetState().Revision, 0u);
    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
}

TEST_F(RadientAnimationRegistryTest, RemovesSelectedTargets)
{
    Rig RigData = CreateRig("Selected removal");
    ASSERT_NE(RigData.pAnimation, nullptr);

    const RadientEntityID                FirstEntity  = CreateSkinnedEntity(RigData);
    const RadientEntityID                SecondEntity = CreateSkinnedEntity(RigData);
    const std::array<RadientEntityID, 2> Entities     = {FirstEntity, SecondEntity};
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, Entities.data(), static_cast<Uint32>(Entities.size())),
              RADIENT_STATUS_OK);

    const RadientEntityID MissingEntity = InvalidRadientEntityID;
    EXPECT_EQ(pRegistry->RemoveAnimatedEntities(RigData.pAnimation, &MissingEntity, 1),
              RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pRegistry->RemoveAnimatedEntities(nullptr, &FirstEntity, 1), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pRegistry->RemoveAnimatedEntities(RigData.pAnimation, nullptr, 1), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pRegistry->RemoveAnimatedEntities(RigData.pAnimation, nullptr, 0), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pRegistry->GetState().Revision, 1u);

    ASSERT_EQ(pRegistry->RemoveAnimatedEntities(RigData.pAnimation, &FirstEntity, 1), RADIENT_STATUS_OK);
    const RadientAnimationRegistryEntry* pEntry = FindEntry(pRegistry->GetState(), RigData.pAnimation);
    ASSERT_NE(pEntry, nullptr);
    EXPECT_EQ(pRegistry->GetState().Revision, 2u);
    EXPECT_EQ(pEntry->TargetCount, 1u);
    EXPECT_EQ(pEntry->pTargets[0].Entity, SecondEntity);

    ASSERT_EQ(pRegistry->RemoveAnimatedEntities(RigData.pAnimation, &SecondEntity, 1), RADIENT_STATUS_OK);
    EXPECT_EQ(pRegistry->GetState().Revision, 3u);
    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
    EXPECT_EQ(pRegistry->GetState().pEntries, nullptr);
}

TEST_F(RadientAnimationRegistryTest, RemovesEntityFromEveryAnimation)
{
    Rig RigData = CreateRig("Shared skeleton");
    ASSERT_NE(RigData.pAnimation, nullptr);
    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pSecondAnimation =
        CreateAnimation(RigData.pSkeleton, "Second animation");
    ASSERT_NE(pSecondAnimation, nullptr);

    const RadientEntityID                SharedEntity = CreateSkinnedEntity(RigData);
    const RadientEntityID                OtherEntity  = CreateSkinnedEntity(RigData);
    const std::array<RadientEntityID, 2> FirstTargets = {SharedEntity, OtherEntity};
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, FirstTargets.data(), static_cast<Uint32>(FirstTargets.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimatedEntities(pSecondAnimation, &SharedEntity, 1), RADIENT_STATUS_OK);

    ASSERT_EQ(pRegistry->RemoveEntity(SharedEntity), RADIENT_STATUS_OK);
    const RadientAnimationRegistryState& State = pRegistry->GetState();
    EXPECT_EQ(State.Revision, 3u);
    ASSERT_EQ(State.EntryCount, 1u);
    const RadientAnimationRegistryEntry* pRemainingEntry = FindEntry(State, RigData.pAnimation);
    ASSERT_NE(pRemainingEntry, nullptr);
    ASSERT_EQ(pRemainingEntry->TargetCount, 1u);
    EXPECT_EQ(pRemainingEntry->pTargets[0].Entity, OtherEntity);
    EXPECT_EQ(FindEntry(State, pSecondAnimation), nullptr);
    EXPECT_EQ(pRegistry->RemoveEntity(SharedEntity), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.Revision, 3u);

    RadientSkinComponent Skin{};
    EXPECT_EQ(pScene->GetSkin(SharedEntity, Skin), RADIENT_STATUS_OK);
    EXPECT_EQ(Skin.pPose, RigData.pPose);
}

TEST_F(RadientAnimationRegistryTest, RemovesAnimationAndAllTargets)
{
    Rig RigData = CreateRig("Animation removal");
    ASSERT_NE(RigData.pAnimation, nullptr);
    const RadientEntityID Entity = CreateSkinnedEntity(RigData);
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, &Entity, 1), RADIENT_STATUS_OK);

    EXPECT_EQ(pRegistry->RemoveAnimation(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    ASSERT_EQ(pRegistry->RemoveAnimation(RigData.pAnimation), RADIENT_STATUS_OK);
    EXPECT_EQ(pRegistry->GetState().Revision, 2u);
    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
    EXPECT_EQ(pRegistry->RemoveAnimation(RigData.pAnimation), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pRegistry->GetState().Revision, 2u);
}

TEST_F(RadientAnimationRegistryTest, MaintainsIndicesAcrossSwapErase)
{
    Rig RigData = CreateRig("Indexed removal");
    ASSERT_NE(RigData.pAnimation, nullptr);
    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pSecondAnimation =
        CreateAnimation(RigData.pSkeleton, "Second indexed animation");
    ASSERT_NE(pSecondAnimation, nullptr);

    const RadientEntityID                SharedEntity     = CreateSkinnedEntity(RigData);
    const RadientEntityID                FirstOnlyEntity  = CreateSkinnedEntity(RigData);
    const RadientEntityID                SecondOnlyEntity = CreateSkinnedEntity(RigData);
    const std::array<RadientEntityID, 2> FirstTargets     = {SharedEntity, FirstOnlyEntity};
    const std::array<RadientEntityID, 2> SecondTargets    = {SharedEntity, SecondOnlyEntity};
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, FirstTargets.data(), static_cast<Uint32>(FirstTargets.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimatedEntities(pSecondAnimation, SecondTargets.data(), static_cast<Uint32>(SecondTargets.size())),
              RADIENT_STATUS_OK);

    // Removing the first target moves both another target in the animation
    // entry and another animation in the entity's reverse index.
    ASSERT_EQ(pRegistry->RemoveAnimatedEntities(RigData.pAnimation, &SharedEntity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->RemoveAnimatedEntities(pSecondAnimation, &SharedEntity, 1), RADIENT_STATUS_OK);

    const RadientAnimationRegistryEntry* pFirstEntry = FindEntry(pRegistry->GetState(), RigData.pAnimation);
    ASSERT_NE(pFirstEntry, nullptr);
    ASSERT_EQ(pFirstEntry->TargetCount, 1u);
    EXPECT_EQ(pFirstEntry->pTargets[0].Entity, FirstOnlyEntity);
    const RadientAnimationRegistryEntry* pSecondEntry = FindEntry(pRegistry->GetState(), pSecondAnimation);
    ASSERT_NE(pSecondEntry, nullptr);
    ASSERT_EQ(pSecondEntry->TargetCount, 1u);
    EXPECT_EQ(pSecondEntry->pTargets[0].Entity, SecondOnlyEntity);

    // Removing the first entry moves the second entry and must repair the
    // animation-to-entry index used by the following operation.
    ASSERT_EQ(pRegistry->RemoveAnimation(RigData.pAnimation), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->RemoveAnimatedEntities(pSecondAnimation, &SecondOnlyEntity, 1), RADIENT_STATUS_OK);
    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
}

TEST_F(RadientAnimationRegistryTest, ResolvesPoseWhenAssociationIsAdded)
{
    Rig RigData = CreateRig("Pose replacement");
    ASSERT_NE(RigData.pAnimation, nullptr);
    ASSERT_NE(RigData.pPose, nullptr);
    const RadientEntityID Entity = CreateSkinnedEntity(RigData);
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, &Entity, 1), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSkeletonPose> pReplacementPose;
    ASSERT_EQ(RigData.pSkeleton->CreatePose(pReplacementPose.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->SetSkin(Entity, {RigData.pSkin, pReplacementPose}), RADIENT_STATUS_OK);

    const RadientAnimationRegistryEntry* pEntry = FindEntry(pRegistry->GetState(), RigData.pAnimation);
    ASSERT_NE(pEntry, nullptr);
    ASSERT_EQ(pEntry->TargetCount, 1u);
    EXPECT_EQ(pEntry->pTargets[0].pPose, RigData.pPose);

    ASSERT_EQ(pRegistry->RemoveAnimatedEntities(RigData.pAnimation, &Entity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimatedEntities(RigData.pAnimation, &Entity, 1), RADIENT_STATUS_OK);
    pEntry = FindEntry(pRegistry->GetState(), RigData.pAnimation);
    ASSERT_NE(pEntry, nullptr);
    ASSERT_EQ(pEntry->TargetCount, 1u);
    EXPECT_EQ(pEntry->pTargets[0].pPose, pReplacementPose);
    EXPECT_EQ(pRegistry->GetState().Revision, 3u);
}

} // namespace
