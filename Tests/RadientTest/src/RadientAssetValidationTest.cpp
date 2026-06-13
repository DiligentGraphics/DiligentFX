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

#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include <array>
#include <cstddef>
#include <limits>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

struct MeshValidationData
{
    std::array<RadientFloat3, 2>       Positions{};
    std::array<Uint16, 3>              Indices16{0, 1, 0};
    std::array<Uint32, 3>              Indices32{0, 1, 0};
    std::array<RadientBoneIndices4, 2> BoneIndices{};
    std::array<RadientFloat4, 2>       BoneWeights{};
    RadientMeshPrimitiveCreateInfo     Primitive{};

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

TEST(RadientAssetValidationTest, ValidatesGLTFLoadInfo)
{
    RadientGLTFLoadInfo LoadInfo{};
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_FALSE(ValidateGLTFLoadInfo(LoadInfo));
    }

    LoadInfo.URI = "";
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"URI must not be null or empty"};
        EXPECT_FALSE(ValidateGLTFLoadInfo(LoadInfo));
    }

    LoadInfo.URI = "scene.gltf";
    EXPECT_TRUE(ValidateGLTFLoadInfo(LoadInfo));
}

TEST(RadientAssetValidationTest, ValidatesTextureLoadInfo)
{
    RadientTextureLoadInfo LoadInfo{};
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"either URI must be non-empty or pData must not be null"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }

    LoadInfo.URI = "";
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"either URI must be non-empty or pData must not be null"};
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

    if ((std::numeric_limits<size_t>::max)() < (std::numeric_limits<Uint64>::max)())
    {
        LoadInfo.DataSize = static_cast<Uint64>((std::numeric_limits<size_t>::max)()) + Uint64{1};
        TestingEnvironment::ErrorScope ExpectedErrors{"exceeds maximum supported size_t value"};
        EXPECT_FALSE(ValidateTextureLoadInfo(LoadInfo));
    }
}
