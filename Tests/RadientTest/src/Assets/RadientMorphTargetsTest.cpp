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
#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientMorphTargetData.hpp"
#include "Assets/RadientMorphTargetSource.hpp"
#include "Scene/Components/RadientMorphComponentStorage.hpp"
#include "Scene/RadientSceneState.hpp"
#include "RadientAnimation.h"
#include "ThreadPool.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>

using namespace Diligent;

namespace
{

struct MorphMeshData
{
    std::array<RadientFloat3, 3> Positions{};
    std::array<Uint32, 3>        Indices{0, 1, 2};
    std::array<Float32, 9>       PositionDeltas{
        0.f, 0.f, 0.f,
        0.1f, 0.2f, 0.3f,
        -0.1f, -0.2f, -0.3f};
    std::array<Float32, 6> CustomDeltas{
        1.f, 2.f,
        3.f, 4.f,
        5.f, 6.f};
    std::array<RadientMorphTargetAttributeDesc, 2>       Attributes{};
    std::array<RadientMorphTargetAttributeCreateInfo, 2> AttributeData{};
    std::array<RadientMorphTargetCreateInfo, 2>          Targets{};
    RadientMeshPrimitiveCreateInfo                       Primitive{};

    MorphMeshData()
    {
        Attributes[0].Semantic       = RadientMorphTargetPositionSemantic;
        Attributes[0].ComponentCount = 3;
        Attributes[1].Semantic       = "CUSTOM";
        Attributes[1].ComponentCount = 2;
        AttributeData[0].pDeltas     = PositionDeltas.data();
        AttributeData[1].pDeltas     = CustomDeltas.data();

        Targets[0].Desc.Name           = "Smile";
        Targets[0].Desc.pAttributes    = Attributes.data();
        Targets[0].Desc.AttributeCount = static_cast<Uint32>(Attributes.size());
        Targets[0].Desc.DefaultWeight  = 0.25f;
        Targets[0].pAttributeData      = AttributeData.data();
        Targets[1].Desc.Name           = nullptr;
        Targets[1].Desc.DefaultWeight  = -0.5f;

        Primitive.IndexCount = static_cast<Uint32>(Indices.size());
    }

    RadientMeshCreateInfo MakeCreateInfo() const
    {
        RadientMeshCreateInfo CI{};
        CI.pPositions       = Positions.data();
        CI.VertexCount      = static_cast<Uint32>(Positions.size());
        CI.pMorphTargets    = Targets.data();
        CI.MorphTargetCount = static_cast<Uint32>(Targets.size());
        CI.pIndices         = Indices.data();
        CI.IndexCount       = static_cast<Uint32>(Indices.size());
        CI.IndexType        = RADIENT_INDEX_TYPE_UINT32;
        CI.pPrimitives      = &Primitive;
        CI.PrimitiveCount   = 1;
        return CI;
    }
};

RefCntAutoPtr<IRadientMeshAsset> CreateMesh(const RadientMeshCreateInfo& CI)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{1});
    EXPECT_NE(pThreadPool, nullptr);
    if (!pThreadPool)
        return {};

    RadientMeshAssetManagerSharedPtr pManager = RadientMeshAssetManager::Create({});
    EXPECT_NE(pManager, nullptr);
    if (!pManager)
        return {};

    RefCntAutoPtr<IRadientMeshAsset> pMesh;
    const RADIENT_STATUS             Status = pManager->CreateMesh(*pThreadPool, CI, pMesh.GetAddressOfEmpty());
    EXPECT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_PENDING);
    EXPECT_NE(pMesh, nullptr);
    pThreadPool->WaitForAllTasks();
    pThreadPool->StopThreads();
    return pMesh;
}

RefCntAutoPtr<IRadientMorphTargetWeights> CreateWeights(IRadientMeshAsset& Mesh)
{
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    EXPECT_EQ(Mesh.CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    EXPECT_NE(pWeights, nullptr);
    return pWeights;
}

RadientAnimationPropertyBindingDesc MakeMorphWeightProperty(
    Uint32                             FirstArrayElement,
    Uint32                             ArraySize,
    RadientAnimationDestinationElement DestinationElement = 0,
    RadientAnimationSchemaID           Schema             = RadientMorphWeightsAnimationSchemaID,
    RadientAnimationPropertyID         Property           = RadientMorphWeightsProperty,
    RADIENT_ANIMATION_VALUE_TYPE       Type               = RADIENT_ANIMATION_VALUE_TYPE_FLOAT)
{
    RadientAnimationPropertyBindingDesc Desc;
    Desc.Schema             = Schema;
    Desc.DestinationElement = DestinationElement;
    Desc.Property           = Property;
    Desc.FirstArrayElement  = FirstArrayElement;
    Desc.Value.Type         = Type;
    Desc.Value.ArraySize    = ArraySize;
    return Desc;
}

class RadientMorphTargetWeightsAnimationDestinationTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_pMesh = CreateMesh(m_Source.MakeCreateInfo());
        ASSERT_NE(m_pMesh, nullptr);
        m_pWeights = CreateWeights(*m_pMesh);
        ASSERT_NE(m_pWeights, nullptr);
        m_pWeights->QueryInterface(
            IID_RadientAnimationDestination,
            m_pDestination.GetAddressOfEmpty());
        ASSERT_NE(m_pDestination, nullptr);
    }

    RefCntAutoPtr<IRadientAnimationDestinationBinding> CreateDestinationBinding(
        const RadientAnimationPropertyBindingDesc* pProperties,
        Uint32                                     PropertyCount,
        RadientAnimationResolvedPropertyDesc*      pResolvedProperties)
    {
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      pProperties,
                      PropertyCount,
                      pResolvedProperties,
                      pBinding.GetAddressOfEmpty()),
                  RADIENT_STATUS_OK);
        return pBinding;
    }

protected:
    MorphMeshData                               m_Source;
    RefCntAutoPtr<IRadientMeshAsset>            m_pMesh;
    RefCntAutoPtr<IRadientMorphTargetWeights>   m_pWeights;
    RefCntAutoPtr<IRadientAnimationDestination> m_pDestination;
};

} // namespace

TEST(RadientMorphTargetsTest, DataCopiesSourceMetadataAndAttributeLayout)
{
    MorphMeshData            Source;
    RadientMorphTargetSource MorphSource{Source.Targets.data(),
                                         static_cast<Uint32>(Source.Targets.size()),
                                         static_cast<Uint32>(Source.Positions.size())};
    ASSERT_EQ(MorphSource.GetStatus(), RADIENT_STATUS_OK);

    const RadientMorphTargetData Data{MorphSource};
    const RadientMeshAssetDesc&  Desc = Data.GetDesc();
    ASSERT_EQ(Desc.MorphTargetCount, 2u);
    ASSERT_NE(Desc.pMorphTargets, nullptr);
    EXPECT_STREQ(Desc.pMorphTargets[0].Name, "Smile");
    EXPECT_FLOAT_EQ(Desc.pMorphTargets[0].DefaultWeight, 0.25f);
    EXPECT_STREQ(Desc.pMorphTargets[1].Name, "");
    EXPECT_FLOAT_EQ(Desc.pMorphTargets[1].DefaultWeight, -0.5f);

    const RadientMorphTargetDesc& Target = Desc.pMorphTargets[0];
    ASSERT_EQ(Target.AttributeCount, 2u);
    ASSERT_NE(Target.pAttributes, nullptr);
    EXPECT_STREQ(Target.pAttributes[0].Semantic, RadientMorphTargetPositionSemantic);
    EXPECT_STREQ(Target.pAttributes[1].Semantic, "CUSTOM");
    EXPECT_EQ(Target.pAttributes[0].ComponentCount, 3u);
    EXPECT_EQ(Target.pAttributes[1].ComponentCount, 2u);
    EXPECT_EQ(Data.GetVertexCount(), Source.Positions.size());
    EXPECT_EQ(Data.GetAttributeDataOffset(0, 0), 0u);
    EXPECT_EQ(Data.GetAttributeDataOffset(0, 1), sizeof(Source.PositionDeltas));
    EXPECT_EQ(Data.GetDataSize(), sizeof(Source.PositionDeltas) + sizeof(Source.CustomDeltas));
}

TEST(RadientMorphTargetsTest, MeshCreatesMutableWeightsFromDefaults)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    const RadientMeshAssetDesc& Desc = pMesh->GetDesc();
    ASSERT_EQ(Desc.MorphTargetCount, 2u);
    ASSERT_NE(Desc.pMorphTargets, nullptr);
    EXPECT_STREQ(Desc.pMorphTargets[0].Name, "Smile");
    EXPECT_STREQ(Desc.pMorphTargets[1].Name, "");

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWeights, nullptr);
    EXPECT_EQ(pWeights->GetMesh(), pMesh.RawPtr());
    EXPECT_EQ(pWeights->GetVersion(), 1u);
    ASSERT_EQ(pWeights->GetWeightCount(), 2u);
    ASSERT_NE(pWeights->GetWeights(), nullptr);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], -0.5f);
}

TEST(RadientMorphTargetsTest, WeightsRetainTheirMesh)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    IRadientMeshAsset* const pExpectedMesh = pMesh;
    pMesh.Release();

    ASSERT_EQ(pWeights->GetMesh(), pExpectedMesh);
    EXPECT_EQ(pWeights->GetMesh()->GetDesc().MorphTargetCount, 2u);
}

TEST(RadientMorphTargetsTest, WeightUpdatesAdvanceVersionAndPreserveOtherValues)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const Float32 NewWeight = 1.5f;
    EXPECT_EQ(pWeights->SetWeights(1, 1, &NewWeight), RADIENT_STATUS_OK);
    EXPECT_EQ(pWeights->GetVersion(), 2u);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], 1.5f);

    EXPECT_EQ(pWeights->SetWeights(1, 1, &NewWeight), RADIENT_STATUS_OK);
    EXPECT_EQ(pWeights->GetVersion(), 3u);
}

TEST(RadientMorphTargetsTest, WeightUpdatesValidateRangesWithoutChangingState)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const Float32 Weight = 1.f;
    EXPECT_EQ(pWeights->SetWeights(3, 0, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWeights->SetWeights(1, 2, &Weight), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWeights->SetWeights(0, 1, nullptr), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(pWeights->GetVersion(), 1u);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], -0.5f);

    EXPECT_EQ(pWeights->SetWeights(2, 0, nullptr), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWeights->GetVersion(), 1u);
}

TEST(RadientMorphTargetsTest, ResetRestoresDefaultsAndAdvancesVersion)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array<Float32, 2> Values{-2.f, 3.f};
    ASSERT_EQ(pWeights->SetWeights(0, 2, Values.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(pWeights->ResetToDefaults(), RADIENT_STATUS_OK);
    EXPECT_EQ(pWeights->GetVersion(), 3u);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(pWeights->GetWeights()[1], -0.5f);
}

TEST(RadientMorphTargetsTest, MeshWithoutTargetsCreatesEmptyWeights)
{
    MorphMeshData         Source;
    RadientMeshCreateInfo CI               = Source.MakeCreateInfo();
    CI.pMorphTargets                       = nullptr;
    CI.MorphTargetCount                    = 0;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(CI);
    ASSERT_NE(pMesh, nullptr);
    EXPECT_EQ(pMesh->GetDesc().MorphTargetCount, 0u);
    EXPECT_EQ(pMesh->GetDesc().pMorphTargets, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights;
    ASSERT_EQ(pMesh->CreateMorphTargetWeights(pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pWeights, nullptr);
    EXPECT_EQ(pWeights->GetWeightCount(), 0u);
    EXPECT_EQ(pWeights->GetWeights(), nullptr);
    EXPECT_EQ(pWeights->SetWeights(0, 0, nullptr), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWeights->ResetToDefaults(), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(pWeights->GetVersion(), 1u);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, WeightsExposeAnimationDestination)
{
    RefCntAutoPtr<IObject> pWeightsIdentity;
    RefCntAutoPtr<IObject> pDestinationIdentity;
    m_pWeights->QueryInterface(IID_Unknown, pWeightsIdentity.GetAddressOfEmpty());
    m_pDestination->QueryInterface(IID_Unknown, pDestinationIdentity.GetAddressOfEmpty());
    ASSERT_NE(pWeightsIdentity, nullptr);
    ASSERT_NE(pDestinationIdentity, nullptr);
    EXPECT_EQ(pWeightsIdentity, pDestinationIdentity);

    RefCntAutoPtr<IRadientMorphTargetWeights> pRoundTripWeights;
    m_pDestination->QueryInterface(
        IID_RadientMorphTargetWeights,
        pRoundTripWeights.GetAddressOfEmpty());
    EXPECT_EQ(pRoundTripWeights, m_pWeights);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, ResolvesAndUpdatesDisjointWeightRangesInOneBatch)
{
    const std::array Properties = {
        MakeMorphWeightProperty(1, 1),
        MakeMorphWeightProperty(0, 1),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 2> Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding = CreateDestinationBinding(
        Properties.data(),
        static_cast<Uint32>(Properties.size()),
        Resolved.data());
    ASSERT_NE(pBinding, nullptr);
    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);

    const Uint64 InitialVersion = m_pWeights->GetVersion();
    void* const* pOutputs       = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    ASSERT_NE(pOutputs[1], nullptr);

    *static_cast<Float32*>(pOutputs[0]) = 0.75f;
    *static_cast<Float32*>(pOutputs[1]) = -0.25f;
    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

    EXPECT_EQ(m_pWeights->GetVersion(), InitialVersion + 1);
    EXPECT_FLOAT_EQ(m_pWeights->GetWeights()[0], -0.25f);
    EXPECT_FLOAT_EQ(m_pWeights->GetWeights()[1], 0.75f);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, IgnoresUnsupportedPropertiesAndCompactsAcceptedOutputs)
{
    static constexpr RadientAnimationSchemaID UnsupportedSchema =
        {0x565498d2, 0xfc54, 0x4b0a, {0xa5, 0xe7, 0x21, 0x1b, 0x30, 0xdf, 0x8b, 0xc6}};
    const std::array Properties = {
        MakeMorphWeightProperty(1, 1),
        MakeMorphWeightProperty(0, 1, 0, UnsupportedSchema),
        MakeMorphWeightProperty(0, 1),
        MakeMorphWeightProperty(0, 1, 0, RadientMorphWeightsAnimationSchemaID, 2),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 4> Resolved;
    for (RadientAnimationResolvedPropertyDesc& Property : Resolved)
        Property.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;

    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(
        Properties.data(),
        static_cast<Uint32>(Properties.size()),
        Resolved.data());
    ASSERT_NE(pBinding, nullptr);

    EXPECT_EQ(Resolved[0].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[1].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
    EXPECT_EQ(Resolved[2].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE);
    EXPECT_EQ(Resolved[3].Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);

    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    ASSERT_NE(pOutputs[0], nullptr);
    ASSERT_NE(pOutputs[1], nullptr);

    *static_cast<Float32*>(pOutputs[0]) = 0.75f;
    *static_cast<Float32*>(pOutputs[1]) = -0.25f;
    ASSERT_EQ(pBinding->EndUpdate(False), RADIENT_STATUS_OK);

    EXPECT_FLOAT_EQ(m_pWeights->GetWeights()[0], -0.25f);
    EXPECT_FLOAT_EQ(m_pWeights->GetWeights()[1], 0.75f);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, ReturnsUnsupportedWhenNoPropertiesAreAccepted)
{
    static constexpr RadientAnimationSchemaID UnsupportedSchema =
        {0x565498d2, 0xfc54, 0x4b0a, {0xa5, 0xe7, 0x21, 0x1b, 0x30, 0xdf, 0x8b, 0xc6}};
    const std::array Properties = {
        MakeMorphWeightProperty(0, 1, 0, UnsupportedSchema),
        MakeMorphWeightProperty(0, 1, 0, RadientMorphWeightsAnimationSchemaID, 2),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 2> Resolved;
    for (RadientAnimationResolvedPropertyDesc& Property : Resolved)
        Property.Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COUNT;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;

    EXPECT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_UNSUPPORTED);
    EXPECT_FALSE(pBinding);
    for (const RadientAnimationResolvedPropertyDesc& Property : Resolved)
        EXPECT_EQ(Property.Semantic, RADIENT_ANIMATION_VALUE_SEMANTIC_UNKNOWN);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, RejectsInvalidDestinationElementBeforeFilteringUnsupportedProperty)
{
    static constexpr RadientAnimationSchemaID UnsupportedSchema =
        {0x565498d2, 0xfc54, 0x4b0a, {0xa5, 0xe7, 0x21, 0x1b, 0x30, 0xdf, 0x8b, 0xc6}};
    const RadientAnimationPropertyBindingDesc Property = MakeMorphWeightProperty(
        0,
        1,
        InvalidRadientAnimationDestinationElement,
        UnsupportedSchema,
        2);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;

    EXPECT_EQ(m_pDestination->CreateBinding(
                  &Property,
                  1,
                  &Resolved,
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, RejectsOverlappingWeightRanges)
{
    const std::array Properties = {
        MakeMorphWeightProperty(0, 2),
        MakeMorphWeightProperty(1, 1),
    };
    std::array<RadientAnimationResolvedPropertyDesc, 2> Resolved{};
    RefCntAutoPtr<IRadientAnimationDestinationBinding>  pBinding;
    EXPECT_EQ(m_pDestination->CreateBinding(
                  Properties.data(),
                  static_cast<Uint32>(Properties.size()),
                  Resolved.data(),
                  pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_FALSE(pBinding);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, RejectsMalformedWeightPropertyLayouts)
{
    struct Case
    {
        RadientAnimationPropertyBindingDesc Property;
        RADIENT_STATUS                      ExpectedStatus;
    };

    static constexpr RadientAnimationSchemaID UnsupportedSchema =
        {0x565498d2, 0xfc54, 0x4b0a, {0xa5, 0xe7, 0x21, 0x1b, 0x30, 0xdf, 0x8b, 0xc6}};
    const std::array Cases = {
        Case{MakeMorphWeightProperty(0, 0, 0, UnsupportedSchema), RADIENT_STATUS_INVALID_ARGUMENT},
        Case{MakeMorphWeightProperty(0, 1, 0, RadientMorphWeightsAnimationSchemaID, RadientMorphWeightsProperty, RADIENT_ANIMATION_VALUE_TYPE_FLOAT2), RADIENT_STATUS_INVALID_ARGUMENT},
        Case{MakeMorphWeightProperty(0, 0), RADIENT_STATUS_INVALID_ARGUMENT},
        Case{MakeMorphWeightProperty(1, 2), RADIENT_STATUS_INVALID_ARGUMENT},
        Case{MakeMorphWeightProperty(0, 1, 1), RADIENT_STATUS_NOT_FOUND},
    };

    for (const Case& TestCase : Cases)
    {
        SCOPED_TRACE(TestCase.ExpectedStatus);
        RadientAnimationResolvedPropertyDesc               Resolved;
        RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding;
        EXPECT_EQ(m_pDestination->CreateBinding(
                      &TestCase.Property,
                      1,
                      &Resolved,
                      pBinding.GetAddressOfEmpty()),
                  TestCase.ExpectedStatus);
        EXPECT_FALSE(pBinding);
    }
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, GenericBindingSamplesDirectlyIntoWeights)
{
    constexpr RadientAnimationObjectID SourceNode = 17;
    RadientAnimationTargetDesc         Target;
    Target.Schema           = RadientMorphWeightsAnimationSchemaID;
    Target.Object           = SourceNode;
    const std::array Times  = {0.f, 1.f};
    const std::array Values = {
        0.f,
        1.f,
        1.f,
        -1.f,
    };
    RadientAnimationSamplerDesc Sampler;
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
    Sampler.Value.ArraySize = 2;
    Sampler.Interpolation   = RADIENT_ANIMATION_INTERPOLATION_LINEAR;
    Sampler.pTimes          = Times.data();
    Sampler.pValues         = Values.data();
    Sampler.ValueDataSize   = sizeof(Values);
    Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

    RadientAnimationChannelDesc Channel;
    Channel.TargetIndex  = 0;
    Channel.Property     = RadientMorphWeightsProperty;
    Channel.SamplerIndex = 0;

    RadientAnimationClipDesc ClipDesc;
    ClipDesc.Name         = "Morph-weight destination test";
    ClipDesc.Duration     = 1.f;
    ClipDesc.pTargets     = &Target;
    ClipDesc.TargetCount  = 1;
    ClipDesc.pSamplers    = &Sampler;
    ClipDesc.SamplerCount = 1;
    ClipDesc.pChannels    = &Channel;
    ClipDesc.ChannelCount = 1;

    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pClip, nullptr);

    RadientAnimationDestinationMappingDesc Mapping;
    Mapping.ClipTargetIndex    = 0;
    Mapping.DestinationElement = 0;
    RadientAnimationDestinationDesc Destination;
    Destination.pDestination = m_pDestination;
    Destination.pMappings    = &Mapping;
    Destination.MappingCount = 1;
    RadientAnimationBindingDesc BindingDesc;
    BindingDesc.pDestinations    = &Destination;
    BindingDesc.DestinationCount = 1;

    RefCntAutoPtr<IRadientAnimationBinding> pBinding;
    ASSERT_EQ(pClip->CreateBinding(BindingDesc, pBinding.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);
    ASSERT_NE(pBinding, nullptr);

    const Uint64                 InitialVersion = m_pWeights->GetVersion();
    RadientAnimationEvaluateInfo Info;
    Info.Time               = 0.25f;
    Info.UpdateDerivedState = False;
    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pWeights->GetVersion(), InitialVersion + 1);
    EXPECT_FLOAT_EQ(m_pWeights->GetWeights()[0], 0.25f);
    EXPECT_FLOAT_EQ(m_pWeights->GetWeights()[1], 0.5f);

    ASSERT_EQ(pBinding->Evaluate(Info), RADIENT_STATUS_OK);
    EXPECT_EQ(m_pWeights->GetVersion(), InitialVersion + 2);
}

TEST_F(RadientMorphTargetWeightsAnimationDestinationTest, DestinationBindingRetainsWeights)
{
    const RadientAnimationPropertyBindingDesc          Property = MakeMorphWeightProperty(0, 2);
    RadientAnimationResolvedPropertyDesc               Resolved;
    RefCntAutoPtr<IRadientAnimationDestinationBinding> pBinding = CreateDestinationBinding(
        &Property,
        1,
        &Resolved);
    ASSERT_NE(pBinding, nullptr);

    IRadientMorphTargetWeights* const pRawWeights = m_pWeights;
    m_pDestination.Release();
    m_pWeights.Release();
    m_pMesh.Release();

    void* const* pOutputs = nullptr;
    ASSERT_EQ(pBinding->BeginUpdate(&pOutputs), RADIENT_STATUS_OK);
    ASSERT_NE(pOutputs, nullptr);
    const std::array Values = {2.f, 3.f};
    std::memcpy(pOutputs[0], Values.data(), sizeof(Values));
    ASSERT_EQ(pBinding->EndUpdate(True), RADIENT_STATUS_OK);

    ASSERT_EQ(pRawWeights->GetWeightCount(), 2u);
    EXPECT_FLOAT_EQ(pRawWeights->GetWeights()[0], 2.f);
    EXPECT_FLOAT_EQ(pRawWeights->GetWeights()[1], 3.f);
    EXPECT_EQ(pRawWeights->GetVersion(), 2u);
}

TEST(RadientMorphTargetsTest, MorphComponentStorageRetainsWeightsAndRepairsPointerAfterMove)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);

    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);
    IRadientMorphTargetWeights* const pExpectedWeights = pWeights;

    MorphComponentStorage Storage;
    Storage.Assign({pWeights});
    pWeights.Release();
    pMesh.Release();

    ASSERT_EQ(Storage.Component.pWeights, pExpectedWeights);
    EXPECT_EQ(Storage.Component.pWeights->GetWeightCount(), 2u);

    MorphComponentStorage MoveConstructed{std::move(Storage)};
    EXPECT_EQ(Storage.Component.pWeights, nullptr);
    EXPECT_EQ(MoveConstructed.Component.pWeights, pExpectedWeights);

    MorphComponentStorage MoveAssigned;
    MoveAssigned = std::move(MoveConstructed);
    EXPECT_EQ(MoveConstructed.Component.pWeights, nullptr);
    EXPECT_EQ(MoveAssigned.Component.pWeights, pExpectedWeights);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRejectsUnknownEntity)
{
    RadientSceneState State;
    EXPECT_EQ(State.SetMorph(InvalidRadientEntityID, {}), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRejectsNullWeights)
{
    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);

    const RadientSceneRevisions Revisions = State.GetSceneRevisions();
    EXPECT_EQ(State.SetMorph(Entity, {}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRequiresMesh)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    EXPECT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_INVALID_OPERATION);
}

TEST(RadientMorphTargetsTest, SceneSetMorphRejectsWeightsForDifferentMesh)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh      = CreateMesh(Source.MakeCreateInfo());
    RefCntAutoPtr<IRadientMeshAsset> pOtherMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    ASSERT_NE(pOtherMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pOtherWeights = CreateWeights(*pOtherMesh);
    ASSERT_NE(pOtherWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);

    const RadientSceneRevisions Revisions = State.GetSceneRevisions();
    EXPECT_EQ(State.SetMorph(Entity, {pOtherWeights}), RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(State.GetSceneRevisions(), Revisions);

    Bool HasMorph = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
}

TEST(RadientMorphTargetsTest, SceneStoresMorphAndReportsDrawableChanges)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);
    IRadientMorphTargetWeights* const pExpectedWeights = pWeights;

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    State.ClearRenderableMeshChanges();

    RadientMorphComponent StoredMorph{pWeights};
    EXPECT_EQ(State.GetMorph(InvalidRadientEntityID, StoredMorph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredMorph, RadientMorphComponent{});
    StoredMorph = {pWeights};
    EXPECT_EQ(State.GetMorph(Entity, StoredMorph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredMorph, RadientMorphComponent{});

    const RadientSceneRevisions RevisionsBeforeMorph = State.GetSceneRevisions();
    ASSERT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_OK);
    EXPECT_EQ(State.GetSceneRevisions().Drawables, RevisionsBeforeMorph.Drawables + 1);

    pWeights.Release();
    pMesh.Release();

    Bool HasMorph = False;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, True);
    EXPECT_EQ(State.GetMorph(Entity, StoredMorph), RADIENT_STATUS_OK);
    ASSERT_EQ(StoredMorph.pWeights, pExpectedWeights);
    EXPECT_EQ(StoredMorph.pWeights->GetWeightCount(), 2u);

    Uint32 UpdatedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++UpdatedCount;
            EXPECT_EQ(Change.Entity, Entity);
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            ASSERT_NE(pRenderable->pMorph, nullptr);
            EXPECT_EQ(pRenderable->pMorph->pWeights, pExpectedWeights);
        });
    EXPECT_EQ(UpdatedCount, 1u);
    State.ClearRenderableMeshChanges();

    const RadientSceneRevisions RevisionsBeforeNoChange = State.GetSceneRevisions();
    EXPECT_EQ(State.SetMorph(Entity, StoredMorph), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(State.GetSceneRevisions(), RevisionsBeforeNoChange);
    Uint32 NoChangeCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&NoChangeCount](const RadientSceneState::RenderableMeshChange&,
                         const RadientSceneState::RenderableMesh*) {
            ++NoChangeCount;
        });
    EXPECT_EQ(NoChangeCount, 0u);

    ASSERT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH), RADIENT_STATUS_OK);
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
    EXPECT_EQ(State.GetMorph(Entity, StoredMorph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(StoredMorph, RadientMorphComponent{});

    Uint32 RemovedMorphCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++RemovedMorphCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            EXPECT_EQ(pRenderable->pMorph, nullptr);
        });
    EXPECT_EQ(RemovedMorphCount, 1u);
    EXPECT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH), RADIENT_STATUS_NO_CHANGE);
}

TEST(RadientMorphTargetsTest, ReplacingMeshRemovesIncompatibleMorph)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh      = CreateMesh(Source.MakeCreateInfo());
    RefCntAutoPtr<IRadientMeshAsset> pOtherMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    ASSERT_NE(pOtherMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_OK);
    State.ClearRenderableMeshChanges();

    ASSERT_EQ(State.SetMesh(Entity, {pOtherMesh}), RADIENT_STATUS_OK);

    Bool HasMorph = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
    RadientMorphComponent Morph{pWeights};
    EXPECT_EQ(State.GetMorph(Entity, Morph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Morph, RadientMorphComponent{});

    Uint32 UpdatedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++UpdatedCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Updated);
            ASSERT_NE(pRenderable, nullptr);
            EXPECT_EQ(pRenderable->Mesh.pMesh, pOtherMesh.RawPtr());
            EXPECT_EQ(pRenderable->pMorph, nullptr);
        });
    EXPECT_EQ(UpdatedCount, 1u);
}

TEST(RadientMorphTargetsTest, RemovingMeshRemovesMorph)
{
    MorphMeshData                    Source;
    RefCntAutoPtr<IRadientMeshAsset> pMesh = CreateMesh(Source.MakeCreateInfo());
    ASSERT_NE(pMesh, nullptr);
    RefCntAutoPtr<IRadientMorphTargetWeights> pWeights = CreateWeights(*pMesh);
    ASSERT_NE(pWeights, nullptr);

    RadientSceneState State;
    RadientEntityID   Entity = InvalidRadientEntityID;
    ASSERT_EQ(State.CreateEntity({}, Entity), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMesh(Entity, {pMesh}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMeshRenderer(Entity, {}), RADIENT_STATUS_OK);
    ASSERT_EQ(State.SetMorph(Entity, {pWeights}), RADIENT_STATUS_OK);
    State.ClearRenderableMeshChanges();

    ASSERT_EQ(State.RemoveComponent(Entity, RADIENT_COMPONENT_TYPE_MESH), RADIENT_STATUS_OK);

    Bool HasMorph = True;
    EXPECT_EQ(State.HasComponent(Entity, RADIENT_COMPONENT_TYPE_MORPH, HasMorph), RADIENT_STATUS_OK);
    EXPECT_EQ(HasMorph, False);
    RadientMorphComponent Morph{pWeights};
    EXPECT_EQ(State.GetMorph(Entity, Morph), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(Morph, RadientMorphComponent{});

    Uint32 RemovedCount = 0;
    State.EnumerateRenderableMeshChanges(
        [&](const RadientSceneState::RenderableMeshChange& Change,
            const RadientSceneState::RenderableMesh*       pRenderable) {
            ++RemovedCount;
            EXPECT_EQ(Change.Type, RadientSceneState::RenderableMeshChangeType::Removed);
            EXPECT_EQ(pRenderable, nullptr);
        });
    EXPECT_EQ(RemovedCount, 1u);
}
