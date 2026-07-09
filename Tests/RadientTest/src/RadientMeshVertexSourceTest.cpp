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

#include "Assets/RadientMeshVertexSource.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Diligent;

namespace
{

template <typename ValueType>
ValueType ReadValue(const std::vector<Uint8>& Buffer, size_t Offset)
{
    ValueType Value{};
    EXPECT_LE(Offset + sizeof(ValueType), Buffer.size());
    if (Offset + sizeof(ValueType) <= Buffer.size())
        std::memcpy(&Value, Buffer.data() + Offset, sizeof(ValueType));
    return Value;
}

RadientMeshCreateInfo MakeVertexMeshCI(const std::array<RadientFloat3, 2>& Positions)
{
    RadientMeshCreateInfo MeshCI{};
    MeshCI.pPositions  = Positions.data();
    MeshCI.VertexCount = static_cast<Uint32>(Positions.size());
    return MeshCI;
}

void ExpectFloat2Eq(const RadientFloat2& Actual, const RadientFloat2& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
}

void ExpectFloat3Eq(const RadientFloat3& Actual, const RadientFloat3& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
    EXPECT_FLOAT_EQ(Actual.z, Expected.z);
}

void ExpectFloat4Eq(const RadientFloat4& Actual, const RadientFloat4& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
    EXPECT_FLOAT_EQ(Actual.z, Expected.z);
    EXPECT_FLOAT_EQ(Actual.w, Expected.w);
}

const std::array<RadientFloat3, 2> DefaultPositions{
    RadientFloat3{1.f, 2.f, 3.f},
    RadientFloat3{4.f, 5.f, 6.f}};
const std::array<RadientFloat3, 2> DefaultNormals{
    RadientFloat3{0.f, 0.f, 1.f},
    RadientFloat3{0.f, 1.f, 0.f}};
const std::array<RadientFloat4, 2> DefaultTangents{
    RadientFloat4{1.f, 0.f, 0.f, 1.f},
    RadientFloat4{0.f, 1.f, 0.f, -1.f}};
const std::array<RadientFloat2, 2> DefaultTexCoords0{
    RadientFloat2{0.25f, 0.5f},
    RadientFloat2{0.75f, 1.f}};
const std::array<RadientColorRGBA8, 2> DefaultColors{
    RadientColorRGBA8{255, 128, 64, 32},
    RadientColorRGBA8{0, 64, 128, 255}};
const std::array<RadientBoneIndices4, 2> DefaultJoints{
    RadientBoneIndices4{1, 2, 3, 4},
    RadientBoneIndices4{5, 6, 7, 8}};
const std::array<RadientFloat4, 2> DefaultWeights{
    RadientFloat4{1.f, 0.f, 0.f, 0.f},
    RadientFloat4{0.25f, 0.25f, 0.25f, 0.25f}};

GLTF::VertexAttributeDesc GetDefaultAttribute(const char* Name)
{
    for (const GLTF::VertexAttributeDesc& Attrib : GLTF::DefaultVertexAttributes)
    {
        if (std::strcmp(Attrib.Name, Name) == 0)
            return Attrib;
    }
    return {};
}

struct PackedDefaultAttribute
{
    std::unique_ptr<RadientMeshVertexSource> Source;
    Uint32                                   BufferIndex = 0;
    std::vector<Uint8>                       Buffer;
};

PackedDefaultAttribute PackDefaultAttribute(GLTF::VertexAttributeDesc DstAttrib)
{
    RadientMeshCreateInfo MeshCI = MakeVertexMeshCI(DefaultPositions);
    if (std::strcmp(DstAttrib.Name, GLTF::NormalAttributeName) == 0)
        MeshCI.pNormals = DefaultNormals.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::TangentAttributeName) == 0)
        MeshCI.pTangents = DefaultTangents.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::Texcoord0AttributeName) == 0)
        MeshCI.pTexCoords0 = DefaultTexCoords0.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::VertexColorAttributeName) == 0)
        MeshCI.pColors0 = DefaultColors.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::JointsAttributeName) == 0 ||
             std::strcmp(DstAttrib.Name, GLTF::WeightsAttributeName) == 0)
    {
        MeshCI.pBoneIndices0 = DefaultJoints.data();
        MeshCI.pBoneWeights0 = DefaultWeights.data();
    }

    PackedDefaultAttribute Result;
    Result.Source.reset(new RadientMeshVertexSource{MeshCI});
    EXPECT_EQ(Result.Source->GetStatus(), RADIENT_STATUS_OK);
    if (Result.Source->GetStatus() != RADIENT_STATUS_OK)
        return Result;

    DstAttrib.RelativeOffset = GLTF::VertexAttributeDesc{}.RelativeOffset;

    if (std::strcmp(DstAttrib.Name, GLTF::PositionAttributeName) == 0)
    {
        DstAttrib.BufferId = 0;
        const std::array<GLTF::VertexAttributeDesc, 1> Attributes{DstAttrib};
        EXPECT_EQ(Result.Source->SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())),
                  RADIENT_STATUS_OK);
        Result.BufferIndex = 0;
    }
    else
    {
        GLTF::VertexAttributeDesc Position = GetDefaultAttribute(GLTF::PositionAttributeName);
        DstAttrib.BufferId                 = 1;

        const std::array<GLTF::VertexAttributeDesc, 2> Attributes{Position, DstAttrib};
        EXPECT_EQ(Result.Source->SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())),
                  RADIENT_STATUS_OK);
        Result.BufferIndex = 1;
    }

    if (Result.BufferIndex >= Result.Source->GetVertexBufferCount())
        return Result;

    const Uint32 BufferSize = Result.Source->GetVertexBufferDataSize(Result.BufferIndex);
    EXPECT_NE(BufferSize, 0u);
    Result.Buffer.resize(BufferSize);
    EXPECT_EQ(Result.Source->PackVertexData(Result.BufferIndex,
                                            RadientMeshVertexSource::PackDestination{Result.Buffer.data(), BufferSize}),
              RADIENT_STATUS_OK);

    return Result;
}

} // namespace

TEST(RadientMeshVertexSourceTest, RejectsInvalidRadientCreateInfo)
{
    RadientMeshCreateInfo MeshCI = MakeVertexMeshCI(DefaultPositions);

    RadientMeshVertexSource ValidSource{MeshCI};
    EXPECT_EQ(ValidSource.GetStatus(), RADIENT_STATUS_OK);

    RadientMeshCreateInfo InvalidMeshCI = MeshCI;
    InvalidMeshCI.pPositions            = nullptr;
    RadientMeshVertexSource MissingPositions{InvalidMeshCI};
    EXPECT_EQ(MissingPositions.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    InvalidMeshCI             = MeshCI;
    InvalidMeshCI.VertexCount = 0;
    RadientMeshVertexSource EmptyVertices{InvalidMeshCI};
    EXPECT_EQ(EmptyVertices.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    std::array<RadientBoneIndices4, 2> BoneIndices{};
    InvalidMeshCI               = MeshCI;
    InvalidMeshCI.pBoneIndices0 = BoneIndices.data();
    RadientMeshVertexSource MissingWeights{InvalidMeshCI};
    EXPECT_EQ(MissingWeights.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshVertexSourceTest, RejectsInvalidSourceCreateInfo)
{
    struct SourceVertex
    {
        RadientFloat3 Position;
        RadientFloat2 TexCoord0;
    };

    std::array<SourceVertex, 2> Vertices{
        SourceVertex{RadientFloat3{1.f, 2.f, 3.f}, RadientFloat2{0.25f, 0.5f}},
        SourceVertex{RadientFloat3{4.f, 5.f, 6.f}, RadientFloat2{0.75f, 1.f}}};
    std::array<RadientMeshVertexSource::SourceAttribute, 2> Attributes{
        RadientMeshVertexSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, &Vertices[0].Position, sizeof(SourceVertex)},
        RadientMeshVertexSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, &Vertices[0].TexCoord0, sizeof(SourceVertex)}};

    RadientMeshVertexSource::CreateInfo CI{};
    CI.pAttributes    = Attributes.data();
    CI.AttributeCount = static_cast<Uint32>(Attributes.size());
    CI.VertexCount    = static_cast<Uint32>(Vertices.size());

    auto ExpectInvalid = [](const RadientMeshVertexSource::CreateInfo& InvalidCI) //
    {
        RadientMeshVertexSource Source{InvalidCI};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    RadientMeshVertexSource::CreateInfo InvalidCI = CI;
    InvalidCI.pAttributes                         = nullptr;
    ExpectInvalid(InvalidCI);

    InvalidCI             = CI;
    Attributes[0].Stride  = sizeof(RadientFloat3) - 1;
    InvalidCI.pAttributes = Attributes.data();
    ExpectInvalid(InvalidCI);
    Attributes[0].Stride = sizeof(SourceVertex);

    InvalidCI             = CI;
    Attributes[1].Name    = GLTF::PositionAttributeName;
    InvalidCI.pAttributes = Attributes.data();
    ExpectInvalid(InvalidCI);
}

TEST(RadientMeshVertexSourceTest, RejectsInvalidVertexAttributes)
{
    auto ExpectInvalidAttributes =
        [](const GLTF::VertexAttributeDesc* pDstAttributes, Uint32 NumDstAttributes) //
    {
        RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions)};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(Source.SetVertexAttributes(pDstAttributes, NumDstAttributes), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    ExpectInvalidAttributes(nullptr, 1);
    ExpectInvalidAttributes(GLTF::DefaultVertexAttributes.data(), 0);

    const std::array<GLTF::VertexAttributeDesc, 1> MissingPosition{
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4}};
    ExpectInvalidAttributes(MissingPosition.data(), static_cast<Uint32>(MissingPosition.size()));

    const std::array<GLTF::VertexAttributeDesc, 1> UnsupportedDstType{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_UINT8, 3}};
    ExpectInvalidAttributes(UnsupportedDstType.data(), static_cast<Uint32>(UnsupportedDstType.size()));

    const std::array<GLTF::VertexAttributeDesc, 1> TooManyComponents{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 5}};
    ExpectInvalidAttributes(TooManyComponents.data(), static_cast<Uint32>(TooManyComponents.size()));

    const std::array<GLTF::VertexAttributeDesc, 2> OverlappingSameBuffer{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{0}}};
    ExpectInvalidAttributes(OverlappingSameBuffer.data(), static_cast<Uint32>(OverlappingSameBuffer.size()));
}

TEST(RadientMeshVertexSourceTest, PacksStridedSourceAttributes)
{
    struct SourceVertex
    {
        RadientFloat3     Position;
        RadientColorRGBA8 Color;
        RadientFloat2     TexCoord0;
    };

    std::array<SourceVertex, 2> Vertices{
        SourceVertex{RadientFloat3{1.f, 2.f, 3.f}, RadientColorRGBA8{255, 128, 0, 64}, RadientFloat2{0.25f, 0.5f}},
        SourceVertex{RadientFloat3{4.f, 5.f, 6.f}, RadientColorRGBA8{0, 64, 128, 255}, RadientFloat2{0.75f, 1.f}}};

    std::array<RadientMeshVertexSource::SourceAttribute, 3> SourceAttributes{
        RadientMeshVertexSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, &Vertices[0].Position, sizeof(SourceVertex)},
        RadientMeshVertexSource::SourceAttribute{GLTF::VertexColorAttributeName, VT_UINT8, 4, true, &Vertices[0].Color, sizeof(SourceVertex)},
        RadientMeshVertexSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, &Vertices[0].TexCoord0, sizeof(SourceVertex)}};

    RadientMeshVertexSource::CreateInfo CI{};
    CI.pAttributes    = SourceAttributes.data();
    CI.AttributeCount = static_cast<Uint32>(SourceAttributes.size());
    CI.VertexCount    = static_cast<Uint32>(Vertices.size());

    RadientMeshVertexSource Source{CI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    Vertices[0] = {};

    const std::array<GLTF::VertexAttributeDesc, 3> Attributes{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3},
        GLTF::VertexAttributeDesc{GLTF::Texcoord0AttributeName, 0, VT_FLOAT32, 2},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 1, VT_FLOAT32, 4}};
    ASSERT_EQ(Source.SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())), RADIENT_STATUS_OK);

    ASSERT_EQ(Source.GetVertexBufferCount(), 2u);
    EXPECT_EQ(Source.GetActiveVertexBufferMask(), 1u | 2u);
    EXPECT_EQ(Source.GetVertexStride(0), 20u);
    EXPECT_EQ(Source.GetVertexStride(1), 16u);

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));
    std::vector<Uint8> Buffer1(Source.GetVertexBufferDataSize(1));

    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshVertexSource::PackDestination{Buffer0.data(),
                                                                             static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.PackVertexData(1,
                                    RadientMeshVertexSource::PackDestination{Buffer1.data(),
                                                                             static_cast<Uint32>(Buffer1.size())}),
              RADIENT_STATUS_OK);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), RadientFloat3{1.f, 2.f, 3.f});
    ExpectFloat2Eq(ReadValue<RadientFloat2>(Buffer0, 12), RadientFloat2{0.25f, 0.5f});
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 20), RadientFloat3{4.f, 5.f, 6.f});
    ExpectFloat2Eq(ReadValue<RadientFloat2>(Buffer0, 32), RadientFloat2{0.75f, 1.f});

    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer1, 0),
                   RadientFloat4{1.f, 128.f / 255.f, 0.f, 64.f / 255.f});
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer1, 16),
                   RadientFloat4{0.f, 64.f / 255.f, 128.f / 255.f, 1.f});
}

TEST(RadientMeshVertexSourceTest, BorrowsSourceDataAndKeepsOwnerAlive)
{
    struct SourceVertex
    {
        RadientFloat3 Position;
        RadientFloat2 TexCoord0;
    };

    struct SourceData
    {
        std::array<SourceVertex, 2> Vertices;
    };

    std::weak_ptr<const void>                WeakOwner;
    std::unique_ptr<RadientMeshVertexSource> Source;

    {
        auto Data      = std::make_shared<SourceData>();
        Data->Vertices = {
            SourceVertex{RadientFloat3{1.f, 2.f, 3.f}, RadientFloat2{0.25f, 0.5f}},
            SourceVertex{RadientFloat3{4.f, 5.f, 6.f}, RadientFloat2{0.75f, 1.f}}};

        std::shared_ptr<const void> Owner = Data;
        WeakOwner                         = Owner;

        std::array<RadientMeshVertexSource::SourceAttribute, 2> SourceAttributes{
            RadientMeshVertexSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, &Data->Vertices[0].Position, sizeof(SourceVertex)},
            RadientMeshVertexSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, &Data->Vertices[0].TexCoord0, sizeof(SourceVertex)}};

        RadientMeshVertexSource::CreateInfo CI{};
        CI.pAttributes      = SourceAttributes.data();
        CI.AttributeCount   = static_cast<Uint32>(SourceAttributes.size());
        CI.VertexCount      = static_cast<Uint32>(Data->Vertices.size());
        CI.pSourceDataOwner = Owner;

        Source.reset(new RadientMeshVertexSource{CI});
        ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);
    }

    ASSERT_FALSE(WeakOwner.expired());

    const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3},
        GLTF::VertexAttributeDesc{GLTF::Texcoord0AttributeName, 0, VT_FLOAT32, 2}};
    ASSERT_EQ(Source->SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())),
              RADIENT_STATUS_OK);

    std::vector<Uint8> Buffer0(Source->GetVertexBufferDataSize(0));
    ASSERT_EQ(Source->PackVertexData(0,
                                     RadientMeshVertexSource::PackDestination{Buffer0.data(),
                                                                              static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), RadientFloat3{1.f, 2.f, 3.f});

    Source.reset();
    EXPECT_TRUE(WeakOwner.expired());
}

TEST(RadientMeshVertexSourceTest, RejectsInvalidPackDestination)
{
    RadientMeshVertexSource SourceWithoutLayout{MakeVertexMeshCI(DefaultPositions)};
    EXPECT_EQ(SourceWithoutLayout.PackVertexData(0,
                                                 RadientMeshVertexSource::PackDestination{nullptr, 0}),
              RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions)};
    ASSERT_EQ(Source.SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                         static_cast<Uint32>(GLTF::DefaultVertexAttributes.size())),
              RADIENT_STATUS_OK);

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));
    EXPECT_EQ(Source.PackVertexData(0, RadientMeshVertexSource::PackDestination{nullptr, Source.GetVertexBufferDataSize(0)}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackVertexData(0, RadientMeshVertexSource::PackDestination{Buffer0.data(), 0}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackVertexData(1,
                                    RadientMeshVertexSource::PackDestination{Buffer0.data(),
                                                                             static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, PacksDefaultPositionAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::PositionAttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 12u);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed.Buffer, 0), DefaultPositions[0]);
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, PacksDefaultNormalAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::NormalAttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 12u);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed.Buffer, 0), DefaultNormals[0]);
    EXPECT_NE(Packed.Source->GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, PacksDefaultTexCoord0Attribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::Texcoord0AttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 8u);
    ExpectFloat2Eq(ReadValue<RadientFloat2>(Packed.Buffer, 0), DefaultTexCoords0[0]);
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, KeepsDefaultTexCoord1AttributeInactive)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::Texcoord1AttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_TRUE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_TEXCOORD1,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, PacksDefaultJointsAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::JointsAttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 16u);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Packed.Buffer, 0),
                   RadientFloat4{1.f, 2.f, 3.f, 4.f});
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, PacksDefaultWeightsAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::WeightsAttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 16u);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Packed.Buffer, 0), RadientFloat4{1.f, 0.f, 0.f, 0.f});
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, PacksDefaultColorAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::VertexColorAttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 16u);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Packed.Buffer, 0),
                   RadientFloat4{1.f, 128.f / 255.f, 64.f / 255.f, 32.f / 255.f});
    EXPECT_NE(Packed.Source->GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientMeshVertexSourceDefaultAttributesTest, PacksDefaultTangentAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::TangentAttributeName));
    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 12u);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed.Buffer, 0), RadientFloat3{1.f, 0.f, 0.f});
    EXPECT_NE(Packed.Source->GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientMeshVertexSourceTest, StagesOnlyPresentAttributeBuffers)
{
    RadientMeshCreateInfo MeshCI = MakeVertexMeshCI(DefaultPositions);
    MeshCI.pColors0              = DefaultColors.data();

    RadientMeshVertexSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                         static_cast<Uint32>(GLTF::DefaultVertexAttributes.size())),
              RADIENT_STATUS_OK);

    EXPECT_NE(Source.GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS,
              PBR_Renderer::PSO_FLAG_NONE);
    EXPECT_EQ(Source.GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_JOINTS,
              PBR_Renderer::PSO_FLAG_NONE);
    ASSERT_EQ(Source.GetVertexBufferCount(), 4u);
    EXPECT_EQ(Source.GetActiveVertexBufferMask(), 1u | 8u);

    EXPECT_NE(Source.GetVertexBufferDataSize(0), 0u);
    EXPECT_NE(Source.GetVertexStride(1), 0u);
    EXPECT_NE(Source.GetVertexStride(2), 0u);
    EXPECT_EQ(Source.GetVertexBufferDataSize(1), 0u);
    EXPECT_EQ(Source.GetVertexBufferDataSize(2), 0u);
    ASSERT_NE(Source.GetVertexBufferDataSize(3), 0u);
}

TEST(RadientMeshVertexSourceTest, PacksCustomVertexAttributeLayout)
{
    RadientMeshCreateInfo MeshCI = MakeVertexMeshCI(DefaultPositions);
    MeshCI.pColors0              = DefaultColors.data();

    const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{16}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4}};

    RadientMeshVertexSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())), RADIENT_STATUS_OK);

    ASSERT_EQ(Source.GetVertexBufferCount(), 4u);
    EXPECT_EQ(Source.GetActiveVertexBufferMask(), 1u | 8u);
    EXPECT_EQ(Source.GetVertexStride(0), 28u);
    EXPECT_EQ(Source.GetVertexStride(1), 0u);
    EXPECT_EQ(Source.GetVertexStride(2), 0u);
    EXPECT_EQ(Source.GetVertexStride(3), 16u);

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));
    std::vector<Uint8> Buffer3(Source.GetVertexBufferDataSize(3));

    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshVertexSource::PackDestination{Buffer0.data(),
                                                                             static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.PackVertexData(3,
                                    RadientMeshVertexSource::PackDestination{Buffer3.data(),
                                                                             static_cast<Uint32>(Buffer3.size())}),
              RADIENT_STATUS_OK);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), RadientFloat3{});
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 16), DefaultPositions[0]);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer3, 16),
                   RadientFloat4{0.f, 64.f / 255.f, 128.f / 255.f, 1.f});
}

TEST(RadientMeshVertexSourceTest, CacheKeyIncludesDestinationVertexLayout)
{
    std::array<GLTF::VertexAttributeDesc, 1> TightPosition{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3}};
    std::array<GLTF::VertexAttributeDesc, 1> PaddedPosition{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{16}}};

    RadientMeshVertexSource TightSource{MakeVertexMeshCI(DefaultPositions)};
    RadientMeshVertexSource SameTightSource{MakeVertexMeshCI(DefaultPositions)};
    RadientMeshVertexSource PaddedSource{MakeVertexMeshCI(DefaultPositions)};

    EXPECT_TRUE(TightSource.MakeCacheKey().empty());

    ASSERT_EQ(TightSource.SetVertexAttributes(TightPosition.data(), static_cast<Uint32>(TightPosition.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SameTightSource.SetVertexAttributes(TightPosition.data(), static_cast<Uint32>(TightPosition.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(PaddedSource.SetVertexAttributes(PaddedPosition.data(), static_cast<Uint32>(PaddedPosition.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(TightSource.MakeCacheKey(), SameTightSource.MakeCacheKey());
    EXPECT_NE(TightSource.MakeCacheKey(), PaddedSource.MakeCacheKey());
}

TEST(RadientMeshVertexSourceTest, CacheKeyIgnoresUnusedMeshInputs)
{
    RadientMeshCreateInfo MeshCI = MakeVertexMeshCI(DefaultPositions);

    RadientMeshCreateInfo MeshCIWithUnusedColor = MeshCI;
    MeshCIWithUnusedColor.pColors0              = DefaultColors.data();

    const std::array<GLTF::VertexAttributeDesc, 1> PositionOnly{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3}};

    RadientMeshVertexSource Source{MeshCI};
    RadientMeshVertexSource SourceWithUnusedColor{MeshCIWithUnusedColor};

    ASSERT_EQ(Source.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SourceWithUnusedColor.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(Source.MakeCacheKey(), SourceWithUnusedColor.MakeCacheKey());

    const RadientFloat4 Red{1.f, 0.f, 0.f, 1.f};
    const RadientFloat4 Green{0.f, 1.f, 0.f, 1.f};

    std::array<GLTF::VertexAttributeDesc, 2> RedInactiveDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4, Uint32{0}, &Red}};
    std::array<GLTF::VertexAttributeDesc, 2> GreenInactiveDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4, Uint32{0}, &Green}};

    RadientMeshVertexSource RedInactiveDefaultSource{MeshCI};
    RadientMeshVertexSource GreenInactiveDefaultSource{MeshCI};

    ASSERT_EQ(RedInactiveDefaultSource.SetVertexAttributes(RedInactiveDefaultColor.data(), static_cast<Uint32>(RedInactiveDefaultColor.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(GreenInactiveDefaultSource.SetVertexAttributes(GreenInactiveDefaultColor.data(), static_cast<Uint32>(GreenInactiveDefaultColor.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(RedInactiveDefaultSource.MakeCacheKey(), GreenInactiveDefaultSource.MakeCacheKey());
}

TEST(RadientMeshVertexSourceTest, CopiesDestinationVertexAttributeDescriptors)
{
    std::string   PositionName{GLTF::PositionAttributeName};
    RadientFloat4 DefaultColor{1.f, 0.f, 0.f, 1.f};

    std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{PositionName.c_str(), 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{12}, &DefaultColor}};

    RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions)};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())),
              RADIENT_STATUS_OK);

    const std::string CacheKey = Source.MakeCacheKey();
    ASSERT_FALSE(CacheKey.empty());

    PositionName[0] = 'X';
    DefaultColor    = RadientFloat4{0.f, 1.f, 0.f, 1.f};

    EXPECT_EQ(Source.MakeCacheKey(), CacheKey);

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));
    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshVertexSource::PackDestination{Buffer0.data(),
                                                                             static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), DefaultPositions[0]);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer0, 12), RadientFloat4{1.f, 0.f, 0.f, 1.f});
}

TEST(RadientMeshVertexSourceTest, PacksUnsortedExplicitVertexAttributeOffsets)
{
    RadientMeshCreateInfo MeshCI = MakeVertexMeshCI(DefaultPositions);
    MeshCI.pColors0              = DefaultColors.data();

    const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{16}},
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}}};

    RadientMeshVertexSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())), RADIENT_STATUS_OK);

    ASSERT_EQ(Source.GetVertexBufferCount(), 1u);
    EXPECT_EQ(Source.GetActiveVertexBufferMask(), 1u);
    EXPECT_EQ(Source.GetVertexStride(0), 32u);

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));

    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshVertexSource::PackDestination{Buffer0.data(),
                                                                             static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), DefaultPositions[0]);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer0, 16),
                   RadientFloat4{1.f, 128.f / 255.f, 64.f / 255.f, 32.f / 255.f});
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 32), DefaultPositions[1]);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer0, 48),
                   RadientFloat4{0.f, 64.f / 255.f, 128.f / 255.f, 1.f});
}
