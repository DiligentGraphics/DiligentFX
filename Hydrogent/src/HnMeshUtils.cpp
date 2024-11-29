/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "HnMeshUtils.hpp"
#include "DebugUtilities.hpp"
#include "AdvancedMath.hpp"
#include "GfTypeConversions.hpp"
#include "PBR_Renderer.hpp"

#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4i.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/imaging/hd/tokens.h"

namespace Diligent
{

namespace USD
{

HnMeshUtils::~HnMeshUtils()
{
}

template <typename HandleFaceType>
void HnMeshUtils::ProcessFaces(HandleFaceType&& HandleFace) const
{
    const pxr::VtIntArray& FaceVertCounts   = m_Topology.GetFaceVertexCounts();
    const size_t           NumFaces         = FaceVertCounts.size();
    const int              NumVertexIndices = static_cast<int>(m_Topology.GetFaceVertexIndices().size());

    VERIFY_EXPR(NumFaces == static_cast<size_t>(m_Topology.GetNumFaces()));

    int FaceStartVertex = 0;
    for (size_t i = 0; i < NumFaces; ++i)
    {
        const int VertCount = FaceVertCounts[i];
        if (FaceStartVertex + VertCount > NumVertexIndices)
        {
            break;
        }

        if (VertCount >= 3)
        {
            HandleFace(i, FaceStartVertex, VertCount);
        }
        FaceStartVertex += VertCount;
    }
}

void HnMeshUtils::Triangulate(bool                UseFaceVertexIndices,
                              const pxr::VtValue* PointsPrimvar,
                              pxr::VtVec3iArray&  TriangleIndices,
                              pxr::VtIntArray&    SubsetStart) const
{
    const size_t NumFaces          = m_Topology.GetNumFaces();
    const int*   FaceVertexIndices = m_Topology.GetFaceVertexIndices().cdata();
    const int    NumVertexIndices  = static_cast<int>(m_Topology.GetFaceVertexIndices().size());

    const pxr::GfVec3f* const Points = (PointsPrimvar != nullptr && PointsPrimvar->IsHolding<pxr::VtVec3fArray>()) ?
        PointsPrimvar->UncheckedGet<pxr::VtVec3fArray>().cdata() :
        nullptr;

    // Count the number of triangles
    size_t NumTriangles = 0;
    ProcessFaces(
        [&](size_t FaceId, int StartVertex, int VertCount) {
            NumTriangles += VertCount - 2;
        });

    // Triangulate faces
    TriangleIndices.reserve(NumTriangles);
    std::vector<size_t> FaceStartTriangle(NumFaces + 1);

    std::vector<float3>               Polygon;
    Polygon3DTriangulator<int, float> Triangulator;
#ifdef DILIGENT_DEVELOPMENT
    std::vector<size_t> dvpFailedFaces;
#endif
    ProcessFaces(
        [&](size_t FaceId, int StartVertex, int VertCount) {
            FaceStartTriangle[FaceId] = TriangleIndices.size();
            if (VertCount <= 4 || Points == nullptr)
            {
                for (int i = 0; i < VertCount - 2; ++i)
                {
                    int Idx0 = StartVertex;
                    int Idx1 = StartVertex + i + 1;
                    int Idx2 = StartVertex + i + 2;
                    if (UseFaceVertexIndices)
                    {
                        VERIFY_EXPR(Idx0 < NumVertexIndices && Idx1 < NumVertexIndices && Idx2 < NumVertexIndices);
                        Idx0 = FaceVertexIndices[Idx0];
                        Idx1 = FaceVertexIndices[Idx1];
                        Idx2 = FaceVertexIndices[Idx2];
                    }

                    TriangleIndices.emplace_back(Idx0, Idx1, Idx2);
                }
            }
            else
            {
                Polygon.resize(VertCount);
                for (int i = 0; i < VertCount; ++i)
                {
                    int Idx = FaceVertexIndices[StartVertex + i];
                    if (Idx >= NumVertexIndices)
                        return; // Invalid vertex index
                    Polygon[i] = ToFloat3(Points[Idx]);
                }
                const std::vector<int>& Indices = Triangulator.Triangulate(Polygon);
#ifdef DILIGENT_DEVELOPMENT
                if (Triangulator.GetResult() != TRIANGULATE_POLYGON_RESULT_OK)
                {
                    dvpFailedFaces.push_back(FaceId);
                }
#endif
                for (size_t i = 0; i < Indices.size() / 3; ++i)
                {
                    int Idx0 = StartVertex + Indices[i * 3 + 0];
                    int Idx1 = StartVertex + Indices[i * 3 + 1];
                    int Idx2 = StartVertex + Indices[i * 3 + 2];
                    if (UseFaceVertexIndices)
                    {
                        VERIFY_EXPR(Idx0 < NumVertexIndices && Idx1 < NumVertexIndices && Idx2 < NumVertexIndices);
                        Idx0 = FaceVertexIndices[Idx0];
                        Idx1 = FaceVertexIndices[Idx1];
                        Idx2 = FaceVertexIndices[Idx2];
                    }
                    TriangleIndices.emplace_back(Idx0, Idx1, Idx2);
                }
            }
        });
#ifdef DILIGENT_DEVELOPMENT
    if (const size_t NumFailedFaces = dvpFailedFaces.size())
    {
        std::stringstream ss;
        for (size_t i = 0; i < NumFailedFaces; ++i)
        {
            ss << dvpFailedFaces[i];
            if (i < NumFailedFaces - 1)
                ss << ", ";
        }
        LOG_WARNING_MESSAGE(NumFailedFaces, (NumFailedFaces > 1 ? " faces" : " face"), " in mesh '", m_MeshId.GetString(),
                            (NumFailedFaces > 1 ? "' were" : "' was"), " triangulated with potential issues: ", ss.str());
    }
#endif

    VERIFY_EXPR(TriangleIndices.size() <= NumTriangles);
    FaceStartTriangle.back() = TriangleIndices.size();

    if (m_Topology.GetOrientation() != pxr::HdTokens->rightHanded)
    {
        for (pxr::GfVec3i& Triangle : TriangleIndices)
        {
            std::swap(Triangle[1], Triangle[2]);
        }
    }

    // Reorder triangles based on subsets
    const pxr::HdGeomSubsets& GeomSubsets = m_Topology.GetGeomSubsets();
    if (!GeomSubsets.empty())
    {
        const size_t NumSubsets = std::max(GeomSubsets.size(), size_t{1});
        SubsetStart.resize(NumSubsets + 1);
        // Count the number of triangles in each subset
        for (size_t subset_idx = 0; subset_idx < NumSubsets; ++subset_idx)
        {
            const pxr::HdGeomSubset& Subset = GeomSubsets[subset_idx];
            for (int FaceIdx : Subset.indices)
            {
                const size_t NumTris = FaceStartTriangle[FaceIdx + 1] - FaceStartTriangle[FaceIdx];
                SubsetStart[subset_idx + 1] += NumTris;
            }
        }

        // Calculate start indices for each subset
        for (size_t subset_idx = 1; subset_idx <= NumSubsets; ++subset_idx)
        {
            SubsetStart[subset_idx] += SubsetStart[subset_idx - 1];
        }

        const int         TotalNumTris = SubsetStart.back();
        pxr::VtVec3iArray SubsetTriangleIndices;
        SubsetTriangleIndices.reserve(TotalNumTris);
        for (size_t subset_idx = 0; subset_idx < NumSubsets; ++subset_idx)
        {
            const pxr::HdGeomSubset& Subset = GeomSubsets[subset_idx];
            for (int FaceIdx : Subset.indices)
            {
                const size_t StartTriangleIdx = FaceStartTriangle[FaceIdx];
                const size_t EndTriangleIdx   = FaceStartTriangle[FaceIdx + 1];
                for (size_t i = StartTriangleIdx; i < EndTriangleIdx; ++i)
                {
                    SubsetTriangleIndices.push_back(TriangleIndices[i]);
                }
            }
        }
        VERIFY_EXPR(SubsetTriangleIndices.size() == static_cast<size_t>(TotalNumTris));
        TriangleIndices = std::move(SubsetTriangleIndices);
    }
    else
    {
        SubsetStart.resize(2);
        SubsetStart[0] = 0;
        SubsetStart[1] = TriangleIndices.size();
    }
}

pxr::VtIntArray HnMeshUtils::ComputeEdgeIndices(bool UseFaceVertexIndices, bool UseLineStrip) const
{
    size_t NumEdges = 0;
    size_t NumFaces = 0; // Count the actual number of faces
    ProcessFaces(
        [&](size_t FaceId, int StartVertex, int VertCount) {
            NumEdges += VertCount;
            ++NumFaces;
        });

    pxr::VtIntArray EdgeIndices;
    if (UseLineStrip)
    {
        EdgeIndices.reserve(NumEdges + NumFaces * 2);
        ProcessFaces(
            [&](size_t FaceId, int StartVertex, int VertCount) {
                for (int v = 0; v < VertCount; ++v)
                {
                    EdgeIndices.push_back(StartVertex + v);
                }
                EdgeIndices.push_back(StartVertex);
                EdgeIndices.push_back(-1);
            });
        VERIFY_EXPR(EdgeIndices.size() == NumEdges + NumFaces * 2);
    }
    else
    {
        EdgeIndices.reserve(NumEdges * 2);
        ProcessFaces(
            [&](size_t FaceId, int StartVertex, int VertCount) {
                for (int v = 0; v < VertCount - 1; ++v)
                {
                    EdgeIndices.push_back(StartVertex + v);
                    EdgeIndices.push_back(StartVertex + (v + 1));
                }
                EdgeIndices.push_back(StartVertex + VertCount - 1);
                EdgeIndices.push_back(StartVertex);
            });
        VERIFY_EXPR(EdgeIndices.size() == NumEdges * 2);
    }

    if (UseFaceVertexIndices)
    {
        const int*   FaceVertexIndices = m_Topology.GetFaceVertexIndices().cdata();
        const size_t NumVertexIndices  = m_Topology.GetFaceVertexIndices().size();
        for (int& Idx : EdgeIndices)
        {
            if (Idx < 0)
                continue;

            VERIFY_EXPR(Idx < NumVertexIndices);
            Idx = FaceVertexIndices[Idx];
        }
    }

    return EdgeIndices;
}

pxr::VtIntArray HnMeshUtils::ComputePointIndices(bool ConvertToFaceVarying) const
{
    const int NumPoints = m_Topology.GetNumPoints();

    pxr::VtIntArray PointIndices;
    PointIndices.reserve(NumPoints);
    if (ConvertToFaceVarying)
    {
        const int*   FaceVertexIndices = m_Topology.GetFaceVertexIndices().cdata();
        const size_t NumVertexIndices  = m_Topology.GetFaceVertexIndices().size();

        std::vector<bool> PointAdded(NumPoints, false);
        ProcessFaces(
            [&](size_t FaceId, int StartVertex, int VertCount) {
                for (int v = 0; v < VertCount; ++v)
                {
                    int FaceVertIdx = StartVertex + v;
                    if (FaceVertIdx >= NumVertexIndices)
                        continue;

                    int PointIdx = FaceVertexIndices[FaceVertIdx];
                    if (PointIdx >= NumPoints)
                        continue;

                    if (!PointAdded[PointIdx])
                    {
                        PointIndices.push_back(FaceVertIdx);
                        PointAdded[PointIdx] = true;
                    }
                }
            });
    }
    else
    {
        PointIndices.resize(NumPoints);
        for (int i = 0; i < NumPoints; ++i)
            PointIndices[i] = i;
    }

    return PointIndices;
}

template <typename T>
pxr::VtValue ConvertVertexArrayToFaceVaryingArray(const pxr::VtIntArray& FaceVertexIndices, const pxr::VtArray<T>& VertexArray, size_t ValuesPerVertex)
{
    const int VertexDataSize = static_cast<int>(VertexArray.size());
    // Use raw pointers as VtArray::operator[] has measurable overhead even in release build
    const T* pVertData = VertexArray.cdata();

    pxr::VtArray<T> FaceArray(FaceVertexIndices.size() * ValuesPerVertex);
    T*              pFaceData = FaceArray.data();
    for (size_t i = 0; i < FaceVertexIndices.size(); ++i)
    {
        const int Idx = FaceVertexIndices[i];
        if (Idx < VertexDataSize)
        {
            for (size_t elem = 0; elem < ValuesPerVertex; ++elem)
            {
                pFaceData[i * ValuesPerVertex + elem] = pVertData[Idx * ValuesPerVertex + elem];
            }
        }
    }
    return pxr::VtValue::Take(FaceArray);
}

pxr::VtValue HnMeshUtils::ConvertVertexPrimvarToFaceVarying(const pxr::VtValue& VertexData, size_t ValuesPerVertex) const
{
    const pxr::VtIntArray& FaceVertexIndices = m_Topology.GetFaceVertexIndices();
    if (VertexData.IsHolding<pxr::VtVec4fArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtVec4fArray>(), ValuesPerVertex);
    }
    else if (VertexData.IsHolding<pxr::VtVec3fArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtVec3fArray>(), ValuesPerVertex);
    }
    else if (VertexData.IsHolding<pxr::VtVec2fArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtVec2fArray>(), ValuesPerVertex);
    }
    else if (VertexData.IsHolding<pxr::VtFloatArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtFloatArray>(), ValuesPerVertex);
    }
    else if (VertexData.IsHolding<pxr::VtVec4iArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtVec4iArray>(), ValuesPerVertex);
    }
    else if (VertexData.IsHolding<pxr::VtVec3iArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtVec3iArray>(), ValuesPerVertex);
    }
    else if (VertexData.IsHolding<pxr::VtVec2iArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtVec2iArray>(), ValuesPerVertex);
    }
    else if (VertexData.IsHolding<pxr::VtIntArray>())
    {
        return ConvertVertexArrayToFaceVaryingArray(FaceVertexIndices, VertexData.UncheckedGet<pxr::VtIntArray>(), ValuesPerVertex);
    }
    else
    {
        LOG_ERROR_MESSAGE("Failed to convert vertex data to face-varying data for mesh '", m_MeshId.GetString(), "': ", VertexData.GetTypeName(), " is not supported");
        return {};
    }
}

pxr::VtValue HnMeshUtils::PackVertexNormals(const pxr::VtValue& Normals) const
{
    if (!Normals.IsHolding<pxr::VtVec3fArray>())
    {
        LOG_ERROR_MESSAGE("Failed to pack vertex normals for mesh '", m_MeshId.GetString(), "': ", Normals.GetTypeName(), " is not supported");
        return {};
    }

    const pxr::VtVec3fArray& NormalsArray = Normals.UncheckedGet<pxr::VtVec3fArray>();
    pxr::VtIntArray          PackedNormals(NormalsArray.size());
    Uint32*                  pPackedNormals = reinterpret_cast<Uint32*>(PackedNormals.data());
    for (size_t i = 0; i < NormalsArray.size(); ++i)
    {
        float3 Normal = ToFloat3(NormalsArray[i]);
        float  Length = length(Normal);
        if (Length != 0)
            Normal /= Length;

        pPackedNormals[i] = PBR_Renderer::PackVertexNormal(Normal);
    }
    return pxr::VtValue::Take(PackedNormals);
}

pxr::VtValue HnMeshUtils::PackVertexPositions(const pxr::VtValue& Points, pxr::GfVec3f& Scale, pxr::GfVec3f& Bias) const
{
    if (!Points.IsHolding<pxr::VtVec3fArray>())
    {
        LOG_ERROR_MESSAGE("Failed to pack vertex positions for mesh '", m_MeshId.GetString(), "': ", Points.GetTypeName(), " is not supported");
        return {};
    }

    const pxr::VtVec3fArray& PointsArray = Points.UncheckedGet<pxr::VtVec3fArray>();

    pxr::GfVec3f MinPos{FLT_MAX};
    pxr::GfVec3f MaxPos{-FLT_MAX};
    for (const pxr::GfVec3f& Pos : PointsArray)
    {
        MinPos[0] = std::min(MinPos[0], Pos[0]);
        MinPos[1] = std::min(MinPos[1], Pos[1]);
        MinPos[2] = std::min(MinPos[2], Pos[2]);
        MaxPos[0] = std::max(MaxPos[0], Pos[0]);
        MaxPos[1] = std::max(MaxPos[1], Pos[1]);
        MaxPos[2] = std::max(MaxPos[2], Pos[2]);
    }
    Bias  = MinPos;
    Scale = MaxPos - MinPos;

    const float3 PackScale{
        Scale[0] != 0.f ? 1.f / Scale[0] : 1.f,
        Scale[1] != 0.f ? 1.f / Scale[1] : 1.f,
        Scale[2] != 0.f ? 1.f / Scale[2] : 1.f,
    };
    const float3 PackBias{
        -MinPos[0],
        -MinPos[1],
        -MinPos[2],
    };

    pxr::VtVec2iArray PackedPositions(PointsArray.size());
    uint2*            pPackedPositions = reinterpret_cast<uint2*>(PackedPositions.data());
    const float3*     pPoints          = reinterpret_cast<const float3*>(PointsArray.data());
    for (size_t i = 0; i < PointsArray.size(); ++i)
    {
        PBR_Renderer::PackVertexPos64(pPoints[i], PackBias, PackScale, pPackedPositions[i].x, pPackedPositions[i].y);
    }

    return pxr::VtValue::Take(PackedPositions);
}

} // namespace USD

} // namespace Diligent
