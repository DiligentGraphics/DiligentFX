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
    const size_t           NumFaces          = m_Topology.GetNumFaces();
    const pxr::VtIntArray& FaceVertexIndices = m_Topology.GetFaceVertexIndices();
    const int              NumVertexIndices  = static_cast<int>(FaceVertexIndices.size());

    const pxr::VtVec3fArray* const Points = (PointsPrimvar != nullptr && PointsPrimvar->IsHolding<pxr::VtVec3fArray>()) ?
        &PointsPrimvar->UncheckedGet<pxr::VtVec3fArray>() :
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
                    Polygon[i] = ToFloat3((*Points)[Idx]);
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
    if (!dvpFailedFaces.empty())
    {
        std::stringstream ss;
        for (size_t i = 0; i < dvpFailedFaces.size(); ++i)
        {
            ss << dvpFailedFaces[i];
            if (i < dvpFailedFaces.size() - 1)
                ss << ", ";
        }
        LOG_WARNING_MESSAGE(dvpFailedFaces.size(), " faces in mesh '", m_MeshId.GetString(), "' were triangulated with potential issues: ", ss.str());
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

pxr::VtVec2iArray HnMeshUtils::ComputeEdgeIndices(bool UseFaceVertexIndices) const
{
    size_t NumEdges = 0;
    ProcessFaces(
        [&](size_t FaceId, int StartVertex, int VertCount) {
            NumEdges += VertCount;
        });

    pxr::VtVec2iArray EdgeIndices;
    EdgeIndices.reserve(NumEdges);

    ProcessFaces(
        [&](size_t FaceId, int StartVertex, int VertCount) {
            for (int v = 0; v < VertCount - 1; ++v)
                EdgeIndices.push_back({StartVertex + v, StartVertex + (v + 1)});
            EdgeIndices.push_back({StartVertex + VertCount - 1, StartVertex});
        });
    VERIFY_EXPR(EdgeIndices.size() == NumEdges);

    if (UseFaceVertexIndices)
    {
        const pxr::VtIntArray& FaceVertexIndices = m_Topology.GetFaceVertexIndices();
        const size_t           NumVertexIndices  = FaceVertexIndices.size();
        for (pxr::GfVec2i& Edge : EdgeIndices)
        {
            int& Idx0 = Edge[0];
            int& Idx1 = Edge[1];
            VERIFY_EXPR(Idx0 < NumVertexIndices && Idx1 < NumVertexIndices);
            Idx0 = FaceVertexIndices[Idx0];
            Idx1 = FaceVertexIndices[Idx1];
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
        const pxr::VtIntArray& FaceVertexIndices = m_Topology.GetFaceVertexIndices();
        const int              NumVertexIndices  = FaceVertexIndices.size();

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
pxr::VtValue ConvertVertexArrayToFaceVaryingArray(const pxr::VtIntArray& FaceVertexIndices, const pxr::VtArray<T>& VertexData, size_t ValuesPerVertex)
{
    pxr::VtArray<T> FaceData(FaceVertexIndices.size() * ValuesPerVertex);
    for (size_t i = 0; i < FaceVertexIndices.size(); ++i)
    {
        const int Idx = FaceVertexIndices[i];
        if (Idx < static_cast<int>(VertexData.size()))
        {
            for (size_t elem = 0; elem < ValuesPerVertex; ++elem)
            {
                FaceData[i * ValuesPerVertex + elem] = VertexData[Idx * ValuesPerVertex + elem];
            }
        }
    }
    return pxr::VtValue{FaceData};
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

} // namespace USD

} // namespace Diligent
