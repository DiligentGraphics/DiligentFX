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
#include "RefCntAutoPtr.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

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
        RadientAnimationPropertyBindingDesc Desc{};
        Desc.Schema             = Schema;
        Desc.DestinationElement = Entity;
        Desc.Property           = Property;
        Desc.FirstArrayElement  = FirstArrayElement;
        Desc.Value.Type         = Type;
        Desc.Value.ArraySize    = ArraySize;
        return Desc;
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
    ASSERT_EQ(m_pWriter->CommitChanges(), RADIENT_STATUS_OK);

    const std::array Properties = {
        MakeNodeProperty(FirstEntity, RadientNodeScaleProperty + 1, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(SecondEntity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(SecondEntity,
                         RadientNodeTranslationProperty,
                         RADIENT_ANIMATION_VALUE_TYPE_FLOAT3,
                         0,
                         1,
                         IID_RadientScene),
        MakeNodeProperty(ThirdEntity, RadientNodeTranslationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
        MakeNodeProperty(FirstEntity, RadientNodeScaleProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 5> Resolved;
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

    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    ASSERT_NE(pOutputs[1], nullptr);
    ASSERT_NE(pOutputs[2], nullptr);

    const RadientQuaternion Rotation    = {0.f, 0.f, 1.f, 0.f};
    const RadientFloat3     Translation = {10.f, 20.f, 30.f};
    const RadientFloat3     Scale       = {4.f, 5.f, 6.f};
    std::memcpy(pOutputs[0], &Rotation, sizeof(Rotation));
    std::memcpy(pOutputs[1], &Translation, sizeof(Translation));
    std::memcpy(pOutputs[2], &Scale, sizeof(Scale));
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
        Entity, RadientNodeScaleProperty + 1, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
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
        MakeNodeProperty(Entity, RadientNodeScaleProperty + 1, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
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
        RadientNodeScaleProperty + 1,
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

TEST_F(RadientSceneAnimationDestinationTest, RejectsDuplicateEntityField)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    const std::array Properties = {
        MakeNodeProperty(Entity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(Entity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
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

TEST_F(RadientSceneAnimationDestinationTest, UnsupportedPropertyDoesNotMaskDuplicateEntityField)
{
    const RadientEntityID Entity = CreateEntity({});
    ASSERT_NE(Entity, InvalidRadientEntityID);
    const std::array Properties = {
        MakeNodeProperty(Entity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(Entity, RadientNodeRotationProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4),
        MakeNodeProperty(Entity, RadientNodeScaleProperty + 1, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3),
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
