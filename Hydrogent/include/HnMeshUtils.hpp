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

#pragma once

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/meshTopology.h"

namespace Diligent
{

namespace USD
{

class HnMeshUtils final
{
public:
    HnMeshUtils(const pxr::HdMeshTopology& Topology, const pxr::SdfPath& MeshId) :
        m_Topology{Topology},
        m_MeshId{MeshId}
    {}
    ~HnMeshUtils();

    /// Triangulates the mesh and returns the triangle indices and the start of each subset.
    ///
    /// \param[in]  UseFaceVertexIndices - Whether to use face vertex indices.
    /// \param[in]  PointsPrimvar        - Pointer to the points primvar data.
    /// \param[out] TriangleIndices      - The triangle indices.
    /// \param[out] SubsetStart          - The index of the first triangle in each subset.
    ///                                    The last element is the total number of triangles.
    ///
    /// Example:
    ///     Input:
    ///         FaceVertexCounts = {4, 4}
    ///         FaceVertexIndices= {0, 1, 2, 3,  3, 2, 4, 5}
    ///
    ///         V1________V2_______V4
    ///          |1      2|5      6|
    ///          |        |        |
    ///          |        |        |
    ///          |0______3|4______7|
    ///         V0        V3       V5
    ///
    ///     Output:
    ///         UseFaceVertexIndices == false
    ///             TriangleIndices = {0, 1, 2,  0, 2, 3,  4, 5, 6,  4, 6, 7}
    ///             SubsetStart     = {0, 2, 4}
    ///
    ///         UseFaceVertexIndices == true
    ///             TriangleIndices = {0, 1, 2,  0, 2, 3,  3, 2, 4,  3, 4, 5}
    ///             SubsetStart     = {0, 2, 4}
    ///
    void Triangulate(bool                UseFaceVertexIndices,
                     const pxr::VtValue* PointsPrimvar,
                     pxr::VtVec3iArray&  TriangleIndices,
                     pxr::VtIntArray&    SubsetStart) const;


    /// Computes the edge indices.
    ///
    /// \param[in] UseFaceVertexIndices - Whether to use face vertex indices.
    /// \return The edge indices.
    ///
    /// Example:
    ///     Input:
    ///         FaceVertexCounts = {4, 4}
    ///         FaceVertexIndices= {0, 1, 2, 3,  3, 2, 4, 5}
    ///
    ///         V1________V2_______V4
    ///          |1      2|5      6|
    ///          |        |        |
    ///          |        |        |
    ///          |0______3|4______7|
    ///         V0        V3       V5
    ///
    ///     Output:
    ///         UseFaceVertexIndices == false
    /// 		    EdgeIndices = {0, 1,  1, 2,  2, 3,  3, 0,  4, 5,  5, 6,  6, 7,  7, 4}
    ///
    ///         UseFaceVertexIndices == true
    /// 		    EdgeIndices = {0, 1,  1, 2,  2, 3,  3, 0,  3, 2,  2, 4,  4, 5,  5, 3}
    pxr::VtVec2iArray ComputeEdgeIndices(bool UseFaceVertexIndices) const;


    /// Computes the point indices.
    ///
    /// \param[in] ConvertToFaceVarying - Whether to convert points to face-varying.
    /// \return The point indices.
    ///
    /// Example:
    ///     Input:
    ///         FaceVertexCounts = {4, 4}
    ///         FaceVertexIndices= {0, 1, 2, 3,  3, 2, 4, 5}
    ///
    ///         V1________V2_______V4
    ///          |1      2|5      6|
    ///          |        |        |
    ///          |        |        |
    ///          |0______3|4______7|
    ///         V0        V3       V5
    ///
    ///     Output:
    ///         ConvertToFaceVarying == false
    /// 		    PointIndices = {0, 1, 2, 3, 4, 5}
    ///
    ///         ConvertToFaceVarying == true
    /// 		    PointIndices = {0, 1, 2, 3, 6, 7}
    pxr::VtIntArray ComputePointIndices(bool ConvertToFaceVarying) const;


    /// Converts vertex/varying primvar data to face-varying primvar data.
    ///
    /// \param[in] VertexData      - The vertex/varying primvar data.
    /// \param[in] ValuesPerVertex - The number of values per vertex.
    /// \return The face-varying primvar data.
    ///
    /// Example:
    ///     Input:
    ///         VertexData       = {V0, V1, V2, V3, V4, V5}
    ///         FaceVertexCounts = {4, 4}
    ///         FaceVertexIndices= {0, 1, 2, 3,  3, 2, 4, 5}
    ///
    ///         V1________V2_______V4
    ///          |1      2|5      6|
    ///          |        |        |
    ///          |        |        |
    ///          |0______3|4______7|
    ///         V0        V3       V5
    ///
    ///     Output:
    ///         FaceVaryingData = {V0, V1, V2, V3,  V3, V2, V4, V5}
    ///
    ///         V1_______V2 V2_______V4
    ///          |        | |        |
    ///          |        | |        |
    ///          |        | |        |
    ///          |________| |________|
    ///         V0       V3 V3       V5
    ///
    pxr::VtValue ConvertVertexPrimvarToFaceVarying(const pxr::VtValue& VertexData, size_t ValuesPerVertex = 1) const;

private:
    template <typename HandleFaceType>
    void ProcessFaces(HandleFaceType&& HandleFace) const;

private:
    const pxr::HdMeshTopology& m_Topology;
    const pxr::SdfPath&        m_MeshId;
};

} // namespace USD

} // namespace Diligent
