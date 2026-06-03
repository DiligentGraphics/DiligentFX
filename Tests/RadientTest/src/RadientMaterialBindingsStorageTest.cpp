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

#include "Scene/Components/RadientMaterialBindingsStorage.hpp"
#include "RadientTestAssetHelpers.hpp"

#include <utility>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

RadientMaterialBindingsComponent MakeBindings(RadientMaterialBinding* pBindings, Uint32 BindingCount)
{
    RadientMaterialBindingsComponent Bindings;
    Bindings.pBindings    = pBindings;
    Bindings.BindingCount = BindingCount;
    return Bindings;
}

TEST(RadientMaterialBindingsStorageTest, AssignKeepsStrongMaterialReferences)
{
    // Material binding storage should copy the binding array and retain each
    // material asset referenced by the caller.
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeTestMaterialAsset("material://heap", 17);

    RadientMaterialBinding Bindings[2];
    Bindings[0].PrimitiveIndex = 3;
    Bindings[0].pMaterial      = pMaterial;
    Bindings[1].PrimitiveIndex = 5;
    Bindings[1].pMaterial      = nullptr;

    MaterialBindingsStorage Storage;
    Storage.Assign(MakeBindings(Bindings, 2));

    const IRadientMaterialAsset* pExpectedMaterial = pMaterial;
    pMaterial.Release();

    ASSERT_EQ(Storage.Component.BindingCount, 2u);
    ASSERT_NE(Storage.Component.pBindings, nullptr);
    EXPECT_EQ(Storage.Component.pBindings, Storage.Bindings.data());
    EXPECT_EQ(Storage.Component.pBindings[0].pMaterial, pExpectedMaterial);
    EXPECT_EQ(Storage.Component.pBindings[0].pMaterial, Storage.Materials[0].RawPtr());
    ASSERT_NE(Storage.Component.pBindings[0].pMaterial, nullptr);
    EXPECT_STREQ(Storage.Component.pBindings[0].pMaterial->GetReference().URI, "material://heap");
    EXPECT_EQ(Storage.Component.pBindings[0].pMaterial->GetReference().Version, 17u);
    EXPECT_EQ(Storage.Component.pBindings[1].pMaterial, nullptr);
    EXPECT_EQ(Storage.Materials[1], nullptr);
}

TEST(RadientMaterialBindingsStorageTest, MoveConstructorRepairsMaterialPointers)
{
    // Moving storage must repair both the binding-array pointer and each
    // material pointer so they reference the moved-to retained assets.
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeTestMaterialAsset("material://move", 23);

    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex = 1;
    Binding.pMaterial      = pMaterial;

    MaterialBindingsStorage Source;
    Source.Assign(MakeBindings(&Binding, 1));

    MaterialBindingsStorage Moved{std::move(Source)};

    ASSERT_EQ(Moved.Component.BindingCount, 1u);
    ASSERT_NE(Moved.Component.pBindings, nullptr);
    EXPECT_EQ(Moved.Component.pBindings, Moved.Bindings.data());
    EXPECT_EQ(Moved.Component.pBindings[0].pMaterial, pMaterial.RawPtr());
    EXPECT_EQ(Moved.Component.pBindings[0].pMaterial, Moved.Materials[0].RawPtr());
    EXPECT_STREQ(Moved.Component.pBindings[0].pMaterial->GetReference().URI, "material://move");
    EXPECT_EQ(Moved.Component.pBindings[0].pMaterial->GetReference().Version, 23u);
}

TEST(RadientMaterialBindingsStorageTest, MoveAssignmentRepairsMaterialPointers)
{
    // Move assignment must replace any existing destination bindings and
    // reconnect raw pointers to the moved retained assets.
    RefCntAutoPtr<IRadientMaterialAsset> pMaterial      = MakeTestMaterialAsset("material://assigned", 29);
    RefCntAutoPtr<IRadientMaterialAsset> pOtherMaterial = MakeTestMaterialAsset("material://other", 1);

    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex = 2;
    Binding.pMaterial      = pMaterial;

    MaterialBindingsStorage Source;
    Source.Assign(MakeBindings(&Binding, 1));

    RadientMaterialBinding OtherBinding;
    OtherBinding.PrimitiveIndex = 0;
    OtherBinding.pMaterial      = pOtherMaterial;

    MaterialBindingsStorage Destination;
    Destination.Assign(MakeBindings(&OtherBinding, 1));
    Destination = std::move(Source);

    ASSERT_EQ(Destination.Component.BindingCount, 1u);
    ASSERT_NE(Destination.Component.pBindings, nullptr);
    EXPECT_EQ(Destination.Component.pBindings, Destination.Bindings.data());
    EXPECT_EQ(Destination.Component.pBindings[0].pMaterial, pMaterial.RawPtr());
    EXPECT_EQ(Destination.Component.pBindings[0].pMaterial, Destination.Materials[0].RawPtr());
    EXPECT_STREQ(Destination.Component.pBindings[0].pMaterial->GetReference().URI, "material://assigned");
    EXPECT_EQ(Destination.Component.pBindings[0].pMaterial->GetReference().Version, 29u);
}

TEST(RadientMaterialBindingsStorageTest, EmptyBindingsRemainEmptyAfterMoveAssignment)
{
    // Moving an empty binding set over a non-empty destination must leave the
    // public component in a clean empty state.
    MaterialBindingsStorage Source;
    Source.Assign({});

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial = MakeTestMaterialAsset("material://other", 1);

    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex = 0;
    Binding.pMaterial      = pMaterial;

    MaterialBindingsStorage Destination;
    Destination.Assign(MakeBindings(&Binding, 1));
    Destination = std::move(Source);

    EXPECT_EQ(Destination.Component.pBindings, nullptr);
    EXPECT_EQ(Destination.Component.BindingCount, 0u);
    EXPECT_TRUE(Destination.Bindings.empty());
    EXPECT_TRUE(Destination.Materials.empty());
}

} // namespace
