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

#include "Assets/RadientAssetValidation.hpp"
#include "RadientMorphTargets.h"
#include "RadientTestAssetHelpers.hpp"

#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

// Descriptor validation must not query mutable storage before acquiring read access.
class SizeOnlyDataBlob final : public ObjectBase<IRadientDataBlob>
{
public:
    using TBase = ObjectBase<IRadientDataBlob>;
    SizeOnlyDataBlob(IReferenceCounters* pRefCounters, Uint64 Size) :
        TBase{pRefCounters}, m_Size{Size} {}
    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientDataBlob, TBase);
    Uint64 DILIGENT_CALL_TYPE GetSize() const override
    {
        ADD_FAILURE() << "Descriptor validation must not query blob storage";
        return m_Size;
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE BeginRead(const void** ppData) override
    {
        ADD_FAILURE() << "Validation must not acquire read access";
        if (ppData != nullptr) *ppData = nullptr;
        return RADIENT_STATUS_INVALID_OPERATION;
    }
    RADIENT_STATUS DILIGENT_CALL_TYPE EndRead() override { return RADIENT_STATUS_INVALID_OPERATION; }

private:
    const Uint64 m_Size;
};

struct MeshValidationData
{
    std::array<RadientFloat3, 2>                         Positions{};
    std::array<Uint16, 3>                                Indices16{0, 1, 0};
    std::array<Uint32, 3>                                Indices32{0, 1, 0};
    std::array<RadientBoneIndices4, 2>                   BoneIndices{};
    std::array<RadientFloat4, 2>                         BoneWeights{};
    std::array<Float32, 6>                               MorphDeltas{};
    RadientMorphTargetAttributeDesc                      MorphAttribute{};
    RadientMorphTargetAttributeCreateInfo                MorphAttributeData{};
    std::array<RadientMorphTargetAttributeDesc, 2>       DuplicateMorphAttributes{};
    std::array<RadientMorphTargetAttributeCreateInfo, 2> DuplicateMorphAttributeData{};
    RadientMorphTargetCreateInfo                         MorphTarget{};
    RadientMeshPrimitiveCreateInfo                       Primitive{};

    std::array<RadientVertexAttributeDesc, 3> VertexAttributes{
        {
            {
                "POSITION",
                0,
                RADIENT_VERTEX_AUTO_OFFSET,
                RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32,
                3,
                False,
            },
            {
                "JOINTS_0",
                1,
                RADIENT_VERTEX_AUTO_OFFSET,
                RADIENT_VERTEX_COMPONENT_TYPE_UINT16,
                4,
                False,
            },
            {
                "WEIGHTS_0",
                2,
                RADIENT_VERTEX_AUTO_OFFSET,
                RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32,
                4,
                False,
            },
        },
    };
    std::array<RadientVertexBufferLayoutDesc, 3>   VertexBuffers{};
    std::array<RefCntAutoPtr<IRadientDataBlob>, 3> VertexBlobs{{MakeTestDataBlob(Positions.data(), sizeof(Positions)),
                                                                MakeTestDataBlob(BoneIndices.data(), sizeof(BoneIndices)),
                                                                MakeTestDataBlob(BoneWeights.data(), sizeof(BoneWeights))}};
    std::array<IRadientDataBlob*, 3>               VertexData{{VertexBlobs[0], VertexBlobs[1], VertexBlobs[2]}};

    RefCntAutoPtr<IRadientDataBlob> IndexBlob16 = MakeTestDataBlob(Indices16.data(), sizeof(Indices16));
    RefCntAutoPtr<IRadientDataBlob> IndexBlob32 = MakeTestDataBlob(Indices32.data(), sizeof(Indices32));

    RadientMeshCreateInfo MakeMeshCI()
    {
        Primitive.FirstIndex = 0;
        Primitive.IndexCount = static_cast<Uint32>(Indices16.size());

        RadientMeshCreateInfo MeshCI{};
        MeshCI.VertexData.VertexLayout    = {VertexAttributes.data(), 1, VertexBuffers.data(), 1};
        MeshCI.VertexData.ppVertexBuffers = VertexData.data();
        MeshCI.VertexData.VertexCount     = static_cast<Uint32>(Positions.size());
        MeshCI.IndexData.pIndexBuffer     = IndexBlob16;
        MeshCI.IndexData.IndexCount       = static_cast<Uint32>(Indices16.size());
        MeshCI.IndexData.IndexType        = RADIENT_INDEX_TYPE_UINT16;
        MeshCI.pPrimitives                = &Primitive;
        MeshCI.PrimitiveCount             = 1;
        return MeshCI;
    }

    void AddMorphTarget(RadientMeshCreateInfo& MeshCI)
    {
        MorphAttribute.Semantic       = RadientMorphTargetPositionSemantic;
        MorphAttribute.ComponentCount = 3;
        MorphAttributeData.pDeltas    = MorphDeltas.data();

        MorphTarget.Desc.Name           = "Target";
        MorphTarget.Desc.pAttributes    = &MorphAttribute;
        MorphTarget.Desc.AttributeCount = 1;
        MorphTarget.Desc.DefaultWeight  = 0.5f;
        MorphTarget.pAttributeData      = &MorphAttributeData;

        MeshCI.MorphTargetData.pMorphTargets    = &MorphTarget;
        MeshCI.MorphTargetData.MorphTargetCount = 1;
    }
};

template <typename MutateType>
void ExpectInvalidMeshCreateInfo(const char* ExpectedError, MutateType&& Mutate)
{
    MeshValidationData    Data;
    RadientMeshCreateInfo MeshCI = Data.MakeMeshCI();
    Mutate(MeshCI, Data);

    TestingEnvironment::ErrorScope ExpectedErrors{ExpectedError};
    EXPECT_FALSE(ValidateMeshCreateInfo(MeshCI));
}

} // namespace

TEST(RadientAssetValidationTest, ValidatesMeshCreateInfo)
{
    MeshValidationData    Data;
    RadientMeshCreateInfo MeshCI = Data.MakeMeshCI();
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));

    const std::array<Uint8, 3>            Indices8{0, 1, 2};
    const RefCntAutoPtr<IRadientDataBlob> pIndexBlob8 = MakeTestDataBlob(Indices8.data(), sizeof(Indices8));
    MeshCI.IndexData.pIndexBuffer                     = pIndexBlob8;
    MeshCI.IndexData.IndexType                        = RADIENT_INDEX_TYPE_UINT8;
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));

    MeshCI.IndexData.pIndexBuffer = Data.IndexBlob32;
    MeshCI.IndexData.IndexType    = RADIENT_INDEX_TYPE_UINT32;
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));

    MeshCI.VertexData.VertexLayout.AttributeCount = MeshCI.VertexData.VertexLayout.BufferCount = 3;
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));
}

TEST(RadientAssetValidationTest, RejectsMeshCreateInfoMissingRequiredData)
{
    ExpectInvalidMeshCreateInfo("VertexCount must not be zero", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.VertexData.VertexCount = 0;
    });
    ExpectInvalidMeshCreateInfo("ppVertexBuffers must not be null", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.VertexData.ppVertexBuffers = nullptr;
    });
    ExpectInvalidMeshCreateInfo("IndexCount must not be zero", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.IndexData.IndexCount = 0;
    });
    ExpectInvalidMeshCreateInfo("pIndexBuffer must not be null", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.IndexData.pIndexBuffer = nullptr;
    });
    ExpectInvalidMeshCreateInfo("IndexType must be RADIENT_INDEX_TYPE_UINT8, RADIENT_INDEX_TYPE_UINT16, or RADIENT_INDEX_TYPE_UINT32", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.IndexData.IndexType = RADIENT_INDEX_TYPE_NONE;
    });
    ExpectInvalidMeshCreateInfo("PrimitiveCount must not be zero", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.PrimitiveCount = 0;
    });
    ExpectInvalidMeshCreateInfo("pPrimitives must not be null", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.pPrimitives = nullptr;
    });
}

TEST(RadientAssetValidationTest, RejectsMeshCreateInfoMismatchedSkinningData)
{
    ExpectInvalidMeshCreateInfo("JOINTS_0 and WEIGHTS_0 must both be specified or both be absent",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    MeshCI.VertexData.VertexLayout.AttributeCount = MeshCI.VertexData.VertexLayout.BufferCount = 2;
                                });
    ExpectInvalidMeshCreateInfo("JOINTS_0 and WEIGHTS_0 must both be specified or both be absent",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.VertexAttributes[1]                      = Data.VertexAttributes[2];
                                    Data.VertexAttributes[1].BufferIndex          = 1;
                                    Data.VertexData[1]                            = Data.VertexData[2];
                                    MeshCI.VertexData.VertexLayout.AttributeCount = MeshCI.VertexData.VertexLayout.BufferCount = 2;
                                });
}

TEST(RadientAssetValidationTest, ValidatesMeshCreateInfoMorphTargets)
{
    MeshValidationData    Data;
    RadientMeshCreateInfo MeshCI = Data.MakeMeshCI();
    Data.AddMorphTarget(MeshCI);
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));

    Data.MorphAttribute.Semantic       = "CUSTOM_ATTRIBUTE";
    Data.MorphAttribute.ComponentCount = 2;
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));
}

TEST(RadientAssetValidationTest, RejectsMissingMorphTargetArray)
{
    ExpectInvalidMeshCreateInfo("MorphTargetData.pMorphTargets must not be null when MorphTargetData.MorphTargetCount is nonzero",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
                                    MeshCI.MorphTargetData.MorphTargetCount = 1;
                                });
}

TEST(RadientAssetValidationTest, RejectsNonFiniteMorphTargetDefaultWeight)
{
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].Desc.DefaultWeight must be finite",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphTarget.Desc.DefaultWeight = std::numeric_limits<Float32>::infinity();
                                });
}

TEST(RadientAssetValidationTest, RejectsMissingMorphTargetAttributeArray)
{
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].Desc.pAttributes must not be null when AttributeCount is nonzero",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphTarget.Desc.pAttributes = nullptr;
                                });
}

TEST(RadientAssetValidationTest, RejectsMissingMorphTargetAttributeDataArray)
{
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].pAttributeData must not be null when Desc.AttributeCount is nonzero",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphTarget.pAttributeData = nullptr;
                                });
}

TEST(RadientAssetValidationTest, RejectsMissingMorphTargetAttributeSemantic)
{
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].Desc.pAttributes[0].Semantic must not be null or empty",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphAttribute.Semantic = nullptr;
                                });
}

TEST(RadientAssetValidationTest, RejectsEmptyMorphTargetAttributeSemantic)
{
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].Desc.pAttributes[0].Semantic must not be null or empty",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphAttribute.Semantic = "";
                                });
}

TEST(RadientAssetValidationTest, RejectsMissingMorphTargetAttributeDeltas)
{
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].pAttributeData[0].pDeltas must not be null",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphAttributeData.pDeltas = nullptr;
                                });
}

TEST(RadientAssetValidationTest, RejectsInvalidMorphTargetAttributeComponentCount)
{
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].Desc.pAttributes[0].ComponentCount must be in [1, 4]",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphAttribute.ComponentCount = 0;
                                });
    ExpectInvalidMeshCreateInfo("pMorphTargets[0].Desc.pAttributes[0].ComponentCount must be in [1, 4]",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphAttribute.ComponentCount = 5;
                                });
}

TEST(RadientAssetValidationTest, RejectsInvalidStandardMorphTargetComponentCount)
{
    ExpectInvalidMeshCreateInfo("standard semantic 'POSITION' requires ComponentCount equal to 3",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.MorphAttribute.ComponentCount = 4;
                                });
}

TEST(RadientAssetValidationTest, RejectsDuplicateMorphTargetAttributeSemantics)
{
    ExpectInvalidMeshCreateInfo("contains duplicate attribute semantic 'POSITION'",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    Data.AddMorphTarget(MeshCI);
                                    Data.DuplicateMorphAttributes[0]     = Data.MorphAttribute;
                                    Data.DuplicateMorphAttributes[1]     = Data.MorphAttribute;
                                    Data.DuplicateMorphAttributeData[0]  = Data.MorphAttributeData;
                                    Data.DuplicateMorphAttributeData[1]  = Data.MorphAttributeData;
                                    Data.MorphTarget.Desc.pAttributes    = Data.DuplicateMorphAttributes.data();
                                    Data.MorphTarget.Desc.AttributeCount = static_cast<Uint32>(Data.DuplicateMorphAttributes.size());
                                    Data.MorphTarget.pAttributeData      = Data.DuplicateMorphAttributeData.data();
                                });
}

TEST(RadientAssetValidationTest, RejectsMeshCreateInfoInvalidPrimitiveRanges)
{
    ExpectInvalidMeshCreateInfo("pPrimitives[0].IndexCount must not be zero", [](RadientMeshCreateInfo&, MeshValidationData& Data) {
        Data.Primitive.IndexCount = 0;
    });
    ExpectInvalidMeshCreateInfo("pPrimitives[0].FirstIndex", [](RadientMeshCreateInfo&, MeshValidationData& Data) {
        Data.Primitive.FirstIndex = static_cast<Uint32>(Data.Indices16.size());
    });
    ExpectInvalidMeshCreateInfo("range [FirstIndex, FirstIndex + IndexCount) exceeds IndexData.IndexCount",
                                [](RadientMeshCreateInfo&, MeshValidationData& Data) {
                                    Data.Primitive.FirstIndex = 2;
                                    Data.Primitive.IndexCount = 2;
                                });
}

TEST(RadientAssetValidationTest, ValidatesSceneLoadInfo)
{
    RadientSceneLoadInfo LoadInfo{};
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_FALSE(ValidateSceneLoadInfo(LoadInfo));
    }

    LoadInfo.URI = "";
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_FALSE(ValidateSceneLoadInfo(LoadInfo));
    }

    LoadInfo.URI = "scene.gltf";
    EXPECT_TRUE(ValidateSceneLoadInfo(LoadInfo));

    LoadInfo.Format = static_cast<RADIENT_SCENE_FORMAT>(255);
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Format is invalid"};
        EXPECT_FALSE(ValidateSceneLoadInfo(LoadInfo));
    }
}

TEST(RadientAssetValidationTest, ValidatesTextureLoadInfo)
{
    RadientTextureLoadInfo LoadInfo{};
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"either URI must be non-empty"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    LoadInfo.URI = "";
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"either URI must be non-empty"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    LoadInfo.URI = "texture.png";
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    std::array<Uint8, 4> Data{1, 2, 3, 4};
    auto                 pEmptyBlob = MakeTestDataBlob(nullptr, 0);
    auto                 pBlob      = MakeTestDataBlob(Data.data(), Data.size());
    ASSERT_NE(pEmptyBlob, nullptr);
    ASSERT_NE(pBlob, nullptr);
    LoadInfo           = {};
    LoadInfo.pDataBlob = pEmptyBlob;
    // Blob contents, including the current size, are checked by the texture source
    // under a read scope. An empty mutable blob can be resized after validation.
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    LoadInfo.pDataBlob = pBlob;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    LoadInfo.URI = "";
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    std::array<Uint8, 16> RawPixels{};
    auto                  pPixelBlob = MakeTestDataBlob(RawPixels.data(), RawPixels.size());
    ASSERT_NE(pPixelBlob, nullptr);

    RadientTextureMipData TextureDataMip{};
    RadientTextureData    TextureData{};
    TextureData.pMipLevels    = &TextureDataMip;
    TextureData.MipLevelCount = 1;
    TextureData.Width         = 2;
    TextureData.Height        = 2;
    TextureData.Format        = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureDataMip.pDataBlob  = pPixelBlob;

    LoadInfo              = {};
    LoadInfo.pTextureData = &TextureData;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    LoadInfo.pDataBlob = pBlob;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"pDataBlob and pTextureData must not both be specified"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    LoadInfo              = {};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureMipData InvalidTextureDataMip = *TextureData.pMipLevels;
    RadientTextureData    InvalidTextureData    = TextureData;
    InvalidTextureData.pMipLevels               = &InvalidTextureDataMip;
    InvalidTextureData.MipLevelCount            = 1;
    InvalidTextureData.Width                    = 0;
    LoadInfo.pTextureData                       = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data width and height must not be zero"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    InvalidTextureData            = TextureData;
    InvalidTextureDataMip         = TextureDataMip;
    InvalidTextureData.pMipLevels = &InvalidTextureDataMip;
    InvalidTextureData.Format     = RADIENT_TEXTURE_FORMAT_UNKNOWN;
    LoadInfo.pTextureData         = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data format must not be RADIENT_TEXTURE_FORMAT_UNKNOWN"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    InvalidTextureData              = TextureData;
    InvalidTextureDataMip           = TextureDataMip;
    InvalidTextureData.pMipLevels   = &InvalidTextureDataMip;
    InvalidTextureDataMip.pDataBlob = nullptr;
    LoadInfo.pTextureData           = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data blob must not be null"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    InvalidTextureData            = TextureData;
    InvalidTextureDataMip         = TextureDataMip;
    InvalidTextureData.pMipLevels = &InvalidTextureDataMip;
    InvalidTextureDataMip.Stride  = 1;
    LoadInfo.pTextureData         = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data stride"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    RefCntAutoPtr<SizeOnlyDataBlob> pUncheckedBlob{MakeNewRCObj<SizeOnlyDataBlob>()(
        (std::numeric_limits<Uint64>::max)())};
    LoadInfo           = {};
    LoadInfo.pDataBlob = pUncheckedBlob;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    // Pixel descriptors also defer all blob access until a read scope is held.
    TextureDataMip.pDataBlob = pUncheckedBlob;
    LoadInfo                 = {};
    LoadInfo.pTextureData    = &TextureData;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
    TextureDataMip.pDataBlob = pEmptyBlob;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
}

TEST(RadientAssetValidationTest, ValidatesCompressedTextureDataWithoutReadingBlob)
{
    RefCntAutoPtr<SizeOnlyDataBlob> pUncheckedBlob{MakeNewRCObj<SizeOnlyDataBlob>()(Uint64{0})};
    for (const auto Format : {RADIENT_TEXTURE_FORMAT_BC1_UNORM,
                              RADIENT_TEXTURE_FORMAT_BC3_UNORM_SRGB,
                              RADIENT_TEXTURE_FORMAT_BC5_SNORM,
                              RADIENT_TEXTURE_FORMAT_BC6H_UF16})
    {
        SCOPED_TRACE(static_cast<Uint32>(Format));
        RadientTextureMipData DataMip{};
        RadientTextureData    Data;
        Data.pMipLevels    = &DataMip;
        Data.MipLevelCount = 1;
        Data.Width         = 8;
        Data.Height        = 8;
        Data.Format        = Format;
        DataMip.pDataBlob  = pUncheckedBlob;
        RadientTextureLoadInfo LoadInfo;
        LoadInfo.pTextureData = &Data;
        EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
        DataMip.Stride = 35; // Accommodates two 16-byte blocks plus unaligned padding.
        EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
        DataMip.Stride = 1;
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data stride"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
}

TEST(RadientAssetValidationTest, RejectsCompressedMipZeroWithPartialBlockDimensionsBeforeReadingBlob)
{
    const Uint32                    Dimensions[][2] = {{5, 8}, {8, 5}, {5, 7}, {2, 2}};
    RefCntAutoPtr<SizeOnlyDataBlob> pUncheckedBlob{MakeNewRCObj<SizeOnlyDataBlob>()(Uint64{0})};
    for (const auto Format : {RADIENT_TEXTURE_FORMAT_BC1_UNORM, RADIENT_TEXTURE_FORMAT_BC7_UNORM})
    {
        for (const auto& Dimension : Dimensions)
        {
            SCOPED_TRACE(static_cast<Uint32>(Format));
            SCOPED_TRACE(Dimension[0]);
            SCOPED_TRACE(Dimension[1]);
            RadientTextureMipData DataMip{};
            RadientTextureData    Data;
            Data.pMipLevels    = &DataMip;
            Data.MipLevelCount = 1;
            Data.Width         = Dimension[0];
            Data.Height        = Dimension[1];
            Data.Format        = Format;
            DataMip.pDataBlob  = pUncheckedBlob;
            RadientTextureLoadInfo LoadInfo;
            LoadInfo.pTextureData = &Data;
            TestingEnvironment::ErrorScope ExpectedErrors{"block dimensions"};
            EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
        }
    }
}

TEST(RadientAssetValidationTest, ValidatesMipChainsWithoutReadingBlobStorage)
{
    RefCntAutoPtr<SizeOnlyDataBlob>      pBlob{MakeNewRCObj<SizeOnlyDataBlob>()(Uint64{0})};
    std::array<RadientTextureMipData, 4> Mips;
    for (auto& Mip : Mips)
        Mip.pDataBlob = pBlob;
    RadientTextureData Data;
    Data.Width = Data.Height = 8;
    Data.Format              = RADIENT_TEXTURE_FORMAT_BC7_UNORM;
    Data.pMipLevels          = Mips.data();
    Data.MipLevelCount       = static_cast<Uint32>(Mips.size());
    RadientTextureLoadInfo LoadInfo;
    LoadInfo.pTextureData = &Data;
    // The last two compressed levels are smaller than one block.
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
    EXPECT_EQ(Data.GenerateMips, True);
    Data.MipLevelCount = 2;
    // Validation accepts compressed requests with missing mips. Loading warns
    // that generation is unavailable and retains the supplied levels.
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
    Data.GenerateMips = False;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
    Data.GenerateMips  = True;
    Data.MipLevelCount = 4;
    Data.Format        = RADIENT_TEXTURE_FORMAT_R8_UNORM;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
    Data.MipLevelCount = 2;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
    const RadientTextureData ValidData = Data;
    for (Uint32 InvalidCount : {0u, 5u})
    {
        Data.MipLevelCount = InvalidCount;
        TestingEnvironment::ErrorScope ExpectedErrors{"MipLevelCount"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
    Data            = ValidData;
    Data.pMipLevels = nullptr;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"pMipLevels"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
    Data              = ValidData;
    Mips[1].pDataBlob = nullptr;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data blob must not be null for mip 1"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
    Mips[1].pDataBlob = pBlob;
    Mips[1].Stride    = 3; // Mip 1 contains four R8 components per row.
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data stride"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
    Mips[1].Stride     = 0;
    Mips[1].ByteOffset = (std::numeric_limits<Uint64>::max)() - 1;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data byte range for mip 1"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    // Keep the high-bit dimension while ensuring the byte range fits 32-bit size_t.
    Data.Width         = 1;
    Data.Height        = 0x80000000u;
    Data.MipLevelCount = 1;
    Data.GenerateMips  = False;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));
    Data.MipLevelCount = 33;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"MipLevelCount"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
}

TEST(RadientAssetValidationTest, RejectsInvalidVertexLayoutsAndMissingBlobs)
{
    ExpectInvalidMeshCreateInfo("VertexLayout is invalid", [](auto& CI, auto&) { CI.VertexData.VertexLayout.pAttributes = nullptr; });
    ExpectInvalidMeshCreateInfo("VertexLayout is invalid", [](auto& CI, auto&) { CI.VertexData.VertexLayout.pBuffers = nullptr; });
    ExpectInvalidMeshCreateInfo("VertexLayout is invalid", [](auto&, auto& Data) { Data.VertexAttributes[0].BufferIndex = 1; });
    ExpectInvalidMeshCreateInfo("VertexLayout is invalid", [](auto&, auto& Data) { Data.VertexBuffers[0].ByteStride = 8; });
    ExpectInvalidMeshCreateInfo("VertexLayout is invalid", [](auto&, auto& Data) { Data.VertexAttributes[0].ComponentType = RADIENT_VERTEX_COMPONENT_TYPE_UNKNOWN; });
    ExpectInvalidMeshCreateInfo("requires a three-component POSITION", [](auto&, auto& Data) { Data.VertexAttributes[0].Semantic = "position"; });
    ExpectInvalidMeshCreateInfo("requires a three-component POSITION", [](auto&, auto& Data) { Data.VertexAttributes[0].ComponentCount = 2; });
    ExpectInvalidMeshCreateInfo("must not be null", [](auto&, auto& Data) { Data.VertexData[0] = nullptr; });
}

TEST(RadientAssetValidationTest, ValidatesSkinningEncodings)
{
    MeshValidationData Data;
    auto               CI                     = Data.MakeMeshCI();
    CI.VertexData.VertexLayout.AttributeCount = CI.VertexData.VertexLayout.BufferCount = 3;
    Data.VertexAttributes[2].ComponentType                                             = RADIENT_VERTEX_COMPONENT_TYPE_UINT8;
    Data.VertexAttributes[2].Normalized                                                = True;
    for (RADIENT_VERTEX_COMPONENT_TYPE Type : {RADIENT_VERTEX_COMPONENT_TYPE_UINT8,
                                               RADIENT_VERTEX_COMPONENT_TYPE_UINT16,
                                               RADIENT_VERTEX_COMPONENT_TYPE_UINT32,
                                               RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32})
    {
        SCOPED_TRACE(static_cast<int>(Type));
        Data.VertexAttributes[1].ComponentType = Type;
        EXPECT_TRUE(ValidateMeshCreateInfo(CI));
    }
    Data.VertexAttributes[1].ComponentType = RADIENT_VERTEX_COMPONENT_TYPE_UINT8;
    {
        TestingEnvironment::ErrorScope Errors{"JOINTS_0 requires non-normalized unsigned integer or FLOAT32 components"};
        Data.VertexAttributes[1].Normalized = True;
        EXPECT_FALSE(ValidateMeshCreateInfo(CI));
    }
    Data.VertexAttributes[1].Normalized = False;
    for (RADIENT_VERTEX_COMPONENT_TYPE Type : {RADIENT_VERTEX_COMPONENT_TYPE_INT8,
                                               RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16})
    {
        SCOPED_TRACE(static_cast<int>(Type));
        TestingEnvironment::ErrorScope Errors{"JOINTS_0 requires non-normalized unsigned integer or FLOAT32 components"};
        Data.VertexAttributes[1].ComponentType = Type;
        EXPECT_FALSE(ValidateMeshCreateInfo(CI));
    }
    Data.VertexAttributes[1].ComponentType = RADIENT_VERTEX_COMPONENT_TYPE_UINT8;
    {
        TestingEnvironment::ErrorScope Errors{"JOINTS_0 and WEIGHTS_0 require four components"};
        Data.VertexAttributes[2].ComponentCount = 3;
        EXPECT_FALSE(ValidateMeshCreateInfo(CI));
    }
}

TEST(RadientAssetValidationTest, DefersMeshBlobAccessUntilSourceConstruction)
{
    MeshValidationData              Data;
    auto                            CI = Data.MakeMeshCI();
    RefCntAutoPtr<SizeOnlyDataBlob> pUncheckedBlob{MakeNewRCObj<SizeOnlyDataBlob>()(Uint64{0})};
    Data.VertexData[0]        = pUncheckedBlob;
    CI.IndexData.pIndexBuffer = pUncheckedBlob;
    EXPECT_TRUE(ValidateMeshCreateInfo(CI));

    auto pEmptyBlob           = MakeTestDataBlob(nullptr, 0);
    Data.VertexData[0]        = pEmptyBlob;
    CI.IndexData.pIndexBuffer = pEmptyBlob;
    EXPECT_TRUE(ValidateMeshCreateInfo(CI));

    auto pMutableBlob         = MakeTestMutableDataBlob(Data.Positions.data(), sizeof(Data.Positions));
    Data.VertexData[0]        = pMutableBlob;
    CI.IndexData.pIndexBuffer = pMutableBlob;
    void* pWrite              = nullptr;
    ASSERT_EQ(pMutableBlob->BeginWrite(&pWrite), RADIENT_STATUS_OK);
    EXPECT_TRUE(ValidateMeshCreateInfo(CI));
    EXPECT_EQ(pMutableBlob->EndWrite(), RADIENT_STATUS_OK);

    // Buffer slots that are not referenced by any attribute do not need storage.
    CI.VertexData.VertexLayout.BufferCount = 3;
    Data.VertexBuffers[1].ByteStride       = 1;
    Data.VertexBuffers[2].ByteStride       = 1;
    Data.VertexData[1]                     = nullptr;
    Data.VertexData[2]                     = pUncheckedBlob;
    EXPECT_TRUE(ValidateMeshCreateInfo(CI));
}
