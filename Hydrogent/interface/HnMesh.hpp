/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/base/tf/token.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/GraphicsTypesX.hpp"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

namespace USD
{

/// Hydra mesh implementation in Hydrogent.
class HnMesh final : public pxr::HdMesh
{
public:
    static HnMesh* Create(const pxr::TfToken& typeId,
                          const pxr::SdfPath& id,
                          Uint32              UID);

    ~HnMesh();

    // Returns the set of dirty bits that should be
    // added to the change tracker for this prim, when this prim is inserted.
    virtual pxr::HdDirtyBits GetInitialDirtyBitsMask() const override final;

    /// Pull invalidated scene data and prepare/update the renderable
    /// representation.
    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits,
                      const pxr::TfToken&   ReprToken) override final;

    // Returns the names of built-in primvars, i.e. primvars that
    // are part of the core geometric schema for this prim.
    virtual const pxr::TfTokenVector& GetBuiltinPrimvarNames() const override final;

    void CommitGPUResources(IRenderDevice* pDevice);

    /// Returns face vertex buffer for the given primvar name (e.g. "points", "normals", etc.).
    /// If the buffer doesn't exist, returns nullptr.
    ///
    /// \remarks    This buffer may be indexed or non-indexed.
    IBuffer* GetFaceVertexBuffer(const pxr::TfToken& Name) const;

    /// Returns the points vertex buffer.
    ///
    /// \remarks    This buffer should be used to render points
    ///             or mesh edges.
    IBuffer* GetPointsVertexBuffer() const { return m_pPointsVertexBuffer; }

    /// Returns the face index buffer.
    ///
    /// \remarks    If not null, this buffer should be used to index
    ///             the face vertex buffers returned by GetFaceVertexBuffer().
    ///             If null, the face vertex buffers are not indexed.
    IBuffer* GetFaceIndexBuffer() const { return m_pFaceIndexBuffer; }

    /// Returns the edge index buffer.
    ///
    /// \remarks    This buffer should be used to render mesh edges.
    ///             It indexes the buffer returned by GetPointsVertexBuffer().
    IBuffer* GetEdgeIndexBuffer() const { return m_pEdgeIndexBuffer; }


    Uint32 GetNumFaceTriangles() const { return m_NumFaceTriangles; }
    Uint32 GetNumEdges() const { return m_NumEdges; }
    Uint32 GetNumPoints() const { return m_Topology.GetNumPoints(); }

    const float4x4& GetTransform() const { return m_Transform; }
    const float4&   GetDisplayColor() const { return m_DisplayColor; }

    const pxr::SdfPath& GetMaterialId() const { return m_MaterialId; }

    Uint32 GetUID() const { return m_UID; }

protected:
    // This callback from Rprim gives the prim an opportunity to set
    // additional dirty bits based on those already set.
    virtual pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override final;

    // Initialize the given representation of this Rprim.
    // This is called prior to syncing the prim, the first time the repr
    // is used.
    virtual void _InitRepr(const pxr::TfToken& reprToken, pxr::HdDirtyBits* dirtyBits) override final;

    void UpdateVertexBuffers(const RenderDeviceX_N& Device);
    void UpdateIndexBuffer(const RenderDeviceX_N& Device);

    void UpdateReprMaterialTags(pxr::HdSceneDelegate* SceneDelegate,
                                pxr::HdRenderParam*   RenderParam);

private:
    HnMesh(const pxr::TfToken& typeId,
           const pxr::SdfPath& id,
           Uint32              UID);

    void UpdateRepr(pxr::HdSceneDelegate& SceneDelegate,
                    pxr::HdRenderParam*   RenderParam,
                    pxr::HdDirtyBits&     DirtyBits,
                    const pxr::TfToken&   ReprToken);

    bool UpdateVertexPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                              pxr::HdRenderParam*   RenderParam,
                              pxr::HdDirtyBits&     DirtyBits,
                              const pxr::TfToken&   ReprToken);

    void UpdateFaceVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                   pxr::HdRenderParam*   RenderParam,
                                   pxr::HdDirtyBits&     DirtyBits,
                                   const pxr::TfToken&   ReprToken);

    void UpdateConstantPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                pxr::HdRenderParam*   RenderParam,
                                pxr::HdDirtyBits&     DirtyBits,
                                const pxr::TfToken&   ReprToken);

    // Converts vertex primvar sources into face-varying primvar sources.
    void ConvertVertexPrimvarSources();

    void UpdateTopology(pxr::HdSceneDelegate& SceneDelegate,
                        pxr::HdRenderParam*   RenderParam,
                        pxr::HdDirtyBits&     DirtyBits,
                        const pxr::TfToken&   ReprToken);

    void AddGeometrySubsetDrawItems(const pxr::HdMeshReprDesc& ReprDesc,
                                    size_t                     NumGeomSubsets,
                                    pxr::HdRepr&               Repr);
    void UpdateDrawItemsForGeometrySubsets(pxr::HdSceneDelegate& SceneDelegate,
                                           pxr::HdRenderParam*   RenderParam);

private:
    const Uint32 m_UID;

    pxr::HdMeshTopology m_Topology;

    struct IndexData
    {
        pxr::VtVec3iArray         TrianglesFaceIndices;
        std::vector<pxr::GfVec2i> MeshEdgeIndices;
    };
    std::unique_ptr<IndexData> m_IndexData;

    struct VertexData
    {
        using BufferSourceMapType = std::unordered_map<pxr::TfToken, std::shared_ptr<pxr::HdBufferSource>, pxr::TfToken::HashFunctor>;
        BufferSourceMapType VertexSources;
        BufferSourceMapType FaceSources;
    };
    std::unique_ptr<VertexData> m_VertexData;


    Uint32 m_NumFaceTriangles = 0;
    Uint32 m_NumEdges         = 0;

    float4x4 m_Transform    = float4x4::Identity();
    float4   m_DisplayColor = {1, 1, 1, 1};

    pxr::SdfPath m_MaterialId;

    RefCntAutoPtr<IBuffer> m_pFaceIndexBuffer;
    RefCntAutoPtr<IBuffer> m_pEdgeIndexBuffer;
    RefCntAutoPtr<IBuffer> m_pPointsVertexBuffer;

    std::unordered_map<pxr::TfToken, RefCntAutoPtr<IBuffer>, pxr::TfToken::HashFunctor> m_FaceVertexBuffers;
};

} // namespace USD

} // namespace Diligent
