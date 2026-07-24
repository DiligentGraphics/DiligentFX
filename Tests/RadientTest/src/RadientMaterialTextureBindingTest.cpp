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
#include "gtest/gtest.h"

#include <array>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> MakeTextureAttribIndices()
{
    std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> Indices;
    Indices.fill(-1);
    Indices[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = GLTF::DefaultBaseColorTextureAttribId;
    Indices[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = GLTF::DefaultNormalTextureAttribId;
    Indices[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]  = GLTF::DefaultMetallicRoughnessTextureAttribId;
    Indices[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = GLTF::DefaultEmissiveTextureAttribId;
    return Indices;
}

RadientMaterialRenderData MakeRenderData(
    const GLTF::Material&                                   Material,
    const std::vector<RefCntAutoPtr<IRadientTextureAsset>>& Textures)
{
    return {
        &Material,
        Textures.data(),
        static_cast<Uint32>(Textures.size())};
}

TEST(RadientMaterialTextureBindingTest, MappingUsesStableSemanticOrder)
{
    GLTF::Material                                   Material;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures{
        MakeTestTextureAsset("texture://base"),
        MakeTestTextureAsset("texture://physical-description"),
        MakeTestTextureAsset("texture://normal")};

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP |
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP);

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures), MakeTextureAttribIndices(), Flags, 3, {}, Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.SlotCount, 3u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 2u);

    EXPECT_EQ(Plan.Slots[0].TextureAttribId, PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);
    EXPECT_EQ(Plan.Slots[0].pTexture, Textures[GLTF::DefaultBaseColorTextureAttribId]);
    EXPECT_EQ(Plan.Slots[1].TextureAttribId, PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);
    EXPECT_EQ(Plan.Slots[1].pTexture, Textures[GLTF::DefaultNormalTextureAttribId]);
    EXPECT_EQ(Plan.Slots[2].TextureAttribId, PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC);
    EXPECT_EQ(Plan.Slots[2].pTexture, Textures[GLTF::DefaultMetallicRoughnessTextureAttribId]);
}

TEST(RadientMaterialTextureBindingTest, MappingKeepsDistinctSemanticSlotsWhenMaterialFits)
{
    GLTF::Material                                   Material;
    RefCntAutoPtr<IRadientTextureAsset>              pSharedTexture = MakeTestTextureAsset("texture://shared-atlas");
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures(GLTF::DefaultEmissiveTextureAttribId + 1);
    Textures[GLTF::DefaultBaseColorTextureAttribId] = pSharedTexture;
    Textures[GLTF::DefaultEmissiveTextureAttribId]  = pSharedTexture;

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures), MakeTextureAttribIndices(), Flags, 2, {}, Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.SlotCount, 2u);
    EXPECT_EQ(Plan.Slots[0].pTexture, pSharedTexture);
    EXPECT_EQ(Plan.Slots[1].pTexture, pSharedTexture);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE], 1u);
}

TEST(RadientMaterialTextureBindingTest, MappingPreservesFallbackSlotsWhenMaterialFits)
{
    GLTF::Material                                   Material;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures;

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures),
                  MakeTextureAttribIndices(),
                  PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP,
                  1,
                  {},
                  Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.SlotCount, 1u);
    EXPECT_EQ(Plan.Slots[0].TextureAttribId, PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);
    EXPECT_EQ(Plan.Slots[0].pTexture, nullptr);
}

TEST(RadientMaterialTextureBindingTest, MappingRejectsInsufficientDistinctSRVSlots)
{
    GLTF::Material                                   Material;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures;

    RadientMaterialTextureBindingPlan Plan;
    Plan.SlotCount                                                    = 7;
    Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = 4;

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP);

    RadientMaterialTextureSRVArray TextureSRVs{};
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = reinterpret_cast<ITextureView*>(size_t{1});
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = reinterpret_cast<ITextureView*>(size_t{2});

    EXPECT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures), MakeTextureAttribIndices(), Flags, 1, TextureSRVs, Plan),
              RADIENT_STATUS_INVALID_OPERATION);

    EXPECT_EQ(Plan.SlotCount, 0u);
    for (Uint16 TextureId : Plan.ShaderTextureIds)
        EXPECT_EQ(TextureId, PBR_Renderer::InvalidMaterialTextureId);
}

TEST(RadientMaterialTextureBindingTest, MappingRejectsMissingAttributeMapping)
{
    GLTF::Material                                   Material;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures;
    auto                                             TextureAttribIndices = MakeTextureAttribIndices();
    TextureAttribIndices[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]          = -1;

    RadientMaterialTextureBindingPlan Plan;
    EXPECT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures),
                  TextureAttribIndices,
                  PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP,
                  1,
                  {},
                  Plan),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Plan.SlotCount, 0u);
}

TEST(RadientMaterialTextureBindingTest, CompactMappingGroupsMatchingSRVs)
{
    GLTF::Material                                   Material;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures{
        MakeTestTextureAsset("texture://base"),
        MakeTestTextureAsset("texture://physical-description"),
        MakeTestTextureAsset("texture://normal"),
        {},
        MakeTestTextureAsset("texture://emissive")};

    ITextureView* const pColorAtlas = reinterpret_cast<ITextureView*>(size_t{1});
    ITextureView* const pDataAtlas  = reinterpret_cast<ITextureView*>(size_t{2});

    RadientMaterialTextureSRVArray TextureSRVs{};
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = pColorAtlas;
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = pColorAtlas;
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = pDataAtlas;
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]  = pDataAtlas;

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures),
                  MakeTextureAttribIndices(),
                  Flags,
                  2,
                  TextureSRVs,
                  Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.SlotCount, 2u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 1u);

    EXPECT_EQ(Plan.Slots[0].TextureAttribId, PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);
    EXPECT_EQ(Plan.Slots[0].pTexture, Textures[GLTF::DefaultBaseColorTextureAttribId]);
    EXPECT_EQ(Plan.Slots[1].TextureAttribId, PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);
    EXPECT_EQ(Plan.Slots[1].pTexture, Textures[GLTF::DefaultNormalTextureAttribId]);
}

TEST(RadientMaterialTextureBindingTest, PreferredMappingKeepsDistinctSlotsWhenMaterialFits)
{
    GLTF::Material                                   Material;
    RefCntAutoPtr<IRadientTextureAsset>              pSharedTexture = MakeTestTextureAsset("texture://shared-atlas");
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures(GLTF::DefaultEmissiveTextureAttribId + 1);
    Textures[GLTF::DefaultBaseColorTextureAttribId] = pSharedTexture;
    Textures[GLTF::DefaultEmissiveTextureAttribId]  = pSharedTexture;

    ITextureView* const            pSharedSRV = reinterpret_cast<ITextureView*>(size_t{1});
    RadientMaterialTextureSRVArray TextureSRVs{};
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = pSharedSRV;
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = pSharedSRV;

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);

    RadientMaterialTextureBindingPlan Plan;
    ASSERT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures),
                  MakeTextureAttribIndices(),
                  Flags,
                  2,
                  TextureSRVs,
                  Plan),
              RADIENT_STATUS_OK);

    ASSERT_EQ(Plan.SlotCount, 2u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(Plan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE], 1u);
}

TEST(RadientMaterialTextureBindingTest, CompactMappingRejectsTooManyDistinctSRVs)
{
    GLTF::Material                                   Material;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures;

    RadientMaterialTextureSRVArray TextureSRVs{};
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = reinterpret_cast<ITextureView*>(size_t{1});
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = reinterpret_cast<ITextureView*>(size_t{2});
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]  = reinterpret_cast<ITextureView*>(size_t{3});

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP);

    RadientMaterialTextureBindingPlan Plan;
    Plan.SlotCount = 7;
    EXPECT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures),
                  MakeTextureAttribIndices(),
                  Flags,
                  2,
                  TextureSRVs,
                  Plan),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Plan.SlotCount, 0u);
}

TEST(RadientMaterialTextureBindingTest, CompactMappingRejectsMissingSRV)
{
    GLTF::Material                                   Material;
    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures;

    RadientMaterialTextureSRVArray TextureSRVs{};
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = reinterpret_cast<ITextureView*>(size_t{1});
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = reinterpret_cast<ITextureView*>(size_t{2});

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP);

    RadientMaterialTextureBindingPlan Plan;
    EXPECT_EQ(BuildMaterialTextureBindingPlan(
                  MakeRenderData(Material, Textures),
                  MakeTextureAttribIndices(),
                  Flags,
                  2,
                  TextureSRVs,
                  Plan),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(Plan.SlotCount, 0u);
}

} // namespace
