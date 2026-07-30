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
#include "gtest/gtest.h"

#include <cstdint>
#include <thread>
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

RadientMaterialTextureRenderData MakeBinding(size_t         ResourceId,
                                             TEXTURE_FORMAT ViewFormat = TEX_FORMAT_RGBA8_UNORM)
{
    RadientMaterialTextureRenderData Binding;
    Binding.pTexture        = Testing::MakeTestTextureAsset();
    Binding.BindingIdentity = {
        static_cast<Int32>(ResourceId),
        ViewFormat,
    };
    return Binding;
}

RadientMaterialTextureBindingPlan MakePlan(std::initializer_list<size_t> ResourceIds)
{
    RadientMaterialTextureBindingPlan Plan;
    Uint32                            Slot = 0;
    for (const size_t ResourceId : ResourceIds)
    {
        Plan.ShaderTextureIds[Slot] = static_cast<Uint16>(Slot);
        Plan.Slots.push_back(MakeBinding(ResourceId));
        ++Slot;
    }
    return Plan;
}

ITextureView* ResolveTestSRV(const RadientMaterialTextureRenderData& Binding)
{
    return reinterpret_cast<ITextureView*>(static_cast<uintptr_t>(Binding.BindingIdentity.StandaloneResourceId));
}

} // namespace

TEST(RadientMaterialSRBTableTest, ReusesEntryForSameLogicalSlots)
{
    RadientMaterialSRBTable                 Table;
    const RadientMaterialTextureBindingPlan Plan   = MakePlan({1, 2});
    const RadientMaterialSRBLease           First  = Table.Acquire(Plan);
    const RadientMaterialSRBLease           Second = Table.Acquire(Plan);

    ASSERT_TRUE(First);
    EXPECT_EQ(Second.GetIndex(), First.GetIndex());
    EXPECT_EQ(Table.GetSize(), 1u);
    EXPECT_EQ(First.GetSRB(), nullptr);
}

TEST(RadientMaterialSRBTableTest, IgnoresShaderTextureMapping)
{
    RadientMaterialSRBTable           Table;
    RadientMaterialTextureBindingPlan FirstPlan  = MakePlan({1, 2});
    RadientMaterialTextureBindingPlan SecondPlan = FirstPlan;
    SecondPlan.ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
    SecondPlan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]  = 0;
    SecondPlan.ShaderTextureIds[PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC] = 1;

    const RadientMaterialSRBLease First  = Table.Acquire(FirstPlan);
    const RadientMaterialSRBLease Second = Table.Acquire(SecondPlan);

    ASSERT_TRUE(First);
    EXPECT_EQ(Second.GetIndex(), First.GetIndex());
    EXPECT_EQ(Table.GetSize(), 1u);
}

TEST(RadientMaterialSRBTableTest, DistinguishesSlotOrderCountAndFormat)
{
    RadientMaterialSRBTable Table;

    RadientMaterialTextureBindingPlan DifferentFormat   = MakePlan({1, 2});
    DifferentFormat.Slots[1].BindingIdentity.ViewFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;

    const RadientMaterialSRBLease First     = Table.Acquire(MakePlan({1, 2}));
    const RadientMaterialSRBLease Reordered = Table.Acquire(MakePlan({2, 1}));
    const RadientMaterialSRBLease Shorter   = Table.Acquire(MakePlan({1}));
    const RadientMaterialSRBLease TypedView = Table.Acquire(DifferentFormat);

    ASSERT_TRUE(First);
    EXPECT_NE(Reordered.GetIndex(), First.GetIndex());
    EXPECT_NE(Shorter.GetIndex(), First.GetIndex());
    EXPECT_NE(TypedView.GetIndex(), First.GetIndex());
    EXPECT_EQ(Table.GetSize(), 4u);
}

TEST(RadientMaterialSRBTableTest, MaterializesAndRefreshesEntryInPlace)
{
    RadientMaterialSRBTable       Table;
    const RadientMaterialSRBLease Lease = Table.Acquire(MakePlan({1, 2}));
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
    EXPECT_EQ(Lease.GetIndex(), 0u);
    EXPECT_EQ(CreateCount, 2u);
}

TEST(RadientMaterialSRBTableTest, ConcurrentAcquireReservesOneEntry)
{
    RadientMaterialSRBTable                 Table;
    const RadientMaterialTextureBindingPlan Plan = MakePlan({1, 2, 3});

    constexpr size_t                     ThreadCount = 16;
    std::vector<RadientMaterialSRBLease> Leases(ThreadCount);
    std::vector<std::thread>             Threads;
    Threads.reserve(ThreadCount);
    for (size_t Thread = 0; Thread < ThreadCount; ++Thread)
    {
        Threads.emplace_back([&Table, &Plan, &Leases, Thread]() {
            Leases[Thread] = Table.Acquire(Plan);
        });
    }
    for (std::thread& Thread : Threads)
        Thread.join();

    ASSERT_TRUE(Leases[0]);
    for (const RadientMaterialSRBLease& Lease : Leases)
        EXPECT_EQ(Lease.GetIndex(), Leases[0].GetIndex());
    EXPECT_EQ(Table.GetSize(), 1u);
}

TEST(RadientMaterialSRBTableTest, RejectsInvalidRecipe)
{
    RadientMaterialSRBTable           Table;
    RadientMaterialTextureBindingPlan Plan;
    Plan.Slots.resize(1);

    EXPECT_FALSE(Table.Acquire(Plan));
    EXPECT_EQ(Table.GetSize(), 0u);
}

} // namespace Diligent
