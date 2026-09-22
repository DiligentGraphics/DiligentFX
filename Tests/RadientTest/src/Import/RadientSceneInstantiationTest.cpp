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

#include "ObjectBase.hpp"
#include "Import/RadientSceneInstantiation.hpp"
#include "RadientEngine.h"
#include "RadientImportedDocument.hpp"
#include "RadientMathTestHelpers.hpp"
#include "RadientTestAssetHelpers.hpp"

#include <array>
#include <initializer_list>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

class FailingAnimationRegistry final : public ObjectBase<IRadientAnimationRegistry>
{
public:
    using TBase = ObjectBase<IRadientAnimationRegistry>;

    FailingAnimationRegistry(IReferenceCounters* pRefCounters, IRadientScene* pScene) :
        TBase{pRefCounters},
        m_pScene{pScene}
    {}

    virtual IRadientScene* DILIGENT_CALL_TYPE GetScene() const override final
    {
        return m_pScene;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE AddAnimationBinding(
        IRadientAnimationBinding* pBinding,
        const RadientEntityID*,
        Uint32) override final
    {
        ++AddCallCount;
        m_pAddedBinding = pBinding;
        return RADIENT_STATUS_FAILED;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimationBinding(
        IRadientAnimationBinding* pBinding,
        const RadientEntityID*,
        Uint32) override final
    {
        ++RemoveCallCount;
        RemovedAddedBinding = pBinding == m_pAddedBinding;
        return RADIENT_STATUS_NO_CHANGE;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveEntity(RadientEntityID) override final
    {
        return RADIENT_STATUS_NO_CHANGE;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimationClip(
        IRadientAnimationClipAsset*) override final
    {
        return RADIENT_STATUS_NO_CHANGE;
    }

    virtual const RadientAnimationRegistryState& DILIGENT_CALL_TYPE GetState() const override final
    {
        return m_State;
    }

    Uint32 AddCallCount        = 0;
    Uint32 RemoveCallCount     = 0;
    bool   RemovedAddedBinding = false;

private:
    RefCntAutoPtr<IRadientScene>  m_pScene;
    IRadientAnimationBinding*     m_pAddedBinding = nullptr;
    RadientAnimationRegistryState m_State;
};

} // namespace

TEST(RadientSceneInstantiationTest, InstantiateSceneGraphPreservesNodeVisibility)
{
    RadientImport::ImportedDocument ImportedScene;
    ImportedScene.Nodes.resize(3);
    ImportedScene.Nodes[0].Name                   = "Hidden parent";
    ImportedScene.Nodes[0].Visible                = False;
    ImportedScene.Nodes[0].Children               = {1};
    ImportedScene.Nodes[1].Name                   = "Visible child";
    ImportedScene.Nodes[2].Name                   = "Visible root";
    ImportedScene.Scenes.emplace_back().RootNodes = {0, 2};

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine({}, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientImport::InstantiateSceneGraph(ImportedScene, 0, *pWriter, RootEntity),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    std::array<RadientEntityID, 2> RootChildren{};
    Uint32                         ChildrenRetrieved = 0;
    ASSERT_EQ(pScene->GetChildren(RootEntity, 0, static_cast<Uint32>(RootChildren.size()),
                                  RootChildren.data(), ChildrenRetrieved),
              RADIENT_STATUS_OK);
    ASSERT_EQ(ChildrenRetrieved, RootChildren.size());

    const RadientEntityID HiddenParent = RootChildren[0];
    const RadientEntityID VisibleRoot  = RootChildren[1];

    RadientEntityID VisibleChild = InvalidRadientEntityID;
    ASSERT_EQ(pScene->GetChildren(HiddenParent, 0, 1, &VisibleChild, ChildrenRetrieved),
              RADIENT_STATUS_OK);
    ASSERT_EQ(ChildrenRetrieved, 1u);

    Bool Visible = True;
    ASSERT_EQ(pScene->GetEntityOwnVisibility(HiddenParent, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
    ASSERT_EQ(pScene->GetEntityEffectiveVisibility(HiddenParent, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    ASSERT_EQ(pScene->GetEntityOwnVisibility(VisibleChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    ASSERT_EQ(pScene->GetEntityEffectiveVisibility(VisibleChild, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);

    ASSERT_EQ(pScene->GetEntityOwnVisibility(VisibleRoot, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    ASSERT_EQ(pScene->GetEntityEffectiveVisibility(VisibleRoot, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
}

TEST(RadientSceneInstantiationTest, InstantiateSceneGraphAnimatesSceneNodeAndSkeletonPose)
{
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine({}, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    ASSERT_EQ(pEngine->GetAssetManager(pAssetManager.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc Joint{};
    Joint.Name = "Root";

    RadientSkeletonDesc SkeletonDesc{};
    SkeletonDesc.Name       = "Generic animation binding skeleton";
    SkeletonDesc.pJoints    = &Joint;
    SkeletonDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    ASSERT_EQ(pAssetManager->CreateSkeleton(SkeletonDesc, pSkeleton.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RadientSkinJointBindingDesc JointBinding{};
    JointBinding.SkeletonJointIndex = 0;

    RadientSkinDesc SkinDesc{};
    SkinDesc.Name       = "Generic animation binding skin";
    SkinDesc.pSkeleton  = pSkeleton;
    SkinDesc.pJoints    = &JointBinding;
    SkinDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    ASSERT_EQ(pAssetManager->CreateSkin(SkinDesc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array<Float32, 2>       Times  = {0.f, 1.f};
    const std::array<RadientFloat3, 2> Values = {RadientFloat3{}, RadientFloat3{1.f, 2.f, 3.f}};

    RadientAnimationTargetDesc Target{};
    Target.Schema = RadientNodeAnimationSchemaID;
    Target.Object = 0;

    RadientAnimationSamplerDesc Sampler{};
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
    Sampler.Value.ArraySize = 1;
    Sampler.pTimes          = Times.data();
    Sampler.pValues         = Values.data();
    Sampler.ValueDataSize   = sizeof(Values);
    Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

    RadientAnimationChannelDesc Channel{};
    Channel.TargetIndex  = 0;
    Channel.Property     = RadientNodeTranslationProperty;
    Channel.SamplerIndex = 0;

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = "Generic animation";
    ClipDesc.Duration     = 1.f;
    ClipDesc.pTargets     = &Target;
    ClipDesc.TargetCount  = 1;
    ClipDesc.pSamplers    = &Sampler;
    ClipDesc.SamplerCount = 1;
    ClipDesc.pChannels    = &Channel;
    ClipDesc.ChannelCount = 1;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://generic-animation-binding", 1);
    ASSERT_NE(pMesh, nullptr);

    RadientImport::ImportedDocument ImportedScene;
    ImportedScene.Skins.emplace_back(pSkin);

    RadientImport::ImportedNode& Node = ImportedScene.Nodes.emplace_back();
    Node.pMesh                        = pMesh;
    Node.SkinIndex                    = 0;

    ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);

    RadientImport::ImportedAnimation&            ImportedAnimation = ImportedScene.Animations.emplace_back();
    RadientImport::ImportedAnimationSkinMapping& SkinMapping       = ImportedAnimation.SkinMappings.emplace_back();
    ImportedAnimation.pClip                                        = pClip;
    SkinMapping.SkinIndex                                          = 0;
    SkinMapping.JointMappings.push_back({0, 0});

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAnimationRegistry> pRegistry;
    ASSERT_EQ(pEngine->CreateAnimationRegistry(pScene, pRegistry.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientImport::InstantiateSceneGraph(
                  ImportedScene, 0, *pWriter, RootEntity, pRegistry),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    Uint32 ChildCount = 0;
    ASSERT_EQ(pScene->GetChildCount(RootEntity, ChildCount), RADIENT_STATUS_OK);
    ASSERT_EQ(ChildCount, 1u);
    RadientEntityID AnimatedEntity    = InvalidRadientEntityID;
    Uint32          ChildrenRetrieved = 0;
    ASSERT_EQ(pScene->GetChildren(RootEntity, 0, 1, &AnimatedEntity, ChildrenRetrieved), RADIENT_STATUS_OK);
    ASSERT_EQ(ChildrenRetrieved, 1u);
    ASSERT_NE(AnimatedEntity, InvalidRadientEntityID);

    const RadientAnimationRegistryState& RegistryState = pRegistry->GetState();
    ASSERT_EQ(RegistryState.EntryCount, 1u);
    EXPECT_EQ(RegistryState.pEntries[0].pClip, pClip);
    ASSERT_EQ(RegistryState.pEntries[0].BindingCount, 2u);

    RadientAnimationEvaluateInfo EvaluateInfo{};
    EvaluateInfo.Time = 1.f;
    for (Uint32 BindingIndex = 0; BindingIndex < RegistryState.pEntries[0].BindingCount; ++BindingIndex)
    {
        IRadientAnimationBinding* const pBinding = RegistryState.pEntries[0].ppBindings[BindingIndex];
        ASSERT_NE(pBinding, nullptr);
        EXPECT_EQ(pBinding->GetClip(), pClip);
        ASSERT_EQ(pBinding->Evaluate(EvaluateInfo), RADIENT_STATUS_OK);
    }

    RadientTransform SceneTransform{};
    ASSERT_EQ(pScene->GetLocalTransform(AnimatedEntity, SceneTransform), RADIENT_STATUS_OK);
    ExpectFloat3Near(SceneTransform.Position, {1.f, 2.f, 3.f});

    RadientSkinComponent Skin{};
    ASSERT_EQ(pScene->GetSkin(AnimatedEntity, Skin), RADIENT_STATUS_OK);
    ASSERT_NE(Skin.pPose, nullptr);
    RadientTransform PoseTransform{};
    ASSERT_EQ(Skin.pPose->GetJointLocalTransforms(0, 1, &PoseTransform), RADIENT_STATUS_OK);
    ExpectFloat3Near(PoseTransform.Position, {1.f, 2.f, 3.f});
}

TEST(RadientSceneInstantiationTest, InstantiateSceneGraphDoesNotRegisterUnsupportedOnlyNodeAnimation)
{
    constexpr RadientAnimationPropertyID UnsupportedNodeProperty = 1000;

    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine({}, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    ASSERT_EQ(pEngine->GetAssetManager(pAssetManager.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetManager, nullptr);

    const std::array<Float32, 2> Times  = {0.f, 1.f};
    const std::array<Float32, 2> Values = {0.f, 1.f};

    RadientAnimationTargetDesc Target{};
    Target.Schema = RadientNodeAnimationSchemaID;
    Target.Object = 0;

    RadientAnimationSamplerDesc Sampler{};
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
    Sampler.Value.ArraySize = 1;
    Sampler.pTimes          = Times.data();
    Sampler.pValues         = Values.data();
    Sampler.ValueDataSize   = sizeof(Values);
    Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

    RadientAnimationChannelDesc Channel{};
    Channel.TargetIndex  = 0;
    Channel.Property     = UnsupportedNodeProperty;
    Channel.SamplerIndex = 0;

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = "Unsupported node animation";
    ClipDesc.Duration     = 1.f;
    ClipDesc.pTargets     = &Target;
    ClipDesc.TargetCount  = 1;
    ClipDesc.pSamplers    = &Sampler;
    ClipDesc.SamplerCount = 1;
    ClipDesc.pChannels    = &Channel;
    ClipDesc.ChannelCount = 1;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pClip, nullptr);

    RadientImport::ImportedDocument ImportedScene;
    ImportedScene.Nodes.emplace_back();
    ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);
    ImportedScene.Animations.emplace_back().pClip = pClip;

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAnimationRegistry> pRegistry;
    ASSERT_EQ(pEngine->CreateAnimationRegistry(pScene, pRegistry.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientImport::InstantiateSceneGraph(
                  ImportedScene, 0, *pWriter, RootEntity, pRegistry),
              RADIENT_STATUS_OK);

    EXPECT_EQ(pRegistry->GetState().EntryCount, 0u);
}

TEST(RadientSceneInstantiationTest, InstantiateSceneGraphSkipsAnimationBindingFailures)
{
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine({}, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    ASSERT_EQ(pEngine->GetAssetManager(pAssetManager.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc Joint{};
    Joint.Name = "Root";

    RadientSkeletonDesc SkeletonDesc{};
    SkeletonDesc.Name       = "Static fallback skeleton";
    SkeletonDesc.pJoints    = &Joint;
    SkeletonDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    ASSERT_EQ(pAssetManager->CreateSkeleton(SkeletonDesc, pSkeleton.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RadientSkinJointBindingDesc JointBinding{};
    JointBinding.SkeletonJointIndex = 0;

    RadientSkinDesc SkinDesc{};
    SkinDesc.Name       = "Static fallback skin";
    SkinDesc.pSkeleton  = pSkeleton;
    SkinDesc.pJoints    = &JointBinding;
    SkinDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    ASSERT_EQ(pAssetManager->CreateSkin(SkinDesc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array<Float32, 2>       Times  = {0.f, 1.f};
    const std::array<RadientFloat3, 2> Values = {RadientFloat3{}, RadientFloat3{1.f, 2.f, 3.f}};

    RadientAnimationTargetDesc Target{};
    Target.Schema = RadientNodeAnimationSchemaID;
    Target.Object = 0;

    RadientAnimationSamplerDesc Sampler{};
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
    Sampler.Value.ArraySize = 1;
    Sampler.pTimes          = Times.data();
    Sampler.pValues         = Values.data();
    Sampler.ValueDataSize   = sizeof(Values);
    Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

    RadientAnimationChannelDesc Channel{};
    Channel.TargetIndex  = 0;
    Channel.Property     = RadientNodeTranslationProperty;
    Channel.SamplerIndex = 0;

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = "Static fallback animation";
    ClipDesc.Duration     = 1.f;
    ClipDesc.pTargets     = &Target;
    ClipDesc.TargetCount  = 1;
    ClipDesc.pSamplers    = &Sampler;
    ClipDesc.SamplerCount = 1;
    ClipDesc.pChannels    = &Channel;
    ClipDesc.ChannelCount = 1;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://static-animation-fallback", 1);
    ASSERT_NE(pMesh, nullptr);

    const auto Instantiate = [&](std::initializer_list<RadientAnimationDestinationElement> DestinationElements,
                                 Uint32                                                    ExpectedBindingCount,
                                 bool                                                      FailRegistration) {
        RadientImport::ImportedDocument ImportedScene;
        ImportedScene.Skins.emplace_back(pSkin);

        RadientImport::ImportedNode& Node = ImportedScene.Nodes.emplace_back();
        Node.pMesh                        = pMesh;
        Node.SkinIndex                    = 0;
        ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);

        RadientImport::ImportedAnimation& ImportedAnimation = ImportedScene.Animations.emplace_back();
        ImportedAnimation.pClip                             = pClip;
        for (const RadientAnimationDestinationElement DestinationElement : DestinationElements)
        {
            RadientImport::ImportedAnimationSkinMapping& Mapping = ImportedAnimation.SkinMappings.emplace_back();
            Mapping.SkinIndex                                    = 0;
            Mapping.JointMappings.push_back({0, DestinationElement});
        }

        RefCntAutoPtr<IRadientScene> pScene;
        ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

        RefCntAutoPtr<IRadientSceneWriter> pWriter;
        ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

        RefCntAutoPtr<IRadientAnimationRegistry> pRegistry;
        RefCntAutoPtr<FailingAnimationRegistry>  pFailingRegistry;
        IRadientAnimationRegistry*               pRegistryInterface = nullptr;
        if (FailRegistration)
        {
            pFailingRegistry = RefCntAutoPtr<FailingAnimationRegistry>{
                MakeNewRCObj<FailingAnimationRegistry>()(pScene)};
            pRegistryInterface = pFailingRegistry;
        }
        else
        {
            ASSERT_EQ(pEngine->CreateAnimationRegistry(pScene, pRegistry.GetAddressOfEmpty()), RADIENT_STATUS_OK);
            pRegistryInterface = pRegistry;
        }
        ASSERT_NE(pRegistryInterface, nullptr);

        RadientEntityID RootEntity = InvalidRadientEntityID;
        ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientImport::InstantiateSceneGraph(
                      ImportedScene, 0, *pWriter, RootEntity, pRegistryInterface),
                  RADIENT_STATUS_OK);

        const RadientAnimationRegistryState& RegistryState = pRegistryInterface->GetState();
        if (ExpectedBindingCount == 0)
        {
            EXPECT_EQ(RegistryState.EntryCount, 0u);
        }
        else
        {
            ASSERT_EQ(RegistryState.EntryCount, 1u);
            ASSERT_EQ(RegistryState.pEntries[0].pClip, pClip);
            EXPECT_EQ(RegistryState.pEntries[0].BindingCount, ExpectedBindingCount);
        }

        if (FailRegistration)
        {
            EXPECT_EQ(pFailingRegistry->AddCallCount, 2u);
            EXPECT_EQ(pFailingRegistry->RemoveCallCount, 2u);
            EXPECT_TRUE(pFailingRegistry->RemovedAddedBinding);
        }

        EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

        Uint32 ChildCount = 0;
        EXPECT_EQ(pScene->GetChildCount(RootEntity, ChildCount), RADIENT_STATUS_OK);
        EXPECT_EQ(ChildCount, 1u);
    };

    // An invalid skeleton mapping does not discard the valid scene-node binding.
    Instantiate({1}, 1, false);

    // A valid skeleton mapping and the scene-node mapping are both registered.
    Instantiate({0, 1}, 2, false);

    // Registry failures are non-fatal and clean up each failed binding.
    Instantiate({0}, 0, true);
}

TEST(RadientSceneInstantiationTest, InstantiateSceneGraphRendersMeshWithoutAvailableSkin)
{
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine({}, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://missing-skin", 1);
    ASSERT_NE(pMesh, nullptr);

    RadientImport::ImportedDocument ImportedScene;
    RadientImport::ImportedNode&    Node = ImportedScene.Nodes.emplace_back();
    Node.Name                            = "Missing skin node";
    Node.pMesh                           = pMesh;
    Node.SkinIndex                       = 0;
    ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientImport::InstantiateSceneGraph(ImportedScene, 0, *pWriter, RootEntity),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    Uint32 ChildCount = 0;
    ASSERT_EQ(pScene->GetChildCount(RootEntity, ChildCount), RADIENT_STATUS_OK);
    ASSERT_EQ(ChildCount, 1u);

    RadientEntityID MeshEntity         = InvalidRadientEntityID;
    Uint32          NumChildrenWritten = 0;
    ASSERT_EQ(pScene->GetChildren(RootEntity, 0, 1, &MeshEntity, NumChildrenWritten), RADIENT_STATUS_OK);
    ASSERT_EQ(NumChildrenWritten, 1u);

    Bool HasComponent = False;
    EXPECT_EQ(pScene->HasComponent(MeshEntity, RADIENT_COMPONENT_TYPE_MESH, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);
    EXPECT_EQ(pScene->HasComponent(MeshEntity, RADIENT_COMPONENT_TYPE_MESH_RENDERER, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);
    EXPECT_EQ(pScene->HasComponent(MeshEntity, RADIENT_COMPONENT_TYPE_SKIN, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
}
