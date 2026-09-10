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

#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientMorphTargetData.hpp"
#include "Assets/RadientMorphTargetSource.hpp"
#include "Render/RadientDrawableMesh.hpp"
#include "Render/Tessera/RadientTesseraMorphData.hpp"
#include "ThreadPool.hpp"
#include "TestingEnvironment.hpp"

#include "gtest/gtest.h"

#include <array>
#include <memory>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

constexpr Uint32 TargetCount = 5;
constexpr Uint32 VertexCount = 3;

struct MorphMeshData
{
    std::array<RadientFloat3, VertexCount> Positions{};
    std::array<Uint32, VertexCount>        Indices{0, 1, 2};
    std::array<Float32, VertexCount * 3>   Deltas{};

    std::array<std::array<RadientMorphTargetAttributeDesc, 3>, TargetCount>       Attributes{};
    std::array<std::array<RadientMorphTargetAttributeCreateInfo, 3>, TargetCount> AttributeData{};
    std::array<RadientMorphTargetCreateInfo, TargetCount>                         Targets{};
    RadientMeshPrimitiveCreateInfo                                                Primitive{};

    MorphMeshData()
    {
        const std::array<std::array<const Char*, 3>, TargetCount> Semantics{{
            {RadientMorphTargetPositionSemantic, nullptr, nullptr},
            {RadientMorphTargetPositionSemantic, RadientMorphTargetNormalSemantic, nullptr},
            {RadientMorphTargetPositionSemantic, RadientMorphTargetTangentSemantic, nullptr},
            {RadientMorphTargetPositionSemantic, RadientMorphTargetNormalSemantic, RadientMorphTargetTangentSemantic},
            {RadientMorphTargetNormalSemantic, nullptr, nullptr},
        }};
        const std::array<Uint32, TargetCount>                     AttributeCounts{1, 2, 2, 3, 1};

        for (Uint32 TargetIndex = 0; TargetIndex < TargetCount; ++TargetIndex)
        {
            for (Uint32 AttributeIndex = 0; AttributeIndex < AttributeCounts[TargetIndex]; ++AttributeIndex)
            {
                Attributes[TargetIndex][AttributeIndex].Semantic       = Semantics[TargetIndex][AttributeIndex];
                Attributes[TargetIndex][AttributeIndex].ComponentCount = 3;
                AttributeData[TargetIndex][AttributeIndex].pDeltas     = Deltas.data();
            }

            Targets[TargetIndex].Desc.pAttributes    = Attributes[TargetIndex].data();
            Targets[TargetIndex].Desc.AttributeCount = AttributeCounts[TargetIndex];
            Targets[TargetIndex].pAttributeData      = AttributeData[TargetIndex].data();
        }

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
    pThreadPool->WaitForAllTasks();
    pThreadPool->StopThreads();
    return pMesh;
}

Uint32 GetElementOffset(const RadientMorphTargetData& Data,
                        Uint32                        GeometryDataByteOffset,
                        Uint32                        TargetIndex,
                        Uint32                        AttributeIndex)
{
    return (GeometryDataByteOffset + Data.GetAttributeDataOffset(TargetIndex, AttributeIndex)) / sizeof(Float32);
}

class RadientTesseraMorphDataTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_pMesh = CreateMesh(m_Source.MakeCreateInfo());
        ASSERT_NE(m_pMesh, nullptr);
        ASSERT_EQ(m_pMesh->CreateMorphTargetWeights(m_pWeights.GetAddressOfEmpty()), RADIENT_STATUS_OK);
        ASSERT_NE(m_pWeights, nullptr);

        RadientMorphTargetSource Source{m_Source.Targets.data(), TargetCount, VertexCount};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        m_pGeometryData = std::make_unique<RadientMorphTargetData>(Source);
        m_DrawableMesh.Geometries.resize(1);
        m_DrawableMesh.Geometries[0].pMorphTargetData = m_pGeometryData.get();
    }

    MorphMeshData                             m_Source;
    RefCntAutoPtr<IRadientMeshAsset>          m_pMesh;
    RefCntAutoPtr<IRadientMorphTargetWeights> m_pWeights;
    std::unique_ptr<RadientMorphTargetData>   m_pGeometryData;
    RadientDrawableMesh                       m_DrawableMesh;
};

TEST_F(RadientTesseraMorphDataTest, SelectsTargetsAndMapsGeometryAttributeOffsets)
{
    const std::array<Float32, TargetCount> Weights{0.25f, -1.f, 0.f, 0.75f, -0.5f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);

    constexpr Uint32 GeometryDataByteOffset            = 64;
    m_DrawableMesh.Geometries[0].MorphTargetDataOffset = GeometryDataByteOffset;
    RadientTesseraMorphData MorphData{m_pWeights, m_DrawableMesh, 3};
    EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_PENDING);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.GetActiveTargetCount(false), 3u);
    const auto* Attribs = MorphData.GetShaderAttribs(0, false);
    ASSERT_NE(Attribs, nullptr);

    EXPECT_FLOAT_EQ(Attribs[0].Weight, -1.f);
    EXPECT_EQ(Attribs[0].PositionDeltaOffset, GetElementOffset(*m_pGeometryData, GeometryDataByteOffset, 1, 0));
    EXPECT_EQ(Attribs[0].NormalDeltaOffset, GetElementOffset(*m_pGeometryData, GeometryDataByteOffset, 1, 1));
    EXPECT_EQ(Attribs[0].TangentDeltaOffset, PBR_Renderer::InvalidMorphTargetDataOffset);

    EXPECT_FLOAT_EQ(Attribs[1].Weight, 0.75f);
    EXPECT_EQ(Attribs[1].PositionDeltaOffset, GetElementOffset(*m_pGeometryData, GeometryDataByteOffset, 3, 0));
    EXPECT_EQ(Attribs[1].NormalDeltaOffset, GetElementOffset(*m_pGeometryData, GeometryDataByteOffset, 3, 1));
    EXPECT_EQ(Attribs[1].TangentDeltaOffset, GetElementOffset(*m_pGeometryData, GeometryDataByteOffset, 3, 2));

    EXPECT_FLOAT_EQ(Attribs[2].Weight, -0.5f);
    EXPECT_EQ(Attribs[2].PositionDeltaOffset, PBR_Renderer::InvalidMorphTargetDataOffset);
    EXPECT_EQ(Attribs[2].NormalDeltaOffset, GetElementOffset(*m_pGeometryData, GeometryDataByteOffset, 4, 0));
    EXPECT_EQ(Attribs[2].TangentDeltaOffset, PBR_Renderer::InvalidMorphTargetDataOffset);
}

TEST_F(RadientTesseraMorphDataTest, MaintainsHistoryWithinContinuousFramesAndResetsAfterGap)
{
    RadientTesseraMorphData MorphData{m_pWeights, m_DrawableMesh, 1};

    const auto SetSingleTarget = [&](Uint32 TargetIndex, Float32 Weight) {
        std::array<Float32, TargetCount> Weights{};
        Weights[TargetIndex] = Weight;
        return m_pWeights->SetWeights(0, TargetCount, Weights.data());
    };
    const auto ReadTarget = [&](bool Previous) {
        EXPECT_EQ(MorphData.GetActiveTargetCount(Previous), 1u);
        return MorphData.GetShaderAttribs(0, Previous)[0];
    };

    ASSERT_EQ(SetSingleTarget(0, 0.25f), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(ReadTarget(false).Weight, 0.25f);
    EXPECT_FLOAT_EQ(ReadTarget(true).Weight, 0.25f);
    const auto* const pInitialPalette = MorphData.GetShaderAttribs(0, false);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), pInitialPalette);

    ASSERT_EQ(SetSingleTarget(1, 0.5f), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(2), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(ReadTarget(false).Weight, 0.5f);
    EXPECT_FLOAT_EQ(ReadTarget(true).Weight, 0.25f);
    const auto* const pNextPalette = MorphData.GetShaderAttribs(0, false);
    EXPECT_NE(pNextPalette, pInitialPalette);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), pInitialPalette);
    EXPECT_EQ(MorphData.Prepare(2), RADIENT_STATUS_NO_CHANGE);

    ASSERT_EQ(SetSingleTarget(2, 0.75f), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(2), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(ReadTarget(false).Weight, 0.75f);
    EXPECT_FLOAT_EQ(ReadTarget(true).Weight, 0.25f);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, false), pNextPalette);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), pInitialPalette);

    ASSERT_EQ(MorphData.Prepare(3), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(ReadTarget(true).Weight, 0.75f);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), pNextPalette);
    EXPECT_EQ(MorphData.Prepare(3), RADIENT_STATUS_NO_CHANGE);

    ASSERT_EQ(SetSingleTarget(3, 1.f), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(5), RADIENT_STATUS_OK);
    EXPECT_FLOAT_EQ(ReadTarget(false).Weight, 1.f);
    EXPECT_FLOAT_EQ(ReadTarget(true).Weight, 1.f);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), MorphData.GetShaderAttribs(0, false));
}

TEST_F(RadientTesseraMorphDataTest, CachesIndependentGeometryOffsetsWithSharedHistory)
{
    // Identical target schemas need not have identical vertex counts or offsets.
    RadientMorphTargetSource SecondSource{m_Source.Targets.data(), TargetCount, 2};
    ASSERT_EQ(SecondSource.GetStatus(), RADIENT_STATUS_OK);
    auto pSecondGeometry = std::make_unique<RadientMorphTargetData>(SecondSource);
    m_DrawableMesh.Geometries.resize(2);
    m_DrawableMesh.Geometries[0].MorphTargetDataOffset = 64;
    m_DrawableMesh.Geometries[1].pMorphTargetData      = pSecondGeometry.get();
    m_DrawableMesh.Geometries[1].MorphTargetDataOffset = 256;
    RadientTesseraMorphData MorphData{m_pWeights, m_DrawableMesh, 1};

    const std::array<Uint32, 2> FirstOffsets{
        GetElementOffset(*m_pGeometryData, 64, 1, 0),
        GetElementOffset(*m_pGeometryData, 64, 3, 0)};
    const std::array<Uint32, 2> SecondOffsets{
        GetElementOffset(*pSecondGeometry, 256, 1, 0),
        GetElementOffset(*pSecondGeometry, 256, 3, 0)};

    // Drawing uses only cached offsets, not the original metadata objects.
    m_pGeometryData.reset();
    pSecondGeometry.reset();

    const auto ExpectTarget = [&](Uint32 GeometryIndex,
                                  bool Previous, Float32 Weight, Uint32 Offset) {
        ASSERT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_OK);
        ASSERT_EQ(MorphData.GetActiveTargetCount(Previous), 1u);
        const auto& Attribs = MorphData.GetShaderAttribs(GeometryIndex, Previous)[0];
        EXPECT_FLOAT_EQ(Attribs.Weight, Weight);
        EXPECT_EQ(Attribs.PositionDeltaOffset, Offset);
    };
    std::array<Float32, TargetCount> Weights{0.f, 0.5f, 0.f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_OK);
    ExpectTarget(0, false, 0.5f, FirstOffsets[0]);
    ExpectTarget(1, false, 0.5f, SecondOffsets[0]);

    Weights = {0.f, 0.f, 0.f, -1.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(2), RADIENT_STATUS_OK);
    ExpectTarget(0, false, -1.f, FirstOffsets[1]);
    ExpectTarget(1, false, -1.f, SecondOffsets[1]);
    ExpectTarget(0, true, 0.5f, FirstOffsets[0]);
    ExpectTarget(1, true, 0.5f, SecondOffsets[0]);
}

TEST_F(RadientTesseraMorphDataTest, RejectsMisalignedGeometryForEntireMorphData)
{
    m_DrawableMesh.Geometries.resize(2, m_DrawableMesh.Geometries[0]);
    m_DrawableMesh.Geometries[1].MorphTargetDataOffset = 1;
    TestingEnvironment::ErrorScope ExpectedErrors{"Float32-aligned allocation"};
    RadientTesseraMorphData        MorphData{m_pWeights, m_DrawableMesh, 1};
    EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(MorphData.Prepare(1), RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(MorphData.Prepare(2), RADIENT_STATUS_INVALID_DATA);
}

TEST_F(RadientTesseraMorphDataTest, RejectsMismatchedGeometryForEntireMorphData)
{
    RadientMorphTargetSource Source{m_Source.Targets.data(), TargetCount - 1, VertexCount};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    const RadientMorphTargetData GeometryData{Source};
    m_DrawableMesh.Geometries.resize(2, m_DrawableMesh.Geometries[0]);
    m_DrawableMesh.Geometries[1].pMorphTargetData = &GeometryData;
    TestingEnvironment::ErrorScope ExpectedErrors{"Morph geometry must match its weights"};
    RadientTesseraMorphData        MorphData{m_pWeights, m_DrawableMesh, 1};
    EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(MorphData.Prepare(1), RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(MorphData.Prepare(2), RADIENT_STATUS_INVALID_DATA);
}

TEST_F(RadientTesseraMorphDataTest, SharesPaletteHistoryAcrossAllGeometries)
{
    m_DrawableMesh.Geometries.resize(2, m_DrawableMesh.Geometries[0]);
    m_DrawableMesh.Geometries[1].MorphTargetDataOffset = 256;
    RadientTesseraMorphData MorphData{m_pWeights, m_DrawableMesh, 1};
    EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_PENDING);
    for (Uint32 Frame = 1; Frame <= 4; ++Frame)
    {
        std::array<Float32, TargetCount> Weights{};
        Weights[Frame - 1] = static_cast<Float32>(Frame) * 0.25f;
        ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
        ASSERT_EQ(MorphData.Prepare(Frame), RADIENT_STATUS_OK);
        EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_OK);
        const Uint32 PreviousFrame = Frame == 1 ? Frame : Frame - 1;
        for (Uint32 GeometryIndex = 0; GeometryIndex < m_DrawableMesh.Geometries.size(); ++GeometryIndex)
        {
            const Uint32 BaseOffset = m_DrawableMesh.Geometries[GeometryIndex].MorphTargetDataOffset;
            const auto&  Current    = MorphData.GetShaderAttribs(GeometryIndex, false)[0];
            const auto&  Previous   = MorphData.GetShaderAttribs(GeometryIndex, true)[0];
            EXPECT_FLOAT_EQ(Current.Weight, static_cast<Float32>(Frame) * 0.25f);
            EXPECT_FLOAT_EQ(Previous.Weight, static_cast<Float32>(PreviousFrame) * 0.25f);
            EXPECT_EQ(Current.PositionDeltaOffset, GetElementOffset(*m_pGeometryData, BaseOffset, Frame - 1, 0));
            EXPECT_EQ(Previous.PositionDeltaOffset, GetElementOffset(*m_pGeometryData, BaseOffset, PreviousFrame - 1, 0));
        }
        EXPECT_EQ(MorphData.Prepare(Frame), RADIENT_STATUS_NO_CHANGE);
    }

    // Same-frame edits replace every current palette without modifying history.
    const std::array<Float32, TargetCount> UpdatedWeights{-0.5f, 0.f, 0.f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, UpdatedWeights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(4), RADIENT_STATUS_OK);
    for (Uint32 GeometryIndex = 0; GeometryIndex < m_DrawableMesh.Geometries.size(); ++GeometryIndex)
    {
        EXPECT_FLOAT_EQ(MorphData.GetShaderAttribs(GeometryIndex, false)[0].Weight, -0.5f);
        EXPECT_FLOAT_EQ(MorphData.GetShaderAttribs(GeometryIndex, true)[0].Weight, 0.75f);
    }
}

TEST_F(RadientTesseraMorphDataTest, RepeatedPreparationPreservesGeometryPalettes)
{
    m_DrawableMesh.Geometries[0].MorphTargetDataOffset = 64;
    RadientTesseraMorphData          MorphData{m_pWeights, m_DrawableMesh, 1};
    std::array<Float32, TargetCount> Weights{0.25f, 0.f, 0.f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_OK);
    Weights = {0.f, 0.75f, 0.f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(2), RADIENT_STATUS_OK);

    const auto* const pCurrent  = MorphData.GetShaderAttribs(0, false);
    const auto* const pPrevious = MorphData.GetShaderAttribs(0, true);
    EXPECT_EQ(MorphData.Prepare(2), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, false), pCurrent);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), pPrevious);
    EXPECT_FLOAT_EQ(pCurrent[0].Weight, 0.75f);
    EXPECT_FLOAT_EQ(pPrevious[0].Weight, 0.25f);
    EXPECT_EQ(MorphData.Prepare(2), RADIENT_STATUS_NO_CHANGE);
}

TEST_F(RadientTesseraMorphDataTest, InitializesMorphGeometriesAroundNonMorphGeometries)
{
    m_DrawableMesh.Geometries.resize(8);
    m_DrawableMesh.Geometries[0].MorphTargetDataOffset = 64;
    m_DrawableMesh.Geometries[7].pMorphTargetData      = m_pGeometryData.get();
    m_DrawableMesh.Geometries[7].MorphTargetDataOffset = 256;
    RadientTesseraMorphData                MorphData{m_pWeights, m_DrawableMesh, 1};
    const std::array<Float32, TargetCount> Weights{0.25f, 0.f, 0.f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_OK);
    const auto* const pFirstPalette = MorphData.GetShaderAttribs(0, false);

    // A single preparation updates every morph geometry, skipping non-morph entries.
    EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_NO_CHANGE);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, false), pFirstPalette);
    EXPECT_FLOAT_EQ(pFirstPalette[0].Weight, 0.25f);
    EXPECT_NE(MorphData.GetShaderAttribs(7, false), pFirstPalette);
    EXPECT_EQ(MorphData.GetShaderAttribs(7, false)[0].PositionDeltaOffset,
              GetElementOffset(*m_pGeometryData, 256, 0, 0));
}

TEST_F(RadientTesseraMorphDataTest, HistorySelectionDoesNotRequireGeometryUpdates)
{
    RadientTesseraMorphData          MorphData{m_pWeights, m_DrawableMesh, 2};
    std::array<Float32, TargetCount> Weights{0.25f, 0.5f, 0.f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_OK);

    Weights = {0.f, 0.f, 0.75f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(2), RADIENT_STATUS_OK);
    const auto* const pCurrentPalette = MorphData.GetShaderAttribs(0, false);
    EXPECT_NE(MorphData.GetShaderAttribs(0, true), pCurrentPalette);
    EXPECT_EQ(MorphData.GetActiveTargetCount(false), 1u);
    EXPECT_EQ(MorphData.GetActiveTargetCount(true), 2u);

    // Stationary history aliases the already prepared current half. No geometry
    // update is needed to select it or to obtain its active count.
    ASSERT_EQ(MorphData.Prepare(3), RADIENT_STATUS_OK);
    EXPECT_EQ(MorphData.GetPreparationStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, false), pCurrentPalette);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), pCurrentPalette);
    EXPECT_EQ(MorphData.GetActiveTargetCount(true), 1u);
    EXPECT_EQ(MorphData.Prepare(3), RADIENT_STATUS_NO_CHANGE);
}

TEST_F(RadientTesseraMorphDataTest, UpdatesPreparedCountsWhenTargetsBecomeInactive)
{
    m_DrawableMesh.Geometries.resize(2, m_DrawableMesh.Geometries[0]);
    RadientTesseraMorphData          MorphData{m_pWeights, m_DrawableMesh, 3};
    std::array<Float32, TargetCount> Weights{0.25f, 0.5f, 0.75f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(1), RADIENT_STATUS_OK);
    EXPECT_EQ(MorphData.GetActiveTargetCount(false), 3u);

    Weights = {0.f, 0.f, 0.75f, 0.f, 0.f};
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(2), RADIENT_STATUS_OK);
    EXPECT_EQ(MorphData.GetActiveTargetCount(false), 1u);
    EXPECT_EQ(MorphData.GetActiveTargetCount(true), 3u);

    Weights.fill(0.f);
    ASSERT_EQ(m_pWeights->SetWeights(0, TargetCount, Weights.data()), RADIENT_STATUS_OK);
    ASSERT_EQ(MorphData.Prepare(3), RADIENT_STATUS_OK);
    EXPECT_EQ(MorphData.GetActiveTargetCount(false), 0u);
    EXPECT_EQ(MorphData.GetActiveTargetCount(true), 1u);
    ASSERT_EQ(MorphData.Prepare(4), RADIENT_STATUS_OK);
    EXPECT_EQ(MorphData.GetActiveTargetCount(true), 0u);
    EXPECT_EQ(MorphData.GetShaderAttribs(0, true), MorphData.GetShaderAttribs(0, false));
    EXPECT_EQ(MorphData.GetShaderAttribs(1, true), MorphData.GetShaderAttribs(1, false));
}

} // namespace
