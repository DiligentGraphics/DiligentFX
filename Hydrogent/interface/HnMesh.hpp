/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include <memory>
#include <unordered_map>
#include <map>
#include <vector>

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/base/tf/token.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/GraphicsTypesX.hpp"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/STDAllocator.hpp"

#include "entt/entity/entity.hpp"

namespace Diligent
{

struct IVertexPoolAllocation;
struct IBufferSuballocation;

namespace USD
{

class HnRenderDelegate;

/// Hydra mesh implementation in Hydrogent.
class HnMesh final : public pxr::HdMesh
{
public:
    static HnMesh* Create(const pxr::TfToken& typeId,
                          const pxr::SdfPath& id,
                          HnRenderDelegate&   RenderDelegate,
                          Uint32              UID,
                          entt::entity        Entity);

    ~HnMesh();

    // Returns the set of dirty bits that should be
    // added to the change tracker for this prim, when this prim is inserted.
    virtual pxr::HdDirtyBits GetInitialDirtyBitsMask() const override final;

    // Pulls invalidated scene data and prepares/updates the renderable
    // representation.
    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits,
                      const pxr::TfToken&   ReprToken) override final;

    // Returns the names of built-in primvars, i.e. primvars that
    // are part of the core geometric schema for this prim.
    virtual const pxr::TfTokenVector& GetBuiltinPrimvarNames() const override final;

    void CommitGPUResources(HnRenderDelegate& RenderDelegate);

    /// Returns the vertex buffer for the given primvar name (e.g. "points", "normals", etc.).
    /// If the buffer doesn't exist, returns nullptr.
    IBuffer* GetVertexBuffer(const pxr::TfToken& Name) const;

    /// Returns the face index buffer.
    ///
    /// \remarks    The index buffer contains the triangle list.
    IBuffer* GetFaceIndexBuffer() const { return m_IndexData.Faces; }

    /// Returns the edges index buffer.
    ///
    /// \remarks    The index buffer contains the line list.
    IBuffer* GetEdgeIndexBuffer() const { return m_IndexData.Edges; }

    /// Returns the points index buffer.
    ///
    /// \remarks    The index buffer contains the point list.
    IBuffer* GetPointsIndexBuffer() const { return m_IndexData.Points; }

    Uint32 GetNumFaceTriangles() const { return m_IndexData.NumFaceTriangles; }
    Uint32 GetNumEdges() const { return m_IndexData.NumEdges; }
    Uint32 GetNumPoints() const { return m_Topology.GetNumPoints(); }

    /// Returns the start index of the face data in the index buffer.
    ///
    /// \remarks    This value should be used as the start index location
    ///             for the face drawing commands.
    Uint32 GetFaceStartIndex() const { return m_IndexData.FaceStartIndex; }

    /// Returns the start index of the edges data in the index buffer.
    ///
    /// \remarks    This value should be used as the start index location
    ///             for the mesh edges drawing commands.
    Uint32 GetEdgeStartIndex() const { return m_IndexData.EdgeStartIndex; }

    /// Returns the start index of the points data in the index buffer.
    ///
    /// \remarks    This value should be used as the start index location
    ///             for the points drawing commands.
    Uint32 GetPointsStartIndex() const { return m_IndexData.PointsStartIndex; }

    struct Components
    {
        struct Transform
        {
            float4x4 Val = float4x4::Identity();
        };

        struct DisplayColor
        {
            float4 Val = {1, 1, 1, 1};
        };

        struct Visibility
        {
            bool Val = true;
        };
    };

    bool GetIsDoubleSided() const { return m_IsDoubleSided; }

    Uint32 GetUID() const { return m_UID; }

    Uint32 GetGeometryVersion() const { return m_GeometryVersion; }
    Uint32 GetMaterialVersion() const { return m_MaterialVersion; }

    entt::entity GetEntity() const { return m_Entity; }

protected:
    // This callback from Rprim gives the prim an opportunity to set
    // additional dirty bits based on those already set.
    virtual pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override final;

    // Initializes the given representation of this Rprim.
    // This is called prior to syncing the prim, the first time the repr
    // is used.
    virtual void _InitRepr(const pxr::TfToken& reprToken, pxr::HdDirtyBits* dirtyBits) override final;

    void UpdateVertexBuffers(HnRenderDelegate& RenderDelegate);
    void UpdateIndexBuffer(HnRenderDelegate& RenderDelegate);
    void AllocatePooledResources(pxr::HdSceneDelegate& SceneDelegate,
                                 pxr::HdRenderParam*   RenderParam);

    void UpdateReprMaterials(pxr::HdSceneDelegate* SceneDelegate,
                             pxr::HdRenderParam*   RenderParam);

private:
    HnMesh(const pxr::TfToken& typeId,
           const pxr::SdfPath& id,
           HnRenderDelegate&   RenderDelegate,
           Uint32              UID,
           entt::entity        Entity);

    void UpdateRepr(pxr::HdSceneDelegate& SceneDelegate,
                    pxr::HdRenderParam*   RenderParam,
                    pxr::HdDirtyBits&     DirtyBits,
                    const pxr::TfToken&   ReprToken);

    void UpdateVertexAndVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                        pxr::HdRenderParam*   RenderParam,
                                        pxr::HdDirtyBits&     DirtyBits,
                                        const pxr::TfToken&   ReprToken);

    using FaceSourcesMapType = std::unordered_map<pxr::TfToken, std::shared_ptr<pxr::HdBufferSource>, pxr::TfToken::HashFunctor>;
    void UpdateFaceVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                   pxr::HdRenderParam*   RenderParam,
                                   pxr::HdDirtyBits&     DirtyBits,
                                   const pxr::TfToken&   ReprToken,
                                   FaceSourcesMapType&   FaceSources);

    void UpdateConstantPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                pxr::HdRenderParam*   RenderParam,
                                pxr::HdDirtyBits&     DirtyBits,
                                const pxr::TfToken&   ReprToken);

    void GenerateSmoothNormals();

    // Converts vertex primvar sources into face-varying primvar sources.
    void ConvertVertexPrimvarSources(FaceSourcesMapType&& FaceSources);

    void UpdateTopology(pxr::HdSceneDelegate& SceneDelegate,
                        pxr::HdRenderParam*   RenderParam,
                        pxr::HdDirtyBits&     DirtyBits,
                        const pxr::TfToken&   ReprToken);

    void AddGeometrySubsetDrawItems(const pxr::HdMeshReprDesc& ReprDesc,
                                    pxr::HdRepr&               Repr);
    void UpdateDrawItemsForGeometrySubsets(pxr::HdSceneDelegate& SceneDelegate,
                                           pxr::HdRenderParam*   RenderParam);

    void UpdateDrawItemGpuGeometry(HnRenderDelegate& RenderDelegate);
    void UpdateDrawItemGpuTopology();

    template <typename HandleDrawItemFuncType, typename HandleGeomSubsetDrawItemFuncType>
    void ProcessDrawItems(HandleDrawItemFuncType&&           HandleDrawItem,
                          HandleGeomSubsetDrawItemFuncType&& HandleGeomSubsetDrawItem);

    void Invalidate();

private:
    const Uint32       m_UID;
    const entt::entity m_Entity;

    pxr::HdMeshTopology m_Topology;

    struct StagingIndexData
    {
        pxr::VtVec3iArray         TrianglesFaceIndices;
        std::vector<pxr::GfVec2i> MeshEdgeIndices;
        std::vector<Uint32>       PointIndices;
    };
    std::unique_ptr<StagingIndexData> m_StagingIndexData;

    struct StagingVertexData
    {
        // Use map to keep buffer sources sorted by name
        std::map<pxr::TfToken, std::shared_ptr<pxr::HdBufferSource>> Sources;
    };
    std::unique_ptr<StagingVertexData> m_StagingVertexData;

    struct IndexData
    {
        Uint32 NumFaceTriangles = 0;
        Uint32 NumEdges         = 0;
        Uint32 FaceStartIndex   = 0;
        Uint32 EdgeStartIndex   = 0;
        Uint32 PointsStartIndex = 0;

        RefCntAutoPtr<IBuffer> Faces;
        RefCntAutoPtr<IBuffer> Edges;
        RefCntAutoPtr<IBuffer> Points;

        RefCntAutoPtr<IBufferSuballocation> FaceAllocation;
        RefCntAutoPtr<IBufferSuballocation> EdgeAllocation;
        RefCntAutoPtr<IBufferSuballocation> PointsAllocation;
    };
    IndexData m_IndexData;

    struct VertexData
    {
        RefCntAutoPtr<IVertexPoolAllocation> PoolAllocation;

        // Buffer name to vertex pool element index (e.g. "normals" -> 0, "points" -> 1, etc.)
        std::unordered_map<pxr::TfToken, Uint32, pxr::TfToken::HashFunctor> NameToPoolIndex;

        // Buffer name to buffer
        std::unordered_map<pxr::TfToken, RefCntAutoPtr<IBuffer>, pxr::TfToken::HashFunctor> Buffers;
    };
    VertexData m_VertexData;

    bool m_IsDoubleSided = false;

    std::atomic<Uint32> m_GeometryVersion{0};
    std::atomic<Uint32> m_MaterialVersion{0};
};

} // namespace USD

} // namespace Diligent
