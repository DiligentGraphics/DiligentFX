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

#include "TempDirectory.hpp"
#include "gtest/gtest.h"

#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "GLTFDocument.hpp"
#include "GLTFLoader.hpp"
#include "Import/RadientGLTFConverter.hpp"
#include "RadientTestAssetHelpers.hpp"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../../../../DiligentTools/ThirdParty/tinygltf/tiny_gltf.h"

#include "TinyGltfModelView.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr Uint32 TestVertexCount = 3;
static constexpr float  EPSILON         = 1e-5f;

const std::array<float, 9> TestPositions{
    -1.f, 2.f, 3.f,
    4.f, -2.f, 6.f,
    0.f, 5.f, -3.f};

const std::array<float, 9> TestNormals{
    0.f, 0.f, 1.f,
    0.f, 1.f, 0.f,
    1.f, 0.f, 0.f};

const std::array<float, 12> TestTangents{
    1.f, 0.f, 0.f, 1.f,
    0.f, 1.f, 0.f, -1.f,
    0.f, 0.f, 1.f, 1.f};

const std::array<float, 6> TestTexCoords0{
    0.25f, 0.5f,
    0.75f, 1.f,
    0.f, 0.125f};

const std::array<float, 6> TestTexCoords1{
    0.125f, 0.25f,
    0.5f, 0.75f,
    1.f, 0.f};

const std::array<Uint8, 12> TestJoints{
    1, 2, 3, 4,
    5, 6, 7, 8,
    9, 10, 11, 12};

const std::array<float, 12> TestWeights{
    1.f, 0.f, 0.f, 0.f,
    0.25f, 0.25f, 0.25f, 0.25f,
    0.f, 0.f, 0.5f, 0.5f};

const std::array<Uint8, 12> TestColors{
    255, 128, 64, 32,
    0, 64, 128, 255,
    16, 32, 48, 64};

template <typename ValueType>
ValueType ReadValue(const std::vector<Uint8>& Buffer, size_t Offset)
{
    ValueType Value{};
    EXPECT_LE(Offset + sizeof(ValueType), Buffer.size());
    if (Offset + sizeof(ValueType) <= Buffer.size())
        std::memcpy(&Value, Buffer.data() + Offset, sizeof(ValueType));
    return Value;
}

template <typename ValueType, size_t Size>
std::vector<Uint8> MakeBytes(const std::array<ValueType, Size>& Values)
{
    std::vector<Uint8> Bytes(sizeof(ValueType) * Values.size());
    std::memcpy(Bytes.data(), Values.data(), Bytes.size());
    return Bytes;
}

void ExpectFloat2Eq(const float2& Actual, const float2& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
}

void ExpectFloat3Eq(const float3& Actual, const float3& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
    EXPECT_FLOAT_EQ(Actual.z, Expected.z);
}

void ExpectFloat4Eq(const float4& Actual, const float4& Expected)
{
    EXPECT_FLOAT_EQ(Actual.x, Expected.x);
    EXPECT_FLOAT_EQ(Actual.y, Expected.y);
    EXPECT_FLOAT_EQ(Actual.z, Expected.z);
    EXPECT_FLOAT_EQ(Actual.w, Expected.w);
}

void ExpectFloat3Near(const RadientFloat3& Value, const RadientFloat3& Reference)
{
    EXPECT_NEAR(Value.x, Reference.x, EPSILON);
    EXPECT_NEAR(Value.y, Reference.y, EPSILON);
    EXPECT_NEAR(Value.z, Reference.z, EPSILON);
}

void ExpectFloat2Near(const RadientFloat2& Value, const RadientFloat2& Reference)
{
    EXPECT_NEAR(Value.x, Reference.x, EPSILON);
    EXPECT_NEAR(Value.y, Reference.y, EPSILON);
}

void ExpectQuaternionNear(const RadientQuaternion& Value, const RadientQuaternion& Reference)
{
    EXPECT_NEAR(Value.x, Reference.x, EPSILON);
    EXPECT_NEAR(Value.y, Reference.y, EPSILON);
    EXPECT_NEAR(Value.z, Reference.z, EPSILON);
    EXPECT_NEAR(Value.w, Reference.w, EPSILON);
}

void ExpectDefaultResult(const RadientGLTFConverter::MeshVertexSourceResult& Result)
{
    EXPECT_EQ(Result.Status, RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Result.pSource, nullptr);
    ExpectFloat3Eq(Result.BBMin, float3{0.f, 0.f, 0.f});
    ExpectFloat3Eq(Result.BBMax, float3{0.f, 0.f, 0.f});
}

void ExpectDefaultResult(const RadientGLTFConverter::MeshIndexSourceResult& Result)
{
    EXPECT_EQ(Result.Status, RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Result.pSource, nullptr);
}

struct AttributeData
{
    std::string        Name;
    int                ComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    const char*        Type          = "VEC3";
    bool               Normalized    = false;
    std::vector<Uint8> Bytes;
};

struct IndexData
{
    int                ComponentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    std::vector<Uint8> Bytes;
};

size_t GetComponentSize(int ComponentType)
{
    switch (ComponentType)
    {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            return 1;

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        case TINYGLTF_COMPONENT_TYPE_SHORT:
            return 2;

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return 4;

        default:
            return 0;
    }
}

size_t GetTypeComponentCount(const char* Type)
{
    if (std::strcmp(Type, "SCALAR") == 0)
        return 1;
    if (std::strcmp(Type, "VEC2") == 0)
        return 2;
    if (std::strcmp(Type, "VEC3") == 0)
        return 3;
    if (std::strcmp(Type, "VEC4") == 0)
        return 4;

    return 0;
}

Uint32 GetElementCount(const AttributeData& Attribute)
{
    const size_t ElementSize = GetComponentSize(Attribute.ComponentType) * GetTypeComponentCount(Attribute.Type);
    EXPECT_NE(ElementSize, 0u);
    EXPECT_EQ(Attribute.Bytes.size() % ElementSize, 0u);
    return ElementSize != 0 ? static_cast<Uint32>(Attribute.Bytes.size() / ElementSize) : 0;
}

Uint32 GetIndexCount(const IndexData& Indices)
{
    const size_t ElementSize = GetComponentSize(Indices.ComponentType);
    EXPECT_NE(ElementSize, 0u);
    EXPECT_EQ(Indices.Bytes.size() % ElementSize, 0u);
    return ElementSize != 0 ? static_cast<Uint32>(Indices.Bytes.size() / ElementSize) : 0;
}

void AlignBuffer(std::vector<Uint8>& Buffer)
{
    while ((Buffer.size() & 3u) != 0u)
        Buffer.push_back(0);
}

std::string WriteBinaryFile(const TempDirectory& TempDir, const char* FileName, const std::vector<Uint8>& Data)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    if (!Data.empty())
        File.write(reinterpret_cast<const char*>(Data.data()), Data.size());

    return Path;
}

std::string WriteTextFile(const TempDirectory& TempDir, const char* FileName, const std::string& Contents)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << Contents;

    return Path;
}

std::shared_ptr<GLTF::Document> LoadDocument(const std::string& GLTFPath)
{
    GLTF::DocumentLoadInfo LoadInfo;
    LoadInfo.FileName     = GLTFPath.c_str();
    LoadInfo.DecodeImages = false;
    return std::make_shared<GLTF::Document>(LoadInfo);
}

std::shared_ptr<GLTF::Document> MakePrimitiveDocument(const std::vector<AttributeData>& Attributes,
                                                      const IndexData*                  pIndices = nullptr)
{
    TempDirectory TempDir{"RadientGLTFConverterTest"};

    std::vector<Uint8> Buffer;
    std::ostringstream BufferViews;
    std::ostringstream Accessors;
    std::ostringstream PrimitiveAttributes;

    bool FirstBufferView = true;
    bool FirstAccessor   = true;
    bool FirstAttribute  = true;

    auto AddComma = [](std::ostringstream& Stream, bool& First) //
    {
        if (!First)
            Stream << ",";
        First = false;
    };

    for (size_t AttributeIndex = 0; AttributeIndex < Attributes.size(); ++AttributeIndex)
    {
        const AttributeData& Attribute  = Attributes[AttributeIndex];
        const size_t         ByteOffset = Buffer.size();

        Buffer.insert(Buffer.end(), Attribute.Bytes.begin(), Attribute.Bytes.end());
        AlignBuffer(Buffer);

        AddComma(BufferViews, FirstBufferView);
        BufferViews << R"({"buffer": 0, "byteOffset": )" << ByteOffset
                    << R"(, "byteLength": )" << Attribute.Bytes.size() << "}";

        AddComma(Accessors, FirstAccessor);
        Accessors << R"({"bufferView": )" << AttributeIndex
                  << R"(, "componentType": )" << Attribute.ComponentType
                  << R"(, "count": )" << GetElementCount(Attribute)
                  << R"(, "type": ")" << Attribute.Type << R"(")";
        if (Attribute.Normalized)
            Accessors << R"(, "normalized": true)";
        if (Attribute.Name == GLTF::PositionAttributeName)
            Accessors << R"(, "min": [-1, -2, -3], "max": [4, 5, 6])";
        Accessors << "}";

        AddComma(PrimitiveAttributes, FirstAttribute);
        PrimitiveAttributes << "\"" << Attribute.Name << "\": " << AttributeIndex;
    }

    int IndexAccessor = -1;
    if (pIndices != nullptr)
    {
        const size_t BufferViewIndex = Attributes.size();
        const size_t ByteOffset      = Buffer.size();

        Buffer.insert(Buffer.end(), pIndices->Bytes.begin(), pIndices->Bytes.end());
        AlignBuffer(Buffer);

        AddComma(BufferViews, FirstBufferView);
        BufferViews << R"({"buffer": 0, "byteOffset": )" << ByteOffset
                    << R"(, "byteLength": )" << pIndices->Bytes.size() << "}";

        IndexAccessor = static_cast<int>(Attributes.size());
        AddComma(Accessors, FirstAccessor);
        Accessors << R"({"bufferView": )" << BufferViewIndex
                  << R"(, "componentType": )" << pIndices->ComponentType
                  << R"(, "count": )" << GetIndexCount(*pIndices)
                  << R"(, "type": "SCALAR"})";
    }

    WriteBinaryFile(TempDir, "mesh.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"({
    "asset": {"version": "2.0"},
    "buffers": [{"uri": "mesh.bin", "byteLength": )"
         << Buffer.size() << R"(}],
    "bufferViews": [)"
         << BufferViews.str() << R"(],
    "accessors": [)"
         << Accessors.str() << R"(],
    "meshes": [{"primitives": [{"attributes": {)"
         << PrimitiveAttributes.str() << "}";
    if (IndexAccessor >= 0)
        GLTF << R"(, "indices": )" << IndexAccessor;
    GLTF << R"(}]}]
})";

    return LoadDocument(WriteTextFile(TempDir, "mesh.gltf", GLTF.str()));
}

AttributeData MakePositionAttribute()
{
    return AttributeData{
        GLTF::PositionAttributeName,
        TINYGLTF_COMPONENT_TYPE_FLOAT,
        "VEC3",
        false,
        MakeBytes(TestPositions)};
}

GLTF::TinyGltfPrimitiveView GetFirstPrimitive(const std::shared_ptr<GLTF::Document>& pDocument)
{
    GLTF::TinyGltfModelView GltfModel{pDocument->GetModel()};
    return GltfModel.GetMesh(0).GetPrimitive(0);
}

const GLTF::VertexAttributeDesc& GetDefaultAttribute(const char* Name)
{
    for (const GLTF::VertexAttributeDesc& Attribute : GLTF::DefaultVertexAttributes)
    {
        if (std::strcmp(Attribute.Name, Name) == 0)
            return Attribute;
    }

    ADD_FAILURE() << "Unexpected default attribute name: " << (Name != nullptr ? Name : "<null>");
    return GLTF::DefaultVertexAttributes[0];
}

std::vector<GLTF::VertexAttributeDesc> MakeDestinationLayout(const char* AttributeName)
{
    GLTF::VertexAttributeDesc Position = GetDefaultAttribute(GLTF::PositionAttributeName);
    Position.BufferId                  = 0;
    Position.RelativeOffset            = 0;

    if (std::strcmp(AttributeName, GLTF::PositionAttributeName) == 0)
        return {Position};

    GLTF::VertexAttributeDesc Attribute = GetDefaultAttribute(AttributeName);
    Attribute.BufferId                  = 1;
    Attribute.RelativeOffset            = 0;
    return {Position, Attribute};
}

std::vector<Uint8> PackAttributeBuffer(const RadientMeshVertexSource& Source, Uint32 BufferIndex)
{
    std::vector<Uint8> Buffer(Source.GetVertexBufferDataSize(BufferIndex));
    EXPECT_EQ(Source.PackVertexData(BufferIndex,
                                    RadientMeshVertexSource::PackDestination{
                                        Buffer.data(),
                                        static_cast<Uint32>(Buffer.size())}),
              RADIENT_STATUS_OK);
    return Buffer;
}

template <typename ValidateType>
void ExpectCreateMeshVertexSourcePacksAttribute(const AttributeData& Attribute, ValidateType&& Validate)
{
    std::vector<AttributeData> Attributes;
    Attributes.emplace_back(MakePositionAttribute());
    if (Attribute.Name != GLTF::PositionAttributeName)
        Attributes.emplace_back(Attribute);

    RadientGLTFConverter::MeshVertexSourceResult Result;
    {
        std::shared_ptr<GLTF::Document> pDocument = MakePrimitiveDocument(Attributes);
        GLTF::TinyGltfModelView         GltfModel{pDocument->GetModel()};
        Result = RadientGLTFConverter::CreateMeshVertexSource(GltfModel, GetFirstPrimitive(pDocument), pDocument);
    }

    ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Result.pSource, nullptr);
    EXPECT_EQ(Result.pSource->GetVertexCount(), TestVertexCount);

    const std::vector<GLTF::VertexAttributeDesc> DstAttributes = MakeDestinationLayout(Attribute.Name.c_str());
    ASSERT_EQ(Result.pSource->SetVertexAttributes(DstAttributes.data(), static_cast<Uint32>(DstAttributes.size())),
              RADIENT_STATUS_OK);

    const Uint32             BufferIndex = Attribute.Name == GLTF::PositionAttributeName ? 0u : 1u;
    const std::vector<Uint8> Buffer      = PackAttributeBuffer(*Result.pSource, BufferIndex);
    Validate(Buffer);
}

void ExpectPackedIndices(const RadientMeshIndexSource& Source,
                         std::initializer_list<Uint32> ExpectedIndices)
{
    std::vector<Uint32> PackedIndices(Source.GetIndexCount(), 0xCDCDCDCDu);
    ASSERT_EQ(PackedIndices.size(), ExpectedIndices.size());

    ASSERT_EQ(Source.PackIndexData(RadientMeshIndexSource::PackDestination{
                  PackedIndices.data(),
                  static_cast<Uint32>(PackedIndices.size() * sizeof(PackedIndices[0]))}),
              RADIENT_STATUS_OK);

    EXPECT_EQ(PackedIndices, std::vector<Uint32>{ExpectedIndices});
}

template <typename IndexType, size_t Size>
IndexData MakeIndexData(const std::array<IndexType, Size>& Indices, int ComponentType)
{
    return IndexData{ComponentType, MakeBytes(Indices)};
}

void ExpectCreateMeshIndexSourcePacksIndices(const IndexData&              Indices,
                                             std::initializer_list<Uint32> ExpectedIndices)
{
    RadientGLTFConverter::MeshIndexSourceResult Result;
    {
        std::shared_ptr<GLTF::Document> pDocument = MakePrimitiveDocument({MakePositionAttribute()}, &Indices);
        GLTF::TinyGltfModelView         GltfModel{pDocument->GetModel()};
        Result = RadientGLTFConverter::CreateMeshIndexSource(GltfModel, GetFirstPrimitive(pDocument), pDocument, TestVertexCount);
    }

    ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Result.pSource, nullptr);
    ExpectPackedIndices(*Result.pSource, ExpectedIndices);
}

} // namespace

TEST(RadientGLTFConverterTest, CreateMeshVertexSourceRejectsInvalidArguments)
{
    const AttributeData Normal{
        GLTF::NormalAttributeName,
        TINYGLTF_COMPONENT_TYPE_FLOAT,
        "VEC3",
        false,
        MakeBytes(TestNormals)};

    std::shared_ptr<GLTF::Document> pDocument = MakePrimitiveDocument({Normal});
    GLTF::TinyGltfModelView         GltfModel{pDocument->GetModel()};

    const GLTF::TinyGltfPrimitiveView Primitive = GetFirstPrimitive(pDocument);

    RadientGLTFConverter::MeshVertexSourceResult Result =
        RadientGLTFConverter::CreateMeshVertexSource(GltfModel, Primitive, {});
    ExpectDefaultResult(Result);

    Result = RadientGLTFConverter::CreateMeshVertexSource(GltfModel, Primitive, pDocument);
    ExpectDefaultResult(Result);
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourceReturnsDefaultResultAfterPartialFailure)
{
    const std::array<float, 6> ShortNormals{
        0.f, 0.f, 1.f,
        0.f, 1.f, 0.f};

    std::shared_ptr<GLTF::Document> pDocument = MakePrimitiveDocument({
        MakePositionAttribute(),
        AttributeData{
            GLTF::NormalAttributeName,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "VEC3",
            false,
            MakeBytes(ShortNormals)},
    });
    GLTF::TinyGltfModelView         GltfModel{pDocument->GetModel()};

    RadientGLTFConverter::MeshVertexSourceResult Result =
        RadientGLTFConverter::CreateMeshVertexSource(GltfModel, GetFirstPrimitive(pDocument), pDocument);

    // The position accessor is valid and bounding-box computation succeeds
    // before the mismatched normal count is detected. The failed result should
    // still be indistinguishable from a fresh default result.
    ExpectDefaultResult(Result);
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourceComputesBoundingBox)
{
    RadientGLTFConverter::MeshVertexSourceResult Result;
    {
        std::shared_ptr<GLTF::Document> pDocument = MakePrimitiveDocument({MakePositionAttribute()});
        GLTF::TinyGltfModelView         GltfModel{pDocument->GetModel()};
        Result = RadientGLTFConverter::CreateMeshVertexSource(GltfModel, GetFirstPrimitive(pDocument), pDocument);
    }

    ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Result.pSource, nullptr);
    ExpectFloat3Eq(Result.BBMin, float3{-1.f, -2.f, -3.f});
    ExpectFloat3Eq(Result.BBMax, float3{4.f, 5.f, 6.f});
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksPositionAttribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        MakePositionAttribute(),
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat3Eq(ReadValue<float3>(Buffer, 0), float3{-1.f, 2.f, 3.f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksNormalAttribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        AttributeData{
            GLTF::NormalAttributeName,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "VEC3",
            false,
            MakeBytes(TestNormals)},
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat3Eq(ReadValue<float3>(Buffer, 0), float3{0.f, 0.f, 1.f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksTexCoord0Attribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        AttributeData{
            GLTF::Texcoord0AttributeName,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "VEC2",
            false,
            MakeBytes(TestTexCoords0)},
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat2Eq(ReadValue<float2>(Buffer, 0), float2{0.25f, 0.5f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksTexCoord1Attribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        AttributeData{
            GLTF::Texcoord1AttributeName,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "VEC2",
            false,
            MakeBytes(TestTexCoords1)},
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat2Eq(ReadValue<float2>(Buffer, 0), float2{0.125f, 0.25f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksJointsAttribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        AttributeData{
            GLTF::JointsAttributeName,
            TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE,
            "VEC4",
            false,
            MakeBytes(TestJoints)},
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat4Eq(ReadValue<float4>(Buffer, 0), float4{1.f, 2.f, 3.f, 4.f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksWeightsAttribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        AttributeData{
            GLTF::WeightsAttributeName,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "VEC4",
            false,
            MakeBytes(TestWeights)},
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat4Eq(ReadValue<float4>(Buffer, 0), float4{1.f, 0.f, 0.f, 0.f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksColorAttribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        AttributeData{
            GLTF::VertexColorAttributeName,
            TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE,
            "VEC4",
            true,
            MakeBytes(TestColors)},
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat4Eq(ReadValue<float4>(Buffer, 0),
                           float4{1.f, 128.f / 255.f, 64.f / 255.f, 32.f / 255.f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshVertexSourcePacksTangentAttribute)
{
    ExpectCreateMeshVertexSourcePacksAttribute(
        AttributeData{
            GLTF::TangentAttributeName,
            TINYGLTF_COMPONENT_TYPE_FLOAT,
            "VEC4",
            false,
            MakeBytes(TestTangents)},
        [](const std::vector<Uint8>& Buffer) {
            ExpectFloat3Eq(ReadValue<float3>(Buffer, 0), float3{1.f, 0.f, 0.f});
        });
}

TEST(RadientGLTFConverterTest, CreateMeshIndexSourcePacksUint8Indices)
{
    const std::array<Uint8, 3> Indices{2, 1, 0};
    ExpectCreateMeshIndexSourcePacksIndices(
        MakeIndexData(Indices, TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE),
        {2, 1, 0});
}

TEST(RadientGLTFConverterTest, CreateMeshIndexSourceReturnsDefaultResultOnFailure)
{
    std::shared_ptr<GLTF::Document>   pDocument = MakePrimitiveDocument({MakePositionAttribute()});
    GLTF::TinyGltfModelView           GltfModel{pDocument->GetModel()};
    const GLTF::TinyGltfPrimitiveView Primitive = GetFirstPrimitive(pDocument);

    RadientGLTFConverter::MeshIndexSourceResult Result =
        RadientGLTFConverter::CreateMeshIndexSource(GltfModel, Primitive, {}, TestVertexCount);
    ExpectDefaultResult(Result);

    Result = RadientGLTFConverter::CreateMeshIndexSource(GltfModel, Primitive, pDocument, 0);
    ExpectDefaultResult(Result);
}

TEST(RadientGLTFConverterTest, CreateMeshIndexSourcePacksUint16Indices)
{
    const std::array<Uint16, 3> Indices{0, 2, 1};
    ExpectCreateMeshIndexSourcePacksIndices(
        MakeIndexData(Indices, TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT),
        {0, 2, 1});
}

TEST(RadientGLTFConverterTest, CreateMeshIndexSourcePacksUint32Indices)
{
    const std::array<Uint32, 3> Indices{1, 0, 2};
    ExpectCreateMeshIndexSourcePacksIndices(
        MakeIndexData(Indices, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT),
        {1, 0, 2});
}

TEST(RadientGLTFConverterTest, CreateMeshIndexSourceGeneratesSequentialIndices)
{
    RadientGLTFConverter::MeshIndexSourceResult Result;
    {
        std::shared_ptr<GLTF::Document> pDocument = MakePrimitiveDocument({MakePositionAttribute()});
        GLTF::TinyGltfModelView         GltfModel{pDocument->GetModel()};
        Result = RadientGLTFConverter::CreateMeshIndexSource(GltfModel, GetFirstPrimitive(pDocument), pDocument, TestVertexCount);
    }

    ASSERT_EQ(Result.Status, RADIENT_STATUS_OK);
    ASSERT_NE(Result.pSource, nullptr);
    ExpectPackedIndices(*Result.pSource, {0, 1, 2});
}

TEST(RadientGLTFConverterTest, ExtractSceneGraphCopiesScenesNodesMeshesAndTransforms)
{
    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://extract-scene-graph", 7);
    ASSERT_NE(pMesh, nullptr);

    GLTF::Model Model;
    Model.DefaultSceneId = 1;

    Model.Meshes.resize(1);
    Model.Meshes[0].Name      = "Triangle";
    Model.Meshes[0].pUserData = RefCntAutoPtr<IObject>{pMesh.RawPtr(), IID_Unknown};

    Model.Nodes.reserve(3);
    Model.Nodes.emplace_back(0);
    Model.Nodes.emplace_back(1);
    Model.Nodes.emplace_back(2);

    Model.Nodes[0].Name        = "Root";
    Model.Nodes[0].Translation = {1.f, 2.f, 3.f};
    Model.Nodes[0].Rotation    = QuaternionF::RotationFromAxisAngle(float3{0.f, 0.f, 1.f}, 0.5f);
    Model.Nodes[0].Scale       = {2.f, 3.f, 4.f};
    Model.Nodes[0].pMesh       = &Model.Meshes[0];
    Model.Nodes[0].Children    = {&Model.Nodes[1], &Model.Nodes[2]};

    Model.Nodes[1].Name        = "ChildA";
    Model.Nodes[1].Parent      = &Model.Nodes[0];
    Model.Nodes[1].Translation = {4.f, 5.f, 6.f};

    Model.Nodes[2].Name   = "ChildB";
    Model.Nodes[2].Parent = &Model.Nodes[0];
    Model.Nodes[2].Scale  = {0.5f, 0.25f, 0.125f};

    Model.Scenes.resize(2);
    Model.Scenes[0].Name      = "UnusedScene";
    Model.Scenes[0].RootNodes = {&Model.Nodes[2]};
    Model.Scenes[1].Name      = "DefaultScene";
    Model.Scenes[1].RootNodes = {&Model.Nodes[0]};

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene), RADIENT_STATUS_OK);

    EXPECT_EQ(Scene.DefaultSceneId, 1u);

    ASSERT_EQ(Scene.Scenes.size(), 2u);
    EXPECT_EQ(Scene.Scenes[0].Name, "UnusedScene");
    ASSERT_EQ(Scene.Scenes[0].RootNodes.size(), 1u);
    EXPECT_EQ(Scene.Scenes[0].RootNodes[0], 2u);
    EXPECT_EQ(Scene.Scenes[1].Name, "DefaultScene");
    ASSERT_EQ(Scene.Scenes[1].RootNodes.size(), 1u);
    EXPECT_EQ(Scene.Scenes[1].RootNodes[0], 0u);

    ASSERT_EQ(Scene.Nodes.size(), 3u);
    EXPECT_EQ(Scene.Nodes[0].Name, "Root");
    EXPECT_EQ(Scene.Nodes[0].pMesh, pMesh);
    ASSERT_EQ(Scene.Nodes[0].Children.size(), 2u);
    EXPECT_EQ(Scene.Nodes[0].Children[0], 1u);
    EXPECT_EQ(Scene.Nodes[0].Children[1], 2u);
    ExpectFloat3Near(Scene.Nodes[0].Transform.Position, {1.f, 2.f, 3.f});
    ExpectQuaternionNear(Scene.Nodes[0].Transform.Rotation,
                         {Model.Nodes[0].Rotation.q.x, Model.Nodes[0].Rotation.q.y, Model.Nodes[0].Rotation.q.z, Model.Nodes[0].Rotation.q.w});
    ExpectFloat3Near(Scene.Nodes[0].Transform.Scale, {2.f, 3.f, 4.f});

    EXPECT_EQ(Scene.Nodes[1].Name, "ChildA");
    EXPECT_EQ(Scene.Nodes[1].pMesh, nullptr);
    EXPECT_TRUE(Scene.Nodes[1].Children.empty());
    ExpectFloat3Near(Scene.Nodes[1].Transform.Position, {4.f, 5.f, 6.f});

    EXPECT_EQ(Scene.Nodes[2].Name, "ChildB");
    ExpectFloat3Near(Scene.Nodes[2].Transform.Scale, {0.5f, 0.25f, 0.125f});
}

TEST(RadientGLTFConverterTest, ExtractSceneGraphConvertsCameras)
{
    GLTF::Model Model;

    Model.Cameras.resize(2);
    Model.Cameras[0].Name                    = "Perspective";
    Model.Cameras[0].Type                    = GLTF::Camera::Projection::Perspective;
    Model.Cameras[0].Perspective.AspectRatio = 1.5f;
    Model.Cameras[0].Perspective.YFov        = 0.7f;
    Model.Cameras[0].Perspective.ZNear       = 0.2f;
    Model.Cameras[0].Perspective.ZFar        = 250.f;

    Model.Cameras[1].Name               = "Orthographic";
    Model.Cameras[1].Type               = GLTF::Camera::Projection::Orthographic;
    Model.Cameras[1].Orthographic.XMag  = 4.f;
    Model.Cameras[1].Orthographic.YMag  = 3.f;
    Model.Cameras[1].Orthographic.ZNear = 0.5f;
    Model.Cameras[1].Orthographic.ZFar  = 500.f;

    Model.Nodes.reserve(2);
    Model.Nodes.emplace_back(0);
    Model.Nodes.emplace_back(1);
    Model.Nodes[0].Name    = "PerspectiveNode";
    Model.Nodes[0].pCamera = &Model.Cameras[0];
    Model.Nodes[1].Name    = "OrthographicNode";
    Model.Nodes[1].pCamera = &Model.Cameras[1];

    Model.Scenes.resize(1);
    Model.Scenes[0].RootNodes = {&Model.Nodes[0], &Model.Nodes[1]};

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Nodes.size(), 2u);
    ASSERT_TRUE(Scene.Nodes[0].Camera.has_value());
    EXPECT_EQ(Scene.Nodes[0].Camera->Projection, RADIENT_CAMERA_PROJECTION_PERSPECTIVE);
    ExpectFloat2Near(Scene.Nodes[0].Camera->ClippingRange, {0.2f, 250.f});
    EXPECT_NEAR(Scene.Nodes[0].Camera->HorizontalAperture,
                Scene.Nodes[0].Camera->VerticalAperture * 1.5f,
                EPSILON);
    EXPECT_NEAR(Scene.Nodes[0].Camera->FocalLength,
                Scene.Nodes[0].Camera->VerticalAperture / (2.f * std::tan(0.7f * 0.5f)),
                EPSILON);

    ASSERT_TRUE(Scene.Nodes[1].Camera.has_value());
    EXPECT_EQ(Scene.Nodes[1].Camera->Projection, RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC);
    EXPECT_NEAR(Scene.Nodes[1].Camera->HorizontalAperture, 4.f, EPSILON);
    EXPECT_NEAR(Scene.Nodes[1].Camera->VerticalAperture, 3.f, EPSILON);
    ExpectFloat2Near(Scene.Nodes[1].Camera->ClippingRange, {0.5f, 500.f});
}

TEST(RadientGLTFConverterTest, ExtractSceneGraphConvertsLights)
{
    GLTF::Model Model;

    Model.Lights.resize(3);
    Model.Lights[0].Name      = "Sun";
    Model.Lights[0].Type      = GLTF::Light::TYPE::DIRECTIONAL;
    Model.Lights[0].Color     = {1.f, 0.8f, 0.6f};
    Model.Lights[0].Intensity = 2.f;

    Model.Lights[1].Name      = "Point";
    Model.Lights[1].Type      = GLTF::Light::TYPE::POINT;
    Model.Lights[1].Color     = {0.2f, 0.3f, 1.f};
    Model.Lights[1].Intensity = 3.f;
    Model.Lights[1].Range     = 10.f;

    Model.Lights[2].Name           = "Spot";
    Model.Lights[2].Type           = GLTF::Light::TYPE::SPOT;
    Model.Lights[2].Color          = {1.f, 1.f, 1.f};
    Model.Lights[2].Intensity      = 4.f;
    Model.Lights[2].InnerConeAngle = 0.1f;
    Model.Lights[2].OuterConeAngle = 0.4f;

    Model.Nodes.reserve(3);
    Model.Nodes.emplace_back(0);
    Model.Nodes.emplace_back(1);
    Model.Nodes.emplace_back(2);
    Model.Nodes[0].Name   = "DirectionalNode";
    Model.Nodes[0].pLight = &Model.Lights[0];
    Model.Nodes[1].Name   = "PointNode";
    Model.Nodes[1].pLight = &Model.Lights[1];
    Model.Nodes[2].Name   = "SpotNode";
    Model.Nodes[2].pLight = &Model.Lights[2];

    Model.Scenes.resize(1);
    Model.Scenes[0].RootNodes = {&Model.Nodes[0], &Model.Nodes[1], &Model.Nodes[2]};

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Nodes.size(), 3u);

    ASSERT_TRUE(Scene.Nodes[0].Light.has_value());
    EXPECT_EQ(Scene.Nodes[0].Light->Type, RADIENT_LIGHT_TYPE_DIRECTIONAL);
    ExpectFloat3Near(Scene.Nodes[0].Light->Color, {1.f, 0.8f, 0.6f});
    EXPECT_NEAR(Scene.Nodes[0].Light->Intensity, 2.f, EPSILON);

    ASSERT_TRUE(Scene.Nodes[1].Light.has_value());
    EXPECT_EQ(Scene.Nodes[1].Light->Type, RADIENT_LIGHT_TYPE_POINT);
    ExpectFloat3Near(Scene.Nodes[1].Light->Color, {0.2f, 0.3f, 1.f});
    EXPECT_NEAR(Scene.Nodes[1].Light->Intensity, 3.f, EPSILON);
    EXPECT_NEAR(Scene.Nodes[1].Light->Range, 10.f, EPSILON);

    ASSERT_TRUE(Scene.Nodes[2].Light.has_value());
    EXPECT_EQ(Scene.Nodes[2].Light->Type, RADIENT_LIGHT_TYPE_SPOT);
    ExpectFloat3Near(Scene.Nodes[2].Light->Color, {1.f, 1.f, 1.f});
    EXPECT_NEAR(Scene.Nodes[2].Light->Intensity, 4.f, EPSILON);
    EXPECT_NEAR(Scene.Nodes[2].Light->InnerConeAngle, 0.1f, EPSILON);
    EXPECT_NEAR(Scene.Nodes[2].Light->OuterConeAngle, 0.4f, EPSILON);
}
