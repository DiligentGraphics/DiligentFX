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
#include <array>

// NoteL tbb.h must be included before mesh.h to avoid compilation errors in tbb headers.
#include "tbb/tbb.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/base/tf/token.h"

#include "RenderDevice.h"
#include "Buffer.h"
#include "RefCntAutoPtr.hpp"
#include "GraphicsTypesX.hpp"
#include "BasicMath.hpp"

namespace Diligent
{

namespace USD
{

/// Hydra mesh implementation in Hydrogent.
class HnMesh final : public pxr::HdMesh
{
public:
    static std::shared_ptr<HnMesh> Create(const pxr::TfToken& typeId,
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

    enum VERTEX_BUFFER_ID
    {
        VERTEX_BUFFER_ID_POSITION = 0,
        VERTEX_BUFFER_ID_NORMAL,
        VERTEX_BUFFER_ID_TEXCOORD,
        VERTEX_BUFFER_ID_COUNT
    };

    IBuffer* GetVertexBuffer(VERTEX_BUFFER_ID BufferId) const { return m_pVertexBuffers[BufferId]; }
    IBuffer* GetTriangleIndexBuffer() const { return m_pTriangleIndexBuffer; }
    IBuffer* GetEdgeIndexBuffer() const { return m_pEdgeIndexBuffer; }

    Uint32 GetNumTriangles() const { return m_NumTriangles; }
    Uint32 GetNumEdges() const { return m_NumEdges; }

    const float4x4& GetTransform() const { return m_Transform; }

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

private:
    HnMesh(const pxr::TfToken& typeId,
           const pxr::SdfPath& id,
           Uint32              UID);

    void UpdateRepr(pxr::HdSceneDelegate& SceneDelegate,
                    pxr::HdRenderParam*   RenderParam,
                    pxr::HdDirtyBits&     DirtyBits,
                    const pxr::TfToken&   ReprToken);

    void UpdateVertexPrims(pxr::HdSceneDelegate& SceneDelegate,
                           pxr::HdRenderParam*   RenderParam,
                           pxr::HdDirtyBits&     DirtyBits,
                           const pxr::TfToken&   ReprToken);

    void UpdateTopology(pxr::HdSceneDelegate& SceneDelegate,
                        pxr::HdRenderParam*   RenderParam,
                        pxr::HdDirtyBits&     DirtyBits,
                        const pxr::TfToken&   ReprToken);

private:
    const Uint32 m_UID;

    pxr::HdMeshTopology m_Topology;

    struct IndexData
    {
        pxr::VtVec3iArray         TrianglesFaceIndices;
        pxr::VtIntArray           PrimitiveParam;
        std::vector<pxr::GfVec2i> MeshEdgeIndices;
    };
    std::unique_ptr<IndexData> m_IndexData;

    std::unordered_map<pxr::TfToken, std::shared_ptr<pxr::HdBufferSource>, pxr::TfToken::HashFunctor> m_BufferSources;

    Uint32 m_NumTriangles = 0;
    Uint32 m_NumEdges     = 0;

    float4x4 m_Transform = float4x4::Identity();

    pxr::SdfPath m_MaterialId;

    RefCntAutoPtr<IBuffer> m_pTriangleIndexBuffer;
    RefCntAutoPtr<IBuffer> m_pEdgeIndexBuffer;

    std::array<RefCntAutoPtr<IBuffer>, VERTEX_BUFFER_ID_COUNT> m_pVertexBuffers;
};

} // namespace USD

} // namespace Diligent
