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

    RadientMeshCreateInfo MakeMeshCI()
    {
        Primitive.FirstIndex = 0;
        Primitive.IndexCount = static_cast<Uint32>(Indices16.size());

        RadientMeshCreateInfo MeshCI{};
        MeshCI.pPositions     = Positions.data();
        MeshCI.VertexCount    = static_cast<Uint32>(Positions.size());
        MeshCI.pIndices       = Indices16.data();
        MeshCI.IndexCount     = static_cast<Uint32>(Indices16.size());
        MeshCI.IndexType      = RADIENT_INDEX_TYPE_UINT16;
        MeshCI.pPrimitives    = &Primitive;
        MeshCI.PrimitiveCount = 1;
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

        MeshCI.pMorphTargets    = &MorphTarget;
        MeshCI.MorphTargetCount = 1;
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

    MeshCI.pIndices  = Data.Indices32.data();
    MeshCI.IndexType = RADIENT_INDEX_TYPE_UINT32;
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));

    MeshCI.pBoneIndices0 = Data.BoneIndices.data();
    MeshCI.pBoneWeights0 = Data.BoneWeights.data();
    EXPECT_TRUE(ValidateMeshCreateInfo(MeshCI));
}

TEST(RadientAssetValidationTest, RejectsMeshCreateInfoMissingRequiredData)
{
    ExpectInvalidMeshCreateInfo("VertexCount must not be zero", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.VertexCount = 0;
    });
    ExpectInvalidMeshCreateInfo("pPositions must not be null", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.pPositions = nullptr;
    });
    ExpectInvalidMeshCreateInfo("IndexCount must not be zero", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.IndexCount = 0;
    });
    ExpectInvalidMeshCreateInfo("pIndices must not be null", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.pIndices = nullptr;
    });
    ExpectInvalidMeshCreateInfo("IndexType must be RADIENT_INDEX_TYPE_UINT16 or RADIENT_INDEX_TYPE_UINT32", [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
        MeshCI.IndexType = RADIENT_INDEX_TYPE_NONE;
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
    ExpectInvalidMeshCreateInfo("pBoneIndices0 and pBoneWeights0 must both be specified or both be null",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    MeshCI.pBoneIndices0 = Data.BoneIndices.data();
                                });
    ExpectInvalidMeshCreateInfo("pBoneIndices0 and pBoneWeights0 must both be specified or both be null",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData& Data) {
                                    MeshCI.pBoneWeights0 = Data.BoneWeights.data();
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
    ExpectInvalidMeshCreateInfo("pMorphTargets must not be null when MorphTargetCount is nonzero",
                                [](RadientMeshCreateInfo& MeshCI, MeshValidationData&) {
                                    MeshCI.MorphTargetCount = 1;
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
    ExpectInvalidMeshCreateInfo("range [FirstIndex, FirstIndex + IndexCount) exceeds mesh IndexCount",
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
    LoadInfo       = {};
    LoadInfo.pData = Data.data();
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"DataSize must not be zero when pData is specified"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    LoadInfo.DataSize = static_cast<Uint64>(Data.size());
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    LoadInfo.URI = "";
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    std::array<Uint8, 16> RawPixels{};

    RadientTextureData TextureData{};
    TextureData.Width  = 2;
    TextureData.Height = 2;
    TextureData.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureData.pData  = RawPixels.data();

    LoadInfo              = {};
    LoadInfo.pTextureData = &TextureData;
    EXPECT_TRUE(ValidateTextureLoadInfo(LoadInfo));

    LoadInfo.pData    = Data.data();
    LoadInfo.DataSize = static_cast<Uint64>(Data.size());
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"pData and pTextureData must not both be specified"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    LoadInfo              = {};
    LoadInfo.pTextureData = &TextureData;

    RadientTextureData InvalidTextureData = TextureData;
    InvalidTextureData.Width              = 0;
    LoadInfo.pTextureData                 = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data width and height must not be zero"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    InvalidTextureData        = TextureData;
    InvalidTextureData.Format = RADIENT_TEXTURE_FORMAT_UNKNOWN;
    LoadInfo.pTextureData     = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data format must not be RADIENT_TEXTURE_FORMAT_UNKNOWN"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    InvalidTextureData       = TextureData;
    InvalidTextureData.pData = nullptr;
    LoadInfo.pTextureData    = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data pointer must not be null"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    InvalidTextureData        = TextureData;
    InvalidTextureData.Stride = 1;
    LoadInfo.pTextureData     = &InvalidTextureData;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"texture data stride"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    if ((std::numeric_limits<size_t>::max)() < (std::numeric_limits<Uint64>::max)())
    {
        LoadInfo          = {};
        LoadInfo.pData    = Data.data();
        LoadInfo.DataSize = static_cast<Uint64>((std::numeric_limits<size_t>::max)()) + Uint64{1};
        TestingEnvironment::ErrorScope ExpectedErrors{"exceeds maximum supported size_t value"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
}
