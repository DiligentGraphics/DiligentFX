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

#include "RadientMeshPrimitives.h"

#include "Math/RadientMath.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Diligent
{

namespace
{

constexpr Float32 PI = 3.14159265358979323846f;

struct MeshBuilder
{
    std::vector<RadientFloat3> Positions;
    std::vector<RadientFloat3> Normals;
    std::vector<RadientFloat2> TexCoords0;
    std::vector<Uint32>        Indices;
};

Float32 Clamp(Float32 Value, Float32 MinValue, Float32 MaxValue)
{
    return Value < MinValue ? MinValue : (Value > MaxValue ? MaxValue : Value);
}

RadientFloat2 GetSphericalTexCoord(const RadientFloat3& Normal)
{
    const Float32 U = 0.5f + std::atan2(Normal.z, Normal.x) / (2.f * PI);
    const Float32 V = std::acos(Clamp(Normal.y, -1.f, 1.f)) / PI;
    return RadientFloat2{U, V};
}

Uint32 GetSeamFixedVertex(MeshBuilder& Mesh, std::vector<Uint32>& SeamVertices, Uint32 Vertex)
{
    Uint32& SeamVertex = SeamVertices[Vertex];
    if (SeamVertex != ~0u)
        return SeamVertex;

    SeamVertex = static_cast<Uint32>(Mesh.Positions.size());
    Mesh.Positions.push_back(Mesh.Positions[Vertex]);
    Mesh.Normals.push_back(Mesh.Normals[Vertex]);
    Mesh.TexCoords0.push_back(RadientFloat2{Mesh.TexCoords0[Vertex].x + 1.f, Mesh.TexCoords0[Vertex].y});
    return SeamVertex;
}

void FixSphericalTexCoordSeams(MeshBuilder& Mesh)
{
    std::vector<Uint32> SeamVertices(Mesh.Positions.size(), ~0u);

    for (size_t Index = 0; Index + 2 < Mesh.Indices.size(); Index += 3)
    {
        const Uint32 Vertex0 = Mesh.Indices[Index + 0];
        const Uint32 Vertex1 = Mesh.Indices[Index + 1];
        const Uint32 Vertex2 = Mesh.Indices[Index + 2];

        const Float32 U0 = Mesh.TexCoords0[Vertex0].x;
        const Float32 U1 = Mesh.TexCoords0[Vertex1].x;
        const Float32 U2 = Mesh.TexCoords0[Vertex2].x;

        const Float32 MinU = std::min(U0, std::min(U1, U2));
        const Float32 MaxU = std::max(U0, std::max(U1, U2));
        if (MaxU - MinU <= 0.5f)
            continue;

        if (U0 < 0.5f)
            Mesh.Indices[Index + 0] = GetSeamFixedVertex(Mesh, SeamVertices, Vertex0);
        if (U1 < 0.5f)
            Mesh.Indices[Index + 1] = GetSeamFixedVertex(Mesh, SeamVertices, Vertex1);
        if (U2 < 0.5f)
            Mesh.Indices[Index + 2] = GetSeamFixedVertex(Mesh, SeamVertices, Vertex2);
    }
}

void AddQuadIndices(std::vector<Uint32>& Indices,
                    Uint32               Vertex00,
                    Uint32               Vertex10,
                    Uint32               Vertex01,
                    Uint32               Vertex11)
{
    Indices.push_back(Vertex00);
    Indices.push_back(Vertex10);
    Indices.push_back(Vertex01);

    Indices.push_back(Vertex10);
    Indices.push_back(Vertex11);
    Indices.push_back(Vertex01);
}

void AddCubeFace(MeshBuilder&         Mesh,
                 const RadientFloat3& Normal,
                 const RadientFloat3& Tangent,
                 const RadientFloat3& Bitangent,
                 Float32              Size,
                 Uint32               Subdivisions)
{
    const Uint32  BaseVertex = static_cast<Uint32>(Mesh.Positions.size());
    const Float32 HalfSize   = Size * 0.5f;

    for (Uint32 Y = 0; Y <= Subdivisions; ++Y)
    {
        const Float32 V = static_cast<Float32>(Y) / static_cast<Float32>(Subdivisions);
        for (Uint32 X = 0; X <= Subdivisions; ++X)
        {
            const Float32 U = static_cast<Float32>(X) / static_cast<Float32>(Subdivisions);

            RadientFloat3 Position = Normal * HalfSize;
            Position               = Position + Tangent * ((U - 0.5f) * Size);
            Position               = Position + Bitangent * ((V - 0.5f) * Size);

            Mesh.Positions.push_back(Position);
            Mesh.Normals.push_back(Normal);
            Mesh.TexCoords0.push_back(RadientFloat2{U, 1.f - V});
        }
    }

    const Uint32 RowStride = Subdivisions + 1;
    for (Uint32 Y = 0; Y < Subdivisions; ++Y)
    {
        for (Uint32 X = 0; X < Subdivisions; ++X)
        {
            const Uint32 Vertex00 = BaseVertex + Y * RowStride + X;
            const Uint32 Vertex10 = Vertex00 + 1;
            const Uint32 Vertex01 = Vertex00 + RowStride;
            const Uint32 Vertex11 = Vertex01 + 1;
            AddQuadIndices(Mesh.Indices, Vertex00, Vertex10, Vertex01, Vertex11);
        }
    }
}

MeshBuilder CreateCube(Float32 Size, Uint32 Subdivisions)
{
    MeshBuilder Mesh;

    const Uint32 VertexCountPerFace = (Subdivisions + 1) * (Subdivisions + 1);
    Mesh.Positions.reserve(6u * VertexCountPerFace);
    Mesh.Normals.reserve(6u * VertexCountPerFace);
    Mesh.TexCoords0.reserve(6u * VertexCountPerFace);
    Mesh.Indices.reserve(6u * Subdivisions * Subdivisions * 6u);

    AddCubeFace(Mesh, {+1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, {0.f, +1.f, 0.f}, Size, Subdivisions);
    AddCubeFace(Mesh, {-1.f, 0.f, 0.f}, {0.f, 0.f, +1.f}, {0.f, +1.f, 0.f}, Size, Subdivisions);
    AddCubeFace(Mesh, {0.f, +1.f, 0.f}, {+1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, Size, Subdivisions);
    AddCubeFace(Mesh, {0.f, -1.f, 0.f}, {+1.f, 0.f, 0.f}, {0.f, 0.f, +1.f}, Size, Subdivisions);
    AddCubeFace(Mesh, {0.f, 0.f, +1.f}, {+1.f, 0.f, 0.f}, {0.f, +1.f, 0.f}, Size, Subdivisions);
    AddCubeFace(Mesh, {0.f, 0.f, -1.f}, {-1.f, 0.f, 0.f}, {0.f, +1.f, 0.f}, Size, Subdivisions);

    return Mesh;
}

MeshBuilder CreateSphere(Float32 Radius, Uint32 Subdivisions)
{
    MeshBuilder Mesh = CreateCube(2.f, Subdivisions);

    for (size_t Vertex = 0; Vertex < Mesh.Positions.size(); ++Vertex)
    {
        const RadientFloat3 Normal = RadientMath::Normalize(Mesh.Positions[Vertex]);
        Mesh.Positions[Vertex]     = Normal * Radius;
        Mesh.Normals[Vertex]       = Normal;
        Mesh.TexCoords0[Vertex]    = GetSphericalTexCoord(Normal);
    }

    FixSphericalTexCoordSeams(Mesh);
    return Mesh;
}

RADIENT_STATUS CreatePrimitiveMesh(IRadientAssetManager*  pAssetManager,
                                   const Char*            Name,
                                   IRadientMaterialAsset* pMaterial,
                                   const MeshBuilder&     Mesh,
                                   IRadientMeshAsset**    ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMesh = nullptr;

    if (pAssetManager == nullptr ||
        Mesh.Positions.empty() ||
        Mesh.Indices.empty())
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    RadientVertexBufferCreateInfo VertexBufferCI{};
    VertexBufferCI.Name        = Name;
    VertexBufferCI.pPositions  = Mesh.Positions.data();
    VertexBufferCI.pNormals    = Mesh.Normals.data();
    VertexBufferCI.pTexCoords0 = Mesh.TexCoords0.data();
    VertexBufferCI.VertexCount = static_cast<Uint32>(Mesh.Positions.size());

    RadientIndexBufferCreateInfo IndexBufferCI{};
    IndexBufferCI.pIndices   = Mesh.Indices.data();
    IndexBufferCI.IndexCount = static_cast<Uint32>(Mesh.Indices.size());
    IndexBufferCI.IndexType  = RADIENT_INDEX_TYPE_UINT32;

    RadientMeshPrimitiveCreateInfo PrimitiveCI{};
    PrimitiveCI.Name              = Name;
    PrimitiveCI.VertexBufferIndex = 0;
    PrimitiveCI.FirstIndex        = 0;
    PrimitiveCI.IndexCount        = static_cast<Uint32>(Mesh.Indices.size());
    PrimitiveCI.pMaterial         = pMaterial;

    RadientMeshCreateInfo MeshCI{};
    MeshCI.Name              = Name;
    MeshCI.pVertexBuffers    = &VertexBufferCI;
    MeshCI.VertexBufferCount = 1;
    MeshCI.IndexBuffer       = IndexBufferCI;
    MeshCI.pPrimitives       = &PrimitiveCI;
    MeshCI.PrimitiveCount    = 1;

    return pAssetManager->CreateMesh(MeshCI, ppMesh);
}

} // namespace

RADIENT_STATUS CreateRadientCubeMesh(IRadientAssetManager*            pAssetManager,
                                     const RadientCubeMeshCreateInfo& MeshCI,
                                     IRadientMeshAsset**              ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMesh = nullptr;

    if (!RadientMath::IsFinitePositive(MeshCI.Size))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const Uint32      Subdivisions = MeshCI.Subdivisions != 0 ? MeshCI.Subdivisions : 1u;
    const MeshBuilder Mesh         = CreateCube(MeshCI.Size, Subdivisions);
    return CreatePrimitiveMesh(pAssetManager, MeshCI.Name, MeshCI.pMaterial, Mesh, ppMesh);
}

RADIENT_STATUS CreateRadientSphereMesh(IRadientAssetManager*              pAssetManager,
                                       const RadientSphereMeshCreateInfo& MeshCI,
                                       IRadientMeshAsset**                ppMesh)
{
    if (ppMesh == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMesh = nullptr;

    if (!RadientMath::IsFinitePositive(MeshCI.Radius))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const Uint32      Subdivisions = MeshCI.Subdivisions != 0 ? MeshCI.Subdivisions : 1u;
    const MeshBuilder Mesh         = CreateSphere(MeshCI.Radius, Subdivisions);
    return CreatePrimitiveMesh(pAssetManager, MeshCI.Name, MeshCI.pMaterial, Mesh, ppMesh);
}

} // namespace Diligent

extern "C"
{
    Diligent::RADIENT_STATUS Diligent_CreateRadientCubeMesh(Diligent::IRadientAssetManager*            pAssetManager,
                                                            const Diligent::RadientCubeMeshCreateInfo* pMeshCI,
                                                            Diligent::IRadientMeshAsset**              ppMesh)
    {
        if (pMeshCI == nullptr)
        {
            if (ppMesh != nullptr)
                *ppMesh = nullptr;
            return Diligent::RADIENT_STATUS_INVALID_ARGUMENT;
        }

        return Diligent::CreateRadientCubeMesh(pAssetManager, *pMeshCI, ppMesh);
    }

    Diligent::RADIENT_STATUS Diligent_CreateRadientSphereMesh(Diligent::IRadientAssetManager*              pAssetManager,
                                                              const Diligent::RadientSphereMeshCreateInfo* pMeshCI,
                                                              Diligent::IRadientMeshAsset**                ppMesh)
    {
        if (pMeshCI == nullptr)
        {
            if (ppMesh != nullptr)
                *ppMesh = nullptr;
            return Diligent::RADIENT_STATUS_INVALID_ARGUMENT;
        }

        return Diligent::CreateRadientSphereMesh(pAssetManager, *pMeshCI, ppMesh);
    }
}
