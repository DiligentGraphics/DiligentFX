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
 */

#include "gtest/gtest.h"

#include "RadientEngine.h"
#include "RefCntAutoPtr.hpp"

#include <array>

using namespace Diligent;

namespace
{

const RadientAnimationRegistryEntry* FindEntry(const RadientAnimationRegistryState& State,
                                               IRadientAnimationClipAsset*          pClip)
{
    for (Uint32 EntryIndex = 0; EntryIndex < State.EntryCount; ++EntryIndex)
    {
        if (State.pEntries[EntryIndex].pClip == pClip)
            return &State.pEntries[EntryIndex];
    }
    return nullptr;
}

IRadientAnimationBinding* FindBinding(const RadientAnimationRegistryEntry& Entry,
                                      IRadientAnimationBinding*            pBinding)
{
    for (Uint32 BindingIndex = 0; BindingIndex < Entry.BindingCount; ++BindingIndex)
    {
        if (Entry.ppBindings[BindingIndex] == pBinding)
            return Entry.ppBindings[BindingIndex];
    }
    return nullptr;
}

class RadientAnimationRegistryTest : public testing::Test
{
protected:
    struct Animation
    {
        RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
        RefCntAutoPtr<IRadientAnimationBinding>   pBinding;
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

    RefCntAutoPtr<IRadientAnimationBinding> CreateBinding(IRadientAnimationClipAsset* pClip)
    {
        RefCntAutoPtr<IRadientAnimationBinding> pBinding;
        EXPECT_EQ(pClip->CreateBinding({}, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        return pBinding;
    }

    Animation CreateAnimation(const Char* Name)
    {
        Animation                Result;
        RadientAnimationClipDesc ClipDesc{};
        ClipDesc.Name = Name;
        EXPECT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, Result.pClip.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        if (Result.pClip != nullptr)
            Result.pBinding = CreateBinding(Result.pClip);
        return Result;
    }

    RadientEntityID CreateEntity()
    {
        RadientEntityID Entity = InvalidRadientEntityID;
        EXPECT_EQ(pWriter->CreateEntity({}, Entity), RADIENT_STATUS_OK);
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

TEST_F(RadientAnimationRegistryTest, GroupsUniqueBindingsByClipAndRetainsThem)
{
    Animation AnimationData = CreateAnimation("Retained clip");
    ASSERT_NE(AnimationData.pClip, nullptr);
    ASSERT_NE(AnimationData.pBinding, nullptr);
    RefCntAutoPtr<IRadientAnimationBinding> pSecondBinding = CreateBinding(AnimationData.pClip);
    ASSERT_NE(pSecondBinding, nullptr);

    const RadientEntityID                FirstEntity   = CreateEntity();
    const RadientEntityID                SecondEntity  = CreateEntity();
    const RadientEntityID                ThirdEntity   = CreateEntity();
    const std::array<RadientEntityID, 3> FirstEntities = {FirstEntity, SecondEntity, FirstEntity};

    ASSERT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, FirstEntities.data(),
                                             static_cast<Uint32>(FirstEntities.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimationBinding(pSecondBinding, &ThirdEntity, 1), RADIENT_STATUS_OK);

    const RadientAnimationRegistryState& State = pRegistry->GetState();
    ASSERT_EQ(State.Revision, 2u);
    ASSERT_EQ(State.EntryCount, 1u);
    const RadientAnimationRegistryEntry* pEntry = FindEntry(State, AnimationData.pClip);
    ASSERT_NE(pEntry, nullptr);
    ASSERT_EQ(pEntry->BindingCount, 2u);
    ASSERT_NE(FindBinding(*pEntry, AnimationData.pBinding), nullptr);
    ASSERT_NE(FindBinding(*pEntry, pSecondBinding), nullptr);

    EXPECT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, FirstEntities.data(),
                                             static_cast<Uint32>(FirstEntities.size())),
              RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, nullptr, 0),
              RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.Revision, 2u);

    IRadientAnimationClipAsset* const pRetainedClip    = AnimationData.pClip;
    IRadientAnimationBinding* const   pRetainedBinding = AnimationData.pBinding;
    IRadientAnimationBinding* const   pRetainedSecond  = pSecondBinding;
    AnimationData.pBinding.Release();
    pSecondBinding.Release();
    AnimationData.pClip.Release();

    pEntry = FindEntry(State, pRetainedClip);
    ASSERT_NE(pEntry, nullptr);
    EXPECT_STREQ(pEntry->pClip->GetDesc().Name, "Retained clip");
    EXPECT_EQ(FindBinding(*pEntry, pRetainedBinding)->GetClip(), pRetainedClip);
    EXPECT_EQ(FindBinding(*pEntry, pRetainedSecond)->GetClip(), pRetainedClip);
}

TEST_F(RadientAnimationRegistryTest, RejectsInvalidBatchAtomically)
{
    Animation AnimationData = CreateAnimation("Atomic validation");
    ASSERT_NE(AnimationData.pBinding, nullptr);

    const RadientEntityID ExistingEntity = CreateEntity();
    const RadientEntityID NewEntity      = CreateEntity();
    ASSERT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, &ExistingEntity, 1),
              RADIENT_STATUS_OK);

    EXPECT_EQ(pRegistry->AddAnimationBinding(nullptr, &NewEntity, 1), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, nullptr, 1), RADIENT_STATUS_INVALID_ARGUMENT);

    const std::array<RadientEntityID, 2> Entities = {NewEntity, InvalidRadientEntityID};
    EXPECT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, Entities.data(),
                                             static_cast<Uint32>(Entities.size())),
              RADIENT_STATUS_NOT_FOUND);

    const RadientAnimationRegistryState& State = pRegistry->GetState();
    EXPECT_EQ(State.Revision, 1u);
    ASSERT_EQ(State.EntryCount, 1u);
    const RadientAnimationRegistryEntry* pEntry = FindEntry(State, AnimationData.pClip);
    ASSERT_NE(pEntry, nullptr);
    ASSERT_EQ(pEntry->BindingCount, 1u);
    EXPECT_EQ(pEntry->ppBindings[0], AnimationData.pBinding);
}

TEST_F(RadientAnimationRegistryTest, AssociatesOneEntityWithMultipleBindingsForTheSameClip)
{
    Animation                               AnimationData  = CreateAnimation("Multiple bindings");
    RefCntAutoPtr<IRadientAnimationBinding> pSecondBinding = CreateBinding(AnimationData.pClip);
    ASSERT_NE(pSecondBinding, nullptr);
    const RadientEntityID Entity = CreateEntity();

    ASSERT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, &Entity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimationBinding(pSecondBinding, &Entity, 1), RADIENT_STATUS_OK);

    const RadientAnimationRegistryEntry* pEntry = FindEntry(pRegistry->GetState(), AnimationData.pClip);
    ASSERT_NE(pEntry, nullptr);
    ASSERT_EQ(pEntry->BindingCount, 2u);
    EXPECT_NE(FindBinding(*pEntry, AnimationData.pBinding), nullptr);
    EXPECT_NE(FindBinding(*pEntry, pSecondBinding), nullptr);
}

TEST_F(RadientAnimationRegistryTest, RemovesSelectedBindingAssociations)
{
    Animation                            AnimationData = CreateAnimation("Selected removal");
    const RadientEntityID                FirstEntity   = CreateEntity();
    const RadientEntityID                SecondEntity  = CreateEntity();
    const std::array<RadientEntityID, 2> Entities      = {FirstEntity, SecondEntity};
    ASSERT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, Entities.data(),
                                             static_cast<Uint32>(Entities.size())),
              RADIENT_STATUS_OK);

    const RadientEntityID MissingEntity = InvalidRadientEntityID;
    EXPECT_EQ(pRegistry->RemoveAnimationBinding(AnimationData.pBinding, &MissingEntity, 1),
              RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pRegistry->RemoveAnimationBinding(nullptr, &FirstEntity, 1), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pRegistry->RemoveAnimationBinding(AnimationData.pBinding, nullptr, 1), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pRegistry->RemoveAnimationBinding(AnimationData.pBinding, nullptr, 0), RADIENT_STATUS_NO_CHANGE);

    ASSERT_EQ(pRegistry->RemoveAnimationBinding(AnimationData.pBinding, &FirstEntity, 1), RADIENT_STATUS_OK);
    const RadientAnimationRegistryEntry* pEntry = FindEntry(pRegistry->GetState(), AnimationData.pClip);
    ASSERT_NE(pEntry, nullptr);
    EXPECT_EQ(pEntry->BindingCount, 1u);
    EXPECT_EQ(pEntry->ppBindings[0], AnimationData.pBinding);

    ASSERT_EQ(pRegistry->RemoveAnimationBinding(AnimationData.pBinding, &SecondEntity, 1), RADIENT_STATUS_OK);
    EXPECT_EQ(pRegistry->GetState().Revision, 3u);
    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
    EXPECT_EQ(pRegistry->GetState().pEntries, nullptr);
}

TEST_F(RadientAnimationRegistryTest, RemovesEntityFromEveryBinding)
{
    Animation                            FirstAnimation  = CreateAnimation("First clip");
    Animation                            SecondAnimation = CreateAnimation("Second clip");
    const RadientEntityID                SharedEntity    = CreateEntity();
    const RadientEntityID                OtherEntity     = CreateEntity();
    const std::array<RadientEntityID, 2> FirstEntities   = {SharedEntity, OtherEntity};
    ASSERT_EQ(pRegistry->AddAnimationBinding(FirstAnimation.pBinding, FirstEntities.data(),
                                             static_cast<Uint32>(FirstEntities.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimationBinding(SecondAnimation.pBinding, &SharedEntity, 1),
              RADIENT_STATUS_OK);

    ASSERT_EQ(pRegistry->RemoveEntity(SharedEntity), RADIENT_STATUS_OK);
    const RadientAnimationRegistryState& State = pRegistry->GetState();
    EXPECT_EQ(State.Revision, 3u);
    ASSERT_EQ(State.EntryCount, 1u);
    const RadientAnimationRegistryEntry* pRemainingEntry = FindEntry(State, FirstAnimation.pClip);
    ASSERT_NE(pRemainingEntry, nullptr);
    ASSERT_EQ(pRemainingEntry->BindingCount, 1u);
    EXPECT_EQ(pRemainingEntry->ppBindings[0], FirstAnimation.pBinding);
    EXPECT_EQ(FindEntry(State, SecondAnimation.pClip), nullptr);
    EXPECT_EQ(pRegistry->RemoveEntity(SharedEntity), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pScene->IsEntityAlive(SharedEntity), RADIENT_STATUS_OK);
}

TEST_F(RadientAnimationRegistryTest, RemovesClipAndAllBindings)
{
    Animation                               AnimationData  = CreateAnimation("Clip removal");
    RefCntAutoPtr<IRadientAnimationBinding> pSecondBinding = CreateBinding(AnimationData.pClip);
    const RadientEntityID                   FirstEntity    = CreateEntity();
    const RadientEntityID                   SecondEntity   = CreateEntity();
    ASSERT_EQ(pRegistry->AddAnimationBinding(AnimationData.pBinding, &FirstEntity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimationBinding(pSecondBinding, &SecondEntity, 1), RADIENT_STATUS_OK);

    EXPECT_EQ(pRegistry->RemoveAnimationClip(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    ASSERT_EQ(pRegistry->RemoveAnimationClip(AnimationData.pClip), RADIENT_STATUS_OK);
    EXPECT_EQ(pRegistry->GetState().Revision, 3u);
    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
    EXPECT_EQ(pRegistry->RemoveAnimationClip(AnimationData.pClip), RADIENT_STATUS_NO_CHANGE);
}

TEST_F(RadientAnimationRegistryTest, MaintainsIndicesAcrossSwapErase)
{
    Animation                               FirstAnimation       = CreateAnimation("Indexed first clip");
    Animation                               SecondAnimation      = CreateAnimation("Indexed second clip");
    RefCntAutoPtr<IRadientAnimationBinding> pFirstSecondBinding  = CreateBinding(FirstAnimation.pClip);
    RefCntAutoPtr<IRadientAnimationBinding> pSecondSecondBinding = CreateBinding(SecondAnimation.pClip);

    const RadientEntityID SharedEntity     = CreateEntity();
    const RadientEntityID FirstOnlyEntity  = CreateEntity();
    const RadientEntityID SecondOnlyEntity = CreateEntity();
    ASSERT_EQ(pRegistry->AddAnimationBinding(FirstAnimation.pBinding, &SharedEntity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimationBinding(pFirstSecondBinding, &FirstOnlyEntity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimationBinding(SecondAnimation.pBinding, &SharedEntity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->AddAnimationBinding(pSecondSecondBinding, &SecondOnlyEntity, 1), RADIENT_STATUS_OK);

    // Removing these associations swap-erases both a binding and an
    // entry in the entity's reverse index.
    ASSERT_EQ(pRegistry->RemoveAnimationBinding(FirstAnimation.pBinding, &SharedEntity, 1), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->RemoveAnimationBinding(SecondAnimation.pBinding, &SharedEntity, 1), RADIENT_STATUS_OK);

    const RadientAnimationRegistryEntry* pFirstEntry = FindEntry(pRegistry->GetState(), FirstAnimation.pClip);
    ASSERT_NE(pFirstEntry, nullptr);
    ASSERT_EQ(pFirstEntry->BindingCount, 1u);
    EXPECT_EQ(pFirstEntry->ppBindings[0], pFirstSecondBinding);
    const RadientAnimationRegistryEntry* pSecondEntry = FindEntry(pRegistry->GetState(), SecondAnimation.pClip);
    ASSERT_NE(pSecondEntry, nullptr);
    ASSERT_EQ(pSecondEntry->BindingCount, 1u);
    EXPECT_EQ(pSecondEntry->ppBindings[0], pSecondSecondBinding);

    // Removing the first entry moves the second and repairs the clip index.
    ASSERT_EQ(pRegistry->RemoveAnimationClip(FirstAnimation.pClip), RADIENT_STATUS_OK);
    ASSERT_EQ(pRegistry->RemoveAnimationBinding(pSecondSecondBinding, &SecondOnlyEntity, 1), RADIENT_STATUS_OK);
    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
}

} // namespace
