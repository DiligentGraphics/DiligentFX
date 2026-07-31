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

#include "Render/RadientMaterialSRBTable.hpp"

#include "ObjectBase.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <cstdint>
#include <vector>

namespace Diligent
{

namespace
{

class TestShaderResourceBinding final : public ObjectBase<IShaderResourceBinding>
{
public:
    using TBase = ObjectBase<IShaderResourceBinding>;

    explicit TestShaderResourceBinding(IReferenceCounters* pRefCounters) :
        TBase{pRefCounters}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_ShaderResourceBinding, TBase)

    virtual IPipelineResourceSignature* DILIGENT_CALL_TYPE GetPipelineResourceSignature() const override final
    {
        return nullptr;
    }

    virtual void DILIGENT_CALL_TYPE BindResources(SHADER_TYPE,
                                                  IResourceMapping*,
                                                  BIND_SHADER_RESOURCES_FLAGS) override final
    {}

    virtual SHADER_RESOURCE_VARIABLE_TYPE_FLAGS DILIGENT_CALL_TYPE CheckResources(
        SHADER_TYPE,
        IResourceMapping*,
        BIND_SHADER_RESOURCES_FLAGS) const override final
    {
        return SHADER_RESOURCE_VARIABLE_TYPE_FLAG_NONE;
    }

    virtual IShaderResourceVariable* DILIGENT_CALL_TYPE GetVariableByName(SHADER_TYPE, const Char*) override final
    {
        return nullptr;
    }

    virtual Uint32 DILIGENT_CALL_TYPE GetVariableCount(SHADER_TYPE) const override final
    {
        return 0;
    }

    virtual IShaderResourceVariable* DILIGENT_CALL_TYPE GetVariableByIndex(SHADER_TYPE, Uint32) override final
    {
        return nullptr;
    }

    virtual Bool DILIGENT_CALL_TYPE StaticResourcesInitialized() const override final
    {
        return True;
    }
};

RefCntAutoPtr<IShaderResourceBinding> MakeTestSRB()
{
    return RefCntAutoPtr<IShaderResourceBinding>{MakeNewRCObj<TestShaderResourceBinding>()()};
}

using TestMaterialTextureArray =
    std::array<RadientMaterialTextureRenderData, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>;

RadientMaterialTextureRenderData MakeBinding(size_t                 ResourceId,
                                             TEXTURE_FORMAT         ViewFormat = TEX_FORMAT_RGBA8_UNORM,
                                             RadientTextureViewType ViewType   = RadientTextureViewType::Linear)
{
    RadientMaterialTextureRenderData Binding;
    Binding.pTexture        = Testing::MakeTestTextureAsset();
    Binding.ViewType        = ViewType;
    Binding.BindingIdentity = {
        static_cast<Int32>(ResourceId),
        ViewFormat,
    };
    return Binding;
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

RADIENT_STATUS AcquireTestMaterialSRB(
    RadientMaterialSRBTable&                       Table,
    PBR_Renderer::PSO_FLAGS                        PSOFlags,
    Uint32                                         MaxTextureSlots,
    const TestMaterialTextureArray&                Textures,
    const RadientMaterialDefaultTextureBindings&   DefaultTextures,
    RadientMaterialSRBLease&                       Lease,
    PBR_Renderer::StaticShaderTextureIdsArrayType& ShaderTextureIds)
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
    return Table.Acquire(MaterialData,
                         TextureAttribIndices,
                         PSOFlags,
                         MaxTextureSlots,
                         DefaultTextures,
                         Lease,
                         ShaderTextureIds);
}

class TestMaterialSRBRecipe
{
public:
    explicit TestMaterialSRBRecipe(std::initializer_list<size_t> ResourceIds)
    {
        Slots.reserve(ResourceIds.size());
        for (const size_t ResourceId : ResourceIds)
            Slots.push_back(MakeBinding(ResourceId));

        SlotSources.reserve(Slots.size());
        for (const RadientMaterialTextureRenderData& Slot : Slots)
            SlotSources.push_back(&Slot);
    }

    RadientMaterialSRBLease Acquire(RadientMaterialSRBTable& Table) const
    {
        return Table.Acquire(SlotSources.data(), static_cast<Uint32>(SlotSources.size()));
    }

    void Clear()
    {
        SlotSources.clear();
        Slots.clear();
    }

    std::vector<RadientMaterialTextureRenderData>        Slots;
    std::vector<const RadientMaterialTextureRenderData*> SlotSources;
};

ITextureView* ResolveTestSRV(const RadientMaterialTextureRenderData& Binding)
{
    return reinterpret_cast<ITextureView*>(static_cast<uintptr_t>(Binding.BindingIdentity.StandaloneResourceId));
}

} // namespace

TEST(RadientMaterialSRBTableTest, ReusesEntryForSameLogicalSlots)
{
    RadientMaterialSRBTable       Table;
    const TestMaterialSRBRecipe   Recipe{{1, 2}};
    const RadientMaterialSRBLease First  = Recipe.Acquire(Table);
    const RadientMaterialSRBLease Second = Recipe.Acquire(Table);

    ASSERT_TRUE(First);
    EXPECT_TRUE(Second);
    EXPECT_EQ(Table.GetSize(), 1u);
    EXPECT_EQ(First.GetSRB(), nullptr);
}

TEST(RadientMaterialSRBTableTest, DistinguishesSlotOrderCountAndFormat)
{
    RadientMaterialSRBTable Table;

    TestMaterialSRBRecipe DifferentFormat{{1, 2}};
    DifferentFormat.Slots[1].BindingIdentity.ViewFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;

    const RadientMaterialSRBLease First     = TestMaterialSRBRecipe{{1, 2}}.Acquire(Table);
    const RadientMaterialSRBLease Reordered = TestMaterialSRBRecipe{{2, 1}}.Acquire(Table);
    const RadientMaterialSRBLease Shorter   = TestMaterialSRBRecipe{1}.Acquire(Table);
    const RadientMaterialSRBLease TypedView = DifferentFormat.Acquire(Table);

    ASSERT_TRUE(First);
    EXPECT_TRUE(Reordered);
    EXPECT_TRUE(Shorter);
    EXPECT_TRUE(TypedView);
    EXPECT_EQ(Table.GetSize(), 4u);
}

TEST(RadientMaterialSRBTableTest, MaterializesAndRefreshesEntryInPlace)
{
    RadientMaterialSRBTable       Table;
    const RadientMaterialSRBLease Lease = TestMaterialSRBRecipe{{1, 2}}.Acquire(Table);
    ASSERT_TRUE(Lease);

    Uint32 CreateCount = 0;
    auto   CreateSRB   = [&CreateCount](ITextureView* const* ppTextureSRVs, Uint32 TextureCount) {
        EXPECT_EQ(TextureCount, 2u);
        EXPECT_NE(ppTextureSRVs[0], nullptr);
        EXPECT_NE(ppTextureSRVs[1], nullptr);
        ++CreateCount;
        return MakeTestSRB();
    };

    ASSERT_EQ(Table.Prepare(1, ResolveTestSRV, CreateSRB), RADIENT_STATUS_OK);
    RefCntAutoPtr<IShaderResourceBinding> pFirstSRB{Lease.GetSRB()};
    ASSERT_NE(pFirstSRB, nullptr);
    EXPECT_EQ(CreateCount, 1u);

    EXPECT_EQ(Table.Prepare(1, ResolveTestSRV, CreateSRB), RADIENT_STATUS_OK);
    EXPECT_EQ(Lease.GetSRB(), pFirstSRB);
    EXPECT_EQ(CreateCount, 1u);

    EXPECT_EQ(Table.Prepare(2, ResolveTestSRV, CreateSRB), RADIENT_STATUS_OK);
    EXPECT_NE(Lease.GetSRB(), nullptr);
    EXPECT_NE(Lease.GetSRB(), pFirstSRB);
    EXPECT_EQ(CreateCount, 2u);
}

TEST(RadientMaterialSRBTableTest, ReleasesTexturesWithLastLease)
{
    RadientMaterialSRBTable             Table;
    RadientMaterialSRBLease             First;
    RadientMaterialSRBLease             Second;
    RefCntWeakPtr<IRadientTextureAsset> pWeakTexture;
    TestMaterialSRBRecipe               Recipe{1};
    pWeakTexture = Recipe.Slots[0].pTexture;

    First  = Recipe.Acquire(Table);
    Second = Recipe.Acquire(Table);
    Recipe.Clear();

    ASSERT_TRUE(First);
    ASSERT_TRUE(Second);
    EXPECT_NE(pWeakTexture.Lock(), nullptr);

    First = {};
    EXPECT_NE(pWeakTexture.Lock(), nullptr);

    Second = {};
    EXPECT_EQ(pWeakTexture.Lock(), nullptr);
    EXPECT_EQ(Table.GetSize(), 1u);
}

TEST(RadientMaterialSRBTableTest, RemovesReleasedEntryDuringPrepare)
{
    RadientMaterialSRBTable Table;

    {
        const RadientMaterialSRBLease Lease = TestMaterialSRBRecipe{1}.Acquire(Table);
        ASSERT_TRUE(Lease);
    }
    EXPECT_EQ(Table.GetSize(), 1u);

    Uint32 CreateCount = 0;
    ASSERT_EQ(Table.Prepare(
                  1,
                  ResolveTestSRV,
                  [&CreateCount](ITextureView* const*, Uint32) {
                      ++CreateCount;
                      return MakeTestSRB();
                  }),
              RADIENT_STATUS_OK);
    EXPECT_EQ(CreateCount, 0u);
    EXPECT_EQ(Table.GetSize(), 0u);

    const RadientMaterialSRBLease Lease = TestMaterialSRBRecipe{2}.Acquire(Table);
    ASSERT_TRUE(Lease);
    EXPECT_EQ(Table.GetSize(), 1u);
}

TEST(RadientMaterialSRBTableTest, RejectsInvalidRecipe)
{
    RadientMaterialSRBTable                 Table;
    RadientMaterialTextureRenderData        InvalidSlot;
    const RadientMaterialTextureRenderData* pInvalidSlot = &InvalidSlot;

    EXPECT_FALSE(Table.Acquire(&pInvalidSlot, 1));
    EXPECT_EQ(Table.GetSize(), 0u);
}

TEST(RadientMaterialSRBTableTest, DefaultMappingUsesSemanticSlots)
{
    TestMaterialTextureArray Textures;
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = MakeBinding(1);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = MakeBinding(2);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC]  = MakeBinding(3);

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP |
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP);

    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       Lease;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;
    ASSERT_EQ(AcquireTestMaterialSRB(Table, Flags, 3, Textures, MakeDefaultBindings(), Lease, ShaderTextureIds),
              RADIENT_STATUS_OK);

    EXPECT_TRUE(Lease);
    EXPECT_EQ(Table.GetSize(), 1u);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 2u);
}

TEST(RadientMaterialSRBTableTest, DefaultMappingProducesCanonicalCompleteRecipes)
{
    const RadientMaterialDefaultTextureBindings Defaults = MakeDefaultBindings();

    TestMaterialTextureArray BaseColorTextures;
    BaseColorTextures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] =
        *Defaults.Get(PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR);

    TestMaterialTextureArray NormalTextures;
    NormalTextures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL] =
        *Defaults.Get(PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL);

    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       BaseColorLease;
    RadientMaterialSRBLease                       NormalLease;
    PBR_Renderer::StaticShaderTextureIdsArrayType BaseColorTextureIds;
    PBR_Renderer::StaticShaderTextureIdsArrayType NormalTextureIds;
    ASSERT_EQ(AcquireTestMaterialSRB(Table,
                                     PBR_Renderer::PSO_FLAG_USE_COLOR_MAP,
                                     8, BaseColorTextures, Defaults, BaseColorLease, BaseColorTextureIds),
              RADIENT_STATUS_OK);
    ASSERT_EQ(AcquireTestMaterialSRB(Table,
                                     PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP,
                                     8, NormalTextures, Defaults, NormalLease, NormalTextureIds),
              RADIENT_STATUS_OK);

    EXPECT_TRUE(BaseColorLease);
    EXPECT_TRUE(NormalLease);
    EXPECT_EQ(Table.GetSize(), 1u);
    for (size_t Slot = 0; Slot < 8; ++Slot)
    {
        EXPECT_EQ(BaseColorTextureIds[Slot], Slot);
        EXPECT_EQ(NormalTextureIds[Slot], Slot);
    }
}

TEST(RadientMaterialSRBTableTest, DefaultMappingKeepsDistinctSemanticSlots)
{
    TestMaterialTextureArray               Textures;
    const RadientMaterialTextureRenderData SharedTexture = MakeBinding(1);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = SharedTexture;
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = SharedTexture;

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);

    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       Lease;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;
    ASSERT_EQ(AcquireTestMaterialSRB(Table, Flags, 8, Textures, MakeDefaultBindings(), Lease, ShaderTextureIds),
              RADIENT_STATUS_OK);

    EXPECT_TRUE(Lease);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE], 6u);
}

TEST(RadientMaterialSRBTableTest, CompactMappingGroupsMatchingLogicalViews)
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

    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       Lease;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;
    ASSERT_EQ(AcquireTestMaterialSRB(Table, Flags, 2, Textures, MakeDefaultBindings(), Lease, ShaderTextureIds),
              RADIENT_STATUS_OK);

    EXPECT_TRUE(Lease);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE], 0u);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 1u);
    EXPECT_EQ(ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC], 1u);
}

TEST(RadientMaterialSRBTableTest, ShaderMappingDoesNotAffectSRBIdentity)
{
    const RadientMaterialTextureRenderData SharedTexture = MakeBinding(1);

    TestMaterialTextureArray BaseColorTextures;
    BaseColorTextures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = SharedTexture;

    TestMaterialTextureArray NormalTextures;
    NormalTextures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL] = SharedTexture;

    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       BaseColorLease;
    RadientMaterialSRBLease                       NormalLease;
    PBR_Renderer::StaticShaderTextureIdsArrayType BaseColorTextureIds;
    PBR_Renderer::StaticShaderTextureIdsArrayType NormalTextureIds;
    ASSERT_EQ(AcquireTestMaterialSRB(Table,
                                     PBR_Renderer::PSO_FLAG_USE_COLOR_MAP,
                                     1, BaseColorTextures, MakeDefaultBindings(), BaseColorLease, BaseColorTextureIds),
              RADIENT_STATUS_OK);
    ASSERT_EQ(AcquireTestMaterialSRB(Table,
                                     PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP,
                                     1, NormalTextures, MakeDefaultBindings(), NormalLease, NormalTextureIds),
              RADIENT_STATUS_OK);

    EXPECT_TRUE(BaseColorLease);
    EXPECT_TRUE(NormalLease);
    EXPECT_EQ(Table.GetSize(), 1u);
    EXPECT_EQ(BaseColorTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], 0u);
    EXPECT_EQ(BaseColorTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], PBR_Renderer::InvalidMaterialTextureId);
    EXPECT_EQ(NormalTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR], PBR_Renderer::InvalidMaterialTextureId);
    EXPECT_EQ(NormalTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL], 0u);
}

TEST(RadientMaterialSRBTableTest, CompactMappingDistinguishesTypedViews)
{
    TestMaterialTextureArray Textures;
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] =
        MakeBinding(1, TEX_FORMAT_RGBA8_UNORM_SRGB, RadientTextureViewType::SRGB);
    Textures[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL] =
        MakeBinding(1, TEX_FORMAT_RGBA8_UNORM, RadientTextureViewType::Linear);

    const auto Flags = static_cast<PBR_Renderer::PSO_FLAGS>(
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP);

    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       Lease;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;
    Testing::TestingEnvironment::ErrorScope       ExpectedErrors{"Material requires more than 1 distinct texture bindings"};
    EXPECT_EQ(AcquireTestMaterialSRB(Table, Flags, 1, Textures, MakeDefaultBindings(), Lease, ShaderTextureIds),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_FALSE(Lease);
    EXPECT_EQ(Table.GetSize(), 0u);
}

TEST(RadientMaterialSRBTableTest, RejectsMissingActiveTexture)
{
    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       Lease;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;
    Testing::TestingEnvironment::ErrorScope       ExpectedErrors{"Material texture 1 used by PBR texture attribute 1 is not initialized"};
    EXPECT_EQ(AcquireTestMaterialSRB(Table,
                                     PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP,
                                     8, {}, MakeDefaultBindings(), Lease, ShaderTextureIds),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_FALSE(Lease);
    EXPECT_EQ(Table.GetSize(), 0u);
}

TEST(RadientMaterialSRBTableTest, MaterialWithoutActiveTexturesUsesDefaults)
{
    const RadientMaterialDefaultTextureBindings   Defaults = MakeDefaultBindings();
    RadientMaterialSRBTable                       Table;
    RadientMaterialSRBLease                       Lease;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;
    ASSERT_EQ(AcquireTestMaterialSRB(Table,
                                     PBR_Renderer::PSO_FLAG_NONE,
                                     8, {}, Defaults, Lease, ShaderTextureIds),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Lease);
    EXPECT_EQ(Table.GetSize(), 1u);
    for (Uint32 Slot = 0; Slot < 8; ++Slot)
        EXPECT_EQ(ShaderTextureIds[Slot], Slot);
}

} // namespace Diligent
