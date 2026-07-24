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
#include "gtest/gtest.h"

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

RadientMaterialTextureBindingPlan MakePlan(std::initializer_list<PBR_Renderer::TEXTURE_ATTRIB_ID> TextureAttribIds)
{
    RadientMaterialTextureBindingPlan Plan;
    for (const PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId : TextureAttribIds)
    {
        Plan.ShaderTextureIds[TextureAttribId] = static_cast<Uint16>(Plan.Slots.size());
        Plan.Slots.push_back({TextureAttribId});
    }
    return Plan;
}

ITextureView* GetTestSRV(size_t Id)
{
    return reinterpret_cast<ITextureView*>(Id);
}

} // namespace

TEST(RadientMaterialSRBTableTest, ReusesSRBForSameSlotIdentities)
{
    RadientMaterialSRBTable        Table;
    RadientMaterialTextureSRVArray TextureSRVs{};
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = GetTestSRV(1);
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = GetTestSRV(2);

    const RadientMaterialTextureBindingPlan Plan = MakePlan({
        PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR,
        PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL,
    });

    Uint32 CreateCount = 0;
    auto   CreateSRB   = [&CreateCount](ITextureView* const*, Uint32) {
        ++CreateCount;
        return MakeTestSRB();
    };

    const RadientMaterialSRBIndex FirstIndex  = Table.GetOrCreate(Plan, TextureSRVs, CreateSRB);
    const RadientMaterialSRBIndex SecondIndex = Table.GetOrCreate(Plan, TextureSRVs, CreateSRB);

    ASSERT_NE(FirstIndex, InvalidRadientMaterialSRBIndex);
    EXPECT_EQ(SecondIndex, FirstIndex);
    EXPECT_EQ(CreateCount, 1u);
    EXPECT_EQ(Table.GetSize(), 1u);
    EXPECT_NE(Table.Get(FirstIndex), nullptr);
}

TEST(RadientMaterialSRBTableTest, IgnoresSemanticMappingAndShaderTextureIds)
{
    RadientMaterialSRBTable Table;

    RadientMaterialTextureSRVArray FirstSRVs{};
    FirstSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = GetTestSRV(1);
    FirstSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = GetTestSRV(2);
    RadientMaterialTextureBindingPlan FirstPlan           = MakePlan({
        PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR,
        PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL,
    });

    RadientMaterialTextureSRVArray SecondSRVs{};
    SecondSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]  = GetTestSRV(1);
    SecondSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC] = GetTestSRV(2);
    RadientMaterialTextureBindingPlan SecondPlan          = MakePlan({
        PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE,
        PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC,
    });
    SecondPlan.ShaderTextureIds.fill(7);

    Uint32 CreateCount = 0;
    auto   CreateSRB   = [&CreateCount](ITextureView* const*, Uint32) {
        ++CreateCount;
        return MakeTestSRB();
    };

    const RadientMaterialSRBIndex FirstIndex  = Table.GetOrCreate(FirstPlan, FirstSRVs, CreateSRB);
    const RadientMaterialSRBIndex SecondIndex = Table.GetOrCreate(SecondPlan, SecondSRVs, CreateSRB);

    ASSERT_NE(FirstIndex, InvalidRadientMaterialSRBIndex);
    EXPECT_EQ(SecondIndex, FirstIndex);
    EXPECT_EQ(CreateCount, 1u);
    EXPECT_EQ(Table.GetSize(), 1u);
}

TEST(RadientMaterialSRBTableTest, DistinguishesSlotOrderAndCount)
{
    RadientMaterialSRBTable        Table;
    RadientMaterialTextureSRVArray TextureSRVs{};
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = GetTestSRV(1);
    TextureSRVs[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = GetTestSRV(2);

    Uint32 CreateCount = 0;
    auto   CreateSRB   = [&CreateCount](ITextureView* const*, Uint32) {
        ++CreateCount;
        return MakeTestSRB();
    };

    const RadientMaterialSRBIndex FirstIndex = Table.GetOrCreate(
        MakePlan({PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR, PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL}),
        TextureSRVs,
        CreateSRB);
    const RadientMaterialSRBIndex ReorderedIndex = Table.GetOrCreate(
        MakePlan({PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL, PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR}),
        TextureSRVs,
        CreateSRB);
    const RadientMaterialSRBIndex ShorterIndex = Table.GetOrCreate(
        MakePlan({PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR}),
        TextureSRVs,
        CreateSRB);

    EXPECT_NE(FirstIndex, InvalidRadientMaterialSRBIndex);
    EXPECT_NE(ReorderedIndex, InvalidRadientMaterialSRBIndex);
    EXPECT_NE(ShorterIndex, InvalidRadientMaterialSRBIndex);
    EXPECT_NE(ReorderedIndex, FirstIndex);
    EXPECT_NE(ShorterIndex, FirstIndex);
    EXPECT_EQ(CreateCount, 3u);
    EXPECT_EQ(Table.GetSize(), 3u);
}

TEST(RadientMaterialSRBTableTest, RejectsInvalidSlotData)
{
    RadientMaterialSRBTable        Table;
    RadientMaterialTextureSRVArray TextureSRVs{};

    RadientMaterialTextureBindingPlan Plan = MakePlan({PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR});

    bool CreateCalled = false;
    EXPECT_EQ(Table.GetOrCreate(
                  Plan,
                  TextureSRVs,
                  [&CreateCalled](ITextureView* const*, Uint32) {
                      CreateCalled = true;
                      return MakeTestSRB();
                  }),
              InvalidRadientMaterialSRBIndex);
    EXPECT_FALSE(CreateCalled);
    EXPECT_EQ(Table.GetSize(), 0u);
    EXPECT_EQ(Table.Get(0), nullptr);
}

} // namespace Diligent
