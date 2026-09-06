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

#include "Render/RadientPBRRenderer.hpp"
#include "GLTF_PBR_Renderer.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>

namespace Diligent
{

TEST(PBRRendererTest, ControlsSpecularFeature)
{
    PBR_Renderer::CreateInfo Settings{};

    EXPECT_EQ(PBR_Renderer::GetEnabledPSOFlags(Settings) & PBR_Renderer::PSO_FLAG_ALL_SPECULAR,
              PBR_Renderer::PSO_FLAG_NONE);

    Settings.EnableSpecular = true;
    EXPECT_EQ(PBR_Renderer::GetEnabledPSOFlags(Settings) & PBR_Renderer::PSO_FLAG_ALL_SPECULAR,
              PBR_Renderer::PSO_FLAG_ALL_SPECULAR);

    EXPECT_FALSE(PBR_Renderer::IsSRGBTextureAttribute(PBR_Renderer::TEXTURE_ATTRIB_ID_SPECULAR));
    EXPECT_TRUE(PBR_Renderer::IsSRGBTextureAttribute(PBR_Renderer::TEXTURE_ATTRIB_ID_SPECULAR_COLOR));

    EXPECT_STREQ(PBR_Renderer::GetDebugViewTypeString(PBR_Renderer::DebugViewType::SpecularFactor),
                 "SpecularFactor");
    EXPECT_STREQ(PBR_Renderer::GetDebugViewTypeString(PBR_Renderer::DebugViewType::SpecularColorFactor),
                 "SpecularColorFactor");
}

TEST(PBRRendererTest, SelectsHighestMagnitudeMorphTargets)
{
    constexpr std::array                           Weights{0.f, -0.75f, 0.5f, -0.5f, 1.25f, -0.f};
    std::array<PBR_Renderer::ActiveMorphTarget, 6> ActiveTargets{};

    const Uint32 ActiveTargetCount = PBR_Renderer::SelectActiveMorphTargets(
        Weights.data(),
        static_cast<Uint32>(Weights.size()),
        ActiveTargets.data(),
        3);

    ASSERT_EQ(ActiveTargetCount, 3u);
    EXPECT_EQ(ActiveTargets[0].TargetIndex, 4u);
    EXPECT_FLOAT_EQ(ActiveTargets[0].Weight, 1.25f);
    EXPECT_EQ(ActiveTargets[1].TargetIndex, 1u);
    EXPECT_FLOAT_EQ(ActiveTargets[1].Weight, -0.75f);
    EXPECT_EQ(ActiveTargets[2].TargetIndex, 2u);
    EXPECT_FLOAT_EQ(ActiveTargets[2].Weight, 0.5f);

    const Uint32 AllActiveTargetCount = PBR_Renderer::SelectActiveMorphTargets(
        Weights.data(),
        static_cast<Uint32>(Weights.size()),
        ActiveTargets.data(),
        static_cast<Uint32>(ActiveTargets.size()));
    ASSERT_EQ(AllActiveTargetCount, 4u);
    EXPECT_EQ(ActiveTargets[3].TargetIndex, 3u);
    EXPECT_FLOAT_EQ(ActiveTargets[3].Weight, -0.5f);
}

TEST(PBRRendererTest, ZeroActiveMorphTargetsDisableFeature)
{
    PBR_Renderer::CreateInfo Settings{};
    EXPECT_NE(PBR_Renderer::GetEnabledPSOFlags(Settings) & PBR_Renderer::PSO_FLAG_USE_MORPH_TARGETS,
              PBR_Renderer::PSO_FLAG_NONE);

    Settings.MaxActiveMorphTargetCount = 0;
    EXPECT_EQ(PBR_Renderer::GetEnabledPSOFlags(Settings) & PBR_Renderer::PSO_FLAG_USE_MORPH_TARGETS,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(PBRRendererTest, PacksOptionalPrimitiveTransformBlocks)
{
    struct LayoutCase
    {
        PBR_Renderer::PSO_FLAGS            Flags;
        PBR_Renderer::VERTEX_POS_PACK_MODE VertexPosPackMode;
        bool                               UseSkinPreTransform;
    };

    constexpr std::array Cases{
        LayoutCase{PBR_Renderer::PSO_FLAG_NONE, PBR_Renderer::VERTEX_POS_PACK_MODE_NONE, false},
        LayoutCase{PBR_Renderer::PSO_FLAG_USE_JOINTS, PBR_Renderer::VERTEX_POS_PACK_MODE_NONE, false},
        LayoutCase{PBR_Renderer::PSO_FLAG_NONE, PBR_Renderer::VERTEX_POS_PACK_MODE_64_BIT, false},
        LayoutCase{PBR_Renderer::PSO_FLAG_USE_JOINTS, PBR_Renderer::VERTEX_POS_PACK_MODE_64_BIT, false},
        LayoutCase{PBR_Renderer::PSO_FLAG_USE_JOINTS, PBR_Renderer::VERTEX_POS_PACK_MODE_NONE, true},
        LayoutCase{PBR_Renderer::PSO_FLAG_USE_JOINTS | PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS,
                   PBR_Renderer::VERTEX_POS_PACK_MODE_64_BIT, true},
    };

    const float4x4 NodeMatrix{1.f};
    const float4x4 PrevNodeMatrix{2.f};
    const float4x4 SkinPreTransform{3.f};
    const float4x4 PrevSkinPreTransform{4.f};
    const float3   PosScale{2.f, 3.f, 4.f};
    const float3   PosBias{5.f, 6.f, 7.f};

    for (const LayoutCase& TestCase : Cases)
    {
        SCOPED_TRACE(static_cast<Uint32>(TestCase.Flags));
        SCOPED_TRACE(static_cast<Uint32>(TestCase.VertexPosPackMode));
        SCOPED_TRACE(TestCase.UseSkinPreTransform);

        std::array<float4, 32> ShaderData{};

        GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData Attribs;
        Attribs.PSOFlags             = TestCase.Flags;
        Attribs.NodeMatrix           = &NodeMatrix;
        Attribs.PrevNodeMatrix       = &PrevNodeMatrix;
        Attribs.JointCount           = 7;
        Attribs.FirstJoint           = 11;
        Attribs.PrevFirstJoint       = 13;
        Attribs.PosScale             = &PosScale;
        Attribs.PosBias              = &PosBias;
        Attribs.SkinPreTransform     = &SkinPreTransform;
        Attribs.PrevSkinPreTransform = &PrevSkinPreTransform;

        void* const pEnd = GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(
            ShaderData.data(),
            Attribs,
            false,
            TestCase.UseSkinPreTransform,
            TestCase.VertexPosPackMode,
            4);

        const Uint8* const pShaderData  = reinterpret_cast<const Uint8*>(ShaderData.data());
        Uint32             Offset       = 0;
        const auto         ExpectMatrix = [&](const float4x4& Expected) {
            EXPECT_EQ(std::memcmp(pShaderData + Offset, &Expected, sizeof(Expected)), 0);
            Offset += static_cast<Uint32>(sizeof(Expected));
        };

        ExpectMatrix(NodeMatrix);
        if ((TestCase.Flags & PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS) != 0)
            ExpectMatrix(PrevNodeMatrix);

        if ((TestCase.Flags & PBR_Renderer::PSO_FLAG_USE_JOINTS) != 0)
        {
            std::array<int, 4> JointData{};
            std::memcpy(JointData.data(), pShaderData + Offset, sizeof(JointData));
            EXPECT_EQ(JointData, (std::array<int, 4>{7, 11, 13, 0}));
            Offset += static_cast<Uint32>(sizeof(float4));

            if (TestCase.UseSkinPreTransform)
            {
                ExpectMatrix(SkinPreTransform);
                if ((TestCase.Flags & PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS) != 0)
                    ExpectMatrix(PrevSkinPreTransform);
            }
        }

        if (TestCase.VertexPosPackMode != PBR_Renderer::VERTEX_POS_PACK_MODE_NONE)
        {
            std::array<float, 8> PositionData{};
            std::memcpy(PositionData.data(), pShaderData + Offset, sizeof(PositionData));
            EXPECT_EQ(PositionData, (std::array<float, 8>{5.f, 6.f, 7.f, 0.f, 2.f, 3.f, 4.f, 0.f}));
            Offset += static_cast<Uint32>(sizeof(PositionData));
        }

        std::array<float, 4> FallbackColor{};
        std::memcpy(FallbackColor.data(), pShaderData + Offset, sizeof(FallbackColor));
        EXPECT_EQ(FallbackColor, (std::array<float, 4>{1.f, 1.f, 1.f, 1.f}));
        Offset += static_cast<Uint32>(sizeof(FallbackColor));

        EXPECT_EQ(pEnd, pShaderData + Offset);
    }
}

TEST(PBRRendererTest, PacksMorphTargetPalettes)
{
    using MorphTargetShaderAttribs = PBR_Renderer::MorphTargetShaderAttribs;
    static_assert(sizeof(MorphTargetShaderAttribs) == sizeof(float4));

    constexpr Uint32     MaxActiveMorphTargetCount = 4;
    constexpr std::array CurrentTargets{
        MorphTargetShaderAttribs{0.75f, 3u, 6u, 9u},
        MorphTargetShaderAttribs{-0.25f, 12u, PBR_Renderer::InvalidMorphTargetDataOffset, 15u},
    };
    constexpr std::array PreviousTargets{
        MorphTargetShaderAttribs{0.5f, 18u, 21u, PBR_Renderer::InvalidMorphTargetDataOffset},
    };

    const float4x4 NodeMatrix{1.f};
    const float4x4 PrevNodeMatrix{2.f};

    GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData Attribs;
    Attribs.PSOFlags =
        PBR_Renderer::PSO_FLAG_USE_MORPH_TARGETS |
        PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS;
    Attribs.NodeMatrix                 = &NodeMatrix;
    Attribs.PrevNodeMatrix             = &PrevNodeMatrix;
    Attribs.MorphTargetVertexOffset    = 37;
    Attribs.pActiveMorphTargets        = CurrentTargets.data();
    Attribs.ActiveMorphTargetCount     = static_cast<Uint32>(CurrentTargets.size());
    Attribs.pPrevActiveMorphTargets    = PreviousTargets.data();
    Attribs.PrevActiveMorphTargetCount = static_cast<Uint32>(PreviousTargets.size());

    std::array<float4, 32> ShaderData{};
    void* const            pEnd = GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(
        ShaderData.data(),
        Attribs,
        false,
        false,
        PBR_Renderer::VERTEX_POS_PACK_MODE_NONE,
        MaxActiveMorphTargetCount);

    const Uint8* const pShaderData = reinterpret_cast<const Uint8*>(ShaderData.data());
    Uint32             Offset      = sizeof(NodeMatrix) + sizeof(PrevNodeMatrix);

    std::array<Uint32, 4> Header{};
    std::memcpy(Header.data(), pShaderData + Offset, sizeof(Header));
    EXPECT_EQ(Header, (std::array<Uint32, 4>{37u, 0u, 0u, 0u}));
    Offset += sizeof(Header);

    const auto ExpectPalette = [&](const MorphTargetShaderAttribs* pExpectedTargets, Uint32 ExpectedTargetCount) {
        for (Uint32 TargetIndex = 0; TargetIndex < MaxActiveMorphTargetCount; ++TargetIndex)
        {
            MorphTargetShaderAttribs Actual{};
            std::memcpy(&Actual, pShaderData + Offset, sizeof(Actual));
            Offset += sizeof(Actual);

            const MorphTargetShaderAttribs Expected = TargetIndex < ExpectedTargetCount ?
                pExpectedTargets[TargetIndex] :
                MorphTargetShaderAttribs{};
            EXPECT_EQ(std::memcmp(&Actual, &Expected, sizeof(Actual)), 0);
        }
    };

    ExpectPalette(CurrentTargets.data(), static_cast<Uint32>(CurrentTargets.size()));
    ExpectPalette(PreviousTargets.data(), static_cast<Uint32>(PreviousTargets.size()));

    const std::array<float, 4> ExpectedFallbackColor{1.f, 1.f, 1.f, 1.f};
    EXPECT_EQ(std::memcmp(pShaderData + Offset, ExpectedFallbackColor.data(), sizeof(ExpectedFallbackColor)), 0);
    Offset += sizeof(ExpectedFallbackColor);

    EXPECT_EQ(pEnd, pShaderData + Offset);
}

TEST(RadientPBRRendererTest, ConvertsDebugVisualizations)
{
    struct DebugVisualizationMapping
    {
        RADIENT_DEBUG_VISUALIZATION RadientVisualization;
        PBR_Renderer::DebugViewType PBRDebugView;
    };

    constexpr std::array Mappings{
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_NONE, PBR_Renderer::DebugViewType::None},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_TEXCOORD0, PBR_Renderer::DebugViewType::Texcoord0},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_TEXCOORD1, PBR_Renderer::DebugViewType::Texcoord1},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_BASE_COLOR, PBR_Renderer::DebugViewType::BaseColor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_TRANSPARENCY, PBR_Renderer::DebugViewType::Transparency},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_OCCLUSION, PBR_Renderer::DebugViewType::Occlusion},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_EMISSIVE, PBR_Renderer::DebugViewType::Emissive},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_METALLIC, PBR_Renderer::DebugViewType::Metallic},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_ROUGHNESS, PBR_Renderer::DebugViewType::Roughness},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_DIFFUSE_COLOR, PBR_Renderer::DebugViewType::DiffuseColor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SPECULAR_COLOR, PBR_Renderer::DebugViewType::SpecularColor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_REFLECTANCE90, PBR_Renderer::DebugViewType::Reflectance90},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_MESH_NORMAL, PBR_Renderer::DebugViewType::MeshNormal},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SHADING_NORMAL, PBR_Renderer::DebugViewType::ShadingNormal},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_MOTION_VECTORS, PBR_Renderer::DebugViewType::MotionVectors},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_N_DOT_V, PBR_Renderer::DebugViewType::NdotV},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_PUNCTUAL_LIGHTING, PBR_Renderer::DebugViewType::PunctualLighting},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_DIFFUSE_IBL, PBR_Renderer::DebugViewType::DiffuseIBL},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SPECULAR_IBL, PBR_Renderer::DebugViewType::SpecularIBL},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_WHITE_BASE_COLOR, PBR_Renderer::DebugViewType::WhiteBaseColor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT, PBR_Renderer::DebugViewType::ClearCoat},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT_FACTOR, PBR_Renderer::DebugViewType::ClearCoatFactor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT_ROUGHNESS, PBR_Renderer::DebugViewType::ClearCoatRoughness},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_CLEAR_COAT_NORMAL, PBR_Renderer::DebugViewType::ClearCoatNormal},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SHEEN, PBR_Renderer::DebugViewType::Sheen},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SHEEN_COLOR, PBR_Renderer::DebugViewType::SheenColor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SHEEN_ROUGHNESS, PBR_Renderer::DebugViewType::SheenRoughness},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SPECULAR_FACTOR, PBR_Renderer::DebugViewType::SpecularFactor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SPECULAR_COLOR_FACTOR, PBR_Renderer::DebugViewType::SpecularColorFactor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_ANISOTROPY_STRENGTH, PBR_Renderer::DebugViewType::AnisotropyStrength},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_ANISOTROPY_DIRECTION, PBR_Renderer::DebugViewType::AnisotropyDirection},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_IRIDESCENCE, PBR_Renderer::DebugViewType::Iridescence},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_IRIDESCENCE_FACTOR, PBR_Renderer::DebugViewType::IridescenceFactor},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_IRIDESCENCE_THICKNESS, PBR_Renderer::DebugViewType::IridescenceThickness},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_TRANSMISSION, PBR_Renderer::DebugViewType::Transmission},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_THICKNESS, PBR_Renderer::DebugViewType::Thickness},
        DebugVisualizationMapping{RADIENT_DEBUG_VISUALIZATION_SCENE_DEPTH, PBR_Renderer::DebugViewType::SceneDepth},
    };
    static_assert(Mappings.size() == RADIENT_DEBUG_VISUALIZATION_COUNT,
                  "Please update the Radient debug visualization mappings");

    for (const DebugVisualizationMapping& Mapping : Mappings)
    {
        EXPECT_EQ(RadientPBRRenderer::GetDebugViewType(Mapping.RadientVisualization),
                  Mapping.PBRDebugView);
    }
}

} // namespace Diligent
