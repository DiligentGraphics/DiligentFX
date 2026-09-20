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

#include "RadientMathTestHelpers.hpp"
#include "RadientTypesX.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "RadientTestDataHelpers.hpp"
#include "TestingEnvironment.hpp"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <cstddef>
#include <memory>
#include <limits>
#include <type_traits>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

// Verifies that storage metadata is queried inside the source's retained read scope.
class ScopeCheckedDataBlob final : public ObjectBase<IRadientDataBlob>
{
public:
    using TBase = ObjectBase<IRadientDataBlob>;
    ScopeCheckedDataBlob(IReferenceCounters* pRefCounters, const void* pData, Uint64 Size) :
        TBase{pRefCounters}, m_pData{pData}, m_Size{Size} {}
    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientDataBlob, TBase);

    Uint64 DILIGENT_CALL_TYPE GetSize() const override
    {
        EXPECT_NE(ReaderCount, 0u);
        return m_Size;
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE BeginRead(const void** ppData) override
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        ++ReaderCount;
        *ppData = m_pData;
        return RADIENT_STATUS_OK;
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE EndRead() override
    {
        if (ReaderCount == 0)
            return RADIENT_STATUS_INVALID_OPERATION;
        --ReaderCount;
        return RADIENT_STATUS_OK;
    }

    Uint32 ReaderCount = 0;

private:
    const void* const m_pData;
    const Uint64      m_Size;
};

RefCntAutoPtr<IRadientDataBlob> MakeReferencedDataBlob(const void* pData, Uint64 Size)
{
    RadientDataBlobCreateInfo CI;
    CI.Size  = Size;
    CI.pData = pData;
    RefCntAutoPtr<IRadientDataBlob> pBlob;
    EXPECT_EQ(CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);
    return pBlob;
}

struct VertexMeshData
{
    RadientVertexLayoutDescX                     Layout;
    std::vector<RefCntAutoPtr<IRadientDataBlob>> Blobs;
    std::vector<IRadientDataBlob*>               Data;
    Uint32                                       VertexCount = 2;

    template <typename T, size_t N>
    void Add(const char* Semantic, RADIENT_VERTEX_COMPONENT_TYPE Type, Uint32 Components, Bool Normalized, const std::array<T, N>& Values)
    {
        const Uint32 BufferIndex = Layout.GetBufferCount();
        Layout.AddBuffer().AddAttribute(Semantic, Type, Components, BufferIndex, RADIENT_VERTEX_AUTO_OFFSET, Normalized);
        Blobs.push_back(MakeTestDataBlob(Values.data(), sizeof(Values)));
        Data.push_back(Blobs.back());
    }

    RadientMeshCreateInfo GetCreateInfo() const
    {
        RadientMeshCreateInfo CI;
        CI.VertexLayout    = Layout;
        CI.ppVertexBuffers = Data.data();
        CI.VertexCount     = VertexCount;
        return CI;
    }
};

VertexMeshData MakeVertexMeshCI(const std::array<RadientFloat3, 2>& Positions)
{
    VertexMeshData Data;
    Data.Add("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False, Positions);
    return Data;
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
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    if (std::strcmp(DstAttrib.Name, GLTF::NormalAttributeName) == 0)
        MeshData.Add("NORMAL", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, false, DefaultNormals);
    else if (std::strcmp(DstAttrib.Name, GLTF::TangentAttributeName) == 0)
        MeshData.Add("TANGENT", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, false, DefaultTangents);
    else if (std::strcmp(DstAttrib.Name, GLTF::Texcoord0AttributeName) == 0)
        MeshData.Add("TEXCOORD_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, false, DefaultTexCoords0);
    else if (std::strcmp(DstAttrib.Name, GLTF::VertexColorAttributeName) == 0)
        MeshData.Add("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, true, DefaultColors);
    else if (std::strcmp(DstAttrib.Name, GLTF::JointsAttributeName) == 0 ||
             std::strcmp(DstAttrib.Name, GLTF::WeightsAttributeName) == 0)
    {
        MeshData.Add("JOINTS_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT16, 4, false, DefaultJoints);
        MeshData.Add("WEIGHTS_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, false, DefaultWeights);
    }

    PackedDefaultAttribute Result;
    Result.Source.reset(new RadientMeshVertexSource{MeshData.GetCreateInfo()});
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
    auto MeshData = MakeVertexMeshCI(DefaultPositions);

    RadientMeshVertexSource ValidSource{MeshData.GetCreateInfo()};
    EXPECT_EQ(ValidSource.GetStatus(), RADIENT_STATUS_OK);

    RadientMeshCreateInfo InvalidMeshCI = MeshData.GetCreateInfo();
    InvalidMeshCI.ppVertexBuffers       = nullptr;
    RadientMeshVertexSource MissingPositions{InvalidMeshCI};
    EXPECT_EQ(MissingPositions.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    InvalidMeshCI             = MeshData.GetCreateInfo();
    InvalidMeshCI.VertexCount = 0;
    RadientMeshVertexSource EmptyVertices{InvalidMeshCI};
    EXPECT_EQ(EmptyVertices.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);

    std::array<RadientBoneIndices4, 2> BoneIndices{};
    MeshData.Add("JOINTS_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT16, 4, false, BoneIndices);
    InvalidMeshCI = MeshData.GetCreateInfo();
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
    auto                                                    pBlob = MakeTestDataBlob(Vertices.data(), sizeof(Vertices));
    std::array<RadientMeshVertexSource::SourceAttribute, 2> Attributes{
        RadientMeshVertexSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, pBlob, sizeof(SourceVertex), offsetof(SourceVertex, Position)},
        RadientMeshVertexSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, pBlob, sizeof(SourceVertex), offsetof(SourceVertex, TexCoord0)}};

    RadientMeshVertexSource::CreateInfo CI{};
    CI.pAttributes    = Attributes.data();
    CI.AttributeCount = static_cast<Uint32>(Attributes.size());
    CI.VertexCount    = static_cast<Uint32>(Vertices.size());

    auto ExpectInvalid = [](const RadientMeshVertexSource::CreateInfo& InvalidCI, const char* ExpectedError) //
    {
        Testing::TestingEnvironment::ErrorScope ExpectedErrors{ExpectedError};
        RadientMeshVertexSource                 Source{InvalidCI};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    RadientMeshVertexSource::CreateInfo InvalidCI = CI;
    InvalidCI.pAttributes                         = nullptr;
    ExpectInvalid(InvalidCI, "Invalid Radient mesh vertex source create info");

    InvalidCI             = CI;
    Attributes[0].Stride  = sizeof(RadientFloat3) - 1;
    InvalidCI.pAttributes = Attributes.data();
    ExpectInvalid(InvalidCI, "Invalid Radient mesh vertex source create info");
    Attributes[0].Stride = sizeof(SourceVertex);

    InvalidCI             = CI;
    Attributes[1].Name    = GLTF::PositionAttributeName;
    InvalidCI.pAttributes = Attributes.data();
    ExpectInvalid(InvalidCI, "Duplicate source vertex attribute 'POSITION'");
}

TEST(RadientMeshVertexSourceTest, RejectsInvalidVertexAttributes)
{
    auto ExpectInvalidAttributes =
        [](const GLTF::VertexAttributeDesc* pDstAttributes, Uint32 NumDstAttributes, const char* ExpectedError) //
    {
        RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        Testing::TestingEnvironment::ErrorScope ExpectedErrors{ExpectedError};
        EXPECT_EQ(Source.SetVertexAttributes(pDstAttributes, NumDstAttributes), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
    };

    ExpectInvalidAttributes(nullptr, 1, "Destination vertex attributes must not be empty");
    ExpectInvalidAttributes(GLTF::DefaultVertexAttributes.data(), 0, "Destination vertex attributes must not be empty");

    const std::array<GLTF::VertexAttributeDesc, 1> MissingPosition{
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4}};
    ExpectInvalidAttributes(MissingPosition.data(), static_cast<Uint32>(MissingPosition.size()),
                            "Destination vertex layout must include source-backed POSITION attribute");

    const std::array<GLTF::VertexAttributeDesc, 1> UnsupportedDstType{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_UINT8, 3}};
    ExpectInvalidAttributes(UnsupportedDstType.data(), static_cast<Uint32>(UnsupportedDstType.size()),
                            "Invalid destination vertex attribute at index 0");

    const GLTF::VertexAttributeDesc EmptySemantic{"", 0, VT_FLOAT32, 3};
    ExpectInvalidAttributes(&EmptySemantic, 1, "Invalid destination vertex attribute at index 0");

    const std::array<GLTF::VertexAttributeDesc, 1> TooManyComponents{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 5}};
    ExpectInvalidAttributes(TooManyComponents.data(), static_cast<Uint32>(TooManyComponents.size()),
                            "Invalid destination vertex attribute at index 0");

    const std::array<GLTF::VertexAttributeDesc, 2> DuplicateDestinationAttributes{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 1, VT_FLOAT32, 3, Uint32{0}}};
    ExpectInvalidAttributes(DuplicateDestinationAttributes.data(), static_cast<Uint32>(DuplicateDestinationAttributes.size()),
                            "Duplicate destination vertex attribute 'POSITION'");

    const std::array<GLTF::VertexAttributeDesc, 2> OverlappingSameBuffer{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{0}}};
    ExpectInvalidAttributes(OverlappingSameBuffer.data(), static_cast<Uint32>(OverlappingSameBuffer.size()),
                            "overlap in vertex buffer");
}

TEST(RadientMeshVertexSourceTest, RejectsStoredStrideMatchingAutomaticSentinel)
{
    auto MeshData        = MakeVertexMeshCI(DefaultPositions);
    MeshData.VertexCount = 1;
    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    const GLTF::VertexAttributeDesc Position{
        "POSITION", 0, VT_FLOAT32, 3, RADIENT_VERTEX_AUTO_STRIDE - Uint32{12}};
    Testing::TestingEnvironment::ErrorScope ExpectedErrors{"Invalid vertex buffer"};
    EXPECT_EQ(Source.SetVertexAttributes(&Position, 1), RADIENT_STATUS_INVALID_ARGUMENT);
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

    auto                                                    pBlob = MakeTestDataBlob(Vertices.data(), sizeof(Vertices));
    std::array<RadientMeshVertexSource::SourceAttribute, 3> SourceAttributes{
        RadientMeshVertexSource::SourceAttribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, pBlob, sizeof(SourceVertex), offsetof(SourceVertex, Position)},
        RadientMeshVertexSource::SourceAttribute{GLTF::VertexColorAttributeName, VT_UINT8, 4, true, pBlob, sizeof(SourceVertex), offsetof(SourceVertex, Color)},
        RadientMeshVertexSource::SourceAttribute{GLTF::Texcoord0AttributeName, VT_FLOAT32, 2, false, pBlob, sizeof(SourceVertex), offsetof(SourceVertex, TexCoord0)}};

    RadientMeshVertexSource::CreateInfo CI{};
    CI.pAttributes    = SourceAttributes.data();
    CI.AttributeCount = static_cast<Uint32>(SourceAttributes.size());
    CI.VertexCount    = static_cast<Uint32>(Vertices.size());

    RadientMeshVertexSource Source{CI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

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

TEST(RadientMeshVertexSourceTest, RetainsReferencedAttributeStorageAndCopiesMetadata)
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

        RadientDataBlobCreateInfo BlobCI;
        BlobCI.Size      = sizeof(Data->Vertices);
        BlobCI.pData     = Data->Vertices.data();
        BlobCI.pUserData = new std::shared_ptr<SourceData>{Data};
        BlobCI.OnDestroy = [](void* pContext) {
            delete static_cast<std::shared_ptr<SourceData>*>(pContext);
        };
        RefCntAutoPtr<IRadientDataBlob> pBlob;
        ASSERT_EQ(CreateRadientDataBlob(BlobCI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &pBlob), RADIENT_STATUS_OK);
        std::string                                             PositionName = GLTF::PositionAttributeName;
        std::string                                             TexcoordName = GLTF::Texcoord0AttributeName;
        std::array<RadientMeshVertexSource::SourceAttribute, 2> SourceAttributes{
            RadientMeshVertexSource::SourceAttribute{PositionName.c_str(), VT_FLOAT32, 3, false, pBlob, sizeof(SourceVertex), offsetof(SourceVertex, Position)},
            RadientMeshVertexSource::SourceAttribute{TexcoordName.c_str(), VT_FLOAT32, 2, false, pBlob, sizeof(SourceVertex), offsetof(SourceVertex, TexCoord0)}};

        RadientMeshVertexSource::CreateInfo CI{};
        CI.pAttributes    = SourceAttributes.data();
        CI.AttributeCount = static_cast<Uint32>(SourceAttributes.size());
        CI.VertexCount    = static_cast<Uint32>(Data->Vertices.size());

        Source.reset(new RadientMeshVertexSource{CI});
        ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);
        PositionName.assign("CHANGED");
        TexcoordName.assign("CHANGED");
        SourceAttributes = {};
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
    RadientMeshVertexSource SourceWithoutLayout{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
    EXPECT_EQ(SourceWithoutLayout.PackVertexData(0,
                                                 RadientMeshVertexSource::PackDestination{nullptr, 0}),
              RADIENT_STATUS_INVALID_ARGUMENT);

    RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
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

TEST(RadientMeshVertexSourceTest, PacksFloat32Joints)
{
    const std::array<RadientFloat4, 2> Expected{
        RadientFloat4{0.f, 1.f, 255.f, 1024.f},
        RadientFloat4{2.f, 31.f, 512.f, 2048.f}};
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Add("JOINTS_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, False, Expected);
    MeshData.Add("WEIGHTS_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, False, DefaultWeights);
    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

    // Merge the separate source attributes into one renderer-selected buffer.
    const std::array<GLTF::VertexAttributeDesc, 2> Destination{{{"POSITION", 0, VT_FLOAT32, 3, Uint32{0}},
                                                                {"JOINTS_0", 0, VT_FLOAT32, 4, Uint32{16}}}};
    ASSERT_EQ(Source.SetVertexAttributes(Destination.data(), static_cast<Uint32>(Destination.size())), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.GetVertexStride(0), 32u);
    std::vector<Uint8> Packed(Source.GetVertexBufferDataSize(0));
    ASSERT_EQ(Source.PackVertexData(0, {Packed.data(), static_cast<Uint32>(Packed.size())}), RADIENT_STATUS_OK);
    for (Uint32 Vertex = 0; Vertex < Expected.size(); ++Vertex)
        ExpectFloat4Eq(ReadValue<RadientFloat4>(Packed, Vertex * 32 + 16), Expected[Vertex]);
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
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Add("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, true, DefaultColors);

    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
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
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Add("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, true, DefaultColors);

    const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{16}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4}};

    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
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

    RadientMeshVertexSource TightSource{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
    RadientMeshVertexSource SameTightSource{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
    RadientMeshVertexSource PaddedSource{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};

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

TEST(RadientMeshVertexSourceTest, CacheKeyIncludesZeroFilledAttributeMetadata)
{
    // POSITION anchors the stride, so changes to the zero-filled attribute leave
    // both the buffer size and all packed bytes unchanged.
    const std::array<GLTF::VertexAttributeDesc, 3> Attributes{
        GLTF::VertexAttributeDesc{"POSITION", 2, VT_FLOAT32, 3, Uint32{16}},
        GLTF::VertexAttributeDesc{"_ZERO", 2, VT_FLOAT32, 1, Uint32{0}},
        GLTF::VertexAttributeDesc{"_INACTIVE", 1, VT_FLOAT32, 1, Uint32{0}}};
    std::vector<Uint8> Expected(56, 0);
    std::memcpy(Expected.data() + 16, &DefaultPositions[0], sizeof(RadientFloat3));
    std::memcpy(Expected.data() + 44, &DefaultPositions[1], sizeof(RadientFloat3));
    auto GetKey = [&Expected](const std::array<GLTF::VertexAttributeDesc, 3>& Layout) //
    {
        RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
        EXPECT_EQ(Source.SetVertexAttributes(Layout.data(), static_cast<Uint32>(Layout.size())), RADIENT_STATUS_OK);
        std::vector<Uint8> Packed(Source.GetVertexBufferDataSize(2));
        EXPECT_EQ(Source.PackVertexData(2, {Packed.data(), static_cast<Uint32>(Packed.size())}), RADIENT_STATUS_OK);
        EXPECT_EQ(Packed, Expected);
        return Source.MakeCacheKey();
    };
    const std::string Key = GetKey(Attributes);
    ASSERT_FALSE(Key.empty());

    auto Renamed    = Attributes;
    Renamed[1].Name = "_OTHER_ZERO";
    EXPECT_NE(GetKey(Renamed), Key);

    auto Relocated              = Attributes;
    Relocated[1].RelativeOffset = 4;
    EXPECT_NE(GetKey(Relocated), Key);

    const float Zero             = 0.f;
    auto        WithDefault      = Attributes;
    WithDefault[1].pDefaultValue = &Zero;
    EXPECT_NE(GetKey(WithDefault), Key);

    auto InactiveChanged              = Attributes;
    InactiveChanged[2].Name           = "_OTHER_INACTIVE";
    InactiveChanged[2].RelativeOffset = 8;
    InactiveChanged[2].NumComponents  = 2;
    EXPECT_EQ(GetKey(InactiveChanged), Key);
}

TEST(RadientMeshVertexSourceTest, CacheKeyIgnoresUnusedMeshInputs)
{
    auto MeshData = MakeVertexMeshCI(DefaultPositions);

    auto MeshDataWithUnusedColor = MeshData;
    MeshDataWithUnusedColor.Add("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, True, DefaultColors);

    auto MeshDataWithUnusedHalf = MeshData;
    MeshDataWithUnusedHalf.Add("_CUSTOM", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16, 1, False, std::array<Uint16, 2>{0x3C00, 0x4000});

    const std::array<GLTF::VertexAttributeDesc, 1> PositionOnly{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3}};

    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
    RadientMeshVertexSource SourceWithUnusedColor{MeshDataWithUnusedColor.GetCreateInfo()};
    RadientMeshVertexSource SourceWithUnusedHalf{MeshDataWithUnusedHalf.GetCreateInfo()};
    ASSERT_EQ(SourceWithUnusedHalf.GetStatus(), RADIENT_STATUS_OK);

    ASSERT_EQ(Source.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SourceWithUnusedColor.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);

    ASSERT_EQ(SourceWithUnusedHalf.SetVertexAttributes(PositionOnly.data(), static_cast<Uint32>(PositionOnly.size())),
              RADIENT_STATUS_OK);

    EXPECT_EQ(Source.MakeCacheKey(), SourceWithUnusedColor.MakeCacheKey());
    EXPECT_EQ(Source.MakeCacheKey(), SourceWithUnusedHalf.MakeCacheKey());

    std::array<RadientFloat3, 2> Packed{};
    ASSERT_EQ(SourceWithUnusedHalf.PackVertexData(0, {Packed.data(), sizeof(Packed)}), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(Packed.data(), DefaultPositions.data(), sizeof(Packed)), 0);

    const RadientFloat4 Red{1.f, 0.f, 0.f, 1.f};
    const RadientFloat4 Green{0.f, 1.f, 0.f, 1.f};

    std::array<GLTF::VertexAttributeDesc, 2> RedInactiveDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4, Uint32{0}, &Red}};
    std::array<GLTF::VertexAttributeDesc, 2> GreenInactiveDefaultColor{
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 3, VT_FLOAT32, 4, Uint32{0}, &Green}};

    RadientMeshVertexSource RedInactiveDefaultSource{MeshData.GetCreateInfo()};
    RadientMeshVertexSource GreenInactiveDefaultSource{MeshData.GetCreateInfo()};

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

    RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
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

TEST(RadientMeshVertexSourceTest, ReflectsStoredVertexLayout)
{
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Add("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, True, DefaultColors);
    MeshData.Add("TEXCOORD_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False, DefaultTexCoords0);
    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    {
        std::string                     PositionName = "POSITION";
        std::string                     ZeroName     = "_ZERO_FILLED_ATTRIBUTE";
        const RadientFloat3             DefaultNormal{0.f, 0.f, 1.f};
        const GLTF::VertexAttributeDesc Attributes[]{
            {PositionName.c_str(), 2, VT_FLOAT32, 3, Uint32{4}},
            {"NORMAL", 2, VT_FLOAT32, 3, Uint32{20}, &DefaultNormal},
            {"COLOR_0", 0, VT_FLOAT32, 4, Uint32{8}},
            {"TEXCOORD_1", 1, VT_FLOAT32, 2},
            {ZeroName.c_str(), 2, VT_FLOAT32, 2},
            {"_INACTIVE_TRAILING", 3, VT_FLOAT32, 1}};
        ASSERT_EQ(Source.SetVertexAttributes(Attributes, 6), RADIENT_STATUS_OK);
        PositionName.assign("MODIFIED");
        ZeroName.assign("MODIFIED");
    }

    const RadientVertexLayoutDesc Layout = Source.GetVertexLayout();
    ASSERT_EQ(Layout.BufferCount, 2u);
    ASSERT_EQ(Layout.AttributeCount, 4u);
    EXPECT_EQ(Layout.pBuffers[0].ByteStride, 24u);
    EXPECT_EQ(Layout.pBuffers[1].ByteStride, 40u);
    const char* const ExpectedNames[]{"POSITION", "NORMAL", "COLOR_0", "_ZERO_FILLED_ATTRIBUTE"};
    const Uint32      ExpectedBuffers[]{1, 1, 0, 1};
    const Uint32      ExpectedOffsets[]{4, 20, 8, 32};
    const Uint32      ExpectedComponents[]{3, 3, 4, 2};
    for (Uint32 Index = 0; Index < Layout.AttributeCount; ++Index)
    {
        const RadientVertexAttributeDesc& Attribute = Layout.pAttributes[Index];
        EXPECT_STREQ(Attribute.Semantic, ExpectedNames[Index]);
        EXPECT_EQ(Attribute.BufferIndex, ExpectedBuffers[Index]);
        EXPECT_EQ(Attribute.ByteOffset, ExpectedOffsets[Index]);
        EXPECT_EQ(Attribute.ComponentType, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32);
        EXPECT_EQ(Attribute.ComponentCount, ExpectedComponents[Index]);
        EXPECT_EQ(Attribute.Normalized, False);
    }

    // The reflected defaults and zero-filled elements occupy real stored bytes.
    std::vector<Uint8> Packed(Source.GetVertexBufferDataSize(2), 0xFF);
    ASSERT_EQ(Source.PackVertexData(2, {Packed.data(), static_cast<Uint32>(Packed.size())}), RADIENT_STATUS_OK);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed, 4), DefaultPositions[0]);
    ExpectFloat3Eq(ReadValue<RadientFloat3>(Packed, 20), RadientFloat3{0.f, 0.f, 1.f});
    ExpectFloat2Eq(ReadValue<RadientFloat2>(Packed, 32), RadientFloat2{0.f, 0.f});
}

TEST(RadientMeshVertexSourceTest, RefreshesStoredVertexLayout)
{
    RadientMeshVertexSource Source{MakeVertexMeshCI(DefaultPositions).GetCreateInfo()};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    const GLTF::VertexAttributeDesc InitialAttributes[]{
        {"POSITION", 2, VT_FLOAT32, 3, Uint32{8}},
        {"NORMAL", 2, VT_FLOAT32, 3}};
    ASSERT_EQ(Source.SetVertexAttributes(InitialAttributes, 2), RADIENT_STATUS_OK);
    ASSERT_EQ(Source.GetVertexLayout().AttributeCount, 2u);
    ASSERT_EQ(Source.GetVertexLayout().pBuffers[0].ByteStride, 32u);

    const GLTF::VertexAttributeDesc Position{"POSITION", 0, VT_FLOAT32, 3};
    ASSERT_EQ(Source.SetVertexAttributes(&Position, 1), RADIENT_STATUS_OK);
    const RadientVertexLayoutDesc Layout = Source.GetVertexLayout();
    ASSERT_EQ(Layout.BufferCount, 1u);
    ASSERT_EQ(Layout.AttributeCount, 1u);
    EXPECT_EQ(Layout.pBuffers[0].ByteStride, 12u);
    EXPECT_STREQ(Layout.pAttributes[0].Semantic, "POSITION");
    EXPECT_EQ(Layout.pAttributes[0].BufferIndex, 0u);
    EXPECT_EQ(Layout.pAttributes[0].ByteOffset, 0u);
}

TEST(RadientMeshVertexSourceTest, PacksUnsortedExplicitVertexAttributeOffsets)
{
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Add("COLOR_0", RADIENT_VERTEX_COMPONENT_TYPE_UINT8, 4, true, DefaultColors);

    const std::array<GLTF::VertexAttributeDesc, 2> Attributes{
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 0, VT_FLOAT32, 4, Uint32{16}},
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName, 0, VT_FLOAT32, 3, Uint32{0}}};

    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
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

TEST(RadientMeshVertexSourceTest, CopiesLayoutMetadataAndRetainsInterleavedBlob)
{
    // Leading and interior gaps contain sentinel bytes that a repack would
    // clear. Attribute order differs from the renderer's order.
    const std::array<GLTF::VertexAttributeDesc, 2> Destination{{{"POSITION", 0, VT_FLOAT32, 3, Uint32{4}},
                                                                {"TEXCOORD_0", 0, VT_FLOAT32, 2, Uint32{20}}}};
    std::vector<Uint8>                             Expected(56, 0xA5);
    for (Uint32 Vertex = 0; Vertex < 2; ++Vertex)
    {
        std::memcpy(Expected.data() + Vertex * 28 + 4, &DefaultPositions[Vertex], 12);
        std::memcpy(Expected.data() + Vertex * 28 + 20, &DefaultTexCoords0[Vertex], 8);
    }
    std::unique_ptr<RadientMeshVertexSource> Source;
    {
        std::string                PositionName = "POSITION";
        std::string                TexcoordName = "TEXCOORD_0";
        RadientVertexAttributeDesc Attributes[]{
            {TexcoordName.c_str(), 1, 20, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False},
            {PositionName.c_str(), 1, 4, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False}};
        RadientVertexBufferLayoutDesc Buffers[]{{1}, {28}};
        auto                          pBlob = MakeTestDataBlob(Expected.data(), Expected.size());
        IRadientDataBlob*             Data[]{nullptr, pBlob};
        RadientMeshCreateInfo         CI;
        CI.VertexLayout    = {Attributes, 2, Buffers, 2};
        CI.ppVertexBuffers = Data;
        CI.VertexCount     = 2;
        Source             = std::make_unique<RadientMeshVertexSource>(CI);
        ASSERT_EQ(Source->GetStatus(), RADIENT_STATUS_OK);
        PositionName.assign("CHANGED");
        TexcoordName.assign("CHANGED");
        Attributes[0]         = {};
        Buffers[1].ByteStride = 1;
        Data[1]               = {};
    }
    ASSERT_EQ(Source->SetVertexAttributes(Destination.data(), 2), RADIENT_STATUS_OK);
    std::vector<Uint8> Packed(56);
    ASSERT_EQ(Source->PackVertexData(0, {Packed.data(), 56}), RADIENT_STATUS_OK);
    EXPECT_EQ(Packed, Expected);

    auto Separate = MakeVertexMeshCI(DefaultPositions);
    Separate.Add("TEXCOORD_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False, DefaultTexCoords0);
    RadientMeshVertexSource Repacked{Separate.GetCreateInfo()};
    ASSERT_EQ(Repacked.SetVertexAttributes(Destination.data(), 2), RADIENT_STATUS_OK);
    ASSERT_EQ(Repacked.PackVertexData(0, {Packed.data(), 56}), RADIENT_STATUS_OK);
    // Padding differs between copying and repacking, but has no vertex-data meaning.
    EXPECT_EQ(Source->MakeCacheKey(), Repacked.MakeCacheKey());
    for (Uint32 Vertex = 0; Vertex < 2; ++Vertex)
    {
        std::memset(Expected.data() + Vertex * 28, 0, 4);
        std::memset(Expected.data() + Vertex * 28 + 16, 0, 4);
    }
    EXPECT_EQ(Packed, Expected);

    RadientVertexLayoutDescX Layout;
    Layout.AddBuffer(28)
        .AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, 0, 4)
        .AddAttribute("TEXCOORD_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, 0, 20);
    auto                  pBlob = MakeTestDataBlob(Expected.data(), Expected.size());
    IRadientDataBlob*     Data  = pBlob;
    RadientMeshCreateInfo CI;
    CI.VertexLayout    = Layout;
    CI.ppVertexBuffers = &Data;
    CI.VertexCount     = 2;
    RadientMeshVertexSource ZeroPadded{CI};
    ASSERT_EQ(ZeroPadded.SetVertexAttributes(Destination.data(), 2), RADIENT_STATUS_OK);
    EXPECT_EQ(ZeroPadded.MakeCacheKey(), Repacked.MakeCacheKey());

    auto ChangedPositions = DefaultPositions;
    ChangedPositions[0]   = {9.f, 2.f, 3.f};
    auto ChangedData      = MakeVertexMeshCI(ChangedPositions);
    ChangedData.Add("TEXCOORD_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False, DefaultTexCoords0);
    RadientMeshVertexSource Changed{ChangedData.GetCreateInfo()};
    ASSERT_EQ(Changed.SetVertexAttributes(Destination.data(), 2), RADIENT_STATUS_OK);
    EXPECT_NE(Changed.MakeCacheKey(), Repacked.MakeCacheKey());
}

TEST(RadientMeshVertexSourceTest, ConvertsSupportedComponentTypesFromUnalignedBuffers)
{
    auto Check = [](RADIENT_VERTEX_COMPONENT_TYPE Type, Bool Normalized, const auto& Values, const std::array<float, 6>& Expected) {
        using ValuesType                        = std::decay_t<decltype(Values)>;
        constexpr Uint32              ValueSize = sizeof(typename ValuesType::value_type);
        constexpr Uint32              Stride    = 3 * ValueSize + 1;
        std::array<Uint8, 2 * Stride> Bytes{};
        for (Uint32 Vertex = 0; Vertex < 2; ++Vertex)
            std::memcpy(Bytes.data() + 1 + Vertex * Stride, Values.data() + Vertex * 3, 3 * ValueSize);
        RadientVertexLayoutDescX Layout;
        Layout.AddBuffer(Stride).AddAttribute("POSITION", Type, 3, 0, 1, Normalized);
        auto                  pBlob = MakeReferencedDataBlob(Bytes.data(), Bytes.size());
        IRadientDataBlob*     Data  = pBlob;
        RadientMeshCreateInfo CI;
        CI.VertexLayout    = Layout;
        CI.ppVertexBuffers = &Data;
        CI.VertexCount     = 2;
        RadientMeshVertexSource Source{CI};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        const GLTF::VertexAttributeDesc Destination{"POSITION", 0, VT_FLOAT32, 3};
        ASSERT_EQ(Source.SetVertexAttributes(&Destination, 1), RADIENT_STATUS_OK);
        std::array<float, 6> Packed{};
        ASSERT_EQ(Source.PackVertexData(0, {Packed.data(), sizeof(Packed)}), RADIENT_STATUS_OK);
        for (Uint32 Component = 0; Component < Packed.size(); ++Component)
            EXPECT_FLOAT_EQ(Packed[Component], Expected[Component]);
    };
    Check(RADIENT_VERTEX_COMPONENT_TYPE_INT8, False, std::array<Int8, 6>{-128, -1, 0, 1, 64, 127}, {-128, -1, 0, 1, 64, 127});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_UINT8, False, std::array<Uint8, 6>{0, 1, 2, 3, 128, 255}, {0, 1, 2, 3, 128, 255});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_INT16, False, std::array<Int16, 6>{-32768, -1, 0, 1, 16384, 32767}, {-32768, -1, 0, 1, 16384, 32767});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_UINT16, False, std::array<Uint16, 6>{0, 1, 2, 3, 32768, 65535}, {0, 1, 2, 3, 32768, 65535});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_INT32, False, std::array<Int32, 6>{-100000, -1, 0, 1, 100000, 200000}, {-100000, -1, 0, 1, 100000, 200000});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_UINT32, False, std::array<Uint32, 6>{0, 1, 2, 3, 100000, 200000}, {0, 1, 2, 3, 100000, 200000});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, False, std::array<float, 6>{-1.5f, -0.25f, 0, 1, 2.5f, 3}, {-1.5f, -0.25f, 0, 1, 2.5f, 3});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_INT8, True, std::array<Int8, 6>{-128, -127, 0, 1, 64, 127}, {-1, -1, 0, 1.f / 127, 64.f / 127, 1});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_UINT8, True, std::array<Uint8, 6>{0, 1, 2, 3, 128, 255}, {0, 1.f / 255, 2.f / 255, 3.f / 255, 128.f / 255, 1});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_INT16, True, std::array<Int16, 6>{-32768, -32767, 0, 1, 16384, 32767}, {-1, -1, 0, 1.f / 32767, 16384.f / 32767, 1});
    Check(RADIENT_VERTEX_COMPONENT_TYPE_UINT16, True, std::array<Uint16, 6>{0, 1, 2, 3, 32768, 65535}, {0, 1.f / 65535, 2.f / 65535, 3.f / 65535, 32768.f / 65535, 1});
}

TEST(RadientMeshVertexSourceTest, RejectsFloat16ToFloat32Conversion)
{
    for (const char* Semantic : {"POSITION", "NORMAL"})
    {
        SCOPED_TRACE(Semantic);
        VertexMeshData MeshData;
        if (std::strcmp(Semantic, "POSITION") != 0)
            MeshData.Add("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False, DefaultPositions);
        MeshData.Add(Semantic, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16, 3, False,
                     std::array<Uint16, 6>{0x3C00, 0x4000, 0x4200, 0x4400, 0x4500, 0x4600});

        RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);

        const GLTF::VertexAttributeDesc Destination[]{
            {"POSITION", 0, VT_FLOAT32, 3},
            {"NORMAL", 0, VT_FLOAT32, 3}};
        Testing::TestingEnvironment::ErrorScope ExpectedErrors{"Unsupported vertex attribute conversion for"};
        EXPECT_EQ(Source.SetVertexAttributes(Destination, 2), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_TRUE(Source.MakeCacheKey().empty());
    }
}

TEST(RadientMeshVertexSourceTest, RepackagesSourceWithOmittedFinalPadding)
{
    std::array<Uint8, 28> Bytes{}; // Two 16-byte records, omitting the final 4 padding bytes.
    std::memcpy(Bytes.data(), &DefaultPositions[0], 12);
    std::memcpy(Bytes.data() + 16, &DefaultPositions[1], 12);
    RadientVertexLayoutDescX Layout;
    Layout.AddBuffer(16).AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, 0, 0);
    auto                  pBlob = MakeReferencedDataBlob(Bytes.data(), Bytes.size());
    IRadientDataBlob*     Data  = pBlob;
    RadientMeshCreateInfo CI;
    CI.VertexLayout    = Layout;
    CI.ppVertexBuffers = &Data;
    CI.VertexCount     = 2;
    RadientMeshVertexSource Source{CI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    const GLTF::VertexAttributeDesc Destination{"POSITION", 0, VT_FLOAT32, 3};
    ASSERT_EQ(Source.SetVertexAttributes(&Destination, 1), RADIENT_STATUS_OK);
    std::array<RadientFloat3, 2> Packed{};
    ASSERT_EQ(Source.PackVertexData(0, {Packed.data(), sizeof(Packed)}), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(Packed.data(), DefaultPositions.data(), sizeof(Packed)), 0);
}

TEST(RadientMeshVertexSourceTest, PacksAdditionalSemanticsIntoIndependentBuffers)
{
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Add("TEXCOORD_1", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False, DefaultTexCoords0);
    const std::array<RadientFloat4, 2> Custom{{{1, 2, 3, 4}, {5, 6, 7, 8}}};
    MeshData.Add("_CUSTOM", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 4, False, Custom);
    RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    const GLTF::VertexAttributeDesc Destination[]{
        {"POSITION", 0, VT_FLOAT32, 3},
        {"TEXCOORD_1", 1, VT_FLOAT32, 3},
        {"_CUSTOM", 1, VT_FLOAT32, 2}};
    ASSERT_EQ(Source.SetVertexAttributes(Destination, 3), RADIENT_STATUS_OK);
    EXPECT_NE(Source.GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_TEXCOORD1, PBR_Renderer::PSO_FLAG_NONE);
    // Buffer 0 is compatible; buffer 1 changes both component counts and packing.
    std::array<RadientFloat3, 2> Positions{};
    ASSERT_EQ(Source.PackVertexData(0, {Positions.data(), sizeof(Positions)}), RADIENT_STATUS_OK);
    EXPECT_EQ(std::memcmp(Positions.data(), DefaultPositions.data(), sizeof(Positions)), 0);
    std::array<float, 10> Packed{};
    ASSERT_EQ(Source.PackVertexData(1, {Packed.data(), sizeof(Packed)}), RADIENT_STATUS_OK);
    const std::array<float, 10> Expected{0.25f, 0.5f, 0, 1, 2, 0.75f, 1, 0, 5, 6};
    EXPECT_EQ(Packed, Expected);
}

TEST(RadientMeshVertexSourceTest, RetainsMutableBlobReadAccessUntilAllSourcesAreDestroyed)
{
    Uint32 ReadReleases = 0;
    auto   pBlob        = MakeTestMutableDataBlob(DefaultPositions.data(), sizeof(DefaultPositions), CountBlobReadReleases, &ReadReleases);
    ASSERT_NE(pBlob, nullptr);
    auto MeshData     = MakeVertexMeshCI(DefaultPositions);
    MeshData.Data[0]  = pBlob;
    auto PublicSource = std::make_unique<RadientMeshVertexSource>(MeshData.GetCreateInfo());
    ASSERT_EQ(PublicSource->GetStatus(), RADIENT_STATUS_OK);

    RadientMeshVertexSource::SourceAttribute Attribute{GLTF::PositionAttributeName, VT_FLOAT32, 3, false, pBlob};
    RadientMeshVertexSource::CreateInfo      CI;
    CI.pAttributes      = &Attribute;
    CI.AttributeCount   = 1;
    CI.VertexCount      = 2;
    auto InternalSource = std::make_unique<RadientMeshVertexSource>(CI);
    ASSERT_EQ(InternalSource->GetStatus(), RADIENT_STATUS_OK);

    void* pWrite = nullptr;
    EXPECT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_EQ(ReadReleases, 0u);
    PublicSource.reset();
    EXPECT_EQ(ReadReleases, 0u);
    EXPECT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_INVALID_OPERATION);
    InternalSource.reset();
    EXPECT_EQ(ReadReleases, 1u);
    ASSERT_EQ(pBlob->BeginWrite(&pWrite), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->EndWrite(), RADIENT_STATUS_OK);
    EXPECT_EQ(pBlob->Resize(1), RADIENT_STATUS_OK);
}

TEST(RadientMeshVertexSourceTest, RejectsBusyBlobAndReleasesEarlierReads)
{
    for (bool UseInternalCreateInfo : {false, true})
    {
        SCOPED_TRACE(UseInternalCreateInfo ? "Internal CreateInfo" : "Public RadientMeshCreateInfo");
        Uint32 ReadReleases = 0;
        auto   pPositions   = MakeTestMutableDataBlob(DefaultPositions.data(), sizeof(DefaultPositions), CountBlobReadReleases, &ReadReleases);
        auto   pNormals     = MakeTestMutableDataBlob(DefaultNormals.data(), sizeof(DefaultNormals));
        auto   MeshData     = MakeVertexMeshCI(DefaultPositions);
        MeshData.Add("NORMAL", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False, DefaultNormals);
        MeshData.Data[0] = pPositions;
        MeshData.Data[1] = pNormals;

        const RadientMeshVertexSource::SourceAttribute Attributes[]{
            {"POSITION", VT_FLOAT32, 3, false, pPositions},
            {"NORMAL", VT_FLOAT32, 3, false, pNormals}};
        const RadientMeshVertexSource::CreateInfo CI{Attributes, 2, 2};
        const auto                                CreateSource = [&]() {
            return UseInternalCreateInfo ? std::make_unique<RadientMeshVertexSource>(CI) :
                                           std::make_unique<RadientMeshVertexSource>(MeshData.GetCreateInfo());
        };

        void* pWrite = nullptr;
        ASSERT_EQ(pNormals->BeginWrite(&pWrite), RADIENT_STATUS_OK);
        auto BusySource = CreateSource();
        EXPECT_EQ(BusySource->GetStatus(), RADIENT_STATUS_INVALID_OPERATION);
        EXPECT_EQ(ReadReleases, 1u);
        ASSERT_EQ(pPositions->BeginWrite(&pWrite), RADIENT_STATUS_OK);
        EXPECT_EQ(pPositions->EndWrite(), RADIENT_STATUS_OK);
        EXPECT_EQ(pNormals->EndWrite(), RADIENT_STATUS_OK);

        ASSERT_EQ(pNormals->Resize(sizeof(DefaultNormals) - 1), RADIENT_STATUS_OK);
        auto ShortSource = CreateSource();
        EXPECT_EQ(ShortSource->GetStatus(), RADIENT_STATUS_INVALID_ARGUMENT);
        EXPECT_EQ(ReadReleases, 2u);
        EXPECT_EQ(pPositions->Resize(0), RADIENT_STATUS_OK);
        EXPECT_EQ(pNormals->Resize(0), RADIENT_STATUS_OK);
    }
}

TEST(RadientMeshVertexSourceTest, AcceptsAddressableSourceSpanLargerThanUint32)
{
    if (sizeof(size_t) <= sizeof(Uint32))
        GTEST_SKIP() << "Requires an address space larger than Uint32.";

    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Layout.SetBuffer(0, (std::numeric_limits<Uint32>::max)() - 3);
    const Uint64 RequiredSize = Uint64{MeshData.Layout.GetBuffer(0).ByteStride} + sizeof(RadientFloat3);
    ASSERT_GT(RequiredSize, (std::numeric_limits<Uint32>::max)());

    // Only construction is exercised; no bytes from the advertised large span are read.
    RefCntAutoPtr<ScopeCheckedDataBlob> pBlob{MakeNewRCObj<ScopeCheckedDataBlob>()(DefaultPositions.data(), RequiredSize)};
    MeshData.Data[0] = pBlob;
    {
        RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
        EXPECT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_EQ(Source.GetVertexCount(), 2u);
        EXPECT_EQ(pBlob->ReaderCount, 1u);
    }
    EXPECT_EQ(pBlob->ReaderCount, 0u);
}

TEST(RadientMeshVertexSourceTest, QueriesSharedBlobSizeUnderReadAccessAndIgnoresUnusedSlots)
{
    RefCntAutoPtr<ScopeCheckedDataBlob> pBlob{MakeNewRCObj<ScopeCheckedDataBlob>()(DefaultPositions.data(), sizeof(DefaultPositions))};
    auto                                MeshData = MakeVertexMeshCI(DefaultPositions);
    MeshData.Add("NORMAL", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False, DefaultNormals);
    MeshData.Data[0] = pBlob;
    MeshData.Data[1] = pBlob;
    MeshData.Layout.AddBuffer(1);
    auto pUnused = MakeTestMutableDataBlob(nullptr, 0);
    MeshData.Data.push_back(pUnused);
    void* pWrite = nullptr;
    ASSERT_EQ(pUnused->BeginWrite(&pWrite), RADIENT_STATUS_OK);
    {
        RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
        ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
        EXPECT_GT(pBlob->ReaderCount, 0u);
        const GLTF::VertexAttributeDesc Destination[]{
            {"POSITION", 0, VT_FLOAT32, 3},
            {"NORMAL", 1, VT_FLOAT32, 3}};
        ASSERT_EQ(Source.SetVertexAttributes(Destination, 2), RADIENT_STATUS_OK);
        std::array<RadientFloat3, 2> Packed{};
        ASSERT_EQ(Source.PackVertexData(1, {Packed.data(), sizeof(Packed)}), RADIENT_STATUS_OK);
        EXPECT_EQ(std::memcmp(Packed.data(), DefaultPositions.data(), sizeof(Packed)), 0);
    }
    EXPECT_EQ(pBlob->ReaderCount, 0u);
    EXPECT_EQ(pUnused->EndWrite(), RADIENT_STATUS_OK);
}

TEST(RadientMeshVertexSourceTest, RejectsInvalidBlobStorageAfterAcquiringReadAccess)
{
    auto MeshData = MakeVertexMeshCI(DefaultPositions);
    auto Check    = [&MeshData](const void* pData, Uint64 Size, RADIENT_STATUS ExpectedStatus) {
        RefCntAutoPtr<ScopeCheckedDataBlob> pBlob{MakeNewRCObj<ScopeCheckedDataBlob>()(pData, Size)};
        MeshData.Data[0] = pBlob;
        RadientMeshVertexSource Source{MeshData.GetCreateInfo()};
        EXPECT_EQ(Source.GetStatus(), ExpectedStatus);
        EXPECT_EQ(pBlob->ReaderCount, 0u);
    };
    Check(nullptr, 0, RADIENT_STATUS_INVALID_ARGUMENT);
    Check(nullptr, sizeof(DefaultPositions), RADIENT_STATUS_INVALID_DATA);
    Check(DefaultPositions.data(), sizeof(DefaultPositions) - 1, RADIENT_STATUS_INVALID_ARGUMENT);

    // The full span is checked with wide arithmetic before accessing any bytes.
    MeshData.VertexCount = (std::numeric_limits<Uint32>::max)();
    MeshData.Layout.SetBuffer(0, RADIENT_VERTEX_AUTO_STRIDE - 1);
    Check(DefaultPositions.data(), sizeof(DefaultPositions), RADIENT_STATUS_INVALID_ARGUMENT);
}
