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

#include "Assets/RadientVertexLayout.hpp"
#include "Assets/RadientAssetValidation.hpp"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

using namespace Diligent;

extern "C" int                            RadientVertexLayout_C_TestHelpers(void);
extern "C" const RadientVertexLayoutDesc* RadientVertexLayout_C_GetAutomaticLayout(void);

namespace
{

struct LayoutData
{
    std::array<RadientVertexAttributeDesc, 4>    Attributes{{
        {"POSITION", 0, 0, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False},
        {"NORMAL", 0, 12, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False},
        {"TEXCOORD_0", 0, 24, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False},
        {"COLOR_0", 1, 0, RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, True},
    }};
    std::array<RadientVertexBufferLayoutDesc, 2> Buffers{{{32}, {4}}};

    RadientVertexLayoutDesc GetLayout() const
    {
        return {Attributes.data(), static_cast<Uint32>(Attributes.size()),
                Buffers.data(), static_cast<Uint32>(Buffers.size())};
    }
};

} // namespace

TEST(RadientVertexLayoutTest, ComputesComponentAndAttributeSizes)
{
    const struct
    {
        RADIENT_VERTEX_COMPONENT_TYPE Type;
        Uint32                        Size;
        Bool                          CanNormalize;
    } Formats[] = {
        {RADIENT_VERTEX_COMPONENT_TYPE_INT8, 1, True},
        {RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 1, True},
        {RADIENT_VERTEX_COMPONENT_TYPE_INT16, 2, True},
        {RADIENT_VERTEX_COMPONENT_TYPE_UINT16, 2, True},
        {RADIENT_VERTEX_COMPONENT_TYPE_INT32, 4, False},
        {RADIENT_VERTEX_COMPONENT_TYPE_UINT32, 4, False},
        {RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16, 2, False},
        {RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, False},
    };

    for (const auto& Format : Formats)
    {
        SCOPED_TRACE(static_cast<int>(Format.Type));
        EXPECT_EQ(GetRadientVertexComponentSize(Format.Type), Format.Size);
        RadientVertexAttributeDesc Attribute;
        Attribute.ComponentType = Format.Type;
        for (Uint32 Count = 1; Count <= 4; ++Count)
        {
            Attribute.ComponentCount = Count;
            Attribute.Normalized     = False;
            EXPECT_EQ(GetRadientVertexAttributeSize(Attribute), Format.Size * Count);
            Attribute.Normalized = True;
            EXPECT_EQ(GetRadientVertexAttributeSize(Attribute), Format.CanNormalize ? Format.Size * Count : 0u);
        }
    }

    EXPECT_EQ(GetRadientVertexComponentSize(RADIENT_VERTEX_COMPONENT_TYPE_UNKNOWN), 0u);
    EXPECT_EQ(GetRadientVertexComponentSize(static_cast<RADIENT_VERTEX_COMPONENT_TYPE>(255)), 0u);
    EXPECT_EQ(GetRadientVertexAttributeSize(RadientVertexAttributeDesc{}), 0u);
    RadientVertexAttributeDesc Attribute{"_VALUE", 0, 0, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 5, False};
    EXPECT_EQ(GetRadientVertexAttributeSize(Attribute), 0u);
    Attribute.ComponentCount = 0;
    EXPECT_EQ(GetRadientVertexAttributeSize(Attribute), 0u);
}

TEST(RadientVertexLayoutTest, ValidatesInterleavedAndSeparateBuffers)
{
    LayoutData Data;
    EXPECT_TRUE(ValidateVertexLayout(Data.GetLayout()));

    const std::array<RadientVertexBufferLayoutDesc, 4> Buffers{{{12}, {12}, {8}, {4}}};
    for (Uint32 Index = 0; Index < Data.Attributes.size(); ++Index)
    {
        Data.Attributes[Index].BufferIndex = Index;
        Data.Attributes[Index].ByteOffset  = 0;
    }
    RadientVertexLayoutDesc Layout = Data.GetLayout();
    Layout.pBuffers                = Buffers.data();
    Layout.BufferCount             = static_cast<Uint32>(Buffers.size());
    EXPECT_TRUE(ValidateVertexLayout(Layout));
}

TEST(RadientVertexLayoutTest, AllowsPaddingUnalignedSourcesAndLargeStrides)
{
    LayoutData Data;
    for (auto& Attribute : Data.Attributes)
    {
        if (Attribute.BufferIndex == 0)
            Attribute.ByteOffset += 1;
    }
    Data.Buffers[0].ByteStride = 33;
    EXPECT_TRUE(ValidateVertexLayout(Data.GetLayout()));
    Data.Buffers[0].ByteStride = 1024;
    EXPECT_TRUE(ValidateVertexLayout(Data.GetLayout()));
}

TEST(RadientVertexLayoutTest, AllowsCustomSemanticsAndAliasedSourceAttributes)
{
    LayoutData Data;
    Data.Attributes[0].Semantic = "_CUSTOM";
    Data.Attributes[1]          = Data.Attributes[0];
    Data.Attributes[1].Semantic = "_ALIAS";
    EXPECT_TRUE(ValidateVertexLayout(Data.GetLayout()));

    // Generic layout validation does not enforce semantic-specific encodings.
    Data.Attributes[0].ComponentType  = RADIENT_VERTEX_COMPONENT_TYPE_UINT16;
    Data.Attributes[0].ComponentCount = 1;
    EXPECT_TRUE(ValidateVertexLayout(Data.GetLayout()));
}

TEST(RadientVertexLayoutTest, ValidatesEmptyAndMissingArrays)
{
    const RadientVertexLayoutDesc Empty;
    EXPECT_TRUE(ValidateVertexLayout(Empty));
    EXPECT_TRUE(AreVertexLayoutsCompatible(Empty, Empty));
    EXPECT_FALSE(AreVertexBuffersCompatible(Empty, 0, Empty, 0));

    LayoutData Data;
    auto       Layout  = Data.GetLayout();
    Layout.pAttributes = nullptr;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    Layout          = Data.GetLayout();
    Layout.pBuffers = nullptr;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    Layout                = Data.GetLayout();
    Layout.AttributeCount = 0;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    Layout             = Data.GetLayout();
    Layout.BufferCount = 0;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    Layout.AttributeCount = 0;
    EXPECT_TRUE(ValidateVertexLayout(Layout));
    EXPECT_TRUE(AreVertexLayoutsCompatible(Layout, Empty));
}

TEST(RadientVertexLayoutTest, RejectsInvalidAttributes)
{
    const auto ExpectInvalid = [](const RadientVertexAttributeDesc& Attribute) {
        LayoutData Data;
        Data.Attributes[0] = Attribute;
        const auto Layout  = Data.GetLayout();
        EXPECT_FALSE(ValidateVertexLayout(Layout));
        EXPECT_FALSE(AreVertexLayoutsCompatible(Layout, Layout));
        EXPECT_FALSE(AreVertexBuffersCompatible(Layout, 0, Layout, 0));
    };
    LayoutData Data;
    auto       Attribute = Data.Attributes[0];
    Attribute.Semantic   = nullptr;
    ExpectInvalid(Attribute);
    Attribute.Semantic = "";
    ExpectInvalid(Attribute);
    Attribute             = Data.Attributes[0];
    Attribute.BufferIndex = 2;
    ExpectInvalid(Attribute);
    Attribute               = Data.Attributes[0];
    Attribute.ComponentType = static_cast<RADIENT_VERTEX_COMPONENT_TYPE>(255);
    ExpectInvalid(Attribute);
    Attribute                = Data.Attributes[0];
    Attribute.ComponentCount = 0;
    ExpectInvalid(Attribute);
    Attribute.ComponentCount = 5;
    ExpectInvalid(Attribute);
    Attribute            = Data.Attributes[0];
    Attribute.Normalized = True;
    ExpectInvalid(Attribute);
    Attribute            = Data.Attributes[0];
    Attribute.ByteOffset = 24;
    ExpectInvalid(Attribute);
}

TEST(RadientVertexLayoutTest, RejectsDuplicateSemanticsByContentAcrossBuffers)
{
    LayoutData        Data;
    const std::string DuplicateName = Data.Attributes[0].Semantic;
    Data.Attributes[3].Semantic     = DuplicateName.c_str();
    EXPECT_FALSE(ValidateVertexLayout(Data.GetLayout()));
    Data.Attributes[3].Semantic = "position";
    EXPECT_TRUE(ValidateVertexLayout(Data.GetLayout()));
}

TEST(RadientVertexLayoutTest, RejectsZeroStrideAndChecksRangesWithoutOverflow)
{
    LayoutData Data;
    Data.Buffers[0].ByteStride = 0;
    EXPECT_FALSE(ValidateVertexLayout(Data.GetLayout()));
    Data.Buffers[0].ByteStride = 11;
    EXPECT_FALSE(ValidateVertexLayout(Data.GetLayout()));

    const Uint32                  Max = (std::numeric_limits<Uint32>::max)();
    RadientVertexAttributeDesc    Attribute{"_VALUE", 0, Max - 5, RADIENT_VERTEX_COMPONENT_TYPE_UINT32, 1, False};
    RadientVertexBufferLayoutDesc Buffer{Max - 1};
    const RadientVertexLayoutDesc Layout{&Attribute, 1, &Buffer, 1};
    EXPECT_TRUE(ValidateVertexLayout(Layout));
    Attribute.ByteOffset = Max - 4;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    Attribute.ByteOffset = Max - 3;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
}

TEST(RadientVertexLayoutTest, ResolvesDefaultOffsetsAndStridesWithoutChangingInput)
{
    RadientVertexAttributeDesc Attribute;
    Attribute.Semantic       = "POSITION";
    Attribute.ComponentType  = RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32;
    Attribute.ComponentCount = 3;
    const RadientVertexBufferLayoutDesc Buffer;
    const RadientVertexLayoutDesc       Layout{&Attribute, 1, &Buffer, 1};

    ResolvedVertexLayout Resolved;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{0}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{12}));
    EXPECT_EQ(Attribute.ByteOffset, RADIENT_VERTEX_AUTO_OFFSET);
    EXPECT_EQ(Buffer.ByteStride, RADIENT_VERTEX_AUTO_STRIDE);
    EXPECT_TRUE(ValidateVertexLayout(Layout));
}

TEST(RadientVertexLayoutTest, ResolvesAutomaticPackingIndependentlyPerBuffer)
{
    const LayoutData Explicit;
    LayoutData       Automatic;
    // Interleave buffer references without changing attribute order within a buffer.
    Automatic.Attributes = {{Explicit.Attributes[0], Explicit.Attributes[3], Explicit.Attributes[1], Explicit.Attributes[2]}};
    for (auto& Attribute : Automatic.Attributes)
        Attribute.ByteOffset = RADIENT_VERTEX_AUTO_OFFSET;
    for (auto& Buffer : Automatic.Buffers)
        Buffer.ByteStride = RADIENT_VERTEX_AUTO_STRIDE;

    ResolvedVertexLayout Resolved;
    ASSERT_TRUE(ResolveVertexLayout(Automatic.GetLayout(), Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{0, 0, 12, 24}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{32, 4}));
    EXPECT_TRUE(AreVertexLayoutsCompatible(Explicit.GetLayout(), Automatic.GetLayout()));
    EXPECT_TRUE(AreVertexLayoutsCompatible(Automatic.GetLayout(), Explicit.GetLayout()));

    // Buffer compatibility also resolves automatic values when the slot indices differ.
    std::swap(Automatic.Buffers[0], Automatic.Buffers[1]);
    for (auto& Attribute : Automatic.Attributes)
        Attribute.BufferIndex = 1 - Attribute.BufferIndex;
    EXPECT_TRUE(AreVertexBuffersCompatible(Explicit.GetLayout(), 0, Automatic.GetLayout(), 1));
    EXPECT_TRUE(AreVertexBuffersCompatible(Automatic.GetLayout(), 0, Explicit.GetLayout(), 1));
    EXPECT_FALSE(AreVertexLayoutsCompatible(Explicit.GetLayout(), Automatic.GetLayout()));
}

TEST(RadientVertexLayoutTest, MixedOffsetsPreserveGapsAndAliasesWithoutAlignmentOrRewinding)
{
    const RadientVertexAttributeDesc Attributes[] = {
        {"_FIRST", 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 1, False},
        {"_HIGH", 0, 32, RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 1, False},
        {"_EARLY", 0, 3, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16, 2, False},
        {"_ALIAS", 0, 32, RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 1, False},
        {"_LAST", 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False},
    };
    RadientVertexBufferLayoutDesc Buffer;
    const RadientVertexLayoutDesc Layout{Attributes, 5, &Buffer, 1};
    ResolvedVertexLayout          Resolved;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{0, 32, 3, 32, 33}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{45}));

    Buffer.ByteStride = 64;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{64}));
    Buffer.ByteStride = 44;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    EXPECT_FALSE(ResolveVertexLayout(Layout, Resolved));
    // A failure does not publish partially resolved output.
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{0, 32, 3, 32, 33}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{64}));
}

TEST(RadientVertexLayoutTest, AutomaticAttributeOrderAffectsCompatibility)
{
    LayoutData Lhs;
    for (auto& Attribute : Lhs.Attributes)
        Attribute.ByteOffset = RADIENT_VERTEX_AUTO_OFFSET;
    for (auto& Buffer : Lhs.Buffers)
        Buffer.ByteStride = RADIENT_VERTEX_AUTO_STRIDE;
    LayoutData Rhs = Lhs;
    std::swap(Rhs.Attributes[0], Rhs.Attributes[1]);
    ASSERT_TRUE(ValidateVertexLayout(Lhs.GetLayout()));
    ASSERT_TRUE(ValidateVertexLayout(Rhs.GetLayout()));
    EXPECT_FALSE(AreVertexLayoutsCompatible(Lhs.GetLayout(), Rhs.GetLayout()));
    EXPECT_FALSE(AreVertexLayoutsCompatible(Rhs.GetLayout(), Lhs.GetLayout()));
    EXPECT_FALSE(AreVertexBuffersCompatible(Lhs.GetLayout(), 0, Rhs.GetLayout(), 0));
    EXPECT_TRUE(AreVertexBuffersCompatible(Lhs.GetLayout(), 1, Rhs.GetLayout(), 1));
}

TEST(RadientVertexLayoutTest, RejectsAutomaticStrideForUnusedBuffersAndClearsEmptyOutput)
{
    const RadientVertexAttributeDesc Attribute{"POSITION", 1, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False};
    RadientVertexBufferLayoutDesc    Buffers[2];
    RadientVertexLayoutDesc          Layout{&Attribute, 1, Buffers, 2};
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    EXPECT_FALSE(AreVertexBuffersCompatible(Layout, 1, Layout, 1));
    Buffers[0].ByteStride = 16;
    ResolvedVertexLayout Resolved;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{0}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{16, 12}));
    Buffers[0].ByteStride = 0;
    EXPECT_FALSE(ValidateVertexLayout(Layout));

    Layout.AttributeCount = 0;
    Layout.BufferCount    = 0;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_TRUE(Resolved.AttributeOffsets.empty());
    EXPECT_TRUE(Resolved.BufferStrides.empty());
}

TEST(RadientVertexLayoutTest, RejectsAutomaticPackingOverflowAndReservedResolvedValues)
{
    const Uint32               Max          = (std::numeric_limits<Uint32>::max)();
    RadientVertexAttributeDesc Attributes[] = {
        {"_HIGH", 0, Max - 2, RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 1, False},
        {"_NEXT", 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 1, False},
    };
    const RadientVertexBufferLayoutDesc Buffer;
    RadientVertexLayoutDesc             Layout{Attributes, 1, &Buffer, 1};
    ResolvedVertexLayout                Resolved;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{Max - 2}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{Max - 1}));

    Layout.AttributeCount = 2;
    EXPECT_FALSE(ValidateVertexLayout(Layout));
    Attributes[1].ComponentCount = 4;
    EXPECT_FALSE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{Max - 2}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{Max - 1}));

    Attributes[0].ByteOffset = RADIENT_VERTEX_AUTO_OFFSET;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{0, 1}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{5}));
}

TEST(RadientVertexLayoutTest, ResolvesAutomaticLayoutFromC)
{
    const RadientVertexLayoutDesc& Layout = *RadientVertexLayout_C_GetAutomaticLayout();
    ResolvedVertexLayout           Resolved;
    ASSERT_TRUE(ResolveVertexLayout(Layout, Resolved));
    EXPECT_EQ(Resolved.AttributeOffsets, (std::vector<Uint32>{0, 12}));
    EXPECT_EQ(Resolved.BufferStrides, (std::vector<Uint32>{20}));
}

TEST(RadientVertexLayoutTest, CompatibilityIgnoresAttributeOrderAndStringAddresses)
{
    LayoutData        Lhs;
    LayoutData        Rhs;
    const std::string Position = "POSITION";
    Rhs.Attributes[0].Semantic = Position.c_str();
    std::reverse(Rhs.Attributes.begin(), Rhs.Attributes.end());
    EXPECT_TRUE(AreVertexLayoutsCompatible(Lhs.GetLayout(), Rhs.GetLayout()));
    EXPECT_TRUE(AreVertexLayoutsCompatible(Rhs.GetLayout(), Lhs.GetLayout()));
}

TEST(RadientVertexLayoutTest, CompatibilityChecksEveryPartOfByteInterpretation)
{
    const LayoutData Lhs;
    const auto       ExpectIncompatible = [&Lhs](const LayoutData& Rhs) {
        EXPECT_TRUE(ValidateVertexLayout(Rhs.GetLayout()));
        EXPECT_FALSE(AreVertexLayoutsCompatible(Lhs.GetLayout(), Rhs.GetLayout()));
        EXPECT_FALSE(AreVertexLayoutsCompatible(Rhs.GetLayout(), Lhs.GetLayout()));
    };
    LayoutData Rhs;
    Rhs.Buffers[0].ByteStride = 36;
    ExpectIncompatible(Rhs);
    Rhs = Lhs;
    std::swap(Rhs.Attributes[0].ByteOffset, Rhs.Attributes[1].ByteOffset);
    ExpectIncompatible(Rhs);
    Rhs                             = Lhs;
    Rhs.Attributes[3].ComponentType = RADIENT_VERTEX_COMPONENT_TYPE_INT8;
    ExpectIncompatible(Rhs);
    Rhs                              = Lhs;
    Rhs.Attributes[3].ComponentCount = 3;
    ExpectIncompatible(Rhs);
    Rhs                          = Lhs;
    Rhs.Attributes[3].Normalized = False;
    ExpectIncompatible(Rhs);
    Rhs                        = Lhs;
    Rhs.Attributes[3].Semantic = "_COLOR";
    ExpectIncompatible(Rhs);
}

TEST(RadientVertexLayoutTest, MatchesIndividualBuffersAcrossDifferentBufferIndices)
{
    LayoutData Lhs;
    LayoutData Rhs;
    std::swap(Rhs.Buffers[0], Rhs.Buffers[1]);
    for (auto& Attribute : Rhs.Attributes)
        Attribute.BufferIndex = 1 - Attribute.BufferIndex;
    EXPECT_TRUE(ValidateVertexLayout(Rhs.GetLayout()));
    EXPECT_FALSE(AreVertexLayoutsCompatible(Lhs.GetLayout(), Rhs.GetLayout()));
    EXPECT_TRUE(AreVertexBuffersCompatible(Lhs.GetLayout(), 0, Rhs.GetLayout(), 1));
    EXPECT_TRUE(AreVertexBuffersCompatible(Lhs.GetLayout(), 1, Rhs.GetLayout(), 0));
    EXPECT_FALSE(AreVertexBuffersCompatible(Lhs.GetLayout(), 2, Rhs.GetLayout(), 0));
    EXPECT_FALSE(AreVertexBuffersCompatible(Lhs.GetLayout(), 0, Rhs.GetLayout(), 2));
}

TEST(RadientVertexLayoutTest, RejectsMissingAttributesEvenWhenStridesMatch)
{
    LayoutData Data;
    const auto Full        = Data.GetLayout();
    auto       Partial     = Full;
    Partial.AttributeCount = 2;
    EXPECT_TRUE(ValidateVertexLayout(Partial));
    EXPECT_FALSE(AreVertexLayoutsCompatible(Full, Partial));
    EXPECT_FALSE(AreVertexBuffersCompatible(Full, 0, Partial, 0));
    EXPECT_FALSE(AreVertexBuffersCompatible(Partial, 0, Full, 0));

    Partial                = Full;
    Partial.AttributeCount = 3;
    EXPECT_TRUE(AreVertexBuffersCompatible(Full, 0, Partial, 0));
    EXPECT_FALSE(AreVertexBuffersCompatible(Full, 1, Partial, 1));
    EXPECT_FALSE(AreVertexBuffersCompatible(Partial, 1, Full, 1));
    Partial.BufferCount = 1;
    EXPECT_FALSE(AreVertexLayoutsCompatible(Full, Partial));
}

TEST(RadientVertexLayoutTest, CallsPublicHelpersFromC)
{
    EXPECT_EQ(RadientVertexLayout_C_TestHelpers(), 0);
}
