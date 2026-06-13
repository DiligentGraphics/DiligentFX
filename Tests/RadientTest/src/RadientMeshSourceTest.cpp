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

} // namespace

TEST(RadientMeshSourceTest, CopiesAndPacksMinimalMesh)
{
    std::array<RadientFloat3, 2> Positions{
        RadientFloat3{1.f, 2.f, 3.f},
        RadientFloat3{4.f, 5.f, 6.f}};
    std::array<Uint16, 3> Indices{0, 1, 0};

    RadientMeshCreateInfo          MeshCI = MakeMeshCI(Positions, Indices);
    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.FirstIndex = 0;
    PrimitiveCI.IndexCount = static_cast<Uint32>(Indices.size());
    MeshCI.pPrimitives     = &PrimitiveCI;
    MeshCI.PrimitiveCount  = 1;

    RadientMeshSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    EXPECT_EQ(Source.GetVertexCount(), 2u);

    Positions[0] = RadientFloat3{};
    Indices[0]   = 7;

    RadientMeshSource::UploadData UploadData;
    ASSERT_EQ(Source.GetUploadData(UploadData), RADIENT_STATUS_OK);
    EXPECT_EQ(UploadData.VertexBufferCount, 1u);
    ASSERT_EQ(UploadData.IndexCount, Indices.size());

    std::vector<Uint32> PackedIndices(UploadData.IndexCount);
    std::vector<Uint8>  Buffer0(UploadData.VertexBufferDataSizes[0]);

    RadientMeshSource::PackDestinations Destinations;
    Destinations.Indices          = RadientMeshSource::PackDestination{PackedIndices.data(), UploadData.GetIndexDataSize()};
    Destinations.VertexBuffers[0] = RadientMeshSource::PackDestination{Buffer0.data(), static_cast<Uint32>(Buffer0.size())};
    ASSERT_EQ(Source.Pack(UploadData, Destinations), RADIENT_STATUS_OK);

    EXPECT_EQ(PackedIndices[0], 0u);
    EXPECT_EQ(PackedIndices[1], 1u);

    ASSERT_FALSE(Buffer0.empty());
    const RadientFloat3 Position0 = ReadValue<RadientFloat3>(Buffer0, 0);
    EXPECT_FLOAT_EQ(Position0.x, 1.f);
    EXPECT_FLOAT_EQ(Position0.y, 2.f);
    EXPECT_FLOAT_EQ(Position0.z, 3.f);

    for (Uint32 BufferIndex = 1; BufferIndex < RadientMeshSource::VertexBufferCount; ++BufferIndex)
        EXPECT_EQ(UploadData.VertexBufferDataSizes[BufferIndex], 0u);
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
    PrimitiveCI.FirstIndex = 0;
    PrimitiveCI.IndexCount = static_cast<Uint32>(Indices.size());
    MeshCI.pPrimitives     = &PrimitiveCI;
    MeshCI.PrimitiveCount  = 1;

    RadientMeshSource Source{MeshCI};
    ASSERT_EQ(Source.GetStatus(), RADIENT_STATUS_OK);
    EXPECT_NE(Source.GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS,
              PBR_Renderer::PSO_FLAG_NONE);
    EXPECT_EQ(Source.GetVertexAttribFlags() & PBR_Renderer::PSO_FLAG_USE_JOINTS,
              PBR_Renderer::PSO_FLAG_NONE);

    RadientMeshSource::UploadData UploadData;
    ASSERT_EQ(Source.GetUploadData(UploadData), RADIENT_STATUS_OK);
    EXPECT_EQ(UploadData.VertexBufferCount, 4u);

    EXPECT_NE(UploadData.VertexBufferDataSizes[0], 0u);
    EXPECT_EQ(UploadData.VertexBufferDataSizes[1], 0u);
    EXPECT_EQ(UploadData.VertexBufferDataSizes[2], 0u);
    ASSERT_NE(UploadData.VertexBufferDataSizes[3], 0u);
    EXPECT_EQ(UploadData.VertexBufferDataSizes[4], 0u);

    std::vector<Uint8> Buffer0(UploadData.VertexBufferDataSizes[0]);
    std::vector<Uint8> Buffer3(UploadData.VertexBufferDataSizes[3]);

    RadientMeshSource::PackDestinations Destinations;
    Destinations.VertexBuffers[0] = RadientMeshSource::PackDestination{Buffer0.data(), static_cast<Uint32>(Buffer0.size())};
    Destinations.VertexBuffers[3] = RadientMeshSource::PackDestination{Buffer3.data(), static_cast<Uint32>(Buffer3.size())};
    ASSERT_EQ(Source.Pack(UploadData, Destinations), RADIENT_STATUS_OK);

    const RadientFloat4 Color0 = ReadValue<RadientFloat4>(Buffer3, 0);
    EXPECT_FLOAT_EQ(Color0.x, 1.f);
    EXPECT_FLOAT_EQ(Color0.y, 128.f / 255.f);
    EXPECT_FLOAT_EQ(Color0.z, 0.f);
    EXPECT_FLOAT_EQ(Color0.w, 64.f / 255.f);
}
