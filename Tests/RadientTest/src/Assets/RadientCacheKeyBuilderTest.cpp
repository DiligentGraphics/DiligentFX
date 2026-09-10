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

#include "Assets/RadientCacheKeyBuilder.hpp"

#include "gtest/gtest.h"

using namespace Diligent;

TEST(RadientCacheKeyBuilderTest, BuildsHumanReadableKey)
{
    RadientCacheKeyBuilder Builder{"texture", 2};
    Builder.AddString("uri", "a:base=b")
        .AddString("base", "")
        .AddInteger("format", 28)
        .AddInteger("size", Uint64{1} << 32)
        .AddBool("srgb", false);

    EXPECT_EQ(Builder.GetKey(),
              "texture:v2:uri=8:a:base=b:base=0::format=28:size=4294967296:srgb=0");
}

TEST(RadientCacheKeyBuilderTest, SerializesIntegerTypes)
{
    enum class TestEnum : Uint16
    {
        Value = 17
    };

    RadientCacheKeyBuilder Builder{"integer", 1};
    Builder.AddInteger("i8", Int8{-8})
        .AddInteger("u16", Uint16{65535})
        .AddInteger("i32", Int32{-2147483647})
        .AddInteger("u64", Uint64{1} << 32)
        .AddInteger("enum", TestEnum::Value);

    EXPECT_EQ(Builder.GetKey(),
              "integer:v1:i8=-8:u16=65535:i32=-2147483647:u64=4294967296:enum=17");

    RadientCacheKeyBuilder Uint8Builder{"integer", 1};
    Uint8Builder.AddInteger("value", Uint8{7});

    RadientCacheKeyBuilder Uint64Builder{"integer", 1};
    Uint64Builder.AddInteger("value", Uint64{7});

    EXPECT_EQ(Uint8Builder.GetKey(), Uint64Builder.GetKey());
}

TEST(RadientCacheKeyBuilderTest, LengthPrefixesStringValues)
{
    RadientCacheKeyBuilder BuilderA{"texture", 1};
    BuilderA.AddString("uri", "a:base=b")
        .AddString("base", "");

    RadientCacheKeyBuilder BuilderB{"texture", 1};
    BuilderB.AddString("uri", "a")
        .AddString("base", "b");

    EXPECT_NE(BuilderA.GetKey(), BuilderB.GetKey());
}
