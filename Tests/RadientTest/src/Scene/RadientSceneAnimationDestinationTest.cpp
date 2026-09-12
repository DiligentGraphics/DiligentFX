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

#include "Math/RadientMath.hpp"
#include "RadientAnimation.h"
#include "RadientEngine.h"
#include "RadientMathTestHelpers.hpp"
#include "Scene/RadientSceneImpl.hpp"
#include "Scene/RadientSceneState.hpp"

#include "Cast.hpp"
#include "RefCntAutoPtr.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr RadientAnimationPropertyID UnknownAnimationProperty = 0xffffffffffffffffull;

class RadientSceneAnimationDestinationTest : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_EQ(CreateRadientEngine({}, m_pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(m_pEngine, nullptr);
        ASSERT_EQ(m_pEngine->CreateScene({}, m_pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(m_pScene, nullptr);
        ASSERT_EQ(m_pEngine->CreateSceneWriter(m_pScene, m_pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(m_pWriter, nullptr);

        m_pWriter->QueryInterface(
            IID_RadientAnimationDestination,
            m_pDestination.GetAddressOfEmpty());
        ASSERT_NE(m_pDestination, nullptr);
    }

    static RadientAnimationPropertyBindingDesc MakeNodeProperty(
        RadientEntityID              Entity,
        RadientAnimationPropertyID   Property,
        RADIENT_ANIMATION_VALUE_TYPE Type,
        Uint32                       FirstArrayElement = 0,
        Uint32                       ArraySize         = 1,
        RadientAnimationSchemaID     Schema            = RadientNodeAnimationSchemaID)
    {
        return MakeProperty(Entity, Schema, Property, Type, FirstArrayElement, ArraySize);
    }

    static RadientAnimationPropertyBindingDesc MakeProperty(
        RadientEntityID              Entity,
        RadientAnimationSchemaID     Schema,
        RadientAnimationPropertyID   Property,
        RADIENT_ANIMATION_VALUE_TYPE Type,
        Uint32                       FirstArrayElement = 0,
        Uint32                       ArraySize         = 1)
    {
        RadientAnimationPropertyBindingDesc Desc{};
        Desc.Schema             = Schema;
        Desc.DestinationElement = Entity;
        Desc.Property           = Property;
        Desc.FirstArrayElement  = FirstArrayElement;
        Desc.Value.Type         = Type;
        Desc.Value.ArraySize    = ArraySize;
        return Desc;
    }

    static RadientLightComponent MakeFullySpecifiedLight()
    {
        RadientLightComponent Light{};
        Light.Type                   = RADIENT_LIGHT_TYPE_SPOT;
        Light.Color                  = {0.1f, 0.2f, 0.3f};
        Light.Intensity              = 2.f;
        Light.Range                  = 10.f;
        Light.Exposure               = -1.f;
        Light.Diffuse                = 0.25f;
        Light.Specular               = 0.75f;
        Light.Normalize              = True;
        Light.EnableColorTemperature = True;
        Light.ColorTemperature       = 4500.f;
        Light.Radius                 = 0.25f;
        Light.Angle                  = 0.75f;
        Light.InnerConeAngle         = 0.1f;
        Light.OuterConeAngle         = 0.5f;
        Light.ShapingFocus           = 0.8f;
        return Light;
    }

    RadientEntityID CreateEntity(const RadientTransform& Transform)
    {
        RadientEntityDesc Desc{};
        Desc.Transform         = Transform;
        RadientEntityID Entity = InvalidRadientEntityID;
        EXPECT_EQ(m_pWriter->CreateEntity(Desc, Entity), RADIENT_STATUS_OK);
        return Entity;
    }

    RefCntAutoPtr<IRadientAnimationDestinationBinding> CreateBinding(
        const std::vector<RadientAnimationPropertyBindingDesc>& Properties,
        std::vector<RadientAnimationResolvedPropertyDesc>*      pResolved = nullptr)
    {
        std::vector<RadientAnimationResolvedPropertyDesc>  Resolved(Properties.size());
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      Properties.data(),
                      static_cast<Uint32>(Properties.size()),
                      Resolved.data(),
                      pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        if (pResolved != nullptr)
            *pResolved = std::move(Resolved);
        return pBinding;
    }

    RadientSceneImpl& GetSceneImpl()
    {
        return *ClassPtrCast<RadientSceneImpl>(m_pScene.RawPtr());
    }

    RadientLightComponent GetLight(RadientEntityID Entity)
    {
        RadientLightComponent Result{};
        bool                  Found = false;
        EXPECT_EQ(GetSceneImpl().GetState().EnumerateRenderableLights(
                      [&](const RadientSceneState::RenderableLight& Renderable) {
                          if (Renderable.Entity == Entity)
                          {
                              Result = Renderable.Light;
                              Found  = true;
                          }
                      }),
                  RADIENT_STATUS_OK);
        EXPECT_TRUE(Found);
        return Result;
    }

protected:
    RefCntAutoPtr<IRadientEngine>               m_pEngine;
    RefCntAutoPtr<IRadientScene>                m_pScene;
    RefCntAutoPtr<IRadientSceneWriter>          m_pWriter;
    RefCntAutoPtr<IRadientAnimationDestination> m_pDestination;
};

TEST_F(RadientSceneAnimationDestinationTest, WriterExposesDestinationWithWriterIdentity)
{
    RefCntAutoPtr<IObject> pWriterIdentity;
    RefCntAutoPtr<IObject> pDestinationIdentity;
    m_pWriter->QueryInterface(IID_Unknown, pWriterIdentity.GetAddressOfEmpty());
    m_pDestination->QueryInterface(IID_Unknown, pDestinationIdentity.GetAddressOfEmpty());
    ASSERT_NE(pWriterIdentity, nullptr);
    ASSERT_NE(pDestinationIdentity, nullptr);
    EXPECT_EQ(pWriterIdentity, pDestinationIdentity);

    RefCntAutoPtr<IRadientSceneWriter> pRoundTripWriter;
    m_pDestination->QueryInterface(
        IID_RadientSceneWriter,
        pRoundTripWriter.GetAddressOfEmpty());
    EXPECT_EQ(pRoundTripWriter, m_pWriter);
}

TEST_F(RadientSceneAnimationDestinationTest, ResolvesTransformPropertiesAndPreservesSparseChanges)
{
    RadientTransform FirstInitial{};
    FirstInitial.Position             = {1.f, 2.f, 3.f};
    FirstInitial.Scale                = {2.f, 3.f, 4.f};
    const RadientEntityID FirstEntity = CreateEntity(FirstInitial);
    ASSERT_NE(FirstEntity, InvalidRadientEntityID);

    RadientTransform SecondInitial{};
    SecondInitial.Position             = {5.f, 6.f, 7.f};
    SecondInitial.Rotation             = {0.f, 0.f, 0.70710678f, 0.70710678f};
    SecondInitial.Scale                = {7.f, 8.f, 9.f};
    const RadientEntityID SecondEntity = CreateEntity(SecondInitial);
    ASSERT_NE(SecondEntity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::vector Properties = {
        MakeNodeProperty(SecondEntity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(FirstEntity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(FirstEntity, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::vector<RadientAnimationResolvedPropertyDesc>  Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding(Properties, &Resolved);
    ASSERT_NE(pBinding, nullptr);
    ASSERT_EQ(Resolved.size(), Properties.size());
    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    const RadientFloat3         Translation           = {10.f, 20.f, 30.f};
    const RadientQuaternion     Rotation              = {0.f, 0.f, 1.f, 0.f};
    const RadientFloat3         Scale                 = {4.f, 5.f, 6.f};
    void* const*                pOutputs              = nullptr;
    const RadientSceneRevisions RevisionsBeforeUpdate = m_pScene->GetSceneRevisions();
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    ASSERT_NE(pOutputs[1], nullptr);
    ASSERT_NE(pOutputs[2], nullptr);
    std::memcpy(pOutputs[0], &Translation, sizeof(Translation));
    std::memcpy(pOutputs[1], &Rotation, sizeof(Rotation));
    std::memcpy(pOutputs[2], &Scale, sizeof(Scale));

    // Scene bindings expose live component fields. Writes are immediately
    // observable, while dirty propagation and revision changes wait for EndUpdate().
    RadientTransform DirectFirstTransform{};
    RadientTransform DirectSecondTransform{};
    ASSERT_EQ(m_pScene->GetLocalTransform(FirstEntity, DirectFirstTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetLocalTransform(SecondEntity, DirectSecondTransform), RADIENT_STATUS_OK);
    ExpectQuaternionNear(DirectFirstTransform.Rotation, Rotation);
    ExpectFloat3Near(DirectFirstTransform.Scale, Scale);
    ExpectFloat3Near(DirectSecondTransform.Position, Translation);
    EXPECT_EQ(m_pScene->GetSceneRevisions().Transforms, RevisionsBeforeUpdate.Transforms);

    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pScene->GetSceneRevisions().Transforms, RevisionsBeforeUpdate.Transforms + 1);

    RadientTransform FirstTransform{};
    RadientTransform SecondTransform{};
    ASSERT_EQ(m_pScene->GetLocalTransform(FirstEntity, FirstTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetLocalTransform(SecondEntity, SecondTransform), RADIENT_STATUS_OK);
    ExpectFloat3Near(FirstTransform.Position, FirstInitial.Position);
    ExpectQuaternionNear(FirstTransform.Rotation, Rotation);
    ExpectFloat3Near(FirstTransform.Scale, Scale);
    ExpectFloat3Near(SecondTransform.Position, Translation);
    ExpectQuaternionNear(SecondTransform.Rotation, SecondInitial.Rotation);
    ExpectFloat3Near(SecondTransform.Scale, SecondInitial.Scale);

    FirstTransform.Position = {100.f, 200.f, 300.f};
    SecondTransform.Scale   = {9.f, 10.f, 11.f};
    ASSERT_EQ(m_pWriter->SetLocalTransform(FirstEntity, FirstTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetLocalTransform(SecondEntity, SecondTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const RadientFloat3 SecondTranslation = {40.f, 50.f, 60.f};
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    std::memcpy(pOutputs[0], &SecondTranslation, sizeof(SecondTranslation));
    std::memcpy(pOutputs[1], &Rotation, sizeof(Rotation));
    std::memcpy(pOutputs[2], &Scale, sizeof(Scale));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    ASSERT_EQ(m_pScene->GetLocalTransform(FirstEntity, FirstTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetLocalTransform(SecondEntity, SecondTransform), RADIENT_STATUS_OK);
    ExpectFloat3Near(FirstTransform.Position, {100.f, 200.f, 300.f});
    ExpectFloat3Near(SecondTransform.Position, SecondTranslation);
    ExpectFloat3Near(SecondTransform.Scale, {9.f, 10.f, 11.f});
}

TEST_F(RadientSceneAnimationDestinationTest, ResolvesSupportedSubsetWithCompactOutputs)
{
    const RadientEntityID FirstEntity  = CreateEntity({});
    const RadientEntityID SecondEntity = CreateEntity({});
    const RadientEntityID ThirdEntity  = CreateEntity({});
    ASSERT_NE(FirstEntity, InvalidRadientEntityID);
    ASSERT_NE(SecondEntity, InvalidRadientEntityID);
    ASSERT_NE(ThirdEntity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetEntityOwnVisibility(FirstEntity, False), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::array Properties = {
        MakeNodeProperty(FirstEntity, UnknownAnimationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(SecondEntity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(SecondEntity,
                         RadientNodeTranslationProperty,
                         RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
                         0,
                         1,
                         IID_RadientScene),
        MakeNodeProperty(FirstEntity, RadientNodeVisibilityProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL),
        MakeNodeProperty(ThirdEntity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(FirstEntity, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 6> Resolved;
    for (RadientAnimationResolvedPropertyDesc& Result : Resolved)
        Result.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    ASSERT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);

    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_NORMALIZED_QUATERNION);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[3].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[4].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[5].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    ASSERT_NE(pOutputs[1], nullptr);
    ASSERT_NE(pOutputs[2], nullptr);
    ASSERT_NE(pOutputs[3], nullptr);

    const RadientQuaternion Rotation    = {0.f, 0.f, 1.f, 0.f};
    const Uint8             Visibility  = 1;
    const RadientFloat3     Translation = {10.f, 20.f, 30.f};
    const RadientFloat3     Scale       = {4.f, 5.f, 6.f};
    std::memcpy(pOutputs[0], &Rotation, sizeof(Rotation));
    std::memcpy(pOutputs[1], &Visibility, sizeof(Visibility));
    std::memcpy(pOutputs[2], &Translation, sizeof(Translation));
    std::memcpy(pOutputs[3], &Scale, sizeof(Scale));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    RadientTransform FirstTransform{};
    RadientTransform SecondTransform{};
    RadientTransform ThirdTransform{};
    ASSERT_EQ(m_pScene->GetLocalTransform(FirstEntity, FirstTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetLocalTransform(SecondEntity, SecondTransform), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetLocalTransform(ThirdEntity, ThirdTransform), RADIENT_STATUS_OK);
    ExpectFloat3Near(FirstTransform.Scale, Scale);
    ExpectQuaternionNear(SecondTransform.Rotation, Rotation);
    ExpectFloat3Near(ThirdTransform.Position, Translation);
    Bool FirstVisible = False;
    ASSERT_EQ(m_pScene->GetEntityOwnVisibility(FirstEntity, FirstVisible), RADIENT_STATUS_OK);
    EXPECT_EQ(FirstVisible, True);
}

TEST_F(RadientSceneAnimationDestinationTest, DefersAndThenCommitsDerivedSceneState)
{
    RadientTransform ParentTransform{};
    ParentTransform.Position     = {1.f, 0.f, 0.f};
    const RadientEntityID Parent = CreateEntity(ParentTransform);
    ASSERT_NE(Parent, InvalidRadientEntityID);

    RadientTransform ChildTransform{};
    ChildTransform.Position     = {0.f, 2.f, 0.f};
    const RadientEntityID Child = CreateEntity(ChildTransform);
    ASSERT_NE(Child, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetParent(Child, Parent, False), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
        MakeNodeProperty(Parent, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    });
    ASSERT_NE(pBinding, nullptr);

    void* const*        pOutputs            = nullptr;
    const RadientFloat3 DeferredTranslation = {5.f, 0.f, 0.f};
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    std::memcpy(pOutputs[0], &DeferredTranslation, sizeof(DeferredTranslation));
    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

    ASSERT_EQ(m_pScene->GetLocalTransform(Parent, ParentTransform), RADIENT_STATUS_OK);
    ExpectFloat3Near(ParentTransform.Position, DeferredTranslation);
    RadientMatrix4x4 ChildWorldMatrix{};
    EXPECT_EQ(m_pScene->GetCachedWorldMatrix(Child, ChildWorldMatrix), RADIENT_STATUS_OUT_OF_DATE);

    const RadientFloat3 CommittedTranslation = {7.f, 0.f, 0.f};
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    std::memcpy(pOutputs[0], &CommittedTranslation, sizeof(CommittedTranslation));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    ASSERT_EQ(m_pScene->GetCachedWorldMatrix(Child, ChildWorldMatrix), RADIENT_STATUS_OK);
    RadientTransform ExpectedChildWorld{};
    ExpectedChildWorld.Position = {7.f, 2.f, 0.f};
    ExpectMatrixNear(ChildWorldMatrix, RadientMath::TransformToMatrix(ExpectedChildWorld));
}

TEST_F(RadientSceneAnimationDestinationTest, StagesVisibilityAndUpdatesOnlyVisibilityState)
{
    const RadientEntityID Parent = CreateEntity({});
    const RadientEntityID Child  = CreateEntity({});
    ASSERT_NE(Parent, InvalidRadientEntityID);
    ASSERT_NE(Child, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetParent(Child, Parent, False), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::vector Properties = {
        MakeNodeProperty(Parent, RadientNodeVisibilityProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL),
    };
    std::vector<RadientAnimationResolvedPropertyDesc>  Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding(Properties, &Resolved);
    ASSERT_NE(pBinding, nullptr);
    ASSERT_EQ(Resolved.size(), 1u);
    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    Bool CachedChildVisible = False;
    ASSERT_EQ(m_pScene->GetCachedEntityEffectiveVisibility(Child, CachedChildVisible), RADIENT_STATUS_OK);
    ASSERT_EQ(CachedChildVisible, True);

    RadientTransform ParentTransformBefore{};
    RadientTransform ChildTransformBefore{};
    ASSERT_EQ(m_pScene->GetLocalTransform(Parent, ParentTransformBefore), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetLocalTransform(Child, ChildTransformBefore), RADIENT_STATUS_OK);
    const RadientSceneRevisions RevisionsBefore = m_pScene->GetSceneRevisions();

    const Uint8  Hidden   = 0;
    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    std::memcpy(pOutputs[0], &Hidden, sizeof(Hidden));

    Bool ParentVisible = False;
    ASSERT_EQ(m_pScene->GetEntityOwnVisibility(Parent, ParentVisible), RADIENT_STATUS_OK);
    EXPECT_EQ(ParentVisible, True);
    EXPECT_EQ(m_pScene->GetSceneRevisions(), RevisionsBefore);

    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetEntityOwnVisibility(Parent, ParentVisible), RADIENT_STATUS_OK);
    EXPECT_EQ(ParentVisible, False);

    RadientSceneRevisions ExpectedRevisions = RevisionsBefore;
    ++ExpectedRevisions.Visibility;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
    EXPECT_EQ(m_pScene->GetCachedEntityEffectiveVisibility(Child, CachedChildVisible),
              RADIENT_STATUS_OUT_OF_DATE);

    RadientTransform ParentTransformAfter{};
    RadientTransform ChildTransformAfter{};
    ASSERT_EQ(m_pScene->GetLocalTransform(Parent, ParentTransformAfter), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetLocalTransform(Child, ChildTransformAfter), RADIENT_STATUS_OK);
    EXPECT_EQ(ParentTransformAfter, ParentTransformBefore);
    EXPECT_EQ(ChildTransformAfter, ChildTransformBefore);

    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    std::memcpy(pOutputs[0], &Hidden, sizeof(Hidden));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
    ASSERT_EQ(m_pScene->GetCachedEntityEffectiveVisibility(Child, CachedChildVisible), RADIENT_STATUS_OK);
    EXPECT_EQ(CachedChildVisible, False);

    const Uint8 Visible = 1;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    std::memcpy(pOutputs[0], &Visible, sizeof(Visible));
    ASSERT_EQ(m_pScene->GetEntityOwnVisibility(Parent, ParentVisible), RADIENT_STATUS_OK);
    EXPECT_EQ(ParentVisible, False);
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    ++ExpectedRevisions.Visibility;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
    ASSERT_EQ(m_pScene->GetEntityOwnVisibility(Parent, ParentVisible), RADIENT_STATUS_OK);
    EXPECT_EQ(ParentVisible, True);
    ASSERT_EQ(m_pScene->GetCachedEntityEffectiveVisibility(Child, CachedChildVisible), RADIENT_STATUS_OK);
    EXPECT_EQ(CachedChildVisible, True);

    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    std::memcpy(pOutputs[0], &Visible, sizeof(Visible));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
}

TEST_F(RadientSceneAnimationDestinationTest, EvaluatesStepVisibilityChannel)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    ASSERT_EQ(m_pEngine->GetAssetManager(pAssetManager.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetManager, nullptr);

    RadientAnimationTargetDesc Target{};
    Target.Schema = RadientNodeAnimationSchemaID;
    Target.Object = 0;

    const std::array<Float32, 2> Times            = {0.f, 1.f};
    const std::array<Uint8, 2>   VisibilityValues = {0, 1};
    RadientAnimationSamplerDesc  Sampler{};
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_BOOL;
    Sampler.Value.ArraySize = 1;
    Sampler.Interpolation   = RADIENT_ANIMATION_INTERPOLATION_STEP;
    Sampler.pTimes          = Times.data();
    Sampler.pValues         = VisibilityValues.data();
    Sampler.ValueDataSize   = sizeof(VisibilityValues);
    Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

    RadientAnimationChannelDesc Channel{};
    Channel.TargetIndex  = 0;
    Channel.Property     = RadientNodeVisibilityProperty;
    Channel.SamplerIndex = 0;

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = "Step visibility";
    ClipDesc.Duration     = 2.f;
    ClipDesc.pTargets     = &Target;
    ClipDesc.TargetCount  = 1;
    ClipDesc.pSamplers    = &Sampler;
    ClipDesc.SamplerCount = 1;
    ClipDesc.pChannels    = &Channel;
    ClipDesc.ChannelCount = 1;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pClip, nullptr);

    RadientAnimationDestinationMappingDesc Mapping{};
    Mapping.ClipTargetIndex    = 0;
    Mapping.DestinationElement = Entity;

    RadientAnimationDestinationDesc Destination{};
    Destination.pDestination = m_pDestination;
    Destination.pMappings    = &Mapping;
    Destination.MappingCount = 1;

    RadientAnimationBindingDesc BindingDesc{};
    BindingDesc.pDestinations    = &Destination;
    BindingDesc.DestinationCount = 1;

    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    ASSERT_EQ(pClip->CreateBinding(BindingDesc, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);

    RadientSceneRevisions        ExpectedRevisions = m_pScene->GetSceneRevisions();
    RadientAnimationEvaluateInfo EvaluateInfo{};
    EvaluateInfo.Time = 0.5f;
    ASSERT_EQ(pBinding->Evaluate(EvaluateInfo), RADIENT_STATUS_OK);

    Bool Visible = True;
    ASSERT_EQ(m_pScene->GetEntityOwnVisibility(Entity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
    ASSERT_EQ(m_pScene->GetCachedEntityEffectiveVisibility(Entity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, False);
    ++ExpectedRevisions.Visibility;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);

    EvaluateInfo.Time = 1.5f;
    ASSERT_EQ(pBinding->Evaluate(EvaluateInfo), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pScene->GetEntityOwnVisibility(Entity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    ASSERT_EQ(m_pScene->GetCachedEntityEffectiveVisibility(Entity, Visible), RADIENT_STATUS_OK);
    EXPECT_EQ(Visible, True);
    ++ExpectedRevisions.Visibility;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
}

TEST_F(RadientSceneAnimationDestinationTest, UpdatesAllSupportedLightPropertiesAndPreservesOtherFields)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const RadientLightComponent Initial = MakeFullySpecifiedLight();
    ASSERT_EQ(m_pWriter->SetLight(Entity, Initial), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);
    GetSceneImpl().ClearPendingRenderChanges();

    const std::vector Properties = {
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightColorProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightRangeProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightExposureProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightInnerConeAngleProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightOuterConeAngleProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightDiffuseProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightSpecularProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightNormalizeProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_BOOL),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightEnableColorTemperatureProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_BOOL),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightColorTemperatureProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightRadiusProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightAngleProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightShapingFocusProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    std::vector<RadientAnimationResolvedPropertyDesc>  Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding(Properties, &Resolved);
    ASSERT_NE(pBinding, nullptr);
    ASSERT_EQ(Resolved.size(), Properties.size());
    for (const RadientAnimationResolvedPropertyDesc& Property : Resolved)
        EXPECT_EQ(Property.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    RadientLightComponent Expected  = Initial;
    Expected.Color                  = {0.9f, 0.8f, 0.7f};
    Expected.Intensity              = 12.f;
    Expected.Range                  = 25.f;
    Expected.Exposure               = 1.5f;
    Expected.InnerConeAngle         = 0.2f;
    Expected.OuterConeAngle         = 0.6f;
    Expected.Diffuse                = 0.6f;
    Expected.Specular               = 0.4f;
    Expected.Normalize              = False;
    Expected.EnableColorTemperature = False;
    Expected.ColorTemperature       = 3200.f;
    Expected.Radius                 = 1.25f;
    Expected.Angle                  = 1.2f;
    Expected.ShapingFocus           = 0.35f;

    const RadientSceneRevisions RevisionsBefore = m_pScene->GetSceneRevisions();
    void* const*                pOutputs        = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    std::memcpy(pOutputs[0], &Expected.Color, sizeof(Expected.Color));
    std::memcpy(pOutputs[1], &Expected.Intensity, sizeof(Expected.Intensity));
    std::memcpy(pOutputs[2], &Expected.Range, sizeof(Expected.Range));
    std::memcpy(pOutputs[3], &Expected.Exposure, sizeof(Expected.Exposure));
    std::memcpy(pOutputs[4], &Expected.InnerConeAngle, sizeof(Expected.InnerConeAngle));
    std::memcpy(pOutputs[5], &Expected.OuterConeAngle, sizeof(Expected.OuterConeAngle));
    const Uint8 Normalize              = static_cast<Uint8>(Expected.Normalize);
    const Uint8 EnableColorTemperature = static_cast<Uint8>(Expected.EnableColorTemperature);
    std::memcpy(pOutputs[6], &Expected.Diffuse, sizeof(Expected.Diffuse));
    std::memcpy(pOutputs[7], &Expected.Specular, sizeof(Expected.Specular));
    EXPECT_NE(pOutputs[8], pOutputs[9]);
    std::memcpy(pOutputs[8], &Normalize, sizeof(Normalize));
    std::memcpy(pOutputs[9], &EnableColorTemperature, sizeof(EnableColorTemperature));
    std::memcpy(pOutputs[10], &Expected.ColorTemperature, sizeof(Expected.ColorTemperature));
    std::memcpy(pOutputs[11], &Expected.Radius, sizeof(Expected.Radius));
    std::memcpy(pOutputs[12], &Expected.Angle, sizeof(Expected.Angle));
    std::memcpy(pOutputs[13], &Expected.ShapingFocus, sizeof(Expected.ShapingFocus));

    RadientLightComponent BeforeEnd  = Expected;
    BeforeEnd.Normalize              = Initial.Normalize;
    BeforeEnd.EnableColorTemperature = Initial.EnableColorTemperature;
    EXPECT_EQ(GetLight(Entity), BeforeEnd);
    EXPECT_EQ(m_pScene->GetSceneRevisions(), RevisionsBefore);

    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);
    EXPECT_EQ(GetLight(Entity), Expected);

    RadientSceneRevisions ExpectedRevisions = RevisionsBefore;
    ++ExpectedRevisions.Lights;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);

    Uint32 UpdateCount = 0;
    GetSceneImpl().GetState().EnumerateRenderableLightChanges(
        [&](const RadientSceneState::RenderableLightChange& Change,
            const RadientSceneState::RenderableLight*       pLight) {
            if (Change.Entity != Entity)
                return;
            ++UpdateCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableLightChangeType::Updated);
            ASSERT_NE(pLight, nullptr);
            EXPECT_EQ(pLight->Light, Expected);
        });
    EXPECT_EQ(UpdateCount, 1u);
}

TEST_F(RadientSceneAnimationDestinationTest, SingleLightPropertyUpdatesPreserveOtherFields)
{
    const RadientLightComponent Initial      = MakeFullySpecifiedLight();
    const auto                  TestProperty = [&](const char*                  Name,
                                  RadientAnimationPropertyID   Property,
                                  RADIENT_ANIMATION_VALUE_TYPE Type,
                                  auto                         Member,
                                  const auto&                  Value) {
        SCOPED_TRACE(Name);
        const RadientEntityID Entity = CreateEntity({});
        ASSERT_NE(Entity, InvalidRadientEntityID);
        ASSERT_EQ(m_pWriter->SetLight(Entity, Initial), RADIENT_STATUS_OK);
        ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
            MakeProperty(Entity, RadientLightAnimationSchemaID, Property, Type),
        });
        ASSERT_NE(pBinding, nullptr);

        RadientLightComponent Expected = Initial;
        Expected.*Member               = Value;

        void* const* pOutputs = nullptr;
        ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
        ASSERT_NE(pOutputs, nullptr);
        ASSERT_NE(pOutputs[0], nullptr);
        std::memcpy(pOutputs[0], &Value, sizeof(Value));
        ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

        EXPECT_EQ(GetLight(Entity), Expected);
    };

    TestProperty("Color", RadientLightColorProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
                 &RadientLightComponent::Color, RadientFloat3{0.9f, 0.8f, 0.7f});
    TestProperty("Intensity", RadientLightIntensityProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::Intensity, 12.f);
    TestProperty("Range", RadientLightRangeProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::Range, 25.f);
    TestProperty("Exposure", RadientLightExposureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::Exposure, 1.5f);
    TestProperty("InnerConeAngle", RadientLightInnerConeAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::InnerConeAngle, 0.2f);
    TestProperty("OuterConeAngle", RadientLightOuterConeAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::OuterConeAngle, 0.6f);
    TestProperty("Diffuse", RadientLightDiffuseProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::Diffuse, 0.6f);
    TestProperty("Specular", RadientLightSpecularProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::Specular, 0.4f);
    TestProperty("Normalize", RadientLightNormalizeProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL,
                 &RadientLightComponent::Normalize, Uint8{0});
    TestProperty("EnableColorTemperature", RadientLightEnableColorTemperatureProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL,
                 &RadientLightComponent::EnableColorTemperature, Uint8{0});
    TestProperty("ColorTemperature", RadientLightColorTemperatureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::ColorTemperature, 3200.f);
    TestProperty("Radius", RadientLightRadiusProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::Radius, 1.25f);
    TestProperty("Angle", RadientLightAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::Angle, 1.2f);
    TestProperty("ShapingFocus", RadientLightShapingFocusProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT,
                 &RadientLightComponent::ShapingFocus, 0.35f);
}

TEST_F(RadientSceneAnimationDestinationTest, InterleavedLightPropertiesUseIndependentEntityStorage)
{
    const RadientEntityID FirstEntity  = CreateEntity({});
    const RadientEntityID SecondEntity = CreateEntity({});
    ASSERT_NE(FirstEntity, InvalidRadientEntityID);
    ASSERT_NE(SecondEntity, InvalidRadientEntityID);

    const RadientLightComponent FirstInitial  = MakeFullySpecifiedLight();
    RadientLightComponent       SecondInitial = MakeFullySpecifiedLight();
    SecondInitial.Type                        = RADIENT_LIGHT_TYPE_POINT;
    SecondInitial.Color                       = {0.7f, 0.6f, 0.5f};
    SecondInitial.Intensity                   = 3.f;
    SecondInitial.Range                       = 20.f;
    SecondInitial.Exposure                    = 1.f;
    SecondInitial.Diffuse                     = 0.4f;
    SecondInitial.Specular                    = 0.6f;
    SecondInitial.Normalize                   = False;
    SecondInitial.EnableColorTemperature      = False;
    SecondInitial.ColorTemperature            = 5500.f;
    SecondInitial.Radius                      = 1.f;
    SecondInitial.Angle                       = 1.5f;
    SecondInitial.InnerConeAngle              = 0.2f;
    SecondInitial.OuterConeAngle              = 0.6f;
    SecondInitial.ShapingFocus                = 0.7f;
    ASSERT_EQ(m_pWriter->SetLight(FirstEntity, FirstInitial), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetLight(SecondEntity, SecondInitial), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);
    GetSceneImpl().ClearPendingRenderChanges();

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
        MakeProperty(FirstEntity, RadientLightAnimationSchemaID, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(SecondEntity, RadientLightAnimationSchemaID, RadientLightNormalizeProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_BOOL),
        MakeProperty(FirstEntity, RadientLightAnimationSchemaID, RadientLightNormalizeProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_BOOL),
        MakeProperty(SecondEntity, RadientLightAnimationSchemaID, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    });
    ASSERT_NE(pBinding, nullptr);

    RadientLightComponent FirstExpected  = FirstInitial;
    RadientLightComponent SecondExpected = SecondInitial;
    FirstExpected.Intensity              = 11.f;
    FirstExpected.Normalize              = False;
    SecondExpected.Intensity             = 22.f;
    SecondExpected.Normalize             = True;
    const Uint8 FirstNormalize           = static_cast<Uint8>(FirstExpected.Normalize);
    const Uint8 SecondNormalize          = static_cast<Uint8>(SecondExpected.Normalize);

    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    for (Uint32 First = 0; First < 4; ++First)
    {
        ASSERT_NE(pOutputs[First], nullptr);
        for (Uint32 Second = First + 1; Second < 4; ++Second)
            EXPECT_NE(pOutputs[First], pOutputs[Second]);
    }
    std::memcpy(pOutputs[0], &FirstExpected.Intensity, sizeof(FirstExpected.Intensity));
    std::memcpy(pOutputs[1], &SecondNormalize, sizeof(SecondNormalize));
    std::memcpy(pOutputs[2], &FirstNormalize, sizeof(FirstNormalize));
    std::memcpy(pOutputs[3], &SecondExpected.Intensity, sizeof(SecondExpected.Intensity));

    const RadientSceneRevisions RevisionsBefore = m_pScene->GetSceneRevisions();
    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

    EXPECT_EQ(GetLight(FirstEntity), FirstExpected);
    EXPECT_EQ(GetLight(SecondEntity), SecondExpected);

    RadientSceneRevisions ExpectedRevisions = RevisionsBefore;
    ++ExpectedRevisions.Lights;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);

    Uint32 FirstUpdateCount  = 0;
    Uint32 SecondUpdateCount = 0;
    GetSceneImpl().GetState().EnumerateRenderableLightChanges(
        [&](const RadientSceneState::RenderableLightChange& Change,
            const RadientSceneState::RenderableLight*       pLight) {
            ASSERT_EQ(Change.Type, RadientSceneState::RenderableLightChangeType::Updated);
            ASSERT_NE(pLight, nullptr);
            if (Change.Entity == FirstEntity)
            {
                ++FirstUpdateCount;
                EXPECT_EQ(pLight->Light, FirstExpected);
            }
            else if (Change.Entity == SecondEntity)
            {
                ++SecondUpdateCount;
                EXPECT_EQ(pLight->Light, SecondExpected);
            }
            else
            {
                ADD_FAILURE() << "Unexpected light update for entity " << Change.Entity;
            }
        });
    EXPECT_EQ(FirstUpdateCount, 1u);
    EXPECT_EQ(SecondUpdateCount, 1u);
}

TEST_F(RadientSceneAnimationDestinationTest, UpdatesAllSupportedCameraPropertiesAndPreservesOtherFields)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);

    RadientCameraComponent Initial{};
    Initial.Projection               = RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC;
    Initial.HorizontalAperture       = 20.f;
    Initial.VerticalAperture         = 15.f;
    Initial.HorizontalApertureOffset = 1.25f;
    Initial.VerticalApertureOffset   = -0.75f;
    Initial.FocalLength              = 35.f;
    Initial.ClippingRange            = {0.25f, 500.f};
    Initial.FStop                    = 2.8f;
    Initial.FocusDistance            = 4.f;
    ASSERT_EQ(m_pWriter->SetCamera(Entity, Initial), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::vector Properties = {
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraHorizontalApertureProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraVerticalApertureProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraFocalLengthProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraNearClipProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraFarClipProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraFStopProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraFocusDistanceProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    std::vector<RadientAnimationResolvedPropertyDesc>  Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding(Properties, &Resolved);
    ASSERT_NE(pBinding, nullptr);
    ASSERT_EQ(Resolved.size(), Properties.size());
    for (const RadientAnimationResolvedPropertyDesc& Property : Resolved)
        EXPECT_EQ(Property.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    RadientCameraComponent Expected = Initial;
    Expected.HorizontalAperture     = 40.f;
    Expected.VerticalAperture       = 30.f;
    Expected.FocalLength            = 75.f;
    Expected.ClippingRange          = {2.f, 250.f};
    Expected.FStop                  = 5.6f;
    Expected.FocusDistance          = 12.f;

    const RadientSceneRevisions RevisionsBefore = m_pScene->GetSceneRevisions();
    void* const*                pOutputs        = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    std::memcpy(pOutputs[0], &Expected.HorizontalAperture, sizeof(Expected.HorizontalAperture));
    std::memcpy(pOutputs[1], &Expected.VerticalAperture, sizeof(Expected.VerticalAperture));
    std::memcpy(pOutputs[2], &Expected.FocalLength, sizeof(Expected.FocalLength));
    std::memcpy(pOutputs[3], &Expected.ClippingRange.x, sizeof(Expected.ClippingRange.x));
    std::memcpy(pOutputs[4], &Expected.ClippingRange.y, sizeof(Expected.ClippingRange.y));
    std::memcpy(pOutputs[5], &Expected.FStop, sizeof(Expected.FStop));
    std::memcpy(pOutputs[6], &Expected.FocusDistance, sizeof(Expected.FocusDistance));

    RadientCameraComponent Direct{};
    ASSERT_EQ(m_pScene->GetCamera(Entity, Direct), RADIENT_STATUS_OK);
    EXPECT_EQ(Direct, Expected);
    EXPECT_EQ(m_pScene->GetSceneRevisions(), RevisionsBefore);

    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);
    RadientCameraComponent Updated{};
    ASSERT_EQ(m_pScene->GetCamera(Entity, Updated), RADIENT_STATUS_OK);
    EXPECT_EQ(Updated, Expected);

    RadientSceneRevisions ExpectedRevisions = RevisionsBefore;
    ++ExpectedRevisions.Cameras;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
}

TEST_F(RadientSceneAnimationDestinationTest, EveryPropertyPublishesItsSceneRevision)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetLight(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetCamera(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    enum class RevisionKind
    {
        Transform,
        Light,
        Camera,
    };
    struct PropertyCase
    {
        RadientAnimationSchemaID     Schema;
        RadientAnimationPropertyID   Property;
        RADIENT_ANIMATION_VALUE_TYPE Type;
        RevisionKind                 Revision;
        Float32                      ScalarValue = 1.f;
    };
    const std::array Cases = {
        PropertyCase{RadientNodeAnimationSchemaID, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, RevisionKind::Transform},
        PropertyCase{RadientNodeAnimationSchemaID, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4, RevisionKind::Transform},
        PropertyCase{RadientNodeAnimationSchemaID, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, RevisionKind::Transform},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightColorProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightIntensityProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightRangeProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightExposureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightInnerConeAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light, 0.2f},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightOuterConeAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light, 0.6f},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightDiffuseProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightSpecularProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightNormalizeProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightEnableColorTemperatureProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightColorTemperatureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light, 3200.f},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightRadiusProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightShapingFocusProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Light},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraHorizontalApertureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Camera},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraVerticalApertureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Camera},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFocalLengthProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Camera},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraNearClipProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Camera, 0.2f},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFarClipProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Camera, 2000.f},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFStopProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Camera},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFocusDistanceProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT, RevisionKind::Camera},
    };

    for (size_t CaseIndex = 0; CaseIndex < Cases.size(); ++CaseIndex)
    {
        SCOPED_TRACE(CaseIndex);
        const PropertyCase&                                Case     = Cases[CaseIndex];
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
            MakeProperty(Entity, Case.Schema, Case.Property, Case.Type),
        });
        ASSERT_NE(pBinding, nullptr);

        void* const* pOutputs = nullptr;
        ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
        ASSERT_NE(pOutputs, nullptr);
        switch (Case.Type)
        {
            case RADIENT_ANIMATION_VALUE_TYPE_FLOAT:
                std::memcpy(pOutputs[0], &Case.ScalarValue, sizeof(Case.ScalarValue));
                break;

            case RADIENT_ANIMATION_VALUE_TYPE_BOOL:
            {
                const Uint8 Value = 1;
                std::memcpy(pOutputs[0], &Value, sizeof(Value));
                break;
            }

            case RADIENT_ANIMATION_VALUE_TYPE_FLOAT3:
            {
                const RadientFloat3 Value{1.f, 1.f, 1.f};
                std::memcpy(pOutputs[0], &Value, sizeof(Value));
                break;
            }

            case RADIENT_ANIMATION_VALUE_TYPE_FLOAT4:
            {
                const RadientQuaternion Value{0.f, 0.f, 0.f, 1.f};
                std::memcpy(pOutputs[0], &Value, sizeof(Value));
                break;
            }

            default:
                FAIL() << "Unexpected test value type";
        }

        const RadientSceneRevisions RevisionsBefore = m_pScene->GetSceneRevisions();
        ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);
        RadientSceneRevisions ExpectedRevisions = RevisionsBefore;
        switch (Case.Revision)
        {
            case RevisionKind::Transform:
                ++ExpectedRevisions.Transforms;
                break;
            case RevisionKind::Light:
                ++ExpectedRevisions.Lights;
                break;
            case RevisionKind::Camera:
                ++ExpectedRevisions.Cameras;
                break;
        }
        EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
    }
}

TEST_F(RadientSceneAnimationDestinationTest, CompactsInterleavedNodeLightAndCameraProperties)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetLight(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetCamera(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::array Properties = {
        MakeProperty(Entity, RadientLightAnimationSchemaID, UnknownAnimationProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraHorizontalApertureProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeNodeProperty(Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightColorProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeProperty(Entity, IID_RadientScene, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraNearClipProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    std::array<RadientAnimationResolvedPropertyDesc, Properties.size()> Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding>                  pBinding;
    ASSERT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);

    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[3].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[4].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[5].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[6].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    const Float32       HorizontalAperture = 42.f;
    const RadientFloat3 Translation        = {1.f, 2.f, 3.f};
    const RadientFloat3 Color              = {0.25f, 0.5f, 0.75f};
    const Float32       Intensity          = 8.f;
    const Float32       NearClip           = 0.5f;
    void* const*        pOutputs           = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    std::memcpy(pOutputs[0], &HorizontalAperture, sizeof(HorizontalAperture));
    std::memcpy(pOutputs[1], &Translation, sizeof(Translation));
    std::memcpy(pOutputs[2], &Color, sizeof(Color));
    std::memcpy(pOutputs[3], &Intensity, sizeof(Intensity));
    std::memcpy(pOutputs[4], &NearClip, sizeof(NearClip));

    const RadientSceneRevisions RevisionsBefore = m_pScene->GetSceneRevisions();
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    RadientTransform Transform{};
    ASSERT_EQ(m_pScene->GetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);
    ExpectFloat3Near(Transform.Position, Translation);

    const RadientLightComponent Light = GetLight(Entity);
    ExpectFloat3Near(Light.Color, Color);
    EXPECT_FLOAT_EQ(Light.Intensity, Intensity);

    RadientCameraComponent Camera{};
    ASSERT_EQ(m_pScene->GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(Camera.HorizontalAperture, HorizontalAperture);
    EXPECT_FLOAT_EQ(Camera.ClippingRange.x, NearClip);

    RadientSceneRevisions ExpectedRevisions = RevisionsBefore;
    ++ExpectedRevisions.Transforms;
    ++ExpectedRevisions.Lights;
    ++ExpectedRevisions.Cameras;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsMalformedLightAndCameraPropertyLayouts)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetLight(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetCamera(Entity, {}), RADIENT_STATUS_OK);

    struct PropertyCase
    {
        RadientAnimationSchemaID     Schema;
        RadientAnimationPropertyID   Property;
        RADIENT_ANIMATION_VALUE_TYPE Type;
    };
    const std::array Cases = {
        PropertyCase{RadientLightAnimationSchemaID, RadientLightColorProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightIntensityProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightRangeProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightExposureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightInnerConeAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightOuterConeAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightDiffuseProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightSpecularProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightNormalizeProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightEnableColorTemperatureProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightColorTemperatureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightRadiusProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightAngleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientLightAnimationSchemaID, RadientLightShapingFocusProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraHorizontalApertureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraVerticalApertureProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFocalLengthProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraNearClipProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFarClipProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFStopProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        PropertyCase{RadientCameraAnimationSchemaID, RadientCameraFocusDistanceProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
    };

    for (size_t CaseIndex = 0; CaseIndex < Cases.size(); ++CaseIndex)
    {
        SCOPED_TRACE(CaseIndex);
        const PropertyCase&                Case = Cases[CaseIndex];
        const RADIENT_ANIMATION_VALUE_TYPE WrongType =
            Case.Type == RADIENT_ANIMATION_VALUE_TYPE_FLOAT ?
            RADIENT_ANIMATION_VALUE_TYPE_FLOAT3 :
            RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
        const std::array InvalidProperties = {
            MakeProperty(Entity, Case.Schema, Case.Property, WrongType),
            MakeProperty(Entity, Case.Schema, Case.Property, Case.Type, 1, 1),
            MakeProperty(Entity, Case.Schema, Case.Property, Case.Type, 0, 2),
        };

        for (const RadientAnimationPropertyBindingDesc& Property : InvalidProperties)
        {
            RadientAnimationResolvedPropertyDesc               Resolved{};
            RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
            EXPECT_EQ(m_pDestination->CreateBinding(
                          &Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
                      RADIENT_STATUS_INVALID_ARGUMENT);
            EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
            EXPECT_EQ(pBinding, nullptr);
        }
    }
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsMissingOrRemovedLightAndCameraComponents)
{
    const RadientEntityID EmptyEntity = CreateEntity({});
    ASSERT_NE(EmptyEntity, InvalidRadientEntityID);

    const std::array MissingProperties = {
        MakeProperty(EmptyEntity, RadientLightAnimationSchemaID, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(EmptyEntity, RadientCameraAnimationSchemaID, RadientCameraFocalLengthProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    for (const RadientAnimationPropertyBindingDesc& Property : MissingProperties)
    {
        RadientAnimationResolvedPropertyDesc               Resolved{};
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      &Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_NOT_FOUND);
        EXPECT_EQ(pBinding, nullptr);
    }

    const std::array UnknownProperties = {
        MakeProperty(EmptyEntity, RadientLightAnimationSchemaID, UnknownAnimationProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(EmptyEntity, RadientCameraAnimationSchemaID, UnknownAnimationProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    };
    for (const RadientAnimationPropertyBindingDesc& Property : UnknownProperties)
    {
        RadientAnimationResolvedPropertyDesc               Resolved{};
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      &Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_UNSUPPORTED);
        EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
        EXPECT_EQ(pBinding, nullptr);
    }

    const RadientEntityID LightEntity = CreateEntity({});
    ASSERT_NE(LightEntity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetLight(LightEntity, {}), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pLightBinding = CreateBinding({
        MakeProperty(LightEntity, RadientLightAnimationSchemaID, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    });
    ASSERT_NE(pLightBinding, nullptr);
    ASSERT_EQ(m_pWriter->RemoveComponent(LightEntity, RADIENT_COMPONENT_TYPE_LIGHT), RADIENT_STATUS_OK);

    void* const* pOutputs = reinterpret_cast<void* const*>(1);
    ASSERT_EQ(pLightBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pOutputs, nullptr);

    const RadientEntityID CameraEntity = CreateEntity({});
    ASSERT_NE(CameraEntity, InvalidRadientEntityID);
    ASSERT_EQ(m_pWriter->SetCamera(CameraEntity, {}), RADIENT_STATUS_OK);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pCameraBinding = CreateBinding({
        MakeProperty(CameraEntity, RadientCameraAnimationSchemaID, RadientCameraFocalLengthProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    });
    ASSERT_NE(pCameraBinding, nullptr);
    ASSERT_EQ(m_pWriter->RemoveComponent(CameraEntity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);

    pOutputs = reinterpret_cast<void* const*>(1);
    ASSERT_EQ(pCameraBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pOutputs, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, ReacquiresOptionalComponentStorageForEveryUpdate)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);

    RadientLightComponent InitialLight{};
    InitialLight.Intensity = 2.f;
    RadientCameraComponent InitialCamera{};
    InitialCamera.FocalLength = 35.f;
    ASSERT_EQ(m_pWriter->SetLight(Entity, InitialLight), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetCamera(Entity, InitialCamera), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
        MakeProperty(Entity, RadientLightAnimationSchemaID, RadientLightIntensityProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
        MakeProperty(Entity, RadientCameraAnimationSchemaID, RadientCameraFocalLengthProperty,
                     RADIENT_ANIMATION_VALUE_TYPE_FLOAT),
    });
    ASSERT_NE(pBinding, nullptr);

    // Grow both optional-component pools after binding creation. A binding must
    // resolve their current storage on every update instead of retaining an ECS pointer.
    RadientEntityID LastFiller = InvalidRadientEntityID;
    for (Uint32 Index = 0; Index < 64; ++Index)
    {
        LastFiller = CreateEntity({});
        ASSERT_NE(LastFiller, InvalidRadientEntityID);
        ASSERT_EQ(m_pWriter->SetLight(LastFiller, {}), RADIENT_STATUS_OK);
        ASSERT_EQ(m_pWriter->SetCamera(LastFiller, {}), RADIENT_STATUS_OK);
    }
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    ASSERT_EQ(m_pWriter->RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);
    void* const* pOutputs = reinterpret_cast<void* const*>(1);
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pOutputs, nullptr);

    RadientCameraComponent RestoredCamera{};
    RestoredCamera.FocalLength = 45.f;
    ASSERT_EQ(m_pWriter->SetCamera(Entity, RestoredCamera), RADIENT_STATUS_OK);
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    *static_cast<Float32*>(pOutputs[0]) = 7.f;
    *static_cast<Float32*>(pOutputs[1]) = 70.f;
    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

    EXPECT_FLOAT_EQ(GetLight(Entity).Intensity, 7.f);
    RadientCameraComponent Camera{};
    ASSERT_EQ(m_pScene->GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(Camera.FocalLength, 70.f);

    ASSERT_EQ(m_pWriter->RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_LIGHT), RADIENT_STATUS_OK);
    pOutputs = reinterpret_cast<void* const*>(1);
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pOutputs, nullptr);

    RadientLightComponent RestoredLight{};
    RestoredLight.Intensity = 3.f;
    ASSERT_EQ(m_pWriter->SetLight(Entity, RestoredLight), RADIENT_STATUS_OK);
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    *static_cast<Float32*>(pOutputs[0]) = 9.f;
    *static_cast<Float32*>(pOutputs[1]) = 90.f;
    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

    EXPECT_FLOAT_EQ(GetLight(Entity).Intensity, 9.f);
    ASSERT_EQ(m_pScene->GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(Camera.FocalLength, 90.f);

    // Replace both components entirely between successful brackets. With packed
    // ECS storage, the last filler is likely moved into the target's former slot;
    // checking it makes stale direct-output pointers observable.
    ASSERT_EQ(m_pWriter->RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_LIGHT), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_CAMERA), RADIENT_STATUS_OK);
    RadientLightComponent ReplacementLight{};
    ReplacementLight.Intensity = 4.f;
    ReplacementLight.Range     = 123.f;
    RadientCameraComponent ReplacementCamera{};
    ReplacementCamera.FocalLength   = 55.f;
    ReplacementCamera.FocusDistance = 8.f;
    ASSERT_EQ(m_pWriter->SetLight(Entity, ReplacementLight), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetCamera(Entity, ReplacementCamera), RADIENT_STATUS_OK);

    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    *static_cast<Float32*>(pOutputs[0]) = 11.f;
    *static_cast<Float32*>(pOutputs[1]) = 110.f;
    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

    const RadientLightComponent UpdatedLight = GetLight(Entity);
    EXPECT_FLOAT_EQ(UpdatedLight.Intensity, 11.f);
    EXPECT_FLOAT_EQ(UpdatedLight.Range, ReplacementLight.Range);
    ASSERT_EQ(m_pScene->GetCamera(Entity, Camera), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(Camera.FocalLength, 110.f);
    EXPECT_FLOAT_EQ(Camera.FocusDistance, ReplacementCamera.FocusDistance);

    EXPECT_FLOAT_EQ(GetLight(LastFiller).Intensity, RadientLightComponent{}.Intensity);
    ASSERT_EQ(m_pScene->GetCamera(LastFiller, Camera), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(Camera.FocalLength, RadientCameraComponent{}.FocalLength);
}

TEST_F(RadientSceneAnimationDestinationTest, EvaluatesAndFansOutLightAndCameraChannels)
{
    const std::array Entities = {
        CreateEntity({}),
        CreateEntity({}),
    };
    for (const RadientEntityID Entity : Entities)
        ASSERT_NE(Entity, InvalidRadientEntityID);

    RadientLightComponent InitialLight{};
    InitialLight.Color                  = {0.2f, 0.3f, 0.4f};
    InitialLight.Intensity              = 1.f;
    InitialLight.Normalize              = False;
    InitialLight.EnableColorTemperature = True;

    RadientCameraComponent InitialCamera{};
    InitialCamera.FocalLength   = 35.f;
    InitialCamera.ClippingRange = {0.25f, 500.f};
    for (const RadientEntityID Entity : Entities)
    {
        ASSERT_EQ(m_pWriter->SetLight(Entity, InitialLight), RADIENT_STATUS_OK);
        ASSERT_EQ(m_pWriter->SetCamera(Entity, InitialCamera), RADIENT_STATUS_OK);
    }
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    ASSERT_EQ(m_pEngine->GetAssetManager(pAssetManager.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetManager, nullptr);

    const std::array Targets = {
        RadientAnimationTargetDesc{RadientLightAnimationSchemaID, 11, "Light"},
        RadientAnimationTargetDesc{RadientCameraAnimationSchemaID, 12, "Camera"},
    };
    const std::array<Float32, 2>       Times  = {0.f, 2.f};
    const std::array<RadientFloat3, 2> Colors = {
        RadientFloat3{0.2f, 0.4f, 0.6f},
        RadientFloat3{0.8f, 1.f, 0.4f},
    };
    const std::array<Float32, 2> Intensities                  = {2.f, 10.f};
    const std::array<Uint8, 2>   NormalizeValues              = {1, 0};
    const std::array<Uint8, 2>   EnableColorTemperatureValues = {0, 1};
    const std::array<Float32, 2> FocalLengths                 = {40.f, 80.f};
    const std::array<Float32, 2> NearClips                    = {0.5f, 1.5f};

    std::array<RadientAnimationSamplerDesc, 6> Samplers{};
    const auto                                 InitializeSampler = [&](RadientAnimationSamplerDesc& Sampler,
                                       RADIENT_ANIMATION_VALUE_TYPE Type,
                                       const void*                  pValues,
                                       Uint64                       ValueDataSize) {
        Sampler.Value.Type      = Type;
        Sampler.Value.ArraySize = 1;
        Sampler.Interpolation   = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
        Sampler.pTimes          = Times.data();
        Sampler.pValues         = pValues;
        Sampler.ValueDataSize   = ValueDataSize;
        Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());
    };
    InitializeSampler(Samplers[0], RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, Colors.data(), sizeof(Colors));
    InitializeSampler(Samplers[1], RADIENT_ANIMATION_VALUE_TYPE_FLOAT, Intensities.data(), sizeof(Intensities));
    InitializeSampler(Samplers[2], RADIENT_ANIMATION_VALUE_TYPE_BOOL, NormalizeValues.data(), sizeof(NormalizeValues));
    Samplers[2].Interpolation = RADIENT_ANIMATION_INTERPOLATION_STEP;
    InitializeSampler(Samplers[3], RADIENT_ANIMATION_VALUE_TYPE_BOOL, EnableColorTemperatureValues.data(), sizeof(EnableColorTemperatureValues));
    Samplers[3].Interpolation = RADIENT_ANIMATION_INTERPOLATION_STEP;
    InitializeSampler(Samplers[4], RADIENT_ANIMATION_VALUE_TYPE_FLOAT, FocalLengths.data(), sizeof(FocalLengths));
    InitializeSampler(Samplers[5], RADIENT_ANIMATION_VALUE_TYPE_FLOAT, NearClips.data(), sizeof(NearClips));

    const std::array Channels = {
        RadientAnimationChannelDesc{0, RadientLightColorProperty, 0, 0},
        RadientAnimationChannelDesc{0, RadientLightIntensityProperty, 0, 1},
        RadientAnimationChannelDesc{0, RadientLightNormalizeProperty, 0, 2},
        RadientAnimationChannelDesc{0, RadientLightEnableColorTemperatureProperty, 0, 3},
        RadientAnimationChannelDesc{1, RadientCameraFocalLengthProperty, 0, 4},
        RadientAnimationChannelDesc{1, RadientCameraNearClipProperty, 0, 5},
    };

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = "Light and camera";
    ClipDesc.Duration     = 2.f;
    ClipDesc.pTargets     = Targets.data();
    ClipDesc.TargetCount  = static_cast<Uint32>(Targets.size());
    ClipDesc.pSamplers    = Samplers.data();
    ClipDesc.SamplerCount = static_cast<Uint32>(Samplers.size());
    ClipDesc.pChannels    = Channels.data();
    ClipDesc.ChannelCount = static_cast<Uint32>(Channels.size());

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pClip, nullptr);

    const std::array Mappings = {
        RadientAnimationDestinationMappingDesc{0, Entities[0]},
        RadientAnimationDestinationMappingDesc{1, Entities[0]},
        RadientAnimationDestinationMappingDesc{0, Entities[1]},
        RadientAnimationDestinationMappingDesc{1, Entities[1]},
    };
    RadientAnimationDestinationDesc Destination{};
    Destination.pDestination = m_pDestination;
    Destination.pMappings    = Mappings.data();
    Destination.MappingCount = static_cast<Uint32>(Mappings.size());
    const RadientAnimationBindingDesc BindingDesc{&Destination, 1};

    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    ASSERT_EQ(pClip->CreateBinding(BindingDesc, pBinding.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);

    const RadientSceneRevisions  RevisionsBefore = m_pScene->GetSceneRevisions();
    RadientAnimationEvaluateInfo EvaluateInfo{};
    EvaluateInfo.Time = 1.f;
    ASSERT_EQ(pBinding->Evaluate(EvaluateInfo), RADIENT_STATUS_OK);

    for (const RadientEntityID Entity : Entities)
    {
        const RadientLightComponent Light = GetLight(Entity);
        ExpectFloat3Near(Light.Color, {0.5f, 0.7f, 0.5f});
        EXPECT_FLOAT_EQ(Light.Intensity, 6.f);
        EXPECT_EQ(Light.Normalize, True);
        EXPECT_EQ(Light.EnableColorTemperature, False);

        RadientCameraComponent Camera{};
        ASSERT_EQ(m_pScene->GetCamera(Entity, Camera), RADIENT_STATUS_OK);
        EXPECT_FLOAT_EQ(Camera.FocalLength, 60.f);
        EXPECT_FLOAT_EQ(Camera.ClippingRange.x, 1.f);
        EXPECT_FLOAT_EQ(Camera.ClippingRange.y, InitialCamera.ClippingRange.y);
    }

    RadientSceneRevisions ExpectedRevisions = RevisionsBefore;
    ++ExpectedRevisions.Lights;
    ++ExpectedRevisions.Cameras;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);

    EvaluateInfo.Time = 2.f;
    ASSERT_EQ(pBinding->Evaluate(EvaluateInfo), RADIENT_STATUS_OK);

    for (const RadientEntityID Entity : Entities)
    {
        const RadientLightComponent Light = GetLight(Entity);
        ExpectFloat3Near(Light.Color, Colors.back());
        EXPECT_FLOAT_EQ(Light.Intensity, Intensities.back());
        EXPECT_EQ(Light.Normalize, False);
        EXPECT_EQ(Light.EnableColorTemperature, True);

        RadientCameraComponent Camera{};
        ASSERT_EQ(m_pScene->GetCamera(Entity, Camera), RADIENT_STATUS_OK);
        EXPECT_FLOAT_EQ(Camera.FocalLength, FocalLengths.back());
        EXPECT_FLOAT_EQ(Camera.ClippingRange.x, NearClips.back());
        EXPECT_FLOAT_EQ(Camera.ClippingRange.y, InitialCamera.ClippingRange.y);
    }

    ++ExpectedRevisions.Lights;
    ++ExpectedRevisions.Cameras;
    EXPECT_EQ(m_pScene->GetSceneRevisions(), ExpectedRevisions);
}

TEST_F(RadientSceneAnimationDestinationTest, ReportsDestroyedBoundEntity)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
        MakeNodeProperty(Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    });
    ASSERT_NE(pBinding, nullptr);

    ASSERT_EQ(m_pWriter->DestroyEntity(Entity), RADIENT_STATUS_OK);
    void* const* pOutputs = nullptr;
    EXPECT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pOutputs, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, DestinationBindingRetainsWriterAndScene)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
        MakeNodeProperty(Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    });
    ASSERT_NE(pBinding, nullptr);

    IRadientScene* const pRawScene = m_pScene;
    m_pDestination.Release();
    m_pWriter.Release();
    m_pScene.Release();

    void* const*        pOutputs    = nullptr;
    const RadientFloat3 Translation = {3.f, 4.f, 5.f};
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    std::memcpy(pOutputs[0], &Translation, sizeof(Translation));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    RadientTransform Transform{};
    ASSERT_EQ(pRawScene->GetLocalTransform(Entity, Transform), RADIENT_STATUS_OK);
    ExpectFloat3Near(Transform.Position, Translation);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsEmptyPropertyRequest)
{
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(nullptr, 0, nullptr, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pBinding, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsNullPropertyArray)
{
    RadientAnimationResolvedPropertyDesc               Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(nullptr, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pBinding, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsNullResolvedPropertyArray)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, nullptr, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pBinding, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsInvalidBindingOutput)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc Resolved{};

    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, nullptr),
              RADIENT_STATUS_INVALID_ARGUMENT);

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({Property});
    ASSERT_NE(pBinding, nullptr);
    IRadientAnimationDestinationBinding* pNonNullOutput = pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, &pNonNullOutput),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pNonNullOutput, pBinding.RawPtr());
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsMalformedPropertyDescriptors)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const std::array Properties = {
        MakeNodeProperty(Entity,
                         RadientNodeTranslationProperty,
                         RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
                         0,
                         1,
                         InvalidRadientAnimationSchemaID),
        MakeNodeProperty(Entity, InvalidRadientAnimationPropertyID, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN),
        MakeNodeProperty(Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 0),
    };

    for (const RadientAnimationPropertyBindingDesc& Property : Properties)
    {
        SCOPED_TRACE(Property.Property);
        RadientAnimationResolvedPropertyDesc               Resolved{};
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pBinding, nullptr);
    }
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsUnsupportedSchema)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        Entity,
        RadientNodeTranslationProperty,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        0,
        1,
        IID_RadientScene);

    RadientAnimationResolvedPropertyDesc Resolved;
    Resolved.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(pBinding, nullptr);
    EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsUnsupportedProperty)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        Entity, UnknownAnimationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc Resolved;
    Resolved.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(pBinding, nullptr);
    EXPECT_EQ(Resolved.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsMalformedSupportedPropertyLayouts)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const std::array Properties = {
        MakeNodeProperty(Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(Entity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(Entity, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 1),
        MakeNodeProperty(Entity, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3, 0, 2),
        MakeNodeProperty(Entity, RadientNodeVisibilityProperty, RADIENT_ANIMATION_VALUE_TYPE_UINT),
        MakeNodeProperty(Entity, RadientNodeVisibilityProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL, 1),
        MakeNodeProperty(Entity, RadientNodeVisibilityProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL, 0, 2),
    };

    for (const RadientAnimationPropertyBindingDesc& Property : Properties)
    {
        SCOPED_TRACE(Property.Property);
        RadientAnimationResolvedPropertyDesc               Resolved{};
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pBinding, nullptr);
    }
}

TEST_F(RadientSceneAnimationDestinationTest, ReturnsUnsupportedWhenNoPropertiesAreAccepted)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);

    const std::array Properties = {
        MakeNodeProperty(Entity,
                         RadientNodeTranslationProperty,
                         RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
                         0,
                         1,
                         IID_RadientScene),
        MakeNodeProperty(Entity, UnknownAnimationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 2> Resolved;
    for (RadientAnimationResolvedPropertyDesc& Result : Resolved)
        Result.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;

    EXPECT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_EQ(pBinding, nullptr);
    for (const RadientAnimationResolvedPropertyDesc& Result : Resolved)
        EXPECT_EQ(Result.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsInvalidDestinationElementBeforeUnsupportedProperty)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        static_cast<RadientEntityID>(InvalidRadientAnimationDestinationElement),
        UnknownAnimationProperty,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
        0,
        1,
        IID_RadientScene);
    RadientAnimationResolvedPropertyDesc               Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pBinding, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsMissingEntity)
{
    const RadientAnimationPropertyBindingDesc Property = MakeNodeProperty(
        InvalidRadientEntityID,
        RadientNodeTranslationProperty,
        RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    RadientAnimationResolvedPropertyDesc               Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(&Property, 1, &Resolved, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pBinding, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsDuplicateEntityProperty)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    struct DuplicateCase
    {
        RadientAnimationSchemaID     Schema;
        RadientAnimationPropertyID   Property;
        RADIENT_ANIMATION_VALUE_TYPE Type;
    };

    const DuplicateCase Cases[] = {
        {RadientNodeAnimationSchemaID, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4},
        {RadientNodeAnimationSchemaID, RadientNodeVisibilityProperty, RADIENT_ANIMATION_VALUE_TYPE_BOOL},
        {RadientLightAnimationSchemaID, RadientLightIntensityProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
        {RadientCameraAnimationSchemaID, RadientCameraFocalLengthProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT},
    };
    ASSERT_EQ(m_pWriter->SetLight(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(m_pWriter->SetCamera(Entity, {}), RADIENT_STATUS_OK);

    for (const DuplicateCase& Case : Cases)
    {
        SCOPED_TRACE(Case.Property);
        const std::array Properties = {
            MakeProperty(Entity, Case.Schema, Case.Property, Case.Type),
            MakeProperty(Entity, Case.Schema, Case.Property, Case.Type),
        };
        std::array<RadientAnimationResolvedPropertyDesc, 2> Resolved{};
        RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      Properties.data(),
                      static_cast<Uint32>(Properties.size()),
                      Resolved.data(),
                      pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(pBinding, nullptr);
    }
}

TEST_F(RadientSceneAnimationDestinationTest, UnsupportedPropertyDoesNotMaskDuplicateEntityProperty)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    const std::array Properties = {
        MakeNodeProperty(Entity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(Entity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(Entity, UnknownAnimationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 3> Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pBinding, nullptr);
}

TEST_F(RadientSceneAnimationDestinationTest, RejectsNullBeginUpdateOutput)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateBinding({
        MakeNodeProperty(Entity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    });
    ASSERT_NE(pBinding, nullptr);

    const RadientSceneRevisions RevisionsBefore = m_pScene->GetSceneRevisions();
    RadientTransform            TransformBefore{};
    ASSERT_EQ(m_pScene->GetLocalTransform(Entity, TransformBefore), RADIENT_STATUS_OK);
    EXPECT_EQ(pBinding->BeginUpdate(nullptr), RADIENT_STATUS_INVALID_ARGUMENT);

    RadientTransform TransformAfter{};
    ASSERT_EQ(m_pScene->GetLocalTransform(Entity, TransformAfter), RADIENT_STATUS_OK);
    EXPECT_EQ(TransformAfter, TransformBefore);
    EXPECT_EQ(m_pScene->GetSceneRevisions(), RevisionsBefore);
}

} // namespace
