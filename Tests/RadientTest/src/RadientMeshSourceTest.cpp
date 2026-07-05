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

#include "Assets/RadientMeshSource.hpp"

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

RadientMeshCreateInfo MakeMeshCI(const std::array<RadientFloat3, 2>& Positions,
                                 const std::array<Uint16, 3>&        Indices)
{
    RadientMeshCreateInfo MeshCI{};
    MeshCI.pPositions  = Positions.data();
    MeshCI.VertexCount = static_cast<Uint32>(Positions.size());
    MeshCI.pIndices    = Indices.data();
    MeshCI.IndexCount  = static_cast<Uint32>(Indices.size());
    MeshCI.IndexType   = RADIENT_INDEX_TYPE_UINT16;
    return MeshCI;
}

void SetWholeMeshPrimitive(RadientMeshCreateInfo&          MeshCI,
                           RadientMeshPrimitiveCreateInfo& PrimitiveCI)
{
    PrimitiveCI.FirstIndex = 0;
    PrimitiveCI.IndexCount = MeshCI.IndexCount;
    MeshCI.pPrimitives     = &PrimitiveCI;
    MeshCI.PrimitiveCount  = 1;
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

const std::array<RadientFloat3, 2> DefaultAttribPositions{
    RadientFloat3{1.f, 2.f, 3.f},
    RadientFloat3{4.f, 5.f, 6.f}};
const std::array<RadientFloat3, 2> DefaultAttribNormals{
    RadientFloat3{0.f, 0.f, 1.f},
    RadientFloat3{0.f, 1.f, 0.f}};
const std::array<RadientFloat4, 2> DefaultAttribTangents{
    RadientFloat4{1.f, 0.f, 0.f, 1.f},
    RadientFloat4{0.f, 1.f, 0.f, -1.f}};
const std::array<RadientFloat2, 2> DefaultAttribTexCoords0{
    RadientFloat2{0.25f, 0.5f},
    RadientFloat2{0.75f, 1.f}};
const std::array<RadientColorRGBA8, 2> DefaultAttribColors{
    RadientColorRGBA8{255, 128, 64, 32},
    RadientColorRGBA8{0, 64, 128, 255}};
const std::array<RadientBoneIndices4, 2> DefaultAttribJoints{
    RadientBoneIndices4{1, 2, 3, 4},
    RadientBoneIndices4{5, 6, 7, 8}};
const std::array<RadientFloat4, 2> DefaultAttribWeights{
    RadientFloat4{1.f, 0.f, 0.f, 0.f},
    RadientFloat4{0.25f, 0.25f, 0.25f, 0.25f}};
const std::array<Uint16, 3> DefaultAttribIndices{0, 1, 0};

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
    std::unique_ptr<RadientMeshSource> Source;
    Uint32                             BufferIndex = 0;
    std::vector<Uint8>                 Buffer;
};

PackedDefaultAttribute CreateDefaultAttributeSource(GLTF::VertexAttributeDesc DstAttrib)
{
    RadientMeshCreateInfo MeshCI = MakeMeshCI(DefaultAttribPositions, DefaultAttribIndices);
    if (std::strcmp(DstAttrib.Name, GLTF::NormalAttributeName) == 0)
        MeshCI.pNormals = DefaultAttribNormals.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::TangentAttributeName) == 0)
        MeshCI.pTangents = DefaultAttribTangents.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::Texcoord0AttributeName) == 0)
        MeshCI.pTexCoords0 = DefaultAttribTexCoords0.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::VertexColorAttributeName) == 0)
        MeshCI.pColors0 = DefaultAttribColors.data();
    else if (std::strcmp(DstAttrib.Name, GLTF::JointsAttributeName) == 0 ||
             std::strcmp(DstAttrib.Name, GLTF::WeightsAttributeName) == 0)
    {
        MeshCI.pBoneIndices0 = DefaultAttribJoints.data();
        MeshCI.pBoneWeights0 = DefaultAttribWeights.data();
    }

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    PackedDefaultAttribute Result;
    Result.Source.reset(new RadientMeshSource{MeshCI});
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

    return Result;
}

PackedDefaultAttribute PackDefaultAttribute(GLTF::VertexAttributeDesc DstAttrib)
{
    PackedDefaultAttribute Result = CreateDefaultAttributeSource(DstAttrib);
    if (Result.Source == nullptr || Result.Source->GetStatus() != RADIENT_STATUS_OK)
        return Result;

    EXPECT_LT(Result.BufferIndex, Result.Source->GetVertexBufferCount());
    if (Result.BufferIndex >= Result.Source->GetVertexBufferCount())
        return Result;

    const Uint32 BufferSize = Result.Source->GetVertexBufferDataSize(Result.BufferIndex);
    EXPECT_NE(BufferSize, 0u);
    if (BufferSize == 0)
        return Result;

    Result.Buffer.resize(BufferSize);
    EXPECT_EQ(Result.Source->PackVertexData(Result.BufferIndex,
                                            RadientMeshSource::PackDestination{Result.Buffer.data(), BufferSize}),
              RADIENT_STATUS_OK);

    return Result;
}

} // namespace

TEST(RadientMeshSourceTest, RejectsInvalidCreateInfo)
{
    std::array<RadientFloat3, 2> Positions{
        RadientFloat3{1.f, 2.f, 3.f},
        RadientFloat3{4.f, 5.f, 6.f}};
    std::array<Uint16, 3>            Indices{0, 1, 0};
    std::array<RadientColorRGBA8, 2> Colors{
        RadientColorRGBA8{255, 128, 0, 64},
        RadientColorRGBA8{0, 64, 128, 255}};

    RadientMeshCreateInfo MeshCI = MakeMeshCI(Positions, Indices);
    MeshCI.pColors0              = Colors.data();
    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    auto ExpectInvalid = [](const RadientMeshCreateInfo& InvalidMeshCI) //
    {
        RadientMeshSource Source{InvalidMeshCI};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    RadientMeshCreateInfo InvalidMeshCI = MeshCI;
    InvalidMeshCI.pPositions            = nullptr;
    ExpectInvalid(InvalidMeshCI);

    InvalidMeshCI             = MeshCI;
    InvalidMeshCI.VertexCount = 0;
    ExpectInvalid(InvalidMeshCI);

    InvalidMeshCI          = MeshCI;
    InvalidMeshCI.pIndices = nullptr;
    ExpectInvalid(InvalidMeshCI);

    InvalidMeshCI           = MeshCI;
    InvalidMeshCI.IndexType = RADIENT_INDEX_TYPE_NONE;
    ExpectInvalid(InvalidMeshCI);

    std::array<RadientBoneIndices4, 2> BoneIndices{};
    InvalidMeshCI               = MeshCI;
    InvalidMeshCI.pBoneIndices0 = BoneIndices.data();
    ExpectInvalid(InvalidMeshCI);
}

TEST(RadientMeshSourceTest, RejectsInvalidSourceCreateInfo)
{
    struct SourceVertex
    {
        RadientFloat3 Position;
        RadientFloat2 TexCoord0;
    };

    std::array<SourceVertex, 2> Vertices{
        SourceVertex{RadientFloat3{1.f, 2.f, 3.f}, RadientFloat2{0.25f, 0.5f}},
        SourceVertex{RadientFloat3{4.f, 5.f, 6.f}, RadientFloat2{0.75f, 1.f}}};
    std::array<Uint16, 3>                             Indices{0, 1, 0};
    std::array<RadientMeshSource::SourceAttribute, 2> Attributes{
        RadientMeshSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, &Vertices[0].Position, sizeof(SourceVertex)},
        RadientMeshSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, &Vertices[0].TexCoord0, sizeof(SourceVertex)}};

    RadientMeshSource::CreateInfo CI{};
    CI.pAttributes    = Attributes.data();
    CI.AttributeCount = static_cast<Uint32>(Attributes.size());
    CI.VertexCount    = static_cast<Uint32>(Vertices.size());
    CI.Indices.pData  = Indices.data();
    CI.Indices.Type   = RADIENT_INDEX_TYPE_UINT16;
    CI.IndexCount     = static_cast<Uint32>(Indices.size());

    auto ExpectInvalid = [](const RadientMeshSource::CreateInfo& InvalidCI) //
    {
        RadientMeshSource Source{InvalidCI};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    RadientMeshSource::CreateInfo InvalidCI = CI;
    InvalidCI.pAttributes                   = nullptr;
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
    Attributes[1].Name = GLTF::Texcoord0AttributeName;

    InvalidCI              = CI;
    InvalidCI.Indices.Type = RADIENT_INDEX_TYPE_NONE;
    ExpectInvalid(InvalidCI);
}

TEST(RadientMeshSourceTest, RejectsInvalidVertexAttributes)
{
    std::array<RadientFloat3, 2> Positions{
        RadientFloat3{1.f, 2.f, 3.f},
        RadientFloat3{4.f, 5.f, 6.f}};
    std::array<Uint16, 3> Indices{0, 1, 0};

    RadientMeshCreateInfo          MeshCI = MakeMeshCI(Positions, Indices);
    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    auto ExpectInvalidAttributes =
        [&MeshCI](const GLTF::VertexAttributeDesc* pDstAttributes, Uint32 NumDstAttributes) //
    {
        RadientMeshSource Source{MeshCI};
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

TEST(RadientMeshSourceTest, PacksStridedSourceAttributesAndTightlyPackedIndices)
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
    std::array<Uint16, 3> Indices{0, 1, 0};

    std::array<RadientMeshSource::SourceAttribute, 3> SourceAttributes{
        RadientMeshSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, &Vertices[0].Position, sizeof(SourceVertex)},
        RadientMeshSource::SourceAttribute{GLTF::VertexColorAttributeName, VT_UINT8, 4, true, &Vertices[0].Color, sizeof(SourceVertex)},
        RadientMeshSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, &Vertices[0].TexCoord0, sizeof(SourceVertex)}};

    RadientMeshSource::CreateInfo CI{};
    CI.pAttributes    = SourceAttributes.data();
    CI.AttributeCount = static_cast<Uint32>(SourceAttributes.size());
    CI.VertexCount    = static_cast<Uint32>(Vertices.size());
    CI.Indices.pData  = Indices.data();
    CI.Indices.Type   = RADIENT_INDEX_TYPE_UINT16;
    CI.IndexCount     = static_cast<Uint32>(Indices.size());

    RadientMeshSource Source{CI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    Vertices[0] = {};
    Indices[0]  = 7;

    const std::array<GLTF::VertexAttributeDesc, 3> Attributes{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3},
        GLTF::VertexAttributeDesc{GLTF::Texcoord0AttributeName, 0, VT_FLOAT32, 2},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 1, VT_FLOAT32, 4}};
    ASSERT_EQ(Source.SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())), RADIENT_STATUS_OK);

    ASSERT_EQ(Source.GetVertexBufferCount(), 2u);
    EXPECT_EQ(Source.GetActiveVertexBufferMask(), 1u | 2u);
    EXPECT_EQ(Source.GetVertexStride(0), 20u);
    EXPECT_EQ(Source.GetVertexStride(1), 16u);

    std::vector<Uint32> PackedIndices(Source.GetIndexCount());
    std::vector<Uint8>  Buffer0(Source.GetVertexBufferDataSize(0));
    std::vector<Uint8>  Buffer1(Source.GetVertexBufferDataSize(1));

    ASSERT_EQ(Source.PackIndexData(RadientMeshSource::PackDestination{
                  PackedIndices.data(),
                  static_cast<Uint32>(PackedIndices.size() * sizeof(PackedIndices[0]))}),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.PackVertexData(1,
                                    RadientMeshSource::PackDestination{Buffer1.data(),
                                                                       static_cast<Uint32>(Buffer1.size())}),
              RADIENT_STATUS_OK);

    EXPECT_EQ(PackedIndices[0], 0u);
    EXPECT_EQ(PackedIndices[1], 1u);
    EXPECT_EQ(PackedIndices[2], 0u);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), RadientFloat3{1.f, 2.f, 3.f});
    ExpectFloat2Eq(ReadValue<RadientFloat2>(Buffer0, 12), RadientFloat2{0.25f, 0.5f});
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 20), RadientFloat3{4.f, 5.f, 6.f});
    ExpectFloat2Eq(ReadValue<RadientFloat2>(Buffer0, 32), RadientFloat2{0.75f, 1.f});

    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer1, 0),
                   RadientFloat4{1.f, 128.f / 255.f, 0.f, 64.f / 255.f});
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer1, 16),
                   RadientFloat4{0.f, 64.f / 255.f, 128.f / 255.f, 1.f});
}

TEST(RadientMeshSourceTest, BorrowsSourceDataAndKeepsOwnerAlive)
{
    struct SourceVertex
    {
        RadientFloat3 Position;
        RadientFloat2 TexCoord0;
    };

    struct SourceData
    {
        std::array<SourceVertex, 2> Vertices;
        std::array<Uint16, 3>       Indices;
    };

    std::weak_ptr<const void> WeakOwner;

    {
        auto Data      = std::make_shared<SourceData>();
        Data->Vertices = {
            SourceVertex{RadientFloat3{1.f, 2.f, 3.f}, RadientFloat2{0.25f, 0.5f}},
            SourceVertex{RadientFloat3{4.f, 5.f, 6.f}, RadientFloat2{0.75f, 1.f}}};
        Data->Indices = {0, 1, 0};

        std::shared_ptr<const void> Owner = Data;
        WeakOwner                         = Owner;

        std::array<RadientMeshSource::SourceAttribute, 2> SourceAttributes{
            RadientMeshSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, &Data->Vertices[0].Position, sizeof(SourceVertex)},
            RadientMeshSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, &Data->Vertices[0].TexCoord0, sizeof(SourceVertex)}};

        RadientMeshSource::CreateInfo CI{};
        CI.pAttributes      = SourceAttributes.data();
        CI.AttributeCount   = static_cast<Uint32>(SourceAttributes.size());
        CI.VertexCount      = static_cast<Uint32>(Data->Vertices.size());
        CI.Indices.pData    = Data->Indices.data();
        CI.Indices.Type     = RADIENT_INDEX_TYPE_UINT16;
        CI.IndexCount       = static_cast<Uint32>(Data->Indices.size());
        CI.pSourceDataOwner = Owner;

        RadientMeshSource Source{CI};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

        Data->Vertices[0].Position  = RadientFloat3{7.f, 8.f, 9.f};
        Data->Vertices[0].TexCoord0 = RadientFloat2{0.125f, 0.875f};
        Data->Indices[0]            = 1;

        Data.reset();
        Owner.reset();
        EXPECT_FALSE(WeakOwner.expired());

        const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
            GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3},
            GLTF::VertexAttributeDesc{GLTF::Texcoord0AttributeName, 0, VT_FLOAT32, 2}};
        ASSERT_EQ(Source.SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())), RADIENT_STATUS_OK);

        std::vector<Uint32> PackedIndices(Source.GetIndexCount());
        std::vector<Uint8>  Buffer0(Source.GetVertexBufferDataSize(0));

        ASSERT_EQ(Source.PackIndexData(RadientMeshSource::PackDestination{
                      PackedIndices.data(),
                      static_cast<Uint32>(PackedIndices.size() * sizeof(PackedIndices[0]))}),
                  RADIENT_STATUS_OK);
        ASSERT_EQ(Source.PackVertexData(0,
                                        RadientMeshSource::PackDestination{Buffer0.data(),
                                                                           static_cast<Uint32>(Buffer0.size())}),
                  RADIENT_STATUS_OK);

        EXPECT_EQ(PackedIndices[0], 1u);
        EXPECT_EQ(PackedIndices[1], 1u);
        EXPECT_EQ(PackedIndices[2], 0u);

        ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), RadientFloat3{7.f, 8.f, 9.f});
        ExpectFloat2Eq(ReadValue<RadientFloat2>(Buffer0, 12), RadientFloat2{0.125f, 0.875f});
    }

    EXPECT_TRUE(WeakOwner.expired());
}

TEST(RadientMeshSourceTest, RejectsInvalidPackDestination)
{
    std::array<RadientFloat3, 2> Positions{
        RadientFloat3{1.f, 2.f, 3.f},
        RadientFloat3{4.f, 5.f, 6.f}};
    std::array<Uint16, 3> Indices{0, 1, 0};

    RadientMeshCreateInfo          MeshCI = MakeMeshCI(Positions, Indices);
    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    RadientMeshSource SourceWithoutLayout{MeshCI};
    ASSERT_EQ(SourceWithoutLayout.GetStatus(), RADIENT_STATUS_OK);
    std::array<Uint8, 16> BufferWithoutLayout{};
    EXPECT_EQ(SourceWithoutLayout.PackVertexData(0,
                                                 RadientMeshSource::PackDestination{
                                                     BufferWithoutLayout.data(),
                                                     static_cast<Uint32>(BufferWithoutLayout.size())}),
              RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMeshSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                         static_cast<Uint32>(GLTF::DefaultVertexAttributes.size())),
              RADIENT_STATUS_OK);

    std::vector<Uint32> PackedIndices(Source.GetIndexCount());
    EXPECT_EQ(Source.PackIndexData(RadientMeshSource::PackDestination{nullptr, Source.GetIndexDataSize()}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackIndexData(RadientMeshSource::PackDestination{PackedIndices.data(),
                                                                      Source.GetIndexDataSize() - 1}),
              RADIENT_STATUS_INVALID_ARGUMENT);

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));
    EXPECT_EQ(Source.PackVertexData(0, RadientMeshSource::PackDestination{nullptr, Source.GetVertexBufferDataSize(0)}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackVertexData(0, RadientMeshSource::PackDestination{Buffer0.data(), 0}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackVertexData(0,
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       Source.GetVertexBufferDataSize(0) - 1}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackVertexData(Source.GetVertexBufferCount(),
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_INVALID_ARGUMENT);
}

TEST(RadientMeshSourceTest, RejectsInactiveVertexBufferWithoutTouchingData)
{
    RadientMeshCreateInfo MeshCI = MakeMeshCI(DefaultAttribPositions, DefaultAttribIndices);
    MeshCI.pTangents             = DefaultAttribTangents.data();

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    RadientMeshSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                         static_cast<Uint32>(GLTF::DefaultVertexAttributes.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.GetVertexBufferCount(), 5u);
    EXPECT_FALSE(Source.IsVertexBufferActive(3));
    EXPECT_EQ(Source.GetVertexBufferDataSize(3), 0u);

    constexpr Uint8      Sentinel = 0xAB;
    std::array<Uint8, 1> TinyInactiveBuffer{Sentinel};
    std::array<Uint8, 1> ExtraInactiveBuffer{Sentinel};
    std::vector<Uint8>   Buffer0(Source.GetVertexBufferDataSize(0));

    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);
    EXPECT_EQ(Source.PackVertexData(3,
                                    RadientMeshSource::PackDestination{TinyInactiveBuffer.data(),
                                                                       static_cast<Uint32>(TinyInactiveBuffer.size())}),
              RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Source.PackVertexData(Source.GetVertexBufferCount(),
                                    RadientMeshSource::PackDestination{ExtraInactiveBuffer.data(),
                                                                       static_cast<Uint32>(ExtraInactiveBuffer.size())}),
              RADIENT_STATUS_INVALID_ARGUMENT);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), DefaultAttribPositions[0]);
    EXPECT_EQ(TinyInactiveBuffer[0], Sentinel);
    EXPECT_EQ(ExtraInactiveBuffer[0], Sentinel);
}

TEST(RadientMeshSourceDefaultAttributesTest, PacksDefaultPositionAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::PositionAttributeName));

    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 12u);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed.Buffer, 0), RadientFloat3{1.f, 2.f, 3.f});
}

TEST(RadientMeshSourceDefaultAttributesTest, PacksDefaultNormalAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::NormalAttributeName));

    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 12u);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed.Buffer, 0), RadientFloat3{0.f, 0.f, 1.f});
    EXPECT_NE(Packed.Source->GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientMeshSourceDefaultAttributesTest, PacksDefaultTexCoord0Attribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::Texcoord0AttributeName));

    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 8u);
    ExpectFloat2Eq(ReadValue<RadientFloat2>(Packed.Buffer, 0), RadientFloat2{0.25f, 0.5f});
    EXPECT_NE(Packed.Source->GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_TEXCOORD0,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientMeshSourceDefaultAttributesTest, KeepsDefaultTexCoord1AttributeInactive)
{
    PackedDefaultAttribute Packed = CreateDefaultAttributeSource(GetDefaultAttribute(GLTF::Texcoord1AttributeName));

    ASSERT_TRUE(Packed.Source != nullptr);
    EXPECT_EQ(Packed.BufferIndex, 1u);
    EXPECT_EQ(Packed.Source->GetVertexBufferCount(), 1u);
    EXPECT_FALSE(Packed.Source->IsVertexBufferActive(Packed.BufferIndex));
}

TEST(RadientMeshSourceDefaultAttributesTest, PacksDefaultJointsAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::JointsAttributeName));

    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 16u);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Packed.Buffer, 0),
                   RadientFloat4{1.f, 2.f, 3.f, 4.f});
}

TEST(RadientMeshSourceDefaultAttributesTest, PacksDefaultWeightsAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::WeightsAttributeName));

    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 16u);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Packed.Buffer, 0), RadientFloat4{1.f, 0.f, 0.f, 0.f});
}

TEST(RadientMeshSourceDefaultAttributesTest, PacksDefaultColorAttribute)
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

TEST(RadientMeshSourceDefaultAttributesTest, PacksDefaultTangentAttribute)
{
    PackedDefaultAttribute Packed = PackDefaultAttribute(GetDefaultAttribute(GLTF::TangentAttributeName));

    ASSERT_TRUE(Packed.Source != nullptr);
    ASSERT_FALSE(Packed.Buffer.empty());
    EXPECT_EQ(Packed.Source->GetVertexStride(Packed.BufferIndex), 12u);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed.Buffer, 0), RadientFloat3{1.f, 0.f, 0.f});
    EXPECT_NE(Packed.Source->GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_VERTEX_TANGENTS,
              PBR_Renderer::PSO_FLAG_NONE);
}

TEST(RadientMeshSourceTest, CopiesAndPacksMinimalMesh)
{
    std::array<RadientFloat3, 2> Positions{
        RadientFloat3{1.f, 2.f, 3.f},
        RadientFloat3{4.f, 5.f, 6.f}};
    std::array<Uint16, 3> Indices{0, 1, 0};

    RadientMeshCreateInfo          MeshCI = MakeMeshCI(Positions, Indices);
    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    RadientMeshSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(GLTF::DefaultVertexAttributes.data(),
                                         static_cast<Uint32>(GLTF::DefaultVertexAttributes.size())),
              RADIENT_STATUS_OK);

    Positions[0] = RadientFloat3{};
    Indices[0]   = 7;

    EXPECT_EQ(Source.GetVertexBufferCount(), 1u);
    EXPECT_EQ(Source.GetActiveVertexBufferMask(), 1u);
    ASSERT_EQ(Source.GetIndexCount(), Indices.size());

    constexpr Uint32    SentinelIndex = 0xDEADBEEF;
    constexpr Uint8     SentinelByte  = 0xCD;
    const Uint32        Buffer0Size   = Source.GetVertexBufferDataSize(0);
    std::vector<Uint32> PackedIndices(Source.GetIndexCount() + 1, SentinelIndex);
    std::vector<Uint8>  Buffer0(Buffer0Size + 4, SentinelByte);

    ASSERT_EQ(Source.PackIndexData(RadientMeshSource::PackDestination{
                  PackedIndices.data(),
                  static_cast<Uint32>(PackedIndices.size() * sizeof(PackedIndices[0]))}),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);

    EXPECT_EQ(PackedIndices[0], 0u);
    EXPECT_EQ(PackedIndices[1], 1u);
    EXPECT_EQ(PackedIndices[Source.GetIndexCount()], SentinelIndex);
    EXPECT_EQ(Buffer0[Buffer0Size], SentinelByte);
    EXPECT_EQ(Buffer0.back(), SentinelByte);

    ASSERT_FALSE(Buffer0.empty());
    const RadientFloat3 Position0 = ReadValue<RadientFloat3>(Buffer0, 0);
    EXPECT_FLOAT_EQ(Position0.x, 1.f);
    EXPECT_FLOAT_EQ(Position0.y, 2.f);
    EXPECT_FLOAT_EQ(Position0.z, 3.f);
}

TEST(RadientMeshSourceTest, StagesOnlyPresentAttributeBuffers)
{
    std::array<RadientFloat3, 2> Positions{
        RadientFloat3{1.f, 0.f, 0.f},
        RadientFloat3{0.f, 1.f, 0.f}};
    std::array<Uint16, 3>            Indices{0, 1, 0};
    std::array<RadientColorRGBA8, 2> Colors{
        RadientColorRGBA8{255, 128, 0, 64},
        RadientColorRGBA8{0, 64, 128, 255}};

    RadientMeshCreateInfo MeshCI = MakeMeshCI(Positions, Indices);
    MeshCI.pColors0              = Colors.data();
    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    RadientMeshSource Source{MeshCI};
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

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));
    std::vector<Uint8> Buffer3(Source.GetVertexBufferDataSize(3));

    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.PackVertexData(3,
                                    RadientMeshSource::PackDestination{Buffer3.data(),
                                                                       static_cast<Uint32>(Buffer3.size())}),
              RADIENT_STATUS_OK);

    const RadientFloat4 Color0 = ReadValue<RadientFloat4>(Buffer3, 0);
    EXPECT_FLOAT_EQ(Color0.x, 1.f);
    EXPECT_FLOAT_EQ(Color0.y, 128.f / 255.f);
    EXPECT_FLOAT_EQ(Color0.z, 0.f);
    EXPECT_FLOAT_EQ(Color0.w, 64.f / 255.f);
}

TEST(RadientMeshSourceTest, PacksCustomVertexAttributeLayout)
{
    std::array<RadientFloat3, 2> Positions{
        RadientFloat3{1.f, 2.f, 3.f},
        RadientFloat3{4.f, 5.f, 6.f}};
    std::array<Uint16, 3>            Indices{0, 1, 0};
    std::array<RadientColorRGBA8, 2> Colors{
        RadientColorRGBA8{255, 128, 0, 64},
        RadientColorRGBA8{0, 64, 128, 255}};

    RadientMeshCreateInfo MeshCI = MakeMeshCI(Positions, Indices);
    MeshCI.pColors0              = Colors.data();
    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{16}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4}};

    RadientMeshSource Source{MeshCI};
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
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);
    ASSERT_EQ(Source.PackVertexData(3,
                                    RadientMeshSource::PackDestination{Buffer3.data(),
                                                                       static_cast<Uint32>(Buffer3.size())}),
              RADIENT_STATUS_OK);

    const RadientFloat3 Padding = ReadValue<RadientFloat3>(Buffer0, 0);
    EXPECT_FLOAT_EQ(Padding.x, 0.f);
    EXPECT_FLOAT_EQ(Padding.y, 0.f);
    EXPECT_FLOAT_EQ(Padding.z, 0.f);

    const RadientFloat3 Position0 = ReadValue<RadientFloat3>(Buffer0, 16);
    EXPECT_FLOAT_EQ(Position0.x, 1.f);
    EXPECT_FLOAT_EQ(Position0.y, 2.f);
    EXPECT_FLOAT_EQ(Position0.z, 3.f);

    const RadientFloat4 Color1 = ReadValue<RadientFloat4>(Buffer3, 16);
    EXPECT_FLOAT_EQ(Color1.x, 0.f);
    EXPECT_FLOAT_EQ(Color1.y, 64.f / 255.f);
    EXPECT_FLOAT_EQ(Color1.z, 128.f / 255.f);
    EXPECT_FLOAT_EQ(Color1.w, 1.f);
}

TEST(RadientMeshSourceTest, CacheKeyIncludesDestinationVertexLayout)
{
    RadientMeshCreateInfo MeshCI = MakeMeshCI(DefaultAttribPositions, DefaultAttribIndices);

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    std::array<GLTF::VertexAttributeDesc, 1> TightPosition{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3}};
    std::array<GLTF::VertexAttributeDesc, 1> PaddedPosition{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{16}}};

    RadientMeshSource TightSource{MeshCI};
    RadientMeshSource SameTightSource{MeshCI};
    RadientMeshSource PaddedSource{MeshCI};

    EXPECT_TRUE(TightSource.MakeCacheKey().empty());

    ASSERT_EQ(TightSource.SetVertexAttributes(TightPosition.data(), static_cast<Uint32>(TightPosition.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SameTightSource.SetVertexAttributes(TightPosition.data(), static_cast<Uint32>(TightPosition.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(PaddedSource.SetVertexAttributes(PaddedPosition.data(), static_cast<Uint32>(PaddedPosition.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(TightSource.MakeCacheKey(), SameTightSource.MakeCacheKey());
    EXPECT_NE(TightSource.MakeCacheKey(), PaddedSource.MakeCacheKey());

    const RadientFloat4 Red{1.f, 0.f, 0.f, 1.f};
    const RadientFloat4 Green{0.f, 1.f, 0.f, 1.f};

    std::array<GLTF::VertexAttributeDesc, 2> RedDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{12}, &Red}};
    std::array<GLTF::VertexAttributeDesc, 2> GreenDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{12}, &Green}};

    RadientMeshSource RedDefaultSource{MeshCI};
    RadientMeshSource GreenDefaultSource{MeshCI};

    ASSERT_EQ(RedDefaultSource.SetVertexAttributes(RedDefaultColor.data(), static_cast<Uint32>(RedDefaultColor.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(GreenDefaultSource.SetVertexAttributes(GreenDefaultColor.data(), static_cast<Uint32>(GreenDefaultColor.size())),
              RADIENT_STATUS_OK);

    EXPECT_NE(RedDefaultSource.MakeCacheKey(), GreenDefaultSource.MakeCacheKey());
}

TEST(RadientMeshSourceTest, CacheKeyIgnoresUnusedMeshInputs)
{
    RadientMeshCreateInfo MeshCI = MakeMeshCI(DefaultAttribPositions, DefaultAttribIndices);

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    RadientMeshCreateInfo MeshCIWithUnusedColor = MeshCI;
    MeshCIWithUnusedColor.pColors0              = DefaultAttribColors.data();

    const std::array<GLTF::VertexAttributeDesc, 1> PositionOnly{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3}};

    RadientMeshSource Source{MeshCI};
    RadientMeshSource SourceWithUnusedColor{MeshCIWithUnusedColor};

    ASSERT_EQ(Source.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SourceWithUnusedColor.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(Source.MakeCacheKey(), SourceWithUnusedColor.MakeCacheKey());

    RadientMeshCreateInfo MeshCIWithColor = MeshCI;
    MeshCIWithColor.pColors0              = DefaultAttribColors.data();

    const RadientFloat4 Red{1.f, 0.f, 0.f, 1.f};
    const RadientFloat4 Green{0.f, 1.f, 0.f, 1.f};

    std::array<GLTF::VertexAttributeDesc, 2> RedIgnoredDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{12}, &Red}};
    std::array<GLTF::VertexAttributeDesc, 2> GreenIgnoredDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{12}, &Green}};

    RadientMeshSource RedIgnoredDefaultSource{MeshCIWithColor};
    RadientMeshSource GreenIgnoredDefaultSource{MeshCIWithColor};

    ASSERT_EQ(RedIgnoredDefaultSource.SetVertexAttributes(RedIgnoredDefaultColor.data(), static_cast<Uint32>(RedIgnoredDefaultColor.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(GreenIgnoredDefaultSource.SetVertexAttributes(GreenIgnoredDefaultColor.data(), static_cast<Uint32>(GreenIgnoredDefaultColor.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(RedIgnoredDefaultSource.MakeCacheKey(), GreenIgnoredDefaultSource.MakeCacheKey());

    std::array<GLTF::VertexAttributeDesc, 2> RedInactiveDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4, Uint32{0}, &Red}};
    std::array<GLTF::VertexAttributeDesc, 2> GreenInactiveDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4, Uint32{0}, &Green}};

    RadientMeshSource RedInactiveDefaultSource{MeshCI};
    RadientMeshSource GreenInactiveDefaultSource{MeshCI};

    ASSERT_EQ(RedInactiveDefaultSource.SetVertexAttributes(RedInactiveDefaultColor.data(), static_cast<Uint32>(RedInactiveDefaultColor.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(GreenInactiveDefaultSource.SetVertexAttributes(GreenInactiveDefaultColor.data(), static_cast<Uint32>(GreenInactiveDefaultColor.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(RedInactiveDefaultSource.MakeCacheKey(), GreenInactiveDefaultSource.MakeCacheKey());
}

TEST(RadientMeshSourceTest, GeometrySourceIgnoresMeshPrimitives)
{
    RadientMeshCreateInfo MeshCI = MakeMeshCI(DefaultAttribPositions, DefaultAttribIndices);

    RadientMeshPrimitiveCreateInfo WholePrimitive{};
    SetWholeMeshPrimitive(MeshCI, WholePrimitive);

    RadientMeshCreateInfo          SubrangeMeshCI = MeshCI;
    RadientMeshPrimitiveCreateInfo SubrangePrimitive{};
    SubrangePrimitive.FirstIndex = 1;
    SubrangePrimitive.IndexCount = 2;
    SubrangeMeshCI.pPrimitives   = &SubrangePrimitive;

    const std::array<GLTF::VertexAttributeDesc, 1> PositionOnly{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3}};

    RadientMeshSource WholeSource{MeshCI};
    RadientMeshSource SubrangeSource{SubrangeMeshCI};

    ASSERT_EQ(WholeSource.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SubrangeSource.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(WholeSource.MakeCacheKey(), SubrangeSource.MakeCacheKey());
}

TEST(RadientMeshSourceTest, CopiesDestinationVertexAttributeDescriptors)
{
    RadientMeshCreateInfo MeshCI = MakeMeshCI(DefaultAttribPositions, DefaultAttribIndices);

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    std::string   PositionName{GLTF::PositionAttributeName};
    RadientFloat4 DefaultColor{1.f, 0.f, 0.f, 1.f};

    std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{PositionName.c_str(), 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{12}, &DefaultColor}};

    RadientMeshSource Source{MeshCI};
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
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), DefaultAttribPositions[0]);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer0, 12), RadientFloat4{1.f, 0.f, 0.f, 1.f});
}

TEST(RadientMeshSourceTest, PacksUnsortedExplicitVertexAttributeOffsets)
{
    RadientMeshCreateInfo MeshCI = MakeMeshCI(DefaultAttribPositions, DefaultAttribIndices);
    MeshCI.pColors0              = DefaultAttribColors.data();

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    SetWholeMeshPrimitive(MeshCI, PrimitiveCI);

    const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{16}},
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}}};

    RadientMeshSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.SetVertexAttributes(Attributes.data(), static_cast<Uint32>(Attributes.size())), RADIENT_STATUS_OK);

    ASSERT_EQ(Source.GetVertexBufferCount(), 1u);
    EXPECT_EQ(Source.GetActiveVertexBufferMask(), 1u);
    EXPECT_EQ(Source.GetVertexStride(0), 32u);

    std::vector<Uint8> Buffer0(Source.GetVertexBufferDataSize(0));

    ASSERT_EQ(Source.PackVertexData(0,
                                    RadientMeshSource::PackDestination{Buffer0.data(),
                                                                       static_cast<Uint32>(Buffer0.size())}),
              RADIENT_STATUS_OK);

    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 0), DefaultAttribPositions[0]);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer0, 16),
                   RadientFloat4{1.f, 128.f / 255.f, 64.f / 255.f, 32.f / 255.f});
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Buffer0, 32), DefaultAttribPositions[1]);
    ExpectFloat4Eq(ReadValue<RadientFloat4>(Buffer0, 48),
                   RadientFloat4{0.f, 64.f / 255.f, 128.f / 255.f, 1.f});
}
