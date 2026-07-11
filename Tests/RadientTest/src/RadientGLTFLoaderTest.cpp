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
 *  for loss of goodwill, work stoppage, computer failure or malfunction, and any
 *  and all other commercial damages or losses), even if such Contributor has been
 *  advised of the possibility of such damages.
 */

#include "TempDirectory.hpp"
#include "TestingEnvironment.hpp"
#include "gtest/gtest.h"

#include "Assets/RadientGLTFLoader.hpp"
#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMeshAssetManager.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "GLTFDocument.hpp"
#include "RadientTestAssetHelpers.hpp"
#include "ThreadPool.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

static constexpr float EPSILON = 1e-5f;

static constexpr const char* TransparentPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAAAABJRU5ErkJggg==";

const std::array<float, 9> TrianglePositions{
    0.f, 0.f, 0.f,
    1.f, 0.f, 0.f,
    0.f, 1.f, 0.f};

const std::array<float, 9> WideTrianglePositions{
    0.f, 0.f, 0.f,
    2.f, 0.f, 0.f,
    0.f, 2.f, 0.f};

const std::array<Uint16, 3> TriangleIndices{0, 1, 2};
const std::array<Uint16, 3> ReversedTriangleIndices{0, 2, 1};

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

template <typename ValueType, size_t Size>
void AppendBytes(std::vector<Uint8>& Buffer, const std::array<ValueType, Size>& Values)
{
    const size_t OldSize = Buffer.size();
    Buffer.resize(OldSize + sizeof(ValueType) * Values.size());
    std::memcpy(Buffer.data() + OldSize, Values.data(), Buffer.size() - OldSize);
}

std::string WriteGLTFFile(const TempDirectory& TempDir, const char* FileName, const char* Contents)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    File << Contents;

    return Path;
}

void WriteBinaryFile(const TempDirectory& TempDir, const char* FileName, const std::vector<Uint8>& Data)
{
    const std::string Path = TempDir.Get() + "/" + FileName;

    std::ofstream File{Path, std::ios::binary};
    EXPECT_TRUE(File.is_open());
    if (!Data.empty())
        File.write(reinterpret_cast<const char*>(Data.data()), Data.size());
}

std::string WriteGLTFMeshFile(const TempDirectory& TempDir, bool WithMaterial)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(sizeof(float) * TrianglePositions.size() + sizeof(Uint16) * TriangleIndices.size());

    const size_t PositionByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t IndexByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    WriteBinaryFile(TempDir, "mesh.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"name": "MainScene", "nodes": [0]}],
    "nodes": [{"name": "TriangleNode", "mesh": 0, "translation": [1, 2, 3], "scale": [2, 3, 4]}],
    "buffers": [{"uri": "mesh.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ])GLTF";

    if (WithMaterial)
    {
        GLTF << R"GLTF(,
    "materials": [{
        "name": "MeshMaterial",
        "pbrMetallicRoughness": {
            "baseColorFactor": [1.0, 0.25, 0.5, 1.0]
        }
    }])GLTF";
    }

    GLTF << R"GLTF(,
    "meshes": [{
        "name": "TriangleMesh",
        "primitives": [{
            "attributes": {"POSITION": 0},
            "indices": 1)GLTF";
    if (WithMaterial)
        GLTF << R"GLTF(,
            "material": 0)GLTF";
    GLTF << R"GLTF(
        }]
    }]
})GLTF";

    return WriteGLTFFile(TempDir, WithMaterial ? "mesh_with_material.gltf" : "mesh.gltf", GLTF.str().c_str());
}

std::string WriteGLTFSceneGraphFile(const TempDirectory& TempDir)
{
    return WriteGLTFFile(TempDir, "scene_graph.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "extensionsUsed": ["KHR_lights_punctual"],
    "extensions": {
        "KHR_lights_punctual": {
            "lights": [
                {"name": "PointLight", "type": "point", "color": [0.25, 0.5, 1.0], "intensity": 3.0, "range": 10.0}
            ]
        }
    },
    "scene": 1,
    "scenes": [
        {"name": "UnusedScene", "nodes": [2]},
        {"name": "DefaultScene", "nodes": [0]}
    ],
    "cameras": [
        {"name": "Camera", "type": "orthographic", "orthographic": {"xmag": 4.0, "ymag": 3.0, "znear": 0.5, "zfar": 100.0}}
    ],
    "nodes": [
        {"name": "RootNode", "translation": [1, 2, 3], "children": [1]},
        {"name": "CameraLightNode", "camera": 0, "extensions": {"KHR_lights_punctual": {"light": 0}}},
        {"name": "UnusedNode", "scale": [2, 2, 2]}
    ]
})GLTF");
}

std::string WriteGLTFMeshWithShiftedAccessorsFile(const TempDirectory& TempDir)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(2 * (sizeof(float) * TrianglePositions.size() + sizeof(Uint16) * TriangleIndices.size()));

    const size_t UnusedPositionByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t UnusedIndexByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    const size_t PositionByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t IndexByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    WriteBinaryFile(TempDir, "mesh_shifted_accessors.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "ShiftedAccessorNode", "mesh": 0}],
    "buffers": [{"uri": "mesh_shifted_accessors.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << UnusedPositionByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << UnusedIndexByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
        {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "meshes": [{
        "name": "ShiftedAccessorMesh",
        "primitives": [{
            "attributes": {"POSITION": 2},
            "indices": 3
        }]
    }]
})GLTF";

    return WriteGLTFFile(TempDir, "mesh_shifted_accessors.gltf", GLTF.str().c_str());
}

std::string WriteGLTFMeshesWithSameGeometryDifferentIndicesFile(const TempDirectory& TempDir)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(sizeof(float) * TrianglePositions.size() +
                   sizeof(Uint16) * (TriangleIndices.size() + ReversedTriangleIndices.size()));

    const size_t PositionByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t IndexAByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    const size_t IndexBByteOffset = Buffer.size();
    AppendBytes(Buffer, ReversedTriangleIndices);

    WriteBinaryFile(TempDir, "same_geometry_different_indices.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "nodes": [
        {"name": "IndexMeshA", "mesh": 0},
        {"name": "IndexMeshB", "mesh": 1}
    ],
    "buffers": [{"uri": "same_geometry_different_indices.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexAByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexBByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * ReversedTriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
        {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "meshes": [
        {"name": "IndexMeshA", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]},
        {"name": "IndexMeshB", "primitives": [{"attributes": {"POSITION": 0}, "indices": 2}]}
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "same_geometry_different_indices.gltf", GLTF.str().c_str());
}

std::string WriteGLTFMeshesWithSameIndicesDifferentGeometryFile(const TempDirectory& TempDir)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(sizeof(float) * (TrianglePositions.size() + WideTrianglePositions.size()) +
                   sizeof(Uint16) * TriangleIndices.size());

    const size_t PositionAByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t PositionBByteOffset = Buffer.size();
    AppendBytes(Buffer, WideTrianglePositions);

    const size_t IndexByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    WriteBinaryFile(TempDir, "same_indices_different_geometry.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "nodes": [
        {"name": "GeometryMeshA", "mesh": 0},
        {"name": "GeometryMeshB", "mesh": 1}
    ],
    "buffers": [{"uri": "same_indices_different_geometry.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionAByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionBByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * WideTrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [2, 2, 0]},
        {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "meshes": [
        {"name": "GeometryMeshA", "primitives": [{"attributes": {"POSITION": 0}, "indices": 2}]},
        {"name": "GeometryMeshB", "primitives": [{"attributes": {"POSITION": 1}, "indices": 2}]}
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "same_indices_different_geometry.gltf", GLTF.str().c_str());
}

std::string WriteGLTFMeshesWithSameGeometryDifferentMaterialsFile(const TempDirectory& TempDir)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(sizeof(float) * TrianglePositions.size() + sizeof(Uint16) * TriangleIndices.size());

    const size_t PositionByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t IndexByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    WriteBinaryFile(TempDir, "same_geometry_different_materials.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "nodes": [
        {"name": "MaterialMeshA", "mesh": 0},
        {"name": "MaterialMeshB", "mesh": 1}
    ],
    "buffers": [{"uri": "same_geometry_different_materials.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "materials": [
        {"name": "Red", "pbrMetallicRoughness": {"baseColorFactor": [1, 0, 0, 1]}},
        {"name": "Green", "pbrMetallicRoughness": {"baseColorFactor": [0, 1, 0, 1]}}
    ],
    "meshes": [
        {"name": "MaterialMeshA", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]},
        {"name": "MaterialMeshB", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 1}]}
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "same_geometry_different_materials.gltf", GLTF.str().c_str());
}

std::string WriteGLTFMeshesWithSameGeometryDifferentPrimitiveListsFile(const TempDirectory& TempDir)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(sizeof(float) * TrianglePositions.size() + sizeof(Uint16) * TriangleIndices.size());

    const size_t PositionByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t IndexByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    WriteBinaryFile(TempDir, "same_geometry_different_primitive_lists.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "nodes": [
        {"name": "PrimitiveListMeshA", "mesh": 0},
        {"name": "PrimitiveListMeshB", "mesh": 1}
    ],
    "buffers": [{"uri": "same_geometry_different_primitive_lists.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "meshes": [
        {"name": "PrimitiveListMeshA", "primitives": [
            {"attributes": {"POSITION": 0}, "indices": 1}
        ]},
        {"name": "PrimitiveListMeshB", "primitives": [
            {"attributes": {"POSITION": 0}, "indices": 1},
            {"attributes": {"POSITION": 0}, "indices": 1}
        ]}
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "same_geometry_different_primitive_lists.gltf", GLTF.str().c_str());
}

std::string WriteGLTFTwoMeshesWithIdenticalPrimitiveDataFile(const TempDirectory& TempDir)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(2 * (sizeof(float) * TrianglePositions.size() + sizeof(Uint16) * TriangleIndices.size()));

    const size_t Position0ByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t Index0ByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    const size_t Position1ByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t Index1ByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    WriteBinaryFile(TempDir, "two_identical_meshes.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0, 1]}],
    "nodes": [
        {"name": "TriangleNodeA", "mesh": 0},
        {"name": "TriangleNodeB", "mesh": 1}
    ],
    "buffers": [{"uri": "two_identical_meshes.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << Position0ByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << Index0ByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << Position1ByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << Index1ByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
        {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "meshes": [
        {
            "name": "TriangleMeshA",
            "primitives": [{
                "attributes": {"POSITION": 0},
                "indices": 1
            }]
        },
        {
            "name": "TriangleMeshB",
            "primitives": [{
                "attributes": {"POSITION": 2},
                "indices": 3
            }]
        }
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "two_identical_meshes.gltf", GLTF.str().c_str());
}

std::string WriteGLTFMeshWithAlternatingPrimitiveGeometryFile(const TempDirectory& TempDir)
{
    std::vector<Uint8> Buffer;
    Buffer.reserve(sizeof(float) * (TrianglePositions.size() + WideTrianglePositions.size()) +
                   2 * sizeof(Uint16) * TriangleIndices.size());

    const size_t PositionAByteOffset = Buffer.size();
    AppendBytes(Buffer, TrianglePositions);

    const size_t IndexAByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    const size_t PositionBByteOffset = Buffer.size();
    AppendBytes(Buffer, WideTrianglePositions);

    const size_t IndexBByteOffset = Buffer.size();
    AppendBytes(Buffer, TriangleIndices);

    WriteBinaryFile(TempDir, "alternating_geometry.bin", Buffer);

    std::ostringstream GLTF;
    GLTF << R"GLTF({
    "asset": {"version": "2.0"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "AlternatingNode", "mesh": 0}],
    "buffers": [{"uri": "alternating_geometry.bin", "byteLength": )GLTF"
         << Buffer.size() << R"GLTF(}],
    "bufferViews": [
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionAByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * TrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexAByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << PositionBByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(float) * WideTrianglePositions.size() << R"GLTF(},
        {"buffer": 0, "byteOffset": )GLTF"
         << IndexBByteOffset << R"GLTF(, "byteLength": )GLTF"
         << sizeof(Uint16) * TriangleIndices.size() << R"GLTF(}
    ],
    "accessors": [
        {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
        {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
        {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [2, 2, 0]},
        {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
    ],
    "meshes": [{
        "name": "AlternatingGeometryMesh",
        "primitives": [
            {"attributes": {"POSITION": 0}, "indices": 1},
            {"attributes": {"POSITION": 2}, "indices": 3},
            {"attributes": {"POSITION": 0}, "indices": 1},
            {"attributes": {"POSITION": 2}, "indices": 3}
        ]
    }]
})GLTF";

    return WriteGLTFFile(TempDir, "alternating_geometry.gltf", GLTF.str().c_str());
}

std::string WriteGLTFDataURITextureFile(const TempDirectory& TempDir)
{
    const std::string Contents =
        std::string{R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "DataURITexture", "uri": "data:image/png;base64,)GLTF"} +
        TransparentPngBase64 +
        R"GLTF("}],
    "textures": [{"source": 0}]
})GLTF";

    return WriteGLTFFile(TempDir, "data_uri_texture.gltf", Contents.c_str());
}

std::string WriteGLTFExternalTextureFile(const TempDirectory& TempDir)
{
    const std::string ImagePath = TempDir.Get() + "/external.png";
    std::ofstream     ImageFile{ImagePath, std::ios::binary};
    EXPECT_TRUE(ImageFile.is_open());
    ImageFile.write(reinterpret_cast<const char*>(TransparentPng.data()), TransparentPng.size());

    return WriteGLTFFile(TempDir, "external_texture.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "ExternalTexture", "uri": "external.png"}],
    "textures": [{"source": 0}]
})GLTF");
}

std::string WriteGLTFMaterialWithoutTexturesFile(const TempDirectory& TempDir)
{
    return WriteGLTFFile(TempDir, "material_without_textures.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "materials": [{
        "name": "PlainMaterial",
        "doubleSided": true,
        "pbrMetallicRoughness": {
            "baseColorFactor": [0.25, 0.5, 0.75, 1.0],
            "metallicFactor": 0.125,
            "roughnessFactor": 0.875
        }
    }]
})GLTF");
}

std::string WriteGLTFMaterialWithTextureDependencyFile(const TempDirectory& TempDir)
{
    const std::string ImagePath = TempDir.Get() + "/external.png";
    std::ofstream     ImageFile{ImagePath, std::ios::binary};
    EXPECT_TRUE(ImageFile.is_open());
    ImageFile.write(reinterpret_cast<const char*>(TransparentPng.data()), TransparentPng.size());

    return WriteGLTFFile(TempDir, "material_with_texture.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "ExternalTexture", "uri": "external.png"}],
    "textures": [{"source": 0}],
    "materials": [{
        "name": "TexturedMaterial",
        "pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0, "texCoord": 1}
        }
    }]
})GLTF");
}

std::string WriteGLTFWithMissingAndValidTextureSourcesFile(const TempDirectory& TempDir)
{
    const std::string ImagePath = TempDir.Get() + "/external.png";
    std::ofstream     ImageFile{ImagePath, std::ios::binary};
    EXPECT_TRUE(ImageFile.is_open());
    ImageFile.write(reinterpret_cast<const char*>(TransparentPng.data()), TransparentPng.size());

    return WriteGLTFFile(TempDir, "missing_and_valid_texture_sources.gltf",
                         R"GLTF({
    "asset": {"version": "2.0"},
    "images": [{"name": "ExternalTexture", "uri": "external.png"}],
    "textures": [
        {"source": 99},
        {"source": 0}
    ]
})GLTF");
}

std::string WriteGLTFBufferViewTextureFile(const TempDirectory& TempDir)
{
    const std::string Contents =
        std::string{R"GLTF({
    "asset": {"version": "2.0"},
    "buffers": [{
        "uri": "data:application/octet-stream;base64,)GLTF"} +
        TransparentPngBase64 +
        R"GLTF(",
        "byteLength": )GLTF" +
        std::to_string(TransparentPng.size()) +
        R"GLTF(
    }],
    "bufferViews": [{
        "buffer": 0,
        "byteOffset": 0,
        "byteLength": )GLTF" +
        std::to_string(TransparentPng.size()) +
        R"GLTF(
    }],
    "images": [{
        "name": "BufferViewTexture",
        "bufferView": 0,
        "mimeType": "image/png"
    }],
    "textures": [{"source": 0}]
})GLTF";

    return WriteGLTFFile(TempDir, "buffer_view_texture.gltf", Contents.c_str());
}

std::string WriteGLTFDataURIAndBufferViewTexturesFile(const TempDirectory& TempDir)
{
    const std::string Contents =
        std::string{R"GLTF({
    "asset": {"version": "2.0"},
    "buffers": [{
        "uri": "data:application/octet-stream;base64,)GLTF"} +
        TransparentPngBase64 +
        R"GLTF(",
        "byteLength": )GLTF" +
        std::to_string(TransparentPng.size()) +
        R"GLTF(
    }],
    "bufferViews": [{
        "buffer": 0,
        "byteOffset": 0,
        "byteLength": )GLTF" +
        std::to_string(TransparentPng.size()) +
        R"GLTF(
    }],
    "images": [
        {
            "name": "BufferViewTexture",
            "bufferView": 0,
            "mimeType": "image/png"
        },
        {
            "name": "DataURITexture",
            "uri": "data:image/png;base64,)GLTF" +
        TransparentPngBase64 +
        R"GLTF("
        }
    ],
    "textures": [
        {"source": 0},
        {"source": 1}
    ]
})GLTF";

    return WriteGLTFFile(TempDir, "data_uri_and_buffer_view_textures.gltf", Contents.c_str());
}

std::shared_ptr<GLTF::Document> LoadMetadataOnlyDocument(const std::string& GLTFPath)
{
    GLTF::DocumentLoadInfo LoadInfo;
    LoadInfo.FileName     = GLTFPath.c_str();
    LoadInfo.DecodeImages = false;
    return std::make_shared<GLTF::Document>(LoadInfo);
}

void ProcessQueuedTasks(IThreadPool& ThreadPool)
{
    while (ThreadPool.GetQueueSize() != 0)
        ThreadPool.ProcessTask(0, false);
}

void WaitForAllTasksAndStop(IThreadPool& ThreadPool)
{
    ThreadPool.WaitForAllTasks();
    ThreadPool.StopThreads();
}

RadientImport::TextureAssetList LoadTextures(IThreadPool&                ThreadPool,
                                             RadientTextureAssetManager& TextureManager,
                                             const std::string&          GLTFPath)
{
    return RadientGLTFLoader::LoadTextures(ThreadPool,
                                           TextureManager,
                                           GLTFPath,
                                           LoadMetadataOnlyDocument(GLTFPath));
}

RadientImport::MaterialAssetList LoadMaterials(RadientMaterialAssetManager&           MaterialManager,
                                               const std::shared_ptr<GLTF::Document>& pDocument,
                                               const RadientImport::TextureAssetList& Textures)
{
    return RadientGLTFLoader::LoadMaterials(MaterialManager, pDocument, Textures);
}

RADIENT_STATUS LoadScene(IThreadPool&                            ThreadPool,
                         RadientMeshAssetManager&                MeshManager,
                         const std::string&                      GLTFPath,
                         const std::shared_ptr<GLTF::Document>&  pDocument,
                         const RadientImport::MaterialAssetList& Materials,
                         RadientImport::ImportedDocument&        Scene)
{
    return RadientGLTFLoader::LoadScene(ThreadPool, MeshManager, GLTFPath, pDocument, Materials, Scene);
}

} // namespace

TEST(RadientGLTFLoaderTest, LoadTexturesCreatesTextureAssetFromExternalImageURI)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFExternalTextureFile(TempDir);

    RadientImport::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);

    const std::string TextureURI = Textures[0]->GetReference().URI;
    EXPECT_NE(TextureURI.find("external.png"), std::string::npos);
    EXPECT_EQ(TextureURI.find("#texture:"), std::string::npos);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadTexturesUsesAssetResolverForExternalImageURI)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFExternalTextureFile(TempDir);

    RefCntAutoPtr<TestRadientAssetResolver> pResolver{MakeNewRCObj<TestRadientAssetResolver>()()};
    pResolver->AddAsset("external.png",
                        "memory://resolved/external.png",
                        std::vector<Uint8>{TransparentPng.begin(), TransparentPng.end()});

    RadientTextureAssetManager::CreateInfo TextureManagerCI;
    TextureManagerCI.pAssetResolver = pResolver;

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create(TextureManagerCI);
    ASSERT_NE(pTextureManager, nullptr);

    RadientImport::TextureAssetList Textures =
        RadientGLTFLoader::LoadTextures(*pThreadPool,
                                        *pTextureManager,
                                        GLTFPath,
                                        LoadMetadataOnlyDocument(GLTFPath));

    EXPECT_EQ(pResolver->GetStats().ResolveLocationCount, 0u);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 0u);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);
    EXPECT_NE(std::string{Textures[0]->GetReference().URI}.find("external.png"), std::string::npos);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(pResolver->GetStats().ResolveLocationCount, 1u);
    EXPECT_EQ(pResolver->GetStats().OpenCount, 1u);
    EXPECT_NE(pResolver->GetStats().LastURI.find("external.png"), std::string::npos);
    EXPECT_EQ(pResolver->GetStats().LastBaseURI, GLTFPath);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadTexturesContinuesAfterUnresolvedTextureSource)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFWithMissingAndValidTextureSourcesFile(TempDir);

    RadientImport::TextureAssetList Textures;
    {
        TestingEnvironment::ErrorScope ExpectedErrors{"Failed to resolve GLTF texture source 0"};
        Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);
    }

    // The first GLTF texture refers to an invalid image source index, so no
    // texture worker is scheduled for that slot.
    ASSERT_EQ(Textures.size(), 2u);
    EXPECT_EQ(Textures[0], nullptr);

    // Later texture slots must preserve their original GLTF indices and still
    // load normally.
    ASSERT_NE(Textures[1], nullptr);
    ASSERT_NE(Textures[1]->GetReference().URI, nullptr);

    const std::string TextureURI = Textures[1]->GetReference().URI;
    EXPECT_NE(TextureURI.find("external.png"), std::string::npos);
    EXPECT_EQ(TextureURI.find("#texture:"), std::string::npos);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[1]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[1]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[1]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadTexturesCreatesTextureAssetFromDataURIImage)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFDataURITextureFile(TempDir);

    RadientImport::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);
    EXPECT_STREQ(Textures[0]->GetReference().URI, (GLTFPath + "#texture:0").c_str());
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadTexturesCreatesTextureAssetFromBufferViewImage)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFBufferViewTextureFile(TempDir);

    RadientImport::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);
    EXPECT_STREQ(Textures[0]->GetReference().URI, (GLTFPath + "#texture:0").c_str());
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, IdenticalDataURIAndBufferViewTexturesSharePayload)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{2});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFDataURIAndBufferViewTexturesFile(TempDir);

    RadientImport::TextureAssetList Textures = LoadTextures(*pThreadPool, *pTextureManager, GLTFPath);

    ASSERT_EQ(Textures.size(), 2u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_NE(Textures[1], nullptr);
    ASSERT_NE(Textures[0]->GetReference().URI, nullptr);
    ASSERT_NE(Textures[1]->GetReference().URI, nullptr);
    EXPECT_STREQ(Textures[0]->GetReference().URI, (GLTFPath + "#texture:0").c_str());
    EXPECT_STREQ(Textures[1]->GetReference().URI, (GLTFPath + "#texture:1").c_str());

    WaitForAllTasksAndStop(*pThreadPool);

    const TexturePayloadImpl* pBufferViewPayload = RadientTextureAssetManager::GetTexturePayload(Textures[0]);
    ASSERT_NE(pBufferViewPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(Textures[1]), pBufferViewPayload);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[1]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[1]), RADIENT_STATUS_NO_GPU_DATA);
}

TEST(RadientGLTFLoaderTest, IdenticalTexturesFromDifferentGLTFsSharePayload)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{2});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string BufferViewGLTFPath = WriteGLTFBufferViewTextureFile(TempDir);
    const std::string DataURIGLTFPath    = WriteGLTFDataURITextureFile(TempDir);

    RadientImport::TextureAssetList BufferViewTextures = LoadTextures(*pThreadPool, *pTextureManager, BufferViewGLTFPath);
    RadientImport::TextureAssetList DataURITextures    = LoadTextures(*pThreadPool, *pTextureManager, DataURIGLTFPath);

    ASSERT_EQ(BufferViewTextures.size(), 1u);
    ASSERT_EQ(DataURITextures.size(), 1u);
    ASSERT_NE(BufferViewTextures[0], nullptr);
    ASSERT_NE(DataURITextures[0], nullptr);
    ASSERT_NE(BufferViewTextures[0]->GetReference().URI, nullptr);
    ASSERT_NE(DataURITextures[0]->GetReference().URI, nullptr);
    EXPECT_STREQ(BufferViewTextures[0]->GetReference().URI, (BufferViewGLTFPath + "#texture:0").c_str());
    EXPECT_STREQ(DataURITextures[0]->GetReference().URI, (DataURIGLTFPath + "#texture:0").c_str());

    WaitForAllTasksAndStop(*pThreadPool);

    const TexturePayloadImpl* pBufferViewPayload = RadientTextureAssetManager::GetTexturePayload(BufferViewTextures[0]);
    ASSERT_NE(pBufferViewPayload, nullptr);
    EXPECT_EQ(RadientTextureAssetManager::GetTexturePayload(DataURITextures[0]), pBufferViewPayload);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(BufferViewTextures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(BufferViewTextures[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(DataURITextures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(DataURITextures[0]), RADIENT_STATUS_NO_GPU_DATA);
}

TEST(RadientGLTFLoaderTest, LoadMaterialsCreatesMaterialAssetWithoutTextures)
{
    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMaterialWithoutTexturesFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::MaterialAssetList Materials = LoadMaterials(*pMaterialManager, pDocument, {});

    ASSERT_EQ(Materials.size(), 1u);
    ASSERT_NE(Materials[0], nullptr);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(Materials[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(Materials[0]), RADIENT_STATUS_OK);

    const GLTF::Material* pMaterial = RadientMaterialAssetManager::GetMaterial(Materials[0]);
    ASSERT_NE(pMaterial, nullptr);
    EXPECT_TRUE(pMaterial->DoubleSided);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.x, 0.25f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.y, 0.5f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.z, 0.75f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.BaseColorFactor.w, 1.0f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.MetallicFactor, 0.125f);
    EXPECT_FLOAT_EQ(pMaterial->Attribs.RoughnessFactor, 0.875f);
}

TEST(RadientGLTFLoaderTest, LoadMaterialsTracksTextureDependencies)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientTextureAssetManagerSharedPtr pTextureManager = RadientTextureAssetManager::Create({});
    ASSERT_NE(pTextureManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMaterialWithTextureDependencyFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::TextureAssetList Textures =
        RadientGLTFLoader::LoadTextures(*pThreadPool, *pTextureManager, GLTFPath, pDocument);
    RadientImport::MaterialAssetList Materials = LoadMaterials(*pMaterialManager, pDocument, Textures);

    ASSERT_EQ(Textures.size(), 1u);
    ASSERT_NE(Textures[0], nullptr);
    ASSERT_EQ(Materials.size(), 1u);
    ASSERT_NE(Materials[0], nullptr);

    // The material keeps the texture asset dependency and remains pending
    // while the texture source load task is still queued.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(Materials[0]), RADIENT_STATUS_PENDING);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(Materials[0]), RADIENT_STATUS_PENDING);

    ProcessQueuedTasks(*pThreadPool);

    // With no render device, CPU/source loading succeeds but no GPU texture
    // storage is created. The material source status follows the texture source,
    // while GPU status reports the null-device condition.
    EXPECT_EQ(RadientTextureAssetManager::GetLoadStatus(Textures[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientTextureAssetManager::GetGPUResourceStatus(Textures[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMaterialAssetManager::GetLoadStatus(Materials[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMaterialAssetManager::GetGPUResourceStatus(Materials[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMaterialAssetManager::GetMaterial(Materials[0]), nullptr);
    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneExtractsSceneGraphComponents)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFSceneGraphFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, {}, Scene), RADIENT_STATUS_OK);

    EXPECT_TRUE(Scene.Meshes.empty());
    EXPECT_EQ(Scene.DefaultSceneId, 1u);

    ASSERT_EQ(Scene.Scenes.size(), 2u);
    EXPECT_EQ(Scene.Scenes[0].Name, "UnusedScene");
    ASSERT_EQ(Scene.Scenes[0].RootNodes.size(), 1u);
    EXPECT_EQ(Scene.Scenes[1].Name, "DefaultScene");
    ASSERT_EQ(Scene.Scenes[1].RootNodes.size(), 1u);

    ASSERT_EQ(Scene.Nodes.size(), 3u);
    const Uint32 UnusedNodeIndex = Scene.Scenes[0].RootNodes[0];
    ASSERT_LT(UnusedNodeIndex, Scene.Nodes.size());
    EXPECT_EQ(Scene.Nodes[UnusedNodeIndex].Name, "UnusedNode");
    ExpectFloat3Near(Scene.Nodes[UnusedNodeIndex].Transform.Scale, {2.f, 2.f, 2.f});

    const Uint32 RootNodeIndex = Scene.Scenes[1].RootNodes[0];
    ASSERT_LT(RootNodeIndex, Scene.Nodes.size());
    EXPECT_EQ(Scene.Nodes[RootNodeIndex].Name, "RootNode");
    ASSERT_EQ(Scene.Nodes[RootNodeIndex].Children.size(), 1u);
    ExpectFloat3Near(Scene.Nodes[RootNodeIndex].Transform.Position, {1.f, 2.f, 3.f});

    const Uint32 CameraLightNodeIndex = Scene.Nodes[RootNodeIndex].Children[0];
    ASSERT_LT(CameraLightNodeIndex, Scene.Nodes.size());
    EXPECT_EQ(Scene.Nodes[CameraLightNodeIndex].Name, "CameraLightNode");
    ASSERT_TRUE(Scene.Nodes[CameraLightNodeIndex].Camera.has_value());
    EXPECT_EQ(Scene.Nodes[CameraLightNodeIndex].Camera->Projection, RADIENT_CAMERA_PROJECTION_ORTHOGRAPHIC);
    EXPECT_NEAR(Scene.Nodes[CameraLightNodeIndex].Camera->HorizontalAperture, 4.f, EPSILON);
    EXPECT_NEAR(Scene.Nodes[CameraLightNodeIndex].Camera->VerticalAperture, 3.f, EPSILON);
    ExpectFloat2Near(Scene.Nodes[CameraLightNodeIndex].Camera->ClippingRange, {0.5f, 100.f});

    ASSERT_TRUE(Scene.Nodes[CameraLightNodeIndex].Light.has_value());
    EXPECT_EQ(Scene.Nodes[CameraLightNodeIndex].Light->Type, RADIENT_LIGHT_TYPE_POINT);
    ExpectFloat3Near(Scene.Nodes[CameraLightNodeIndex].Light->Color, {0.25f, 0.5f, 1.f});
    EXPECT_NEAR(Scene.Nodes[CameraLightNodeIndex].Light->Intensity, 3.f, EPSILON);
    EXPECT_NEAR(Scene.Nodes[CameraLightNodeIndex].Light->Range, 10.f, EPSILON);

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneCreatesMeshAsset)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMeshFile(TempDir, false);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, {}, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 1u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    EXPECT_EQ(Scene.DefaultSceneId, 0u);
    ASSERT_EQ(Scene.Scenes.size(), 1u);
    EXPECT_EQ(Scene.Scenes[0].Name, "MainScene");
    ASSERT_EQ(Scene.Scenes[0].RootNodes.size(), 1u);
    EXPECT_EQ(Scene.Scenes[0].RootNodes[0], 0u);

    ASSERT_EQ(Scene.Nodes.size(), 1u);
    EXPECT_EQ(Scene.Nodes[0].Name, "TriangleNode");
    EXPECT_EQ(Scene.Nodes[0].pMesh, Scene.Meshes[0]);
    EXPECT_FALSE(Scene.Nodes[0].Camera.has_value());
    EXPECT_FALSE(Scene.Nodes[0].Light.has_value());
    EXPECT_TRUE(Scene.Nodes[0].Children.empty());
    ExpectFloat3Near(Scene.Nodes[0].Transform.Position, {1.f, 2.f, 3.f});
    ExpectFloat3Near(Scene.Nodes[0].Transform.Scale, {2.f, 3.f, 4.f});

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(Scene.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_NE(RadientMeshAssetManager::GetMeshVertexData(Scene.Meshes[0]), nullptr);
    EXPECT_NE(RadientMeshAssetManager::GetMeshIndexData(Scene.Meshes[0]), nullptr);

    const RadientDrawableMeshResolveResult DrawableMesh =
        RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[0], false);
    EXPECT_EQ(DrawableMesh.Status, RADIENT_STATUS_OK);
    EXPECT_NE(DrawableMesh.pMesh, nullptr);

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneReloadsSameMeshSharesPayload)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath = WriteGLTFMeshFile(TempDir, false);

    RadientImport::ImportedDocument FirstScene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, LoadMetadataOnlyDocument(GLTFPath), {}, FirstScene),
              RADIENT_STATUS_OK);
    ASSERT_EQ(FirstScene.Meshes.size(), 1u);
    ASSERT_NE(FirstScene.Meshes[0], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    const MeshPayloadImpl* pFirstPayload = RadientMeshAssetManager::GetMeshPayload(FirstScene.Meshes[0]);
    ASSERT_NE(pFirstPayload, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(FirstScene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(FirstScene.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);

    RadientImport::ImportedDocument SecondScene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, LoadMetadataOnlyDocument(GLTFPath), {}, SecondScene),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SecondScene.Meshes.size(), 1u);
    ASSERT_NE(SecondScene.Meshes[0], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(SecondScene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(SecondScene.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(SecondScene.Meshes[0]), pFirstPayload);

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneSharesPayloadForIdenticalMeshesFromDifferentGLTFs)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPathA = WriteGLTFMeshFile(TempDir, false);
    const std::string GLTFPathB = WriteGLTFMeshWithShiftedAccessorsFile(TempDir);

    RadientImport::ImportedDocument SceneA;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPathA, LoadMetadataOnlyDocument(GLTFPathA), {}, SceneA),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SceneA.Meshes.size(), 1u);
    ASSERT_NE(SceneA.Meshes[0], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    const MeshPayloadImpl* pPayloadA = RadientMeshAssetManager::GetMeshPayload(SceneA.Meshes[0]);
    ASSERT_NE(pPayloadA, nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(SceneA.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(SceneA.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);

    RadientImport::ImportedDocument SceneB;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPathB, LoadMetadataOnlyDocument(GLTFPathB), {}, SceneB),
              RADIENT_STATUS_OK);
    ASSERT_EQ(SceneB.Meshes.size(), 1u);
    ASSERT_NE(SceneB.Meshes[0], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(SceneB.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(SceneB.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);

    // The second GLTF uses different accessor ids and buffer views, but the
    // source bytes and resolved mesh view are identical to the first GLTF.
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(SceneB.Meshes[0]), pPayloadA);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(SceneB.Meshes[0]),
              RadientMeshAssetManager::GetMeshVertexData(SceneA.Meshes[0]));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(SceneB.Meshes[0]),
              RadientMeshAssetManager::GetMeshIndexData(SceneA.Meshes[0]));

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneSharesVertexPayloadForSameGeometryDifferentIndices)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMeshesWithSameGeometryDifferentIndicesFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, {}, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 2u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    ASSERT_NE(Scene.Meshes[1], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[1]), RADIENT_STATUS_OK);

    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[1]),
              RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[0]));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[0], 0));
    EXPECT_NE(RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[0], 0));

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneSharesIndexPayloadForSameIndicesDifferentGeometry)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMeshesWithSameIndicesDifferentGeometryFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, {}, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 2u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    ASSERT_NE(Scene.Meshes[1], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[1]), RADIENT_STATUS_OK);

    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[1]),
              RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[0]));
    EXPECT_NE(RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[0], 0));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[0], 0));

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneSameGeometryDifferentMaterialsShareGeometryPayloadsOnly)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMeshesWithSameGeometryDifferentMaterialsFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::MaterialAssetList Materials = LoadMaterials(*pMaterialManager, pDocument, {});
    ASSERT_EQ(Materials.size(), 2u);
    ASSERT_NE(Materials[0], nullptr);
    ASSERT_NE(Materials[1], nullptr);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, Materials, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 2u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    ASSERT_NE(Scene.Meshes[1], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[1]), RADIENT_STATUS_OK);

    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[1]),
              RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[0]));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[0], 0));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[0], 0));

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneSameGeometryDifferentPrimitiveListsShareGeometryPayloadsOnly)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMeshesWithSameGeometryDifferentPrimitiveListsFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, {}, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 2u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    ASSERT_NE(Scene.Meshes[1], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[1]), RADIENT_STATUS_OK);

    EXPECT_NE(RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[1]),
              RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[0]));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[0], 0));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[1], 0),
              RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[0], 0));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshGeometryCount(Scene.Meshes[0]), 1u);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshGeometryCount(Scene.Meshes[1]), 1u);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexDataPayload(Scene.Meshes[0], 1), nullptr);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexDataPayload(Scene.Meshes[0], 1), nullptr);

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneSharesGPUDataForIdenticalPrimitiveData)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFTwoMeshesWithIdenticalPrimitiveDataFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, {}, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 2u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    ASSERT_NE(Scene.Meshes[1], nullptr);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[1]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(Scene.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(Scene.Meshes[1]), RADIENT_STATUS_NO_GPU_DATA);

    const MeshPayloadImpl* pMeshPayload = RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[0]);
    ASSERT_NE(pMeshPayload, nullptr);

    // The GLTF primitives use different accessors and buffer views, but the
    // source bytes are identical. The loader should therefore resolve both
    // meshes to the same cached mesh/GPU-data payload.
    EXPECT_EQ(RadientMeshAssetManager::GetMeshPayload(Scene.Meshes[1]), pMeshPayload);
    EXPECT_EQ(RadientMeshAssetManager::GetMeshVertexData(Scene.Meshes[1]),
              RadientMeshAssetManager::GetMeshVertexData(Scene.Meshes[0]));
    EXPECT_EQ(RadientMeshAssetManager::GetMeshIndexData(Scene.Meshes[1]),
              RadientMeshAssetManager::GetMeshIndexData(Scene.Meshes[0]));

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneDeduplicatesAlternatingPrimitiveGeometries)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMeshWithAlternatingPrimitiveGeometryFile(TempDir);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, {}, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 1u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    ASSERT_EQ(Scene.Nodes.size(), 1u);
    ASSERT_EQ(Scene.Scenes.size(), 1u);
    ASSERT_EQ(Scene.Scenes[0].RootNodes.size(), 1u);
    EXPECT_EQ(Scene.Scenes[0].RootNodes[0], 0u);
    EXPECT_EQ(Scene.Nodes[0].pMesh, Scene.Meshes[0]);

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(Scene.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);

    // The GLTF primitive order is A, B, A, B. The loader should group by
    // planned vertex/index data before constructing the mesh view, otherwise
    // a last-geometry check would create four geometry records instead of two.
    EXPECT_EQ(RadientMeshAssetManager::GetMeshGeometryCount(Scene.Meshes[0]), 2u);

    pThreadPool->StopThreads();
}

TEST(RadientGLTFLoaderTest, LoadSceneCreatesMeshAssetWithMaterial)
{
    RefCntAutoPtr<IThreadPool> pThreadPool = CreateThreadPool(ThreadPoolCreateInfo{0});
    ASSERT_NE(pThreadPool, nullptr);

    RadientMeshAssetManagerSharedPtr pMeshManager = RadientMeshAssetManager::Create({});
    ASSERT_NE(pMeshManager, nullptr);

    RadientMaterialAssetManagerSharedPtr pMaterialManager = RadientMaterialAssetManager::Create();
    ASSERT_NE(pMaterialManager, nullptr);

    TempDirectory     TempDir{"RadientGLTFLoaderTest"};
    const std::string GLTFPath  = WriteGLTFMeshFile(TempDir, true);
    auto              pDocument = LoadMetadataOnlyDocument(GLTFPath);

    RadientImport::MaterialAssetList Materials = LoadMaterials(*pMaterialManager, pDocument, {});
    ASSERT_EQ(Materials.size(), 1u);
    ASSERT_NE(Materials[0], nullptr);

    RadientImport::ImportedDocument Scene;
    EXPECT_EQ(LoadScene(*pThreadPool, *pMeshManager, GLTFPath, pDocument, Materials, Scene), RADIENT_STATUS_OK);

    ASSERT_EQ(Scene.Meshes.size(), 1u);
    ASSERT_NE(Scene.Meshes[0], nullptr);
    ASSERT_EQ(Scene.Nodes.size(), 1u);
    ASSERT_EQ(Scene.Scenes.size(), 1u);
    ASSERT_EQ(Scene.Scenes[0].RootNodes.size(), 1u);
    EXPECT_EQ(Scene.Scenes[0].RootNodes[0], 0u);
    EXPECT_EQ(Scene.Nodes[0].Name, "TriangleNode");
    EXPECT_EQ(Scene.Nodes[0].pMesh, Scene.Meshes[0]);
    ExpectFloat3Near(Scene.Nodes[0].Transform.Position, {1.f, 2.f, 3.f});

    ProcessQueuedTasks(*pThreadPool);

    EXPECT_EQ(RadientMeshAssetManager::GetLoadStatus(Scene.Meshes[0]), RADIENT_STATUS_OK);
    EXPECT_EQ(RadientMeshAssetManager::GetGPUResourceStatus(Scene.Meshes[0]), RADIENT_STATUS_NO_GPU_DATA);

    const GLTF::Material* pMaterial = RadientMaterialAssetManager::GetMaterial(Materials[0]);
    ASSERT_NE(pMaterial, nullptr);

    const RadientDrawableMeshResolveResult DrawableMesh =
        RadientMeshAssetManager::GetDrawableMesh(Scene.Meshes[0], false);
    EXPECT_EQ(DrawableMesh.Status, RADIENT_STATUS_OK);
    EXPECT_NE(DrawableMesh.pMesh, nullptr);

    pThreadPool->StopThreads();
}
