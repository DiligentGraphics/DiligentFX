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

#include "gtest/gtest.h"

#include <array>

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
