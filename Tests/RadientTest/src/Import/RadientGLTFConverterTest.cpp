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
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include "ObjectBase.hpp"

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientMeshIndexSource.hpp"
#include "Assets/RadientMeshTestHelpers.hpp"
#include "Assets/RadientMeshVertexSource.hpp"
#include "GLTFDocument.hpp"
#include "GLTFBuilder.hpp"
#include "GLTFLoader.hpp"
#include "Import/RadientGLTFConverter.hpp"
#include "RadientEngine.h"
#include "RadientMaterialTestHelpers.hpp"
#include "RadientMathTestHelpers.hpp"
#include "RadientStandardMaterialParameters.h"
#include "RadientSkinning.h"
#include "RadientTestAssetHelpers.hpp"
#include "RadientTestDataHelpers.hpp"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "../../../../../DiligentTools/ThirdParty/tinygltf/tiny_gltf.h"

#include "TinyGltfModelView.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <limits>
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

class FailingAnimationRegistry final : public ObjectBase<IRadientAnimationRegistry>
{
public:
    using TBase = ObjectBase<IRadientAnimationRegistry>;

    FailingAnimationRegistry(IReferenceCounters* pRefCounters, IRadientScene* pScene) :
        TBase{pRefCounters},
        m_pScene{pScene}
    {}

    virtual IRadientScene* DILIGENT_CALL_TYPE GetScene() const override final
    {
        return m_pScene;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE AddAnimationBinding(
        IRadientAnimationBinding* pBinding,
        const RadientEntityID*,
        Uint32) override final
    {
        ++AddCallCount;
        m_pAddedBinding = pBinding;
        return RADIENT_STATUS_FAILED;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimationBinding(
        IRadientAnimationBinding* pBinding,
        const RadientEntityID*,
        Uint32) override final
    {
        ++RemoveCallCount;
        RemovedAddedBinding = pBinding == m_pAddedBinding;
        return RADIENT_STATUS_NO_CHANGE;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveEntity(RadientEntityID) override final
    {
        return RADIENT_STATUS_NO_CHANGE;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveAnimationClip(
        IRadientAnimationClipAsset*) override final
    {
        return RADIENT_STATUS_NO_CHANGE;
    }

    virtual const RadientAnimationRegistryState& DILIGENT_CALL_TYPE GetState() const override final
    {
        return m_State;
    }

    Uint32 AddCallCount        = 0;
    Uint32 RemoveCallCount     = 0;
    bool   RemovedAddedBinding = false;

private:
    RefCntAutoPtr<IRadientScene>  m_pScene;
    IRadientAnimationBinding*     m_pAddedBinding = nullptr;
    RadientAnimationRegistryState m_State;
};

RADIENT_STATUS ExtractSingleMorphAnimation(
    Uint32                                     MorphTargetCount,
    GLTF::AnimationSampler::INTERPOLATION_TYPE Interpolation,
    Uint32                                     OutputComponentCount,
    std::initializer_list<Float32>             Outputs,
    RadientImport::ImportedDocument&           Scene,
    std::initializer_list<Float32>             Inputs = {0.f, 1.f})
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    if (pAssetManager == nullptr)
        return RADIENT_STATUS_FAILED;

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://animation-morph");
    if (pMesh == nullptr)
        return RADIENT_STATUS_FAILED;

    GLTF::Model Model;
    Model.Meshes.resize(1);
    Model.Meshes[0].pUserData = RefCntAutoPtr<IObject>{pMesh.RawPtr(), IID_Unknown};
    Model.Meshes[0].Primitives.emplace_back(0u, 0u, 0u, 1u, 0u, float3{}, float3{});
    Model.Meshes[0].Primitives[0].MorphTargets.resize(MorphTargetCount);
    Model.Nodes.emplace_back(0);
    Model.Nodes[0].pMesh = &Model.Meshes[0];

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Samplers.emplace_back(Interpolation);
    Animation.Samplers[0].Inputs.assign(Inputs);
    Animation.Samplers[0].OutputComponentCount = OutputComponentCount;
    Animation.Samplers[0].Outputs.assign(Outputs);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::WEIGHTS, &Model.Nodes[0], 0);

    return RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager);
}

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

Uint32 FindAnimationTargetIndex(const RadientAnimationClipDesc& Clip,
                                const RadientAnimationSchemaID& Schema,
                                RadientAnimationObjectID        Object)
{
    for (Uint32 TargetIndex = 0; TargetIndex < Clip.TargetCount; ++TargetIndex)
    {
        const RadientAnimationTargetDesc& Target = Clip.pTargets[TargetIndex];
        if (Target.Schema == Schema && Target.Object == Object)
            return TargetIndex;
    }

    return InvalidRadientAnimationTargetIndex;
}

const RadientAnimationChannelDesc* FindAnimationChannel(const RadientAnimationClipDesc& Clip,
                                                        Uint32                          TargetIndex,
                                                        RadientAnimationPropertyID      Property)
{
    for (Uint32 ChannelIndex = 0; ChannelIndex < Clip.ChannelCount; ++ChannelIndex)
    {
        const RadientAnimationChannelDesc& Channel = Clip.pChannels[ChannelIndex];
        if (Channel.TargetIndex == TargetIndex && Channel.Property == Property)
            return &Channel;
    }

    return nullptr;
}

const RadientImport::ImportedAnimationSkinMapping* FindAnimationSkinMapping(
    const RadientImport::ImportedAnimation& Animation,
    Uint32                                  SkinIndex)
{
    for (const RadientImport::ImportedAnimationSkinMapping& SkinMapping : Animation.SkinMappings)
    {
        if (SkinMapping.SkinIndex == SkinIndex)
            return &SkinMapping;
    }

    return nullptr;
}

const RadientAnimationDestinationMappingDesc* FindJointMapping(
    const RadientImport::ImportedAnimationSkinMapping& SkinMapping,
    Uint32                                             ClipTargetIndex)
{
    for (const RadientAnimationDestinationMappingDesc& JointMapping : SkinMapping.JointMappings)
    {
        if (JointMapping.ClipTargetIndex == ClipTargetIndex)
            return &JointMapping;
    }

    return nullptr;
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
    Animation.Samplers.back().Inputs               = {2.f, 4.f};
    Animation.Samplers.back().OutputComponentCount = 3;
    Animation.Samplers.back().Outputs              = {
        0.f, 0.f, 3.f,
        2.f, 0.f, 3.f};

    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::CUBICSPLINE);
    Animation.Samplers.back().Inputs               = {2.f, 4.f};
    Animation.Samplers.back().OutputComponentCount = 4;
    Animation.Samplers.back().Outputs              = {
        0.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
        0.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 0.f};

    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::STEP);
    Animation.Samplers.back().Inputs               = {2.f, 4.f};
    Animation.Samplers.back().OutputComponentCount = 3;
    Animation.Samplers.back().Outputs              = {
        2.f, 3.f, 4.f,
        5.f, 6.f, 7.f};

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
    ASSERT_NE(ImportedAnimation.pClip, nullptr);
    const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
    EXPECT_STREQ(ClipDesc.Name, "Joint motion");
    EXPECT_FLOAT_EQ(ClipDesc.Duration, 2.f);
    ASSERT_EQ(ClipDesc.TargetCount, 2u);
    ASSERT_NE(ClipDesc.pTargets, nullptr);
    ASSERT_EQ(ClipDesc.SamplerCount, 3u);
    ASSERT_NE(ClipDesc.pSamplers, nullptr);
    ASSERT_EQ(ClipDesc.ChannelCount, 3u);
    ASSERT_NE(ClipDesc.pChannels, nullptr);

    const Uint32 JointATargetIndex = FindAnimationTargetIndex(ClipDesc, RadientNodeAnimationSchemaID, 2u);
    const Uint32 JointBTargetIndex = FindAnimationTargetIndex(ClipDesc, RadientNodeAnimationSchemaID, 3u);
    ASSERT_NE(JointATargetIndex, InvalidRadientAnimationTargetIndex);
    ASSERT_NE(JointBTargetIndex, InvalidRadientAnimationTargetIndex);
    EXPECT_STREQ(ClipDesc.pTargets[JointATargetIndex].Name, "JointA");
    EXPECT_STREQ(ClipDesc.pTargets[JointBTargetIndex].Name, "JointB");

    const RadientAnimationChannelDesc* const pTranslationChannel =
        FindAnimationChannel(ClipDesc, JointATargetIndex, RadientNodeTranslationProperty);
    const RadientAnimationChannelDesc* const pRotationChannel =
        FindAnimationChannel(ClipDesc, JointBTargetIndex, RadientNodeRotationProperty);
    const RadientAnimationChannelDesc* const pScaleChannel =
        FindAnimationChannel(ClipDesc, JointBTargetIndex, RadientNodeScaleProperty);
    ASSERT_NE(pTranslationChannel, nullptr);
    ASSERT_NE(pRotationChannel, nullptr);
    ASSERT_NE(pScaleChannel, nullptr);
    EXPECT_EQ(pTranslationChannel->FirstArrayElement, 0u);
    EXPECT_EQ(pRotationChannel->FirstArrayElement, 0u);
    EXPECT_EQ(pScaleChannel->FirstArrayElement, 0u);
    ASSERT_LT(pTranslationChannel->SamplerIndex, ClipDesc.SamplerCount);
    ASSERT_LT(pRotationChannel->SamplerIndex, ClipDesc.SamplerCount);
    ASSERT_LT(pScaleChannel->SamplerIndex, ClipDesc.SamplerCount);

    const RadientAnimationSamplerDesc& TranslationSampler =
        ClipDesc.pSamplers[pTranslationChannel->SamplerIndex];
    EXPECT_EQ(TranslationSampler.Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    EXPECT_EQ(TranslationSampler.Value.ArraySize, 1u);
    EXPECT_EQ(TranslationSampler.Interpolation, RADIENT_ANIMATION_INTERPOLATION_LINEAR);
    ASSERT_EQ(TranslationSampler.KeyframeCount, 2u);
    EXPECT_EQ(TranslationSampler.ValueDataSize, sizeof(RadientFloat3) * 2u);
    ASSERT_NE(TranslationSampler.pTimes, nullptr);
    EXPECT_FLOAT_EQ(TranslationSampler.pTimes[0], 0.f);
    EXPECT_FLOAT_EQ(TranslationSampler.pTimes[1], 2.f);
    ASSERT_NE(TranslationSampler.pValues, nullptr);
    const auto* const pTranslations = static_cast<const RadientFloat3*>(TranslationSampler.pValues);
    ExpectFloat3Near(pTranslations[0], {0.f, 0.f, 3.f});
    ExpectFloat3Near(pTranslations[1], {2.f, 0.f, 3.f});

    const RadientAnimationSamplerDesc& RotationSampler =
        ClipDesc.pSamplers[pRotationChannel->SamplerIndex];
    EXPECT_EQ(RotationSampler.Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT4);
    EXPECT_EQ(RotationSampler.Value.ArraySize, 1u);
    EXPECT_EQ(RotationSampler.Interpolation, RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE);
    ASSERT_EQ(RotationSampler.KeyframeCount, 2u);
    EXPECT_EQ(RotationSampler.ValueDataSize, sizeof(RadientQuaternion) * 6u);
    ASSERT_NE(RotationSampler.pTimes, nullptr);
    EXPECT_FLOAT_EQ(RotationSampler.pTimes[0], 0.f);
    EXPECT_FLOAT_EQ(RotationSampler.pTimes[1], 2.f);
    ASSERT_NE(RotationSampler.pValues, nullptr);
    const auto* const pRotations = static_cast<const RadientQuaternion*>(RotationSampler.pValues);
    ExpectQuaternionNear(pRotations[1], {0.f, 0.f, 0.f, 1.f});
    ExpectQuaternionNear(pRotations[4], {0.f, 0.f, 1.f, 0.f});

    const RadientAnimationSamplerDesc& ScaleSampler =
        ClipDesc.pSamplers[pScaleChannel->SamplerIndex];
    EXPECT_EQ(ScaleSampler.Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    EXPECT_EQ(ScaleSampler.Value.ArraySize, 1u);
    EXPECT_EQ(ScaleSampler.Interpolation, RADIENT_ANIMATION_INTERPOLATION_STEP);
    ASSERT_EQ(ScaleSampler.KeyframeCount, 2u);
    EXPECT_EQ(ScaleSampler.ValueDataSize, sizeof(RadientFloat3) * 2u);
    ASSERT_NE(ScaleSampler.pTimes, nullptr);
    EXPECT_FLOAT_EQ(ScaleSampler.pTimes[0], 0.f);
    EXPECT_FLOAT_EQ(ScaleSampler.pTimes[1], 2.f);
    ASSERT_NE(ScaleSampler.pValues, nullptr);
    const auto* const pScales = static_cast<const RadientFloat3*>(ScaleSampler.pValues);
    ExpectFloat3Near(pScales[0], {2.f, 3.f, 4.f});
    ExpectFloat3Near(pScales[1], {5.f, 6.f, 7.f});

    ASSERT_EQ(ImportedAnimation.SkinMappings.size(), 1u);
    const RadientImport::ImportedAnimationSkinMapping* const pSkinMapping =
        FindAnimationSkinMapping(ImportedAnimation, 0u);
    ASSERT_NE(pSkinMapping, nullptr);
    ASSERT_EQ(pSkinMapping->JointMappings.size(), 2u);
    const RadientAnimationDestinationMappingDesc* const pJointAMapping =
        FindJointMapping(*pSkinMapping, JointATargetIndex);
    const RadientAnimationDestinationMappingDesc* const pJointBMapping =
        FindJointMapping(*pSkinMapping, JointBTargetIndex);
    ASSERT_NE(pJointAMapping, nullptr);
    ASSERT_NE(pJointBMapping, nullptr);
    EXPECT_EQ(pJointAMapping->DestinationElement, 2u);
    EXPECT_EQ(pJointBMapping->DestinationElement, 3u);
}

TEST(RadientGLTFConverterTest, ExtractSceneGraphCreatesGenericAnimationWithoutSkins)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.emplace_back(0);
    Model.Nodes[0].Name = "AnimatedNode";

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Node motion";
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {2.f, 4.f};
    Animation.Samplers[0].OutputComponentCount = 3;
    Animation.Samplers[0].Outputs              = {
        1.f, 2.f, 3.f,
        4.f, 5.f, 6.f};
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::STEP);
    Animation.Samplers[1].Inputs               = {3.f, 5.f};
    Animation.Samplers[1].OutputComponentCount = 3;
    Animation.Samplers[1].Outputs              = {
        1.f, 1.f, 1.f,
        2.f, 2.f, 2.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::SCALE, &Model.Nodes[0], 1);

    Model.Scenes.resize(1);
    Model.Scenes[0].RootNodes = {&Model.Nodes[0]};

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    EXPECT_TRUE(Scene.Skins.empty());
    ASSERT_EQ(Scene.Animations.size(), 1u);
    const RadientImport::ImportedAnimation& ImportedAnimation = Scene.Animations[0];
    EXPECT_TRUE(ImportedAnimation.SkinMappings.empty());

    ASSERT_NE(ImportedAnimation.pClip, nullptr);
    const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
    EXPECT_STREQ(ClipDesc.Name, "Node motion");
    EXPECT_FLOAT_EQ(ClipDesc.Duration, 3.f);
    ASSERT_EQ(ClipDesc.TargetCount, 1u);
    ASSERT_EQ(ClipDesc.SamplerCount, 2u);
    ASSERT_EQ(ClipDesc.ChannelCount, 2u);

    const Uint32 NodeTargetIndex = FindAnimationTargetIndex(ClipDesc, RadientNodeAnimationSchemaID, 0u);
    ASSERT_NE(NodeTargetIndex, InvalidRadientAnimationTargetIndex);
    EXPECT_STREQ(ClipDesc.pTargets[NodeTargetIndex].Name, "AnimatedNode");
    const RadientAnimationChannelDesc* const pTranslationChannel =
        FindAnimationChannel(ClipDesc, NodeTargetIndex, RadientNodeTranslationProperty);
    ASSERT_NE(pTranslationChannel, nullptr);
    ASSERT_LT(pTranslationChannel->SamplerIndex, ClipDesc.SamplerCount);

    const RadientAnimationSamplerDesc& Sampler = ClipDesc.pSamplers[pTranslationChannel->SamplerIndex];
    EXPECT_EQ(Sampler.Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    EXPECT_EQ(Sampler.Value.ArraySize, 1u);
    EXPECT_EQ(Sampler.Interpolation, RADIENT_ANIMATION_INTERPOLATION_LINEAR);
    ASSERT_EQ(Sampler.KeyframeCount, 2u);
    ASSERT_NE(Sampler.pTimes, nullptr);
    EXPECT_FLOAT_EQ(Sampler.pTimes[0], 0.f);
    EXPECT_FLOAT_EQ(Sampler.pTimes[1], 2.f);

    const RadientAnimationChannelDesc* const pScaleChannel =
        FindAnimationChannel(ClipDesc, NodeTargetIndex, RadientNodeScaleProperty);
    ASSERT_NE(pScaleChannel, nullptr);
    ASSERT_LT(pScaleChannel->SamplerIndex, ClipDesc.SamplerCount);
    const RadientAnimationSamplerDesc& ScaleSampler = ClipDesc.pSamplers[pScaleChannel->SamplerIndex];
    EXPECT_EQ(ScaleSampler.Interpolation, RADIENT_ANIMATION_INTERPOLATION_STEP);
    ASSERT_EQ(ScaleSampler.KeyframeCount, 2u);
    ASSERT_NE(ScaleSampler.pTimes, nullptr);
    EXPECT_FLOAT_EQ(ScaleSampler.pTimes[0], 1.f);
    EXPECT_FLOAT_EQ(ScaleSampler.pTimes[1], 3.f);
}

TEST(RadientGLTFConverterTest, ExtractSceneGraphCreatesMorphWeightAnimation)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://animation-morph");
    ASSERT_NE(pMesh, nullptr);

    GLTF::Model Model;
    Model.Meshes.resize(1);
    Model.Meshes[0].pUserData = RefCntAutoPtr<IObject>{pMesh.RawPtr(), IID_Unknown};
    Model.Meshes[0].Primitives.emplace_back(0u, 0u, 0u, 1u, 0u, float3{}, float3{});
    Model.Meshes[0].Primitives[0].MorphTargets.resize(3);
    Model.Nodes.emplace_back(0);
    Model.Nodes[0].Name  = "AnimatedMesh";
    Model.Nodes[0].pMesh = &Model.Meshes[0];

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Expression";
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::CUBICSPLINE);
    Animation.Samplers[0].Inputs               = {2.f, 4.f};
    Animation.Samplers[0].OutputComponentCount = 1;
    Animation.Samplers[0].Outputs              = {
        0.f, 0.f, 0.f,
        0.1f, 0.2f, 0.3f,
        1.f, 2.f, 3.f,
        4.f, 5.f, 6.f,
        0.4f, 0.5f, 0.6f,
        0.f, 0.f, 0.f};
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[1].Inputs               = {2.f, 4.f};
    Animation.Samplers[1].OutputComponentCount = 3;
    Animation.Samplers[1].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 2.f, 3.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::WEIGHTS, &Model.Nodes[0], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 1);

    Model.Scenes.resize(1);
    Model.Scenes[0].RootNodes = {&Model.Nodes[0]};

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Animations.size(), 1u);
    const RadientImport::ImportedAnimation& ImportedAnimation = Scene.Animations[0];
    EXPECT_TRUE(ImportedAnimation.SkinMappings.empty());
    ASSERT_NE(ImportedAnimation.pClip, nullptr);

    const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
    EXPECT_STREQ(ClipDesc.Name, "Expression");
    EXPECT_FLOAT_EQ(ClipDesc.Duration, 2.f);
    ASSERT_EQ(ClipDesc.TargetCount, 2u);
    ASSERT_EQ(ClipDesc.SamplerCount, 2u);
    ASSERT_EQ(ClipDesc.ChannelCount, 2u);

    const Uint32 MorphTargetIndex = FindAnimationTargetIndex(
        ClipDesc, RadientMorphWeightsAnimationSchemaID, 0u);
    const Uint32 TransformTargetIndex = FindAnimationTargetIndex(
        ClipDesc, RadientNodeAnimationSchemaID, 0u);
    ASSERT_NE(MorphTargetIndex, InvalidRadientAnimationTargetIndex);
    ASSERT_NE(TransformTargetIndex, InvalidRadientAnimationTargetIndex);
    EXPECT_NE(MorphTargetIndex, TransformTargetIndex);
    EXPECT_STREQ(ClipDesc.pTargets[MorphTargetIndex].Name, "AnimatedMesh");
    EXPECT_STREQ(ClipDesc.pTargets[TransformTargetIndex].Name, "AnimatedMesh");

    const RadientAnimationChannelDesc* const pWeightChannel =
        FindAnimationChannel(ClipDesc, MorphTargetIndex, RadientMorphWeightsProperty);
    ASSERT_NE(pWeightChannel, nullptr);
    EXPECT_EQ(pWeightChannel->FirstArrayElement, 0u);
    ASSERT_LT(pWeightChannel->SamplerIndex, ClipDesc.SamplerCount);

    const RadientAnimationSamplerDesc& WeightSampler =
        ClipDesc.pSamplers[pWeightChannel->SamplerIndex];
    EXPECT_EQ(WeightSampler.Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    EXPECT_EQ(WeightSampler.Value.ArraySize, 3u);
    EXPECT_EQ(WeightSampler.Interpolation, RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE);
    ASSERT_EQ(WeightSampler.KeyframeCount, 2u);
    EXPECT_EQ(WeightSampler.ValueDataSize, sizeof(Float32) * Animation.Samplers[0].Outputs.size());
    ASSERT_NE(WeightSampler.pTimes, nullptr);
    EXPECT_FLOAT_EQ(WeightSampler.pTimes[0], 0.f);
    EXPECT_FLOAT_EQ(WeightSampler.pTimes[1], 2.f);
    ASSERT_NE(WeightSampler.pValues, nullptr);
    const auto* const pWeights = static_cast<const Float32*>(WeightSampler.pValues);
    for (size_t ValueIndex = 0; ValueIndex < Animation.Samplers[0].Outputs.size(); ++ValueIndex)
        EXPECT_FLOAT_EQ(pWeights[ValueIndex], Animation.Samplers[0].Outputs[ValueIndex]);

    const RadientAnimationChannelDesc* const pTranslationChannel =
        FindAnimationChannel(ClipDesc, TransformTargetIndex, RadientNodeTranslationProperty);
    ASSERT_NE(pTranslationChannel, nullptr);
    ASSERT_LT(pTranslationChannel->SamplerIndex, ClipDesc.SamplerCount);
    const RadientAnimationSamplerDesc& TranslationSampler =
        ClipDesc.pSamplers[pTranslationChannel->SamplerIndex];
    EXPECT_EQ(TranslationSampler.Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    EXPECT_EQ(TranslationSampler.Value.ArraySize, 1u);
}

TEST(RadientGLTFConverterTest, NonFiniteMorphChannelDoesNotDiscardValidTransformChannel)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://animation-morph-invalid");
    ASSERT_NE(pMesh, nullptr);

    GLTF::Model Model;
    Model.Meshes.resize(1);
    Model.Meshes[0].pUserData = RefCntAutoPtr<IObject>{pMesh.RawPtr(), IID_Unknown};
    Model.Meshes[0].Primitives.emplace_back(0u, 0u, 0u, 1u, 0u, float3{}, float3{});
    Model.Meshes[0].Primitives[0].MorphTargets.resize(3);
    Model.Nodes.emplace_back(0);
    Model.Nodes[0].Name  = "AnimatedMesh";
    Model.Nodes[0].pMesh = &Model.Meshes[0];

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Valid transform and non-finite morph";
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 1;
    Animation.Samplers[0].Outputs              = {
        0.f, 0.1f, 0.2f,
        0.3f, std::numeric_limits<Float32>::quiet_NaN(), 0.5f};
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[1].Inputs               = {0.f, 1.f};
    Animation.Samplers[1].OutputComponentCount = 3;
    Animation.Samplers[1].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 2.f, 3.f};

    // Put the malformed channel first to verify that a partial morph-channel
    // attempt does not leave an orphan target or sampler in the resulting clip.
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::WEIGHTS, &Model.Nodes[0], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 1);

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Animations.size(), 1u);
    ASSERT_NE(Scene.Animations[0].pClip, nullptr);
    const RadientAnimationClipDesc& ClipDesc = Scene.Animations[0].pClip->GetDesc();
    ASSERT_EQ(ClipDesc.TargetCount, 1u);
    ASSERT_EQ(ClipDesc.SamplerCount, 1u);
    ASSERT_EQ(ClipDesc.ChannelCount, 1u);
    EXPECT_EQ(ClipDesc.pTargets[0].Schema, RadientNodeAnimationSchemaID);
    EXPECT_EQ(ClipDesc.pTargets[0].Object, 0u);

    const RadientAnimationChannelDesc& Channel = ClipDesc.pChannels[0];
    EXPECT_EQ(Channel.TargetIndex, 0u);
    EXPECT_EQ(Channel.Property, RadientNodeTranslationProperty);
    EXPECT_EQ(Channel.SamplerIndex, 0u);
    EXPECT_EQ(ClipDesc.pSamplers[0].Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    EXPECT_EQ(ClipDesc.pSamplers[0].Value.ArraySize, 1u);
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsNonFiniteWeightValue)
{
    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(ExtractSingleMorphAnimation(
                  2,
                  GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR,
                  1,
                  {0.f, 0.25f,
                   std::numeric_limits<Float32>::quiet_NaN(), 1.f},
                  Scene),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsNonFiniteKeyTime)
{
    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(ExtractSingleMorphAnimation(
                  2,
                  GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR,
                  1,
                  {0.f, 0.25f,
                   0.75f, 1.f},
                  Scene,
                  {0.f, std::numeric_limits<Float32>::infinity()}),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsNonMonotonicKeyTimes)
{
    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(ExtractSingleMorphAnimation(
                  2,
                  GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR,
                  1,
                  {0.f, 0.25f,
                   0.75f, 1.f},
                  Scene,
                  {1.f, 0.f}),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsInvalidLinearWeightValueCount)
{
    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(ExtractSingleMorphAnimation(
                  3,
                  GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR,
                  1,
                  {0.f, 0.1f, 0.2f, 0.3f, 0.4f},
                  Scene),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsInvalidCubicWeightValueCount)
{
    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(ExtractSingleMorphAnimation(
                  3,
                  GLTF::AnimationSampler::INTERPOLATION_TYPE::CUBICSPLINE,
                  1,
                  {0.f, 0.f, 0.f,
                   0.1f, 0.2f, 0.3f,
                   0.f, 0.f, 0.f,
                   0.f, 0.f, 0.f,
                   0.4f, 0.5f, 0.6f,
                   0.f, 0.f},
                  Scene),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsNonScalarOutputAccessor)
{
    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(ExtractSingleMorphAnimation(
                  3,
                  GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR,
                  3,
                  {0.f, 0.1f, 0.2f,
                   0.3f, 0.4f, 0.5f},
                  Scene),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsNodeWithoutMorphTargets)
{
    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(ExtractSingleMorphAnimation(
                  0,
                  GLTF::AnimationSampler::INTERPOLATION_TYPE::STEP,
                  1,
                  {0.f, 1.f},
                  Scene),
              RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsNodeWithoutMesh)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.emplace_back(0);
    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::STEP);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 1;
    Animation.Samplers[0].Outputs              = {0.f, 1.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::WEIGHTS, &Model.Nodes[0], 0);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, MorphAnimationSkipsSharedSamplerWithIncompatibleArraySize)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    RefCntAutoPtr<IRadientMeshAsset> pMesh2 = MakeTestMeshAsset("mesh://animation-morph-two");
    RefCntAutoPtr<IRadientMeshAsset> pMesh3 = MakeTestMeshAsset("mesh://animation-morph-three");
    ASSERT_NE(pMesh2, nullptr);
    ASSERT_NE(pMesh3, nullptr);

    GLTF::Model Model;
    Model.Meshes.resize(2);
    Model.Meshes[0].pUserData = RefCntAutoPtr<IObject>{pMesh2.RawPtr(), IID_Unknown};
    Model.Meshes[1].pUserData = RefCntAutoPtr<IObject>{pMesh3.RawPtr(), IID_Unknown};
    Model.Meshes[0].Primitives.emplace_back(0u, 0u, 0u, 1u, 0u, float3{}, float3{});
    Model.Meshes[1].Primitives.emplace_back(0u, 0u, 0u, 1u, 0u, float3{}, float3{});
    Model.Meshes[0].Primitives[0].MorphTargets.resize(2);
    Model.Meshes[1].Primitives[0].MorphTargets.resize(3);
    Model.Nodes.reserve(2);
    Model.Nodes.emplace_back(0);
    Model.Nodes.emplace_back(1);
    Model.Nodes[0].pMesh = &Model.Meshes[0];
    Model.Nodes[1].pMesh = &Model.Meshes[1];

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 1;
    Animation.Samplers[0].Outputs              = {0.f, 0.25f, 0.5f, 0.75f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::WEIGHTS, &Model.Nodes[0], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::WEIGHTS, &Model.Nodes[1], 0);

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);
    ASSERT_EQ(Scene.Animations.size(), 1u);
    ASSERT_NE(Scene.Animations[0].pClip, nullptr);

    const RadientAnimationClipDesc& ClipDesc = Scene.Animations[0].pClip->GetDesc();
    ASSERT_EQ(ClipDesc.TargetCount, 1u);
    ASSERT_EQ(ClipDesc.SamplerCount, 1u);
    ASSERT_EQ(ClipDesc.ChannelCount, 1u);
    EXPECT_EQ(ClipDesc.pTargets[0].Schema, RadientMorphWeightsAnimationSchemaID);
    EXPECT_EQ(ClipDesc.pTargets[0].Object, 0u);
    EXPECT_EQ(ClipDesc.pSamplers[0].Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT);
    EXPECT_EQ(ClipDesc.pSamplers[0].Value.ArraySize, 2u);
    EXPECT_EQ(ClipDesc.pChannels[0].TargetIndex, 0u);
    EXPECT_EQ(ClipDesc.pChannels[0].Property, RadientMorphWeightsProperty);
    EXPECT_EQ(ClipDesc.pChannels[0].SamplerIndex, 0u);
}

TEST(RadientGLTFConverterTest, GenericAnimationRetainsTargetsOutsideSkeletonMappings)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.reserve(3);
    Model.Nodes.emplace_back(0);
    Model.Nodes.emplace_back(1);
    Model.Nodes.emplace_back(2);
    Model.Nodes[0].Name     = "LooseNode";
    Model.Nodes[1].Name     = "SkeletonRoot";
    Model.Nodes[1].Children = {&Model.Nodes[2]};
    Model.Nodes[2].Name     = "Joint";
    Model.Nodes[2].Parent   = &Model.Nodes[1];

    Model.Skins.resize(1);
    Model.Skins[0].pSkeletonRoot = &Model.Nodes[1];
    Model.Skins[0].Joints        = {&Model.Nodes[2]};

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Mixed motion";
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 3;
    Animation.Samplers[0].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 0.f, 0.f};
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::STEP);
    Animation.Samplers[1].Inputs               = {0.f, 1.f};
    Animation.Samplers[1].OutputComponentCount = 3;
    Animation.Samplers[1].Outputs              = {
        1.f, 1.f, 1.f,
        2.f, 2.f, 2.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[1], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::SCALE, &Model.Nodes[0], 1);

    Model.Scenes.resize(1);
    Model.Scenes[0].RootNodes = {&Model.Nodes[0], &Model.Nodes[1]};

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Animations.size(), 1u);
    const RadientImport::ImportedAnimation& ImportedAnimation = Scene.Animations[0];
    ASSERT_NE(ImportedAnimation.pClip, nullptr);
    const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
    ASSERT_EQ(ClipDesc.TargetCount, 2u);
    ASSERT_EQ(ClipDesc.SamplerCount, 2u);
    ASSERT_EQ(ClipDesc.ChannelCount, 2u);

    const Uint32 RootTargetIndex  = FindAnimationTargetIndex(ClipDesc, RadientNodeAnimationSchemaID, 1u);
    const Uint32 LooseTargetIndex = FindAnimationTargetIndex(ClipDesc, RadientNodeAnimationSchemaID, 0u);
    ASSERT_NE(RootTargetIndex, InvalidRadientAnimationTargetIndex);
    ASSERT_NE(LooseTargetIndex, InvalidRadientAnimationTargetIndex);
    EXPECT_STREQ(ClipDesc.pTargets[RootTargetIndex].Name, "SkeletonRoot");
    EXPECT_STREQ(ClipDesc.pTargets[LooseTargetIndex].Name, "LooseNode");
    EXPECT_NE(FindAnimationChannel(ClipDesc, RootTargetIndex, RadientNodeTranslationProperty), nullptr);
    EXPECT_NE(FindAnimationChannel(ClipDesc, LooseTargetIndex, RadientNodeScaleProperty), nullptr);

    ASSERT_EQ(ImportedAnimation.SkinMappings.size(), 1u);
    const RadientImport::ImportedAnimationSkinMapping* const pSkinMapping =
        FindAnimationSkinMapping(ImportedAnimation, 0u);
    ASSERT_NE(pSkinMapping, nullptr);
    ASSERT_EQ(pSkinMapping->JointMappings.size(), 1u);
    const RadientAnimationDestinationMappingDesc* const pRootMapping =
        FindJointMapping(*pSkinMapping, RootTargetIndex);
    ASSERT_NE(pRootMapping, nullptr);
    EXPECT_EQ(pRootMapping->DestinationElement, 0u);
    EXPECT_EQ(FindJointMapping(*pSkinMapping, LooseTargetIndex), nullptr);
}

TEST(RadientGLTFConverterTest, GenericAnimationReusesCompatibleSourceSamplerAcrossProperties)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.emplace_back(0);
    Model.Nodes[0].Name = "AnimatedNode";

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Shared sampler";
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 3;
    Animation.Samplers[0].Outputs              = {
        1.f, 2.f, 3.f,
        4.f, 5.f, 6.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::SCALE, &Model.Nodes[0], 0);

    Model.Scenes.resize(1);
    Model.Scenes[0].RootNodes = {&Model.Nodes[0]};

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Animations.size(), 1u);
    const RadientImport::ImportedAnimation& ImportedAnimation = Scene.Animations[0];
    ASSERT_NE(ImportedAnimation.pClip, nullptr);
    EXPECT_TRUE(ImportedAnimation.SkinMappings.empty());

    const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
    ASSERT_EQ(ClipDesc.TargetCount, 1u);
    ASSERT_EQ(ClipDesc.SamplerCount, 1u);
    ASSERT_EQ(ClipDesc.ChannelCount, 2u);
    const Uint32 NodeTargetIndex = FindAnimationTargetIndex(ClipDesc, RadientNodeAnimationSchemaID, 0u);
    ASSERT_NE(NodeTargetIndex, InvalidRadientAnimationTargetIndex);

    const RadientAnimationChannelDesc* const pTranslationChannel =
        FindAnimationChannel(ClipDesc, NodeTargetIndex, RadientNodeTranslationProperty);
    const RadientAnimationChannelDesc* const pScaleChannel =
        FindAnimationChannel(ClipDesc, NodeTargetIndex, RadientNodeScaleProperty);
    ASSERT_NE(pTranslationChannel, nullptr);
    ASSERT_NE(pScaleChannel, nullptr);
    EXPECT_EQ(pTranslationChannel->SamplerIndex, pScaleChannel->SamplerIndex);
    EXPECT_EQ(pTranslationChannel->SamplerIndex, 0u);
    EXPECT_EQ(ClipDesc.pSamplers[0].Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
    EXPECT_EQ(ClipDesc.pSamplers[0].Value.ArraySize, 1u);
}

TEST(RadientGLTFConverterTest, GenericAnimationSkipsChannelWithIncompatibleSharedSamplerType)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.emplace_back(0);

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 3;
    Animation.Samplers[0].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 1.f, 1.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 0);
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::ROTATION, &Model.Nodes[0], 0);

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Animations.size(), 1u);
    ASSERT_NE(Scene.Animations[0].pClip, nullptr);
    const RadientAnimationClipDesc& ClipDesc = Scene.Animations[0].pClip->GetDesc();
    ASSERT_EQ(ClipDesc.TargetCount, 1u);
    ASSERT_EQ(ClipDesc.SamplerCount, 1u);
    ASSERT_EQ(ClipDesc.ChannelCount, 1u);
    EXPECT_EQ(ClipDesc.pChannels[0].Property, RadientNodeTranslationProperty);
    EXPECT_EQ(ClipDesc.pSamplers[0].Value.Type, RADIENT_ANIMATION_VALUE_TYPE_FLOAT3);
}

TEST(RadientGLTFConverterTest, MalformedTransformOnlyAnimationDoesNotFailSceneConversion)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.emplace_back(0);

    Model.Animations.resize(1);
    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Name             = "Malformed transform animation";
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 2;
    Animation.Samplers[0].Outputs              = {
        0.f, 0.f,
        1.f, 1.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 0);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);
    EXPECT_TRUE(Scene.Animations.empty());
}

TEST(RadientGLTFConverterTest, InvalidAnimationTimeRangeDoesNotDiscardFollowingAnimation)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);

    GLTF::Model Model;
    Model.Nodes.emplace_back(0);
    Model.Animations.resize(2);

    const Float32 MaxTime = (std::numeric_limits<Float32>::max)();

    GLTF::Animation& InvalidAnimation = Model.Animations[0];
    InvalidAnimation.Name             = "Overflowing time range";
    InvalidAnimation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    InvalidAnimation.Samplers[0].Inputs               = {-MaxTime, -1.f};
    InvalidAnimation.Samplers[0].OutputComponentCount = 3;
    InvalidAnimation.Samplers[0].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 0.f, 0.f};
    InvalidAnimation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    InvalidAnimation.Samplers[1].Inputs               = {1.f, MaxTime};
    InvalidAnimation.Samplers[1].OutputComponentCount = 3;
    InvalidAnimation.Samplers[1].Outputs              = {
        1.f, 1.f, 1.f,
        2.f, 2.f, 2.f};
    InvalidAnimation.Channels.emplace_back(
        GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 0);
    InvalidAnimation.Channels.emplace_back(
        GLTF::AnimationChannel::PATH_TYPE::SCALE, &Model.Nodes[0], 1);

    GLTF::Animation& ValidAnimation = Model.Animations[1];
    ValidAnimation.Name             = "Valid following animation";
    ValidAnimation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    ValidAnimation.Samplers[0].Inputs               = {2.f, 4.f};
    ValidAnimation.Samplers[0].OutputComponentCount = 3;
    ValidAnimation.Samplers[0].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 2.f, 3.f};
    ValidAnimation.Channels.emplace_back(
        GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 0);

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Animations.size(), 1u);
    ASSERT_NE(Scene.Animations[0].pClip, nullptr);
    const RadientAnimationClipDesc& ClipDesc = Scene.Animations[0].pClip->GetDesc();
    EXPECT_STREQ(ClipDesc.Name, "Valid following animation");
    EXPECT_FLOAT_EQ(ClipDesc.Duration, 2.f);
    ASSERT_EQ(ClipDesc.TargetCount, 1u);
    ASSERT_EQ(ClipDesc.SamplerCount, 1u);
    ASSERT_EQ(ClipDesc.ChannelCount, 1u);
    EXPECT_EQ(ClipDesc.pChannels[0].Property, RadientNodeTranslationProperty);
    ASSERT_EQ(ClipDesc.pSamplers[0].KeyframeCount, 2u);
    ASSERT_NE(ClipDesc.pSamplers[0].pTimes, nullptr);
    EXPECT_FLOAT_EQ(ClipDesc.pSamplers[0].pTimes[0], 0.f);
    EXPECT_FLOAT_EQ(ClipDesc.pSamplers[0].pTimes[1], 2.f);
}

TEST(RadientGLTFConverterTest, StoppedAssetManagerAnimationFailureRemainsFatal)
{
    RefCntAutoPtr<RadientAssetManagerImpl> pAssetManager = RadientAssetManagerImpl::Create({});
    ASSERT_NE(pAssetManager, nullptr);
    ASSERT_EQ(pAssetManager->Stop(nullptr), RADIENT_STATUS_OK);

    GLTF::Model Model;
    Model.Nodes.emplace_back(0);
    Model.Animations.resize(1);

    GLTF::Animation& Animation = Model.Animations[0];
    Animation.Samplers.emplace_back(GLTF::AnimationSampler::INTERPOLATION_TYPE::LINEAR);
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 3;
    Animation.Samplers[0].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 2.f, 3.f};
    Animation.Channels.emplace_back(
        GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[0], 0);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager),
              RADIENT_STATUS_INVALID_OPERATION);
    EXPECT_TRUE(Scene.Animations.empty());
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
    Animation.Samplers[0].Inputs               = {0.f, 1.f};
    Animation.Samplers[0].OutputComponentCount = 3;
    Animation.Samplers[0].Outputs              = {
        0.f, 0.f, 0.f,
        1.f, 0.f, 0.f};
    Animation.Channels.emplace_back(GLTF::AnimationChannel::PATH_TYPE::TRANSLATION, &Model.Nodes[1], 0);

    RadientImport::ImportedDocument Scene;
    ASSERT_EQ(RadientGLTFConverter::ExtractSceneGraph(Model, Scene, pAssetManager), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Skins.size(), 2u);
    ASSERT_EQ(Scene.Animations.size(), 1u);

    const RadientImport::ImportedAnimation& ImportedAnimation = Scene.Animations[0];
    ASSERT_NE(ImportedAnimation.pClip, nullptr);
    const RadientAnimationClipDesc& ClipDesc = ImportedAnimation.pClip->GetDesc();
    EXPECT_STREQ(ClipDesc.Name, "Shared motion");
    EXPECT_FLOAT_EQ(ClipDesc.Duration, 1.f);
    ASSERT_EQ(ClipDesc.TargetCount, 1u);
    ASSERT_EQ(ClipDesc.SamplerCount, 1u);
    ASSERT_EQ(ClipDesc.ChannelCount, 1u);

    const Uint32 JointTargetIndex = FindAnimationTargetIndex(ClipDesc, RadientNodeAnimationSchemaID, 1u);
    ASSERT_NE(JointTargetIndex, InvalidRadientAnimationTargetIndex);
    EXPECT_STREQ(ClipDesc.pTargets[JointTargetIndex].Name, "Joint");
    const RadientAnimationChannelDesc* const pTranslationChannel =
        FindAnimationChannel(ClipDesc, JointTargetIndex, RadientNodeTranslationProperty);
    ASSERT_NE(pTranslationChannel, nullptr);

    ASSERT_EQ(ImportedAnimation.SkinMappings.size(), 2u);
    for (Uint32 SkinIndex = 0; SkinIndex < 2; ++SkinIndex)
    {
        const RadientImport::ImportedAnimationSkinMapping* const pSkinMapping =
            FindAnimationSkinMapping(ImportedAnimation, SkinIndex);
        ASSERT_NE(pSkinMapping, nullptr);
        ASSERT_EQ(pSkinMapping->JointMappings.size(), 1u);
        const RadientAnimationDestinationMappingDesc* const pJointMapping =
            FindJointMapping(*pSkinMapping, JointTargetIndex);
        ASSERT_NE(pJointMapping, nullptr);
        EXPECT_EQ(pJointMapping->DestinationElement, 1u);
    }

    EXPECT_NE(Scene.Skins[0]->GetDesc().pSkeleton, Scene.Skins[1]->GetDesc().pSkeleton);
}

TEST(RadientGLTFConverterTest, InstantiateSceneGraphRegistersGenericAnimationBinding)
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
    SkeletonDesc.Name       = "Generic animation binding skeleton";
    SkeletonDesc.pJoints    = &Joint;
    SkeletonDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    ASSERT_EQ(pAssetManager->CreateSkeleton(SkeletonDesc, pSkeleton.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RadientSkinJointBindingDesc JointBinding{};
    JointBinding.SkeletonJointIndex = 0;

    RadientSkinDesc SkinDesc{};
    SkinDesc.Name       = "Generic animation binding skin";
    SkinDesc.pSkeleton  = pSkeleton;
    SkinDesc.pJoints    = &JointBinding;
    SkinDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    ASSERT_EQ(pAssetManager->CreateSkin(SkinDesc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array<Float32, 2>       Times  = {0.f, 1.f};
    const std::array<RadientFloat3, 2> Values = {RadientFloat3{}, RadientFloat3{1.f, 2.f, 3.f}};

    RadientAnimationTargetDesc Target{};
    Target.Schema = RadientNodeAnimationSchemaID;
    Target.Object = 0;

    RadientAnimationSamplerDesc Sampler{};
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
    Sampler.Value.ArraySize = 1;
    Sampler.pTimes          = Times.data();
    Sampler.pValues         = Values.data();
    Sampler.ValueDataSize   = sizeof(Values);
    Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

    RadientAnimationChannelDesc Channel{};
    Channel.TargetIndex  = 0;
    Channel.Property     = RadientNodeTranslationProperty;
    Channel.SamplerIndex = 0;

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = "Generic animation";
    ClipDesc.Duration     = 1.f;
    ClipDesc.pTargets     = &Target;
    ClipDesc.TargetCount  = 1;
    ClipDesc.pSamplers    = &Sampler;
    ClipDesc.SamplerCount = 1;
    ClipDesc.pChannels    = &Channel;
    ClipDesc.ChannelCount = 1;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://generic-animation-binding", 1);
    ASSERT_NE(pMesh, nullptr);

    RadientImport::ImportedDocument ImportedScene;
    ImportedScene.Skins.emplace_back(pSkin);

    RadientImport::ImportedNode& Node = ImportedScene.Nodes.emplace_back();
    Node.pMesh                        = pMesh;
    Node.SkinIndex                    = 0;

    ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);

    RadientImport::ImportedAnimation&            ImportedAnimation = ImportedScene.Animations.emplace_back();
    RadientImport::ImportedAnimationSkinMapping& SkinMapping       = ImportedAnimation.SkinMappings.emplace_back();
    ImportedAnimation.pClip                                        = pClip;
    SkinMapping.SkinIndex                                          = 0;
    SkinMapping.JointMappings.push_back({0, 0});

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
    EXPECT_EQ(RegistryState.pEntries[0].pClip, pClip);
    EXPECT_EQ(RegistryState.pEntries[0].BindingCount, 1u);
    ASSERT_NE(RegistryState.pEntries[0].ppBindings[0], nullptr);
    EXPECT_EQ(RegistryState.pEntries[0].ppBindings[0]->GetClip(), pClip);
}

TEST(RadientGLTFConverterTest, InstantiateSceneGraphSkipsAnimationBindingFailures)
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
    SkeletonDesc.Name       = "Static fallback skeleton";
    SkeletonDesc.pJoints    = &Joint;
    SkeletonDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkeletonAsset> pSkeleton;
    ASSERT_EQ(pAssetManager->CreateSkeleton(SkeletonDesc, pSkeleton.GetAddressOfEmpty()),
              RADIENT_STATUS_OK);

    RadientSkinJointBindingDesc JointBinding{};
    JointBinding.SkeletonJointIndex = 0;

    RadientSkinDesc SkinDesc{};
    SkinDesc.Name       = "Static fallback skin";
    SkinDesc.pSkeleton  = pSkeleton;
    SkinDesc.pJoints    = &JointBinding;
    SkinDesc.JointCount = 1;

    RefCntAutoPtr<IRadientSkinAsset> pSkin;
    ASSERT_EQ(pAssetManager->CreateSkin(SkinDesc, pSkin.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    const std::array<Float32, 2>       Times  = {0.f, 1.f};
    const std::array<RadientFloat3, 2> Values = {RadientFloat3{}, RadientFloat3{1.f, 2.f, 3.f}};

    RadientAnimationTargetDesc Target{};
    Target.Schema = RadientNodeAnimationSchemaID;
    Target.Object = 0;

    RadientAnimationSamplerDesc Sampler{};
    Sampler.Value.Type      = RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
    Sampler.Value.ArraySize = 1;
    Sampler.pTimes          = Times.data();
    Sampler.pValues         = Values.data();
    Sampler.ValueDataSize   = sizeof(Values);
    Sampler.KeyframeCount   = static_cast<Uint32>(Times.size());

    RadientAnimationChannelDesc Channel{};
    Channel.TargetIndex  = 0;
    Channel.Property     = RadientNodeTranslationProperty;
    Channel.SamplerIndex = 0;

    RadientAnimationClipDesc ClipDesc{};
    ClipDesc.Name         = "Static fallback animation";
    ClipDesc.Duration     = 1.f;
    ClipDesc.pTargets     = &Target;
    ClipDesc.TargetCount  = 1;
    ClipDesc.pSamplers    = &Sampler;
    ClipDesc.SamplerCount = 1;
    ClipDesc.pChannels    = &Channel;
    ClipDesc.ChannelCount = 1;

    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;
    ASSERT_EQ(pAssetManager->CreateAnimationClip(ClipDesc, pClip.GetAddressOfEmpty()), RADIENT_STATUS_OK);

    RefCntAutoPtr<IRadientMeshAsset> pMesh = MakeTestMeshAsset("mesh://static-animation-fallback", 1);
    ASSERT_NE(pMesh, nullptr);

    const auto Instantiate = [&](std::initializer_list<RadientAnimationDestinationElement> DestinationElements,
                                 Uint32                                                    ExpectedBindingCount,
                                 bool                                                      FailRegistration) {
        RadientImport::ImportedDocument ImportedScene;
        ImportedScene.Skins.emplace_back(pSkin);

        RadientImport::ImportedNode& Node = ImportedScene.Nodes.emplace_back();
        Node.pMesh                        = pMesh;
        Node.SkinIndex                    = 0;
        ImportedScene.Scenes.emplace_back().RootNodes.push_back(0);

        RadientImport::ImportedAnimation& ImportedAnimation = ImportedScene.Animations.emplace_back();
        ImportedAnimation.pClip                             = pClip;
        for (const RadientAnimationDestinationElement DestinationElement : DestinationElements)
        {
            RadientImport::ImportedAnimationSkinMapping& Mapping = ImportedAnimation.SkinMappings.emplace_back();
            Mapping.SkinIndex                                    = 0;
            Mapping.JointMappings.push_back({0, DestinationElement});
        }

        RefCntAutoPtr<IRadientScene> pScene;
        ASSERT_EQ(pEngine->CreateScene({}, pScene.GetAddressOfEmpty()), RADIENT_STATUS_OK);

        RefCntAutoPtr<IRadientSceneWriter> pWriter;
        ASSERT_EQ(pEngine->CreateSceneWriter(pScene, pWriter.GetAddressOfEmpty()), RADIENT_STATUS_OK);

        RefCntAutoPtr<IRadientAnimationRegistry> pRegistry;
        RefCntAutoPtr<FailingAnimationRegistry>  pFailingRegistry;
        IRadientAnimationRegistry*               pRegistryInterface = nullptr;
        if (FailRegistration)
        {
            pFailingRegistry = RefCntAutoPtr<FailingAnimationRegistry>{
                MakeNewRCObj<FailingAnimationRegistry>()(pScene)};
            pRegistryInterface = pFailingRegistry;
        }
        else
        {
            ASSERT_EQ(pEngine->CreateAnimationRegistry(pScene, pRegistry.GetAddressOfEmpty()), RADIENT_STATUS_OK);
            pRegistryInterface = pRegistry;
        }
        ASSERT_NE(pRegistryInterface, nullptr);

        RadientEntityID RootEntity = InvalidRadientEntityID;
        ASSERT_EQ(pWriter->CreateEntity({}, RootEntity), RADIENT_STATUS_OK);
        EXPECT_EQ(RadientGLTFConverter::InstantiateSceneGraph(
                      ImportedScene, 0, *pWriter, RootEntity, pRegistryInterface),
                  RADIENT_STATUS_OK);

        const RadientAnimationRegistryState& RegistryState = pRegistryInterface->GetState();
        if (ExpectedBindingCount == 0)
        {
            EXPECT_EQ(RegistryState.EntryCount, 0u);
        }
        else
        {
            ASSERT_EQ(RegistryState.EntryCount, 1u);
            ASSERT_EQ(RegistryState.pEntries[0].pClip, pClip);
            EXPECT_EQ(RegistryState.pEntries[0].BindingCount, ExpectedBindingCount);
        }

        if (FailRegistration)
        {
            EXPECT_EQ(pFailingRegistry->AddCallCount, 1u);
            EXPECT_EQ(pFailingRegistry->RemoveCallCount, 1u);
            EXPECT_TRUE(pFailingRegistry->RemovedAddedBinding);
        }

        EXPECT_EQ(pWriter->CommitChanges(), RADIENT_STATUS_OK);

        Uint32 ChildCount = 0;
        EXPECT_EQ(pScene->GetChildCount(RootEntity, ChildCount), RADIENT_STATUS_OK);
        EXPECT_EQ(ChildCount, 1u);
    };

    // A lone invalid binding leaves a usable static scene.
    Instantiate({1}, 0, false);

    // A later invalid binding does not discard an earlier valid binding.
    Instantiate({0, 1}, 1, false);

    // A registry failure is non-fatal and cleans up only the failed binding.
    Instantiate({0}, 0, true);
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
