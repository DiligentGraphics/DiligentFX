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

#include "RadientMaterialBindingsStorage.hpp"

#include <cstring>
#include <memory>
#include <utility>

using namespace Diligent;

namespace
{

RadientMaterialBindingsComponent MakeBindings(RadientMaterialBinding* pBindings, Uint32 BindingCount)
{
    RadientMaterialBindingsComponent Bindings;
    Bindings.pBindings    = pBindings;
    Bindings.BindingCount = BindingCount;
    return Bindings;
}

TEST(RadientMaterialBindingsStorageTest, AssignCopiesURIs)
{
    const char OriginalURI[] = "material://heap";
    const char ChangedURI[]  = "material://changed";

    std::unique_ptr<char[]> MaterialURI = std::make_unique<char[]>(64);
    std::memcpy(MaterialURI.get(), OriginalURI, sizeof(OriginalURI));

    RadientMaterialBinding Bindings[2];
    Bindings[0].PrimitiveIndex   = 3;
    Bindings[0].Material.URI     = MaterialURI.get();
    Bindings[0].Material.Version = 17;
    Bindings[1].PrimitiveIndex   = 5;
    Bindings[1].Material.URI     = nullptr;
    Bindings[1].Material.Version = 19;

    MaterialBindingsStorage Storage;
    Storage.Assign(MakeBindings(Bindings, 2));

    std::memcpy(MaterialURI.get(), ChangedURI, sizeof(ChangedURI));
    MaterialURI.reset();

    ASSERT_EQ(Storage.Component.BindingCount, 2u);
    ASSERT_NE(Storage.Component.pBindings, nullptr);
    EXPECT_EQ(Storage.Component.pBindings, Storage.Bindings.data());
    EXPECT_STREQ(Storage.Component.pBindings[0].Material.URI, OriginalURI);
    EXPECT_EQ(Storage.Component.pBindings[0].Material.URI, Storage.MaterialURIs[0].c_str());
    EXPECT_EQ(Storage.Component.pBindings[0].Material.Version, 17u);
    EXPECT_EQ(Storage.Component.pBindings[1].Material.URI, nullptr);
    EXPECT_EQ(Storage.Component.pBindings[1].Material.Version, 19u);
}

TEST(RadientMaterialBindingsStorageTest, MoveConstructorRepairsURIs)
{
    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex   = 1;
    Binding.Material.URI     = "material://move";
    Binding.Material.Version = 23;

    MaterialBindingsStorage Source;
    Source.Assign(MakeBindings(&Binding, 1));

    MaterialBindingsStorage Moved{std::move(Source)};

    ASSERT_EQ(Moved.Component.BindingCount, 1u);
    ASSERT_NE(Moved.Component.pBindings, nullptr);
    EXPECT_EQ(Moved.Component.pBindings, Moved.Bindings.data());
    EXPECT_STREQ(Moved.Component.pBindings[0].Material.URI, "material://move");
    EXPECT_EQ(Moved.Component.pBindings[0].Material.URI, Moved.MaterialURIs[0].c_str());
    EXPECT_EQ(Moved.Component.pBindings[0].Material.Version, 23u);
}

TEST(RadientMaterialBindingsStorageTest, MoveAssignmentRepairsURIs)
{
    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex   = 2;
    Binding.Material.URI     = "material://assigned";
    Binding.Material.Version = 29;

    MaterialBindingsStorage Source;
    Source.Assign(MakeBindings(&Binding, 1));

    RadientMaterialBinding OtherBinding;
    OtherBinding.PrimitiveIndex   = 0;
    OtherBinding.Material.URI     = "material://other";
    OtherBinding.Material.Version = 1;

    MaterialBindingsStorage Destination;
    Destination.Assign(MakeBindings(&OtherBinding, 1));
    Destination = std::move(Source);

    ASSERT_EQ(Destination.Component.BindingCount, 1u);
    ASSERT_NE(Destination.Component.pBindings, nullptr);
    EXPECT_EQ(Destination.Component.pBindings, Destination.Bindings.data());
    EXPECT_STREQ(Destination.Component.pBindings[0].Material.URI, "material://assigned");
    EXPECT_EQ(Destination.Component.pBindings[0].Material.URI, Destination.MaterialURIs[0].c_str());
    EXPECT_EQ(Destination.Component.pBindings[0].Material.Version, 29u);
}

TEST(RadientMaterialBindingsStorageTest, EmptyBindingsRemainEmptyAfterMoveAssignment)
{
    MaterialBindingsStorage Source;
    Source.Assign({});

    RadientMaterialBinding Binding;
    Binding.PrimitiveIndex   = 0;
    Binding.Material.URI     = "material://other";
    Binding.Material.Version = 1;

    MaterialBindingsStorage Destination;
    Destination.Assign(MakeBindings(&Binding, 1));
    Destination = std::move(Source);

    EXPECT_EQ(Destination.Component.pBindings, nullptr);
    EXPECT_EQ(Destination.Component.BindingCount, 0u);
    EXPECT_TRUE(Destination.Bindings.empty());
    EXPECT_TRUE(Destination.MaterialURIs.empty());
}

} // namespace
