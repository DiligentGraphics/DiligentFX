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

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "GLTFDocument.hpp"
#include "GLTFBuilder.hpp"
#include "GLTFLoader.hpp"
#include "Import/RadientGLTFConverter.hpp"
#include "RadientEngine.h"
#include "RadientStandardMaterialParameters.h"
#include "RadientSkinning.h"
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
#include <iterator>
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

struct StandardMaterialTextureTestInfo
{
    Uint32      TextureAttribId;
    const char* ParameterName;
};

static constexpr std::array<StandardMaterialTextureTestInfo, 17> StandardMaterialTextureTestInfos{{
    {GLTF::DefaultBaseColorTextureAttribId, "BaseColorTexture"},
    {GLTF::DefaultMetallicRoughnessTextureAttribId, "MetallicRoughnessTexture"},
    {GLTF::DefaultNormalTextureAttribId, "NormalTexture"},
    {GLTF::DefaultOcclusionTextureAttribId, "OcclusionTexture"},
    {GLTF::DefaultEmissiveTextureAttribId, "EmissiveTexture"},
    {GLTF::DefaultClearcoatTextureAttribId, "ClearCoatTexture"},
    {GLTF::DefaultClearcoatRoughnessTextureAttribId, "ClearCoatRoughnessTexture"},
    {GLTF::DefaultClearcoatNormalTextureAttribId, "ClearCoatNormalTexture"},
    {GLTF::DefaultSheenColorTextureAttribId, "SheenColorTexture"},
    {GLTF::DefaultSheenRoughnessTextureAttribId, "SheenRoughnessTexture"},
    {GLTF::DefaultSpecularTextureAttribId, "SpecularTexture"},
    {GLTF::DefaultSpecularColorTextureAttribId, "SpecularColorTexture"},
    {GLTF::DefaultAnisotropyTextureAttribId, "AnisotropyTexture"},
    {GLTF::DefaultIridescenceTextureAttribId, "IridescenceTexture"},
    {GLTF::DefaultIridescenceThicknessTextureAttribId, "IridescenceThicknessTexture"},
    {GLTF::DefaultTransmissionTextureAttribId, "TransmissionTexture"},
    {GLTF::DefaultThicknessTextureAttribId, "ThicknessTexture"},
}};

template <typename ValueType>
ValueType GetMaterialParameter(IRadientMaterialAsset& Material, const char* Name)
{
    ValueType                        Value{};
    IRadientMaterialDefinitionAsset* pDefinition = Material.GetDefinition();
    RadientMaterialParameterHandle   Handle;
    EXPECT_NE(pDefinition, nullptr);
    if (pDefinition != nullptr)
    {
        EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK);
        if (Handle)
        {
            EXPECT_EQ(Material.GetParameter(Handle, &Value, static_cast<Uint32>(sizeof(Value))),
                      RADIENT_STATUS_OK);
        }
    }
    return Value;
}

RefCntAutoPtr<IRadientTextureAsset> GetMaterialTexture(IRadientMaterialAsset& Material,
                                                       const char*            Name)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture;
    IRadientMaterialDefinitionAsset*    pDefinition = Material.GetDefinition();
    RadientMaterialParameterHandle      Handle;
    EXPECT_NE(pDefinition, nullptr);
    if (pDefinition != nullptr)
    {
        EXPECT_EQ(pDefinition->FindParameter(Name, &Handle), RADIENT_STATUS_OK);
        if (Handle)
            EXPECT_EQ(Material.GetTexture(Handle, 0, pTexture.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    }
    return pTexture;
}

GLTF::Material MakeExtendedGLTFMaterial(bool AddTextures)
{
    GLTF::Material Material;
    Material.Attribs.BaseColorFactor          = float4{0.1f, 0.2f, 0.3f, 0.4f};
    Material.Attribs.EmissiveFactor           = float3{0.5f, 0.6f, 0.7f};
    Material.Attribs.NormalScale              = 0.8f;
    Material.Attribs.AlphaMode                = GLTF::Material::ALPHA_MODE_MASK;
    Material.Attribs.AlphaCutoff              = 0.25f;
    Material.Attribs.MetallicFactor           = 0.35f;
    Material.Attribs.RoughnessFactor          = 0.45f;
    Material.Attribs.OcclusionFactor          = 0.55f;
    Material.Attribs.ClearcoatFactor          = 0.65f;
    Material.Attribs.ClearcoatRoughnessFactor = 0.75f;
    Material.Attribs.ClearcoatNormalScale     = 0.85f;
    Material.DoubleSided                      = true;
    Material.HasClearcoat                     = true;

    Material.Sheen                  = std::make_unique<GLTF::Material::SheenShaderAttribs>();
    Material.Sheen->ColorFactor     = float3{0.15f, 0.25f, 0.35f};
    Material.Sheen->RoughnessFactor = 0.46f;

    Material.Specular              = std::make_unique<GLTF::Material::SpecularShaderAttribs>();
    Material.Specular->Factor      = 0.57f;
    Material.Specular->ColorFactor = float3{0.26f, 0.36f, 0.46f};

    Material.Anisotropy           = std::make_unique<GLTF::Material::AnisotropyShaderAttribs>();
    Material.Anisotropy->Strength = 0.56f;
    Material.Anisotropy->Rotation = 0.66f;

    Material.Iridescence                   = std::make_unique<GLTF::Material::IridescenceShaderAttribs>();
    Material.Iridescence->Factor           = 0.76f;
    Material.Iridescence->IOR              = 1.4f;
    Material.Iridescence->ThicknessMinimum = 120.f;
    Material.Iridescence->ThicknessMaximum = 360.f;

    Material.Transmission         = std::make_unique<GLTF::Material::TransmissionShaderAttribs>();
    Material.Transmission->Factor = 0.86f;
    Material.Transmission->IOR    = 1.45f;

    Material.Volume                      = std::make_unique<GLTF::Material::VolumeShaderAttribs>();
    Material.Volume->ThicknessFactor     = 0.96f;
    Material.Volume->AttenuationColor    = float3{0.2f, 0.4f, 0.6f};
    Material.Volume->AttenuationDistance = 12.f;

    if (AddTextures)
    {
        GLTF::MaterialBuilder Builder{Material};
        for (Uint32 TextureAttribId = GLTF::DefaultBaseColorTextureAttribId;
             TextureAttribId <= GLTF::DefaultSpecularColorTextureAttribId;
             ++TextureAttribId)
        {
            Builder.SetTextureId(TextureAttribId, 0);
        }
        Builder.Finalize();
    }

    return Material;
}

RefCntAutoPtr<IRadientMaterialAsset> ConvertMaterial(
    const GLTF::Material&                        Material,
    IRadientTextureAsset* const*                 ppTextures,
    Uint32                                       TextureCount,
    RadientStandardMaterialDefinitionCreateInfo& DefinitionCI)
{
    RADIENT_STATUS Status = RadientGLTFConverter::ConvertMaterialDefinition(Material, DefinitionCI);
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    RadientMaterialAssetManagerSharedPtr           pManager = RadientMaterialAssetManager::Create();
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    Status = pManager->CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    RefCntAutoPtr<IRadientMaterialAsset> pMaterial;
    Status = pManager->CreateMaterial(pDefinition, pMaterial.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    RefCntAutoPtr<IRadientMaterialWriter> pWriter;
    Status = pMaterial->CreateWriter(pWriter.GetAddressOfEmpty());
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    Status = RadientGLTFConverter::PopulateMaterial(
        Material, ppTextures, TextureCount, *pDefinition, *pWriter);
    EXPECT_EQ(Status, RADIENT_STATUS_OK);
    if (Status != RADIENT_STATUS_OK)
        return {};

    Status = pWriter->Commit();
    EXPECT_TRUE(Status == RADIENT_STATUS_OK || Status == RADIENT_STATUS_NO_CHANGE);
    if (Status != RADIENT_STATUS_OK && Status != RADIENT_STATUS_NO_CHANGE)
        return {};
    return pMaterial;
}

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
    EXPECT_EQ(Result.Status, RADIENT_STATUS_INVALID_DATA);
    EXPECT_EQ(Result.pSource, nullptr);
    ExpectFloat3Eq(Result.BBMin, float3{0.f, 0.f, 0.f});
    ExpectFloat3Eq(Result.BBMax, float3{0.f, 0.f, 0.f});
}

void ExpectDefaultResult(const RadientGLTFConverter::MeshIndexSourceResult& Result)
{
    EXPECT_EQ(Result.Status, RADIENT_STATUS_INVALID_DATA);
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

TEST(RadientGLTFConverterTest, ConvertsExtendedMaterialDefinitionAndValues)
{
    const GLTF::Material Material = MakeExtendedGLTFMaterial(true);

    RefCntAutoPtr<IRadientTextureAsset> pTexture =
        MakeTestTextureAsset("texture://gltf-standard-material");
    IRadientTextureAsset* const Textures[] = {pTexture};

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialAsset>        pMaterial =
        ConvertMaterial(Material, Textures, 1, DefinitionCI);
    ASSERT_NE(pMaterial, nullptr);

    EXPECT_EQ(DefinitionCI.ShadingModel, RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS);
    EXPECT_EQ(DefinitionCI.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS_ALL);

    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat4>(*pMaterial, "BaseColorFactor").w, 0.4f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "MetallicFactor"), 0.35f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "RoughnessFactor"), 0.45f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat3>(*pMaterial, "EmissiveFactor").z, 0.7f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "NormalScale"), 0.8f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "OcclusionStrength"), 0.55f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "ClearCoatFactor"), 0.65f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "ClearCoatRoughnessFactor"), 0.75f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "ClearCoatNormalScale"), 0.85f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat3>(*pMaterial, "SheenColorFactor").y, 0.25f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "SheenRoughnessFactor"), 0.46f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "SpecularWeight"), 0.57f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat3>(*pMaterial, "SpecularColorFactor").z, 0.46f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "AnisotropyStrength"), 0.56f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "AnisotropyRotation"), 0.66f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "IridescenceFactor"), 0.76f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "IridescenceIOR"), 1.4f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "IridescenceThicknessMinimum"), 120.f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "IridescenceThicknessMaximum"), 360.f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "TransmissionFactor"), 0.86f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "IOR"), 1.45f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "ThicknessFactor"), 0.96f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat3>(*pMaterial, "AttenuationColor").y, 0.4f);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, "AttenuationDistance"), 12.f);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurfaceMaterial{
        pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurfaceMaterial, nullptr);
    EXPECT_EQ(pSurfaceMaterial->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_MASKED);
    EXPECT_FLOAT_EQ(pSurfaceMaterial->GetAlphaCutoff(), 0.25f);
    EXPECT_TRUE(pSurfaceMaterial->IsDoubleSided());

    for (const StandardMaterialTextureTestInfo& TextureInfo : StandardMaterialTextureTestInfos)
        EXPECT_EQ(GetMaterialTexture(*pMaterial, TextureInfo.ParameterName), pTexture);
}

TEST(RadientGLTFConverterTest, DeclaresShaderRequiredTextureParameters)
{
    GLTF::Material Material;

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialAsset>        pMaterial =
        ConvertMaterial(Material, nullptr, 0, DefinitionCI);
    ASSERT_NE(pMaterial, nullptr);

    for (size_t TextureIndex = 0; TextureIndex < StandardMaterialTextureTestInfos.size(); ++TextureIndex)
    {
        const StandardMaterialTextureTestInfo& TextureInfo = StandardMaterialTextureTestInfos[TextureIndex];
        RadientMaterialParameterHandle         Handle;
        const RADIENT_STATUS                   FindStatus =
            pMaterial->GetDefinition()->FindParameter(TextureInfo.ParameterName, &Handle);

        if (TextureIndex < 5)
        {
            EXPECT_EQ(FindStatus, RADIENT_STATUS_OK);
            EXPECT_EQ(GetMaterialTexture(*pMaterial, TextureInfo.ParameterName), nullptr);
        }
        else
        {
            EXPECT_EQ(FindStatus, RADIENT_STATUS_NOT_FOUND);
        }
    }
}

TEST(RadientGLTFConverterTest, DeclaresShaderRequiredExtensionTextureParameters)
{
    const GLTF::Material Material = MakeExtendedGLTFMaterial(false);

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialAsset>        pMaterial =
        ConvertMaterial(Material, nullptr, 0, DefinitionCI);
    ASSERT_NE(pMaterial, nullptr);

    for (const StandardMaterialTextureTestInfo& TextureInfo : StandardMaterialTextureTestInfos)
        EXPECT_EQ(GetMaterialTexture(*pMaterial, TextureInfo.ParameterName), nullptr);
}

TEST(RadientGLTFConverterTest, ConvertsTextureBindingParametersForAllSupportedSemantics)
{
    GLTF::Material        Material = MakeExtendedGLTFMaterial(true);
    GLTF::MaterialBuilder Builder{Material};

    for (size_t TextureIndex = 0; TextureIndex < StandardMaterialTextureTestInfos.size(); ++TextureIndex)
    {
        const StandardMaterialTextureTestInfo& TextureInfo    = StandardMaterialTextureTestInfos[TextureIndex];
        GLTF::Material::TextureShaderAttribs&  TextureAttribs = Builder.GetTextureAttrib(TextureInfo.TextureAttribId);

        TextureAttribs.SetUVSelector(static_cast<int>(TextureIndex % 2));
        TextureAttribs.UVScaleAndRotation = float2x2{
            1.f + static_cast<float>(TextureIndex), 0.1f + static_cast<float>(TextureIndex),
            0.2f + static_cast<float>(TextureIndex), 2.f + static_cast<float>(TextureIndex)};
        TextureAttribs.UBias = 0.01f * static_cast<float>(TextureIndex + 1);
        TextureAttribs.VBias = 0.02f * static_cast<float>(TextureIndex + 1);
        TextureAttribs.SetWrapUMode(TextureIndex % 2 == 0 ? TEXTURE_ADDRESS_MIRROR : TEXTURE_ADDRESS_CLAMP);
        TextureAttribs.SetWrapVMode(TextureIndex % 2 == 0 ? TEXTURE_ADDRESS_CLAMP : TEXTURE_ADDRESS_WRAP);
    }
    Builder.Finalize();

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialAsset>        pMaterial =
        ConvertMaterial(Material, nullptr, 0, DefinitionCI);
    ASSERT_NE(pMaterial, nullptr);

    for (const StandardMaterialTextureTestInfo& TextureInfo : StandardMaterialTextureTestInfos)
    {
        const GLTF::Material::TextureShaderAttribs& Expected = Material.GetTextureAttrib(TextureInfo.TextureAttribId);
        const std::string                           Name{TextureInfo.ParameterName};

        EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterial, (Name + "UVSelector").c_str()), Expected.GetUVSelector());

        const float2x2 ActualUVScaleAndRotation =
            GetMaterialParameter<float2x2>(*pMaterial, (Name + "UVScaleAndRotation").c_str());
        EXPECT_FLOAT_EQ(ActualUVScaleAndRotation._11, Expected.UVScaleAndRotation._11);
        EXPECT_FLOAT_EQ(ActualUVScaleAndRotation._12, Expected.UVScaleAndRotation._12);
        EXPECT_FLOAT_EQ(ActualUVScaleAndRotation._21, Expected.UVScaleAndRotation._21);
        EXPECT_FLOAT_EQ(ActualUVScaleAndRotation._22, Expected.UVScaleAndRotation._22);

        const RadientFloat2 ActualUVBias =
            GetMaterialParameter<RadientFloat2>(*pMaterial, (Name + "UVBias").c_str());
        EXPECT_FLOAT_EQ(ActualUVBias.x, Expected.UBias);
        EXPECT_FLOAT_EQ(ActualUVBias.y, Expected.VBias);
        EXPECT_EQ(GetMaterialParameter<Uint32>(*pMaterial, (Name + "WrapU").c_str()),
                  static_cast<Uint32>(Expected.GetWrapUMode()));
        EXPECT_EQ(GetMaterialParameter<Uint32>(*pMaterial, (Name + "WrapV").c_str()),
                  static_cast<Uint32>(Expected.GetWrapVMode()));
        EXPECT_EQ(GetMaterialTexture(*pMaterial, Name.c_str()), nullptr);
    }
}

TEST(RadientGLTFConverterTest, ConvertsUnlitMaterialDefinitionAndValues)
{
    RefCntAutoPtr<IRadientTextureAsset> pTexture =
        MakeTestTextureAsset("texture://gltf-unlit-material");
    IRadientTextureAsset* const Textures[] = {pTexture};

    GLTF::Material Material;
    Material.Attribs.Workflow        = GLTF::Material::PBR_WORKFLOW_UNLIT;
    Material.Attribs.BaseColorFactor = float4{0.2f, 0.4f, 0.6f, 0.8f};
    Material.Attribs.AlphaMode       = GLTF::Material::ALPHA_MODE_BLEND;
    GLTF::MaterialBuilder Builder{Material};
    Builder.SetTextureId(GLTF::DefaultBaseColorTextureAttribId, 0);
    Builder.Finalize();

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialAsset>        pMaterial =
        ConvertMaterial(Material, Textures, 1, DefinitionCI);
    ASSERT_NE(pMaterial, nullptr);

    EXPECT_EQ(DefinitionCI.ShadingModel, RADIENT_SURFACE_SHADING_MODEL_UNLIT);
    EXPECT_EQ(DefinitionCI.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);
    EXPECT_FLOAT_EQ(GetMaterialParameter<RadientFloat4>(*pMaterial, "BaseColorFactor").z, 0.6f);
    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurfaceMaterial{
        pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurfaceMaterial, nullptr);
    EXPECT_EQ(pSurfaceMaterial->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT);
    EXPECT_EQ(GetMaterialTexture(*pMaterial, "BaseColorTexture"), pTexture);

    RadientMaterialParameterHandle Handle;
    EXPECT_EQ(pMaterial->GetDefinition()->FindParameter("MetallicFactor", &Handle), RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pMaterial->GetDefinition()->FindParameter("NormalTexture", &Handle), RADIENT_STATUS_NOT_FOUND);
}

TEST(RadientGLTFConverterTest, ConvertsSpecularGlossinessMaterialDefinitionAndValues)
{
    GLTF::Material Material;
    Material.Attribs.Workflow        = GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS;
    Material.Attribs.BaseColorFactor = float4{0.15f, 0.25f, 0.35f, 0.45f};
    Material.Attribs.SpecularFactor  = float3{0.55f, 0.65f, 0.75f};
    Material.Attribs.RoughnessFactor = 0.85f; // Carries glossiness for this workflow.
    Material.Attribs.EmissiveFactor  = float3{0.12f, 0.23f, 0.34f};
    Material.Attribs.NormalScale     = 0.46f;
    Material.Attribs.OcclusionFactor = 0.57f;
    Material.Attribs.AlphaMode       = GLTF::Material::ALPHA_MODE_BLEND;
    Material.Attribs.AlphaCutoff     = 0.68f;
    Material.DoubleSided             = true;

    GLTF::MaterialBuilder Builder{Material};
    Builder.SetTextureId(GLTF::DefaultDiffuseTextureAttribId, 0);
    Builder.SetTextureId(GLTF::DefaultSpecularGlossinessTextureAttibId, 1);

    GLTF::Material::TextureShaderAttribs& DiffuseTextureAttribs =
        Builder.GetTextureAttrib(GLTF::DefaultDiffuseTextureAttribId);
    DiffuseTextureAttribs.SetUVSelector(1);
    DiffuseTextureAttribs.UVScaleAndRotation = float2x2{2.f, 0.1f, 0.2f, 3.f};
    DiffuseTextureAttribs.UBias              = 0.11f;
    DiffuseTextureAttribs.VBias              = 0.22f;
    DiffuseTextureAttribs.SetWrapUMode(TEXTURE_ADDRESS_CLAMP);
    DiffuseTextureAttribs.SetWrapVMode(TEXTURE_ADDRESS_WRAP);

    GLTF::Material::TextureShaderAttribs& SpecularGlossinessTextureAttribs =
        Builder.GetTextureAttrib(GLTF::DefaultSpecularGlossinessTextureAttibId);
    SpecularGlossinessTextureAttribs.SetUVSelector(0);
    SpecularGlossinessTextureAttribs.UVScaleAndRotation = float2x2{4.f, 0.3f, 0.4f, 5.f};
    SpecularGlossinessTextureAttribs.UBias              = 0.33f;
    SpecularGlossinessTextureAttribs.VBias              = 0.44f;
    SpecularGlossinessTextureAttribs.SetWrapUMode(TEXTURE_ADDRESS_WRAP);
    SpecularGlossinessTextureAttribs.SetWrapVMode(TEXTURE_ADDRESS_CLAMP);
    Builder.Finalize();

    RefCntAutoPtr<IRadientTextureAsset> pDiffuseTexture =
        MakeTestTextureAsset("texture://gltf-spec-gloss-diffuse");
    RefCntAutoPtr<IRadientTextureAsset> pSpecularGlossinessTexture =
        MakeTestTextureAsset("texture://gltf-spec-gloss-physical-description");
    IRadientTextureAsset* const Textures[] = {pDiffuseTexture, pSpecularGlossinessTexture};

    RadientStandardMaterialDefinitionCreateInfo DefinitionCI{};
    RefCntAutoPtr<IRadientMaterialAsset>        pMaterial =
        ConvertMaterial(Material, Textures, static_cast<Uint32>(std::size(Textures)), DefinitionCI);
    ASSERT_NE(pMaterial, nullptr);

    EXPECT_EQ(DefinitionCI.ShadingModel, RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS);
    EXPECT_EQ(DefinitionCI.Features, RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE);

    const RadientFloat4 DiffuseFactor =
        GetMaterialParameter<RadientFloat4>(*pMaterial, RadientStandardMaterialDiffuseFactorName);
    EXPECT_FLOAT_EQ(DiffuseFactor.x, Material.Attribs.BaseColorFactor.x);
    EXPECT_FLOAT_EQ(DiffuseFactor.y, Material.Attribs.BaseColorFactor.y);
    EXPECT_FLOAT_EQ(DiffuseFactor.z, Material.Attribs.BaseColorFactor.z);
    EXPECT_FLOAT_EQ(DiffuseFactor.w, Material.Attribs.BaseColorFactor.w);

    const RadientFloat3 SpecularFactor =
        GetMaterialParameter<RadientFloat3>(*pMaterial, RadientStandardMaterialSpecularFactorName);
    EXPECT_FLOAT_EQ(SpecularFactor.x, Material.Attribs.SpecularFactor.x);
    EXPECT_FLOAT_EQ(SpecularFactor.y, Material.Attribs.SpecularFactor.y);
    EXPECT_FLOAT_EQ(SpecularFactor.z, Material.Attribs.SpecularFactor.z);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, RadientStandardMaterialGlossinessFactorName),
                    Material.Attribs.RoughnessFactor);

    const RadientFloat3 EmissiveFactor =
        GetMaterialParameter<RadientFloat3>(*pMaterial, RadientStandardMaterialEmissiveFactorName);
    EXPECT_FLOAT_EQ(EmissiveFactor.x, Material.Attribs.EmissiveFactor.x);
    EXPECT_FLOAT_EQ(EmissiveFactor.y, Material.Attribs.EmissiveFactor.y);
    EXPECT_FLOAT_EQ(EmissiveFactor.z, Material.Attribs.EmissiveFactor.z);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, RadientStandardMaterialNormalScaleName),
                    Material.Attribs.NormalScale);
    EXPECT_FLOAT_EQ(GetMaterialParameter<Float32>(*pMaterial, RadientStandardMaterialOcclusionStrengthName),
                    Material.Attribs.OcclusionFactor);

    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurfaceMaterial{
        pMaterial, IID_RadientSurfaceMaterialAsset};
    ASSERT_NE(pSurfaceMaterial, nullptr);
    EXPECT_EQ(pSurfaceMaterial->GetSurfaceMode(), RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT);
    EXPECT_FLOAT_EQ(pSurfaceMaterial->GetAlphaCutoff(), Material.Attribs.AlphaCutoff);
    EXPECT_TRUE(pSurfaceMaterial->IsDoubleSided());

    EXPECT_EQ(GetMaterialTexture(*pMaterial, RadientStandardMaterialDiffuseTextureName),
              pDiffuseTexture);
    EXPECT_EQ(GetMaterialTexture(*pMaterial, RadientStandardMaterialSpecularGlossinessTextureName),
              pSpecularGlossinessTexture);

    const auto ExpectTextureBinding = [&](const RadientStandardMaterialTextureParameterNames& Names,
                                          const GLTF::Material::TextureShaderAttribs&         Expected) {
        EXPECT_EQ(GetMaterialParameter<Int32>(*pMaterial, Names.UVSelector), Expected.GetUVSelector());

        const float2x2 UVScaleAndRotation =
            GetMaterialParameter<float2x2>(*pMaterial, Names.UVScaleAndRotation);
        EXPECT_FLOAT_EQ(UVScaleAndRotation._11, Expected.UVScaleAndRotation._11);
        EXPECT_FLOAT_EQ(UVScaleAndRotation._12, Expected.UVScaleAndRotation._12);
        EXPECT_FLOAT_EQ(UVScaleAndRotation._21, Expected.UVScaleAndRotation._21);
        EXPECT_FLOAT_EQ(UVScaleAndRotation._22, Expected.UVScaleAndRotation._22);

        const RadientFloat2 UVBias =
            GetMaterialParameter<RadientFloat2>(*pMaterial, Names.UVBias);
        EXPECT_FLOAT_EQ(UVBias.x, Expected.UBias);
        EXPECT_FLOAT_EQ(UVBias.y, Expected.VBias);
        EXPECT_EQ(GetMaterialParameter<Uint32>(*pMaterial, Names.WrapU),
                  static_cast<Uint32>(Expected.GetWrapUMode()));
        EXPECT_EQ(GetMaterialParameter<Uint32>(*pMaterial, Names.WrapV),
                  static_cast<Uint32>(Expected.GetWrapVMode()));
    };

    ExpectTextureBinding(RadientStandardMaterialDiffuseTextureParameterNames,
                         Material.GetTextureAttrib(GLTF::DefaultDiffuseTextureAttribId));
    ExpectTextureBinding(RadientStandardMaterialSpecularGlossinessTextureParameterNames,
                         Material.GetTextureAttrib(GLTF::DefaultSpecularGlossinessTextureAttibId));

    RadientMaterialParameterHandle Handle;
    EXPECT_EQ(pMaterial->GetDefinition()->FindParameter(RadientStandardMaterialBaseColorFactorName, &Handle),
              RADIENT_STATUS_NOT_FOUND);
    EXPECT_EQ(pMaterial->GetDefinition()->FindParameter(RadientStandardMaterialMetallicRoughnessTextureName, &Handle),
              RADIENT_STATUS_NOT_FOUND);
}

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
    EXPECT_EQ(Result.Status, RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Result.pSource, nullptr);

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
    EXPECT_EQ(Result.Status, RADIENT_STATUS_INVALID_ARGUMENT);
    EXPECT_EQ(Result.pSource, nullptr);

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
    EXPECT_NEAR(Scene.Nodes[1].Camera->HorizontalAperture, 8.f, EPSILON);
    EXPECT_NEAR(Scene.Nodes[1].Camera->VerticalAperture, 6.f, EPSILON);
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

TEST(RadientGLTFConverterTest, ExtractSceneGraphCreatesSkinWithCompleteJointHierarchy)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://skinned", 1);
    ASSERT_NE(pMesh, nullptr);

    GLTF::Model Model;
    Model.Meshes.resize(1);
    Model.Meshes[0].pUserData = RefCntAutoPtr<IObject>{pMesh.RawPtr(), IID_Unknown};

    Model.Nodes.reserve(5);
    for (int NodeIndex = 0; NodeIndex < 5; ++NodeIndex)
        Model.Nodes.emplace_back(NodeIndex);

    Model.Nodes[0].Name        = "Root";
    Model.Nodes[0].Translation = {1.f, 0.f, 0.f};
    Model.Nodes[0].Children    = {&Model.Nodes[1]};

    Model.Nodes[1].Name        = "NonJointAncestor";
    Model.Nodes[1].Parent      = &Model.Nodes[0];
    Model.Nodes[1].Translation = {0.f, 2.f, 0.f};
    Model.Nodes[1].Children    = {&Model.Nodes[2]};

    Model.Nodes[2].Name        = "JointA";
    Model.Nodes[2].Parent      = &Model.Nodes[1];
    Model.Nodes[2].Translation = {0.f, 0.f, 3.f};
    Model.Nodes[2].Children    = {&Model.Nodes[3]};

    Model.Nodes[3].Name   = "JointB";
    Model.Nodes[3].Parent = &Model.Nodes[2];
    Model.Nodes[3].Scale  = {2.f, 3.f, 4.f};

    Model.Skins.resize(1);
    Model.Skins[0].Name          = "Character";
    Model.Skins[0].pSkeletonRoot = &Model.Nodes[2];
    Model.Skins[0].Joints        = {&Model.Nodes[2], &Model.Nodes[3]};

    Model.Nodes[4].Name  = "SkinnedMesh";
    Model.Nodes[4].pMesh = &Model.Meshes[0];
    Model.Nodes[4].pSkin = &Model.Skins[0];

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Joint motion";

    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers.back().Inputs      = {2.f, 4.f};
    Animation.Samplers.back().OutputsVec4 = {
        {0.f, 0.f, 3.f, 0.f},
        {2.f, 0.f, 3.f, 0.f},
    };

    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::CUBICSPLINE);
    Animation.Samplers.back().Inputs      = {2.f, 4.f};
    Animation.Samplers.back().OutputsVec4 = {
        {0.f, 0.f, 0.f, 0.f},
        {0.f, 0.f, 0.f, 1.f},
        {0.f, 0.f, 0.f, 0.f},
        {0.f, 0.f, 0.f, 0.f},
        {0.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 0.f, 0.f},
    };

    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::STEP);
    Animation.Samplers.back().Inputs      = {2.f, 4.f};
    Animation.Samplers.back().OutputsVec4 = {
        {2.f, 3.f, 4.f, 0.f},
        {5.f, 6.f, 7.f, 0.f},
    };

    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[2], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::ROTATION, &Model.Nodes[3], 1);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::SCALE, &Model.Nodes[3], 2);

    Model.Scenes.resize(1);
    Model.Scenes[0].RootNodes = {&Model.Nodes[0], &Model.Nodes[4]};

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Skins.size(), 1u);
    ASSERT_NE(Scene.Skins[0], nullptr);
    EXPECT_EQ(Scene.Nodes[4].SkinIndex, 0u);

    const RadientSkinDesc& SkinDesc = Scene.Skins[0]->GetDesc();
    ASSERT_NE(SkinDesc.pSkeleton, nullptr);
    ASSERT_EQ(SkinDesc.JointCount, 2u);
    EXPECT_EQ(SkinDesc.pJoints[0].SkeletonJointIndex, 2u);
    EXPECT_EQ(SkinDesc.pJoints[1].SkeletonJointIndex, 3u);
    EXPECT_EQ(SkinDesc.pJoints[0].InverseBindMatrix, RadientMatrix4x4{});
    EXPECT_EQ(SkinDesc.pJoints[1].InverseBindMatrix, RadientMatrix4x4{});

    const RadientSkeletonDesc& SkeletonDesc = SkinDesc.pSkeleton->GetDesc();
    ASSERT_EQ(SkeletonDesc.JointCount, 4u);
    EXPECT_STREQ(SkeletonDesc.pJoints[0].Name, "Root");
    EXPECT_EQ(SkeletonDesc.pJoints[0].ParentJointIndex, InvalidRadientJointIndex);
    EXPECT_STREQ(SkeletonDesc.pJoints[1].Name, "NonJointAncestor");
    EXPECT_EQ(SkeletonDesc.pJoints[1].ParentJointIndex, 0u);
    EXPECT_STREQ(SkeletonDesc.pJoints[2].Name, "JointA");
    EXPECT_EQ(SkeletonDesc.pJoints[2].ParentJointIndex, 1u);
    EXPECT_STREQ(SkeletonDesc.pJoints[3].Name, "JointB");
    EXPECT_EQ(SkeletonDesc.pJoints[3].ParentJointIndex, 2u);
    ExpectFloat3Near(SkeletonDesc.pJoints[0].LocalRestTransform.Position, {1.f, 0.f, 0.f});
    ExpectFloat3Near(SkeletonDesc.pJoints[1].LocalRestTransform.Position, {0.f, 2.f, 0.f});
    ExpectFloat3Near(SkeletonDesc.pJoints[2].LocalRestTransform.Position, {0.f, 0.f, 3.f});
    ExpectFloat3Near(SkeletonDesc.pJoints[3].LocalRestTransform.Scale, {2.f, 3.f, 4.f});

    ASSERT_EQ(Scene.Animations.size(), 1u);
    const RadientImport::ImportedAnimation& ImportedAnimation = Scene.Animations[0];
    EXPECT_EQ(ImportedAnimation.Name, "Joint motion");
    EXPECT_FLOAT_EQ(ImportedAnimation.Duration, 2.f);
    ASSERT_EQ(ImportedAnimation.SkeletonAnimationBindings.size(), 1u);
    ASSERT_NE(ImportedAnimation.SkeletonAnimationBindings[0].pAnimation, nullptr);

    const RadientSkeletonAnimationDesc& AnimationDesc =
        ImportedAnimation.SkeletonAnimationBindings[0].pAnimation->GetDesc();
    EXPECT_EQ(AnimationDesc.pSkeleton, SkinDesc.pSkeleton);
    EXPECT_FLOAT_EQ(AnimationDesc.Duration, 2.f);
    ASSERT_EQ(AnimationDesc.TrackCount, 2u);

    const RadientSkeletonAnimationTrackDesc& JointATrack = AnimationDesc.pTracks[0];
    EXPECT_EQ(JointATrack.SkeletonJointIndex, 2u);
    EXPECT_EQ(JointATrack.Translation.Interpolation, RADIENT_ANIMATION_INTERPOLATION_LINEAR);
    ASSERT_EQ(JointATrack.Translation.KeyframeCount, 2u);
    EXPECT_FLOAT_EQ(JointATrack.Translation.pTimes[0], 0.f);
    EXPECT_FLOAT_EQ(JointATrack.Translation.pTimes[1], 2.f);

    const RadientSkeletonAnimationTrackDesc& JointBTrack = AnimationDesc.pTracks[1];
    EXPECT_EQ(JointBTrack.SkeletonJointIndex, 3u);
    EXPECT_EQ(JointBTrack.Rotation.Interpolation, RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE);
    EXPECT_EQ(JointBTrack.Scale.Interpolation, RADIENT_ANIMATION_INTERPOLATION_STEP);
    ASSERT_EQ(JointBTrack.Rotation.KeyframeCount, 2u);
    ASSERT_EQ(JointBTrack.Scale.KeyframeCount, 2u);
}

TEST(RadientGLTFConverterTest, OneSourceAnimationTargetsEveryAffectedSkeleton)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.reserve(2);
    Model.Nodes.emplace_back(0);
    Model.Nodes.emplace_back(1);
    Model.Nodes[0].Name     = "Root";
    Model.Nodes[0].Children = {&Model.Nodes[1]};
    Model.Nodes[1].Name     = "Joint";
    Model.Nodes[1].Parent   = &Model.Nodes[0];

    Model.Skins.resize(2);
    for (GLTF::Skin& Skin : Model.Skins)
    {
        Skin.pSkeletonRoot = &Model.Nodes[1];
        Skin.Joints        = {&Model.Nodes[1]};
    }

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Shared motion";
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs      = {0.f, 1.f};
    Animation.Samplers[0].OutputsVec4 = {
        {0.f, 0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f, 0.f},
    };
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[1], 0);

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Skins.size(), 2u);
    ASSERT_EQ(Scene.Animations.size(), 1u);
    ASSERT_EQ(Scene.Animations[0].SkeletonAnimationBindings.size(), 2u);

    for (Uint32 SkinIndex = 0; SkinIndex < 2; ++SkinIndex)
    {
        IRadientSkeletonAnimationAsset* const pAnimation =
            Scene.Animations[0].SkeletonAnimationBindings[SkinIndex].pAnimation;
        ASSERT_NE(pAnimation, nullptr);
        EXPECT_EQ(pAnimation->GetDesc().pSkeleton, Scene.Skins[SkinIndex]->GetDesc().pSkeleton);
    }
    EXPECT_NE(Scene.Skins[0]->GetDesc().pSkeleton, Scene.Skins[1]->GetDesc().pSkeleton);
}

TEST(RadientGLTFConverterTest, InstantiateSceneGraphSkipsNullAnimationBindings)
{
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine({}, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pEngine, nullptr);

    RefCntAutoPtr<IRadientAssetManager> pAssetManager;
    ASSERT_EQ(pEngine->GetAssetManager(pAssetManager.GetAddressOfEmpty()), RADIENT_STATUS_OK);
    ASSERT_NE(pAssetManager, nullptr);

    RadientSkeletonJointDesc Joint{};
    Joint.Name = "Root";

    RadientSkeletonDesc SkeletonDesc{};
    SkeletonDesc.Name       = "Null animation binding skeleton";
    SkeletonDesc.pJoints    = &Joint;
    SkeletonDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    ASSERT_EQ(pAssetManager->CreateSkeleton(SkeletonDesc, pSkeleton.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RadientSkinJointBindingDesc JointBinding{};
    JointBinding.SkeletonJointIndex = 0;

    RadientSkinDesc SkinDesc{};
    SkinDesc.Name       = "Null animation binding skin";
    SkinDesc.pSkeleton  = pSkeleton;
    SkinDesc.pJoints    = &JointBinding;
    SkinDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    ASSERT_EQ(pAssetManager->CreateSkin(SkinDesc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientSkeletonAnimationDesc AnimationDesc{};
    AnimationDesc.Name      = "Valid animation";
    AnimationDesc.pSkeleton = pSkeleton;
    AnimationDesc.Duration  = 1.f;

    RefCntAutoPtr<IRadientSkeletonAnimationAsset> pAnimation;
    ASSERT_EQ(pAssetManager->CreateSkeletonAnimation(AnimationDesc, pAnimation.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://null-animation-binding", 1);
    ASSERT_NE(pMesh, nullptr);

    RadientImport::ImportedDocument ImportedScene;
    ImportedScene.Skins.emplace_back(pSkin);

    RadientImport::ImportedNode& Node = ImportedScene.Nodes.emplace_back();
    Node.pMesh                        = pMesh;
    Node.SkinIndex                    = 0;

    ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);

    RadientImport::ImportedAnimation& ImportedAnimation = ImportedScene.Animations.emplace_back();
    ImportedAnimation.Name                              = "Imported animation";
    ImportedAnimation.AddSkeletonAnimation(pAnimation);
    ImportedAnimation.SkeletonAnimationBindings.push_back({nullptr});

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientAnimationRegistry> pRegistry;
    ASSERT_EQ(pEngine->CreateAnimationRegistry(pScene, pRegistry.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientGLTFConverter::InstantiateSceneGraph(
                  ImportedScene, 0, *pWriter, RootEntity, pRegistry),
              RADIENT_STATUS_OK);

    const RadientAnimationRegistryState& RegistryState = pRegistry->GetState();
    ASSERT_EQ(RegistryState.EntryCount, 1u);
    EXPECT_EQ(RegistryState.pEntries[0].pAnimation, pAnimation);
    EXPECT_EQ(RegistryState.pEntries[0].TargetCount, 1u);
}

TEST(RadientGLTFConverterTest, InstantiateSceneGraphRendersMeshWithoutAvailableSkin)
{
    RefCntAutoPtr<IRadientEngine> pEngine;
    ASSERT_EQ(CreateRadientEngine({}, pEngine.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://missing-skin", 1);
    ASSERT_NE(pMesh, nullptr);

    RadientImport::ImportedDocument ImportedScene;
    RadientImport::ImportedNode&    Node = ImportedScene.Nodes.emplace_back();
    Node.Name                            = "Missing skin node";
    Node.pMesh                           = pMesh;
    Node.SkinIndex                       = 0;
    ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);

    RefCntAutoPtr<IRadientScene> pScene;
    ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientSceneWriter> pWriter;
    ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RadientEntityID RootEntity = InvalidRadientEntityID;
    ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
    ASSERT_EQ(RadientGLTFConverter::InstantiateSceneGraph(ImportedScene, 0, *pWriter, RootEntity),
              RADIENT_STATUS_OK);
    ASSERT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

    Uint32 ChildCount = 0;
    ASSERT_EQ(pScene->GetChildCount(RootEntity, ChildCount), RADIENT_STATUS_OK);
    ASSERT_EQ(ChildCount, 1u);

    RadientEntityID MeshEntity         = InvalidRadientEntityID;
    Uint32          NumChildrenWritten = 0;
    ASSERT_EQ(pScene->GetChildren(RootEntity, 0, 1, &MeshEntity, NumChildrenWritten), RADIENT_STATUS_OK);
    ASSERT_EQ(NumChildrenWritten, 1u);

    Bool HasComponent = False;
    EXPECT_EQ(pScene->HasComponent(MeshEntity, RADIENT_COMPONENT_TYPE_MESH, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);
    EXPECT_EQ(pScene->HasComponent(MeshEntity, RADIENT_COMPONENT_TYPE_MESH_RENDERER, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, True);
    EXPECT_EQ(pScene->HasComponent(MeshEntity, RADIENT_COMPONENT_TYPE_SKIN, HasComponent), RADIENT_STATUS_OK);
    EXPECT_EQ(HasComponent, False);
}
