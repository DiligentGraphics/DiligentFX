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

#include "Render/RadientMaterialTextureBinding.hpp"

#include "RadientTestAssetHelpers.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

using TestMaterialTextureArray =
    std::array<RadientMaterialTextureRenderData, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>;

RadientMaterialTextureRenderData MakeBinding(size_t                 ResourceId,
                                             TEXTURE_FORMAT         ViewFormat = TEX_FORMAT_RGBA8_UNORM,
                                             RadientTextureViewType ViewType   = RadientTextureViewType::Linear)
{
    RadientMaterialTextureRenderData TextureData;
    TextureData.pTexture        = MakeTestTextureAsset();
    TextureData.ViewType        = ViewType;
    TextureData.BindingIdentity = {
        static_cast<Int32>(ResourceId),
        ViewFormat,
    };
    return TextureData;
}

RadientMaterialDefaultTextureBindings MakeDefaultBindings()
{
    RadientMaterialDefaultTextureBindings Bindings;
    Bindings.WhiteLinear  = MakeBinding(100);
    Bindings.WhiteSRGB    = MakeBinding(101, TEX_FORMAT_RGBA8_UNORM_SRGB, RadientTextureViewType::SRGB);
    Bindings.BlackSRGB    = MakeBinding(102, TEX_FORMAT_RGBA8_UNORM_SRGB, RadientTextureViewType::SRGB);
    Bindings.Normal       = MakeBinding(103);
    Bindings.PhysicalDesc = MakeBinding(104);
    return Bindings;
}

RADIENT_STATUS BuildTestMaterialTextureBindingPlan(
    PBR_Renderer::PSO_FLAGS                      PSOFlags,
    Uint32                                       MaxTextureSlots,
    const TestMaterialTextureArray&              Textures,
    const RadientMaterialDefaultTextureBindings& DefaultTextures,
    RadientMaterialTextureBindingPlan&           Plan)
{
    static const GLTF::Material                                         Material;
    static const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> TextureAttribIndices = [] {
        std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> Indices;
        for (size_t Attrib = 0; Attrib < Indices.size(); ++Attrib)
            Indices[Attrib] = static_cast<int>(Attrib);
        return Indices;
    }();

    const RadientMaterialRenderData MaterialData{
        &Material,
        Textures.data(),
        static_cast<Uint32>(Textures.size()),
    };
    return BuildMaterialTextureBindingPlan(MaterialData,
                                           TextureAttribIndices,
                                           PSOFlags,
                                           MaxTextureSlots,
                                           DefaultTextures,
                                           Plan);
}

TEST(RadientMaterialTextureBindingTest, DefaultMappingUsesSemanticSlots)
{
    TestMaterialTextureArray Textures;
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = MakeBinding(1);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = MakeBinding(2);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]  = MakeBinding(3);

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP |
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP);

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildTestMaterialTextureBindingPlan(Flags, 3, Textures, MakeDefaultBindings(), Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.Slots.size(), 3u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 2u);
    EXPECT_EQ(Plan.Slots[0].BindingIdentity, Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR].BindingIdentity);
    EXPECT_EQ(Plan.Slots[1].BindingIdentity, Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL].BindingIdentity);
    EXPECT_EQ(Plan.Slots[2].BindingIdentity, Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC].BindingIdentity);
}

TEST(RadientMaterialTextureBindingTest, DefaultMappingProducesCanonicalCompleteRecipes)
{
    const RadientMaterialDefaultTextureBindings Defaults = MakeDefaultBindings();

    TestMaterialTextureArray BaseColorTextures;
    BaseColorTextures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] =
        *Defaults.Get(PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);

    TestMaterialTextureArray NormalTextures;
    NormalTextures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL] =
        *Defaults.Get(PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);

    RadientMaterialTextureBindingPlan BaseColorPlan;
    RadientMaterialTextureBindingPlan NormalPlan;
    ASSERT_EQ(BuildTestMaterialTextureBindingPlan(PBR_Renderer::PSO_FLAG_USE_COLOR_MAP,
                                                  8, BaseColorTextures, Defaults, BaseColorPlan),
              RADIENT_STATUS_OK);
    ASSERT_EQ(BuildTestMaterialTextureBindingPlan(PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP,
                                                  8, NormalTextures, Defaults, NormalPlan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(BaseColorPlan.Slots.size(), 8u);
    ASSERT_EQ(NormalPlan.Slots.size(), BaseColorPlan.Slots.size());
    for (size_t Slot = 0; Slot < BaseColorPlan.Slots.size(); ++Slot)
    {
        EXPECT_EQ(BaseColorPlan.Slots[Slot].BindingIdentity, NormalPlan.Slots[Slot].BindingIdentity);
        EXPECT_EQ(BaseColorPlan.ShaderTextureIds[Slot], Slot);
        EXPECT_EQ(NormalPlan.ShaderTextureIds[Slot], Slot);
    }
}

TEST(RadientMaterialTextureBindingTest, DefaultMappingKeepsDistinctSemanticSlots)
{
    TestMaterialTextureArray               Textures;
    const RadientMaterialTextureRenderData SharedTexture = MakeBinding(1);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = SharedTexture;
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = SharedTexture;

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildTestMaterialTextureBindingPlan(Flags, 8, Textures, MakeDefaultBindings(), Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.Slots.size(), 8u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE], 6u);
    EXPECT_EQ(Plan.Slots[0].BindingIdentity, SharedTexture.BindingIdentity);
    EXPECT_EQ(Plan.Slots[6].BindingIdentity, SharedTexture.BindingIdentity);
}

TEST(RadientMaterialTextureBindingTest, CompactMappingGroupsMatchingLogicalViews)
{
    TestMaterialTextureArray Textures;
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = MakeBinding(1);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = MakeBinding(1);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = MakeBinding(2);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]  = MakeBinding(2);

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildTestMaterialTextureBindingPlan(Flags, 2, Textures, MakeDefaultBindings(), Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.Slots.size(), 2u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 1u);
}

TEST(RadientMaterialTextureBindingTest, CompactMappingDistinguishesTypedViews)
{
    TestMaterialTextureArray Textures;
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] =
        MakeBinding(1, TEX_FORMAT_RGBA8_UNORM_SRGB, RadientTextureViewType::SRGB);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL] =
        MakeBinding(1, TEX_FORMAT_RGBA8_UNORM, RadientTextureViewType::Linear);

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP);

    RadientMaterialTextureBindingPlan Plan;
    TestingEnvironment::ErrorScope    ExpectedErrors{"Material requires more than 1 distinct texture bindings"};
    EXPECT_EQ(BuildTestMaterialTextureBindingPlan(Flags, 1, Textures, MakeDefaultBindings(), Plan),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_TRUE(Plan.Slots.empty());
}

TEST(RadientMaterialTextureBindingTest, RejectsMissingActiveTexture)
{
    RadientMaterialTextureBindingPlan Plan;
    TestingEnvironment::ErrorScope    ExpectedErrors{"Material texture 1 used by PBR texture attribute 1 is not initialized"};
    EXPECT_EQ(BuildTestMaterialTextureBindingPlan(PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP,
                                                  8, {}, MakeDefaultBindings(), Plan),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_TRUE(Plan.Slots.empty());
}

TEST(RadientMaterialTextureBindingTest, MaterialWithoutActiveTexturesUsesDefaults)
{
    const RadientMaterialDefaultTextureBindings Defaults = MakeDefaultBindings();
    RadientMaterialTextureBindingPlan           Plan;
    ASSERT_EQ(BuildTestMaterialTextureBindingPlan(PBR_Renderer::PSO_FLAG_NONE,
                                                  8, {}, Defaults, Plan),
              RADIENT_STATUS_OK);
    EXPECT_EQ(Plan.Slots.size(), 8u);
    for (Uint32 Slot = 0; Slot < Plan.Slots.size(); ++Slot)
    {
        const auto                              TextureAttribId = static_cast<PBR_Renderer::TEXTURE_ATTRIB_ID>(Slot);
        const RadientMaterialTextureRenderData* pDefaultTexture = Defaults.Get(TextureAttribId);
        ASSERT_NE(pDefaultTexture, nullptr);
        EXPECT_EQ(Plan.Slots[Slot].BindingIdentity, pDefaultTexture->BindingIdentity);
        EXPECT_EQ(Plan.ShaderTextureIds[Slot], Slot);
    }
}

} // namespace
