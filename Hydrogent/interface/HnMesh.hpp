/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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

#include "HnGeometryPool.hpp"

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
class HnSkinningComputation;

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

    struct Components
    {
        struct Transform
        {
            float4x4 Matrix   = float4x4::Identity();
            float3   PosScale = {1, 1, 1};
            float3   PosBias  = {0, 0, 0};
        };

        struct DisplayColor
        {
            float4 Val = {1, 1, 1, 1};
        };

        struct Visibility
        {
            bool Val = true;
        };

        struct Skinning
        {
            const HnSkinningComputation* Computation = nullptr;

            float4x4 GeomBindXform = float4x4::Identity();

            explicit operator bool() const { return Computation != nullptr; }
        };
    };

    CULL_MODE GetCullMode() const { return m_CullMode != CULL_MODE_UNDEFINED ? m_CullMode : CULL_MODE_BACK; }

    Uint32 GetUID() const { return m_UID; }

    Uint32 GetGeometryVersion() const { return m_GeometryVersion; }
    Uint32 GetMaterialVersion() const { return m_MaterialVersion; }

    entt::entity GetEntity() const { return m_Entity; }

    static Uint32 GetCacheResourceVersion(const HnRenderDelegate& RenderDelegate);

protected:
    // This callback from Rprim gives the prim an opportunity to set
    // additional dirty bits based on those already set.
    virtual pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override final;

    // Initializes the given representation of this Rprim.
    // This is called prior to syncing the prim, the first time the repr
    // is used.
    virtual void _InitRepr(const pxr::TfToken& reprToken, pxr::HdDirtyBits* dirtyBits) override final;

    void UpdateReprMaterials(const pxr::HdRenderIndex& RenderIndex,
                             pxr::HdRenderParam*       RenderParam);

private:
    HnMesh(const pxr::TfToken& typeId,
           const pxr::SdfPath& id,
           HnRenderDelegate&   RenderDelegate,
           Uint32              UID,
           entt::entity        Entity);

    class HdMeshTopologyWrapper;
    struct StagingIndexData;
    struct StagingVertexData;

    bool UpdateRepr(pxr::HdSceneDelegate& SceneDelegate,
                    pxr::HdRenderParam*   RenderParam,
                    pxr::HdDirtyBits&     DirtyBits,
                    const pxr::TfToken&   ReprToken);

    struct PrimvarsInfo
    {
        // The total number of supported primvars
        Uint32 Count = 0;

        // Dirty primvars arranged by name.
        // Typically, the name is the same as the primvar descriptor name,
        // but it may be different if the primvar is found using the role.
        std::unordered_map<pxr::TfToken, pxr::HdPrimvarDescriptor, pxr::TfToken::HashFunctor> Dirty;

        // Computation primvars
        pxr::HdExtComputationPrimvarDescriptorVector ExtComp;

        void AddDirtyPrimvar(pxr::HdDirtyBits&               DirtyBits,
                             const pxr::SdfPath&             Id,
                             const pxr::TfToken&             Name,
                             const pxr::HdPrimvarDescriptor& PrimDesc,
                             const pxr::TfToken&             Role);
    };
    void GetPrimvarsInfo(pxr::HdSceneDelegate& SceneDelegate,
                         pxr::HdDirtyBits&     DirtyBits,
                         PrimvarsInfo&         VertexPrimvarsInfo,
                         PrimvarsInfo&         FacePrimvarsInfo) const;

    void UpdateVertexAndVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                        pxr::HdRenderParam*   RenderParam,
                                        pxr::HdDirtyBits&     DirtyBits,
                                        const pxr::TfToken&   ReprToken,
                                        const PrimvarsInfo&   VertexPrimvarsInfo,
                                        StagingVertexData&    StagingVerts);

    void UpdateFaceVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                   pxr::HdRenderParam*   RenderParam,
                                   pxr::HdDirtyBits&     DirtyBits,
                                   const pxr::TfToken&   ReprToken,
                                   const PrimvarsInfo&   FacePrimvarsInfo,
                                   StagingVertexData&    StagingVerts);

    void UpdateConstantPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                pxr::HdRenderParam*   RenderParam,
                                pxr::HdDirtyBits&     DirtyBits,
                                const pxr::TfToken&   ReprToken);

    bool AddStagingBufferSourceForPrimvar(HnRenderDelegate*    RenderDelegate,
                                          StagingVertexData&   StagingVerts,
                                          const pxr::TfToken&  Name,
                                          pxr::VtValue         Primvar,
                                          pxr::HdInterpolation Interpolation,
                                          int                  ValuesPerElement = 1);

    void PreprocessPrimvar(HnRenderDelegate* RenderDelegate, const pxr::TfToken& Name, pxr::VtValue& Primvar);

    bool AddJointInfluencesStagingBufferSource(const pxr::VtValue& NumInfluencesPerComponentVal,
                                               const pxr::VtValue& InfluencesVal,
                                               StagingVertexData&  StagingVerts);

    void UpdateSkinningPrimvars(pxr::HdSceneDelegate&                         SceneDelegate,
                                pxr::HdRenderParam*                           RenderParam,
                                pxr::HdDirtyBits&                             DirtyBits,
                                const pxr::TfToken&                           ReprToken,
                                const pxr::HdExtComputationPrimvarDescriptor& SkinningCompPrimDesc,
                                StagingVertexData&                            StagingVerts);

    struct GeometrySubsetRange
    {
        Uint32 StartIndex = 0;
        Uint32 NumIndices = 0;
    };

    void UpdateIndexData(StagingIndexData& StagingInds, const pxr::HdMeshTopology& Topology, const pxr::VtValue& Points, bool UseStripTopology);

    void UpdateTopology(pxr::HdSceneDelegate&  SceneDelegate,
                        pxr::HdRenderParam*    RenderParam,
                        pxr::HdDirtyBits&      DirtyBits,
                        const pxr::TfToken&    ReprToken,
                        HdMeshTopologyWrapper& MeshTopology);

    void AddGeometrySubsetDrawItems(const pxr::HdRenderIndex&  RenderIndex,
                                    const pxr::HdMeshReprDesc& ReprDesc,
                                    pxr::HdRepr&               Repr);
    void UpdateDrawItemsForGeometrySubsets(pxr::HdSceneDelegate& SceneDelegate,
                                           pxr::HdRenderParam*   RenderParam);

    bool UpdateGeometry(pxr::HdSceneDelegate& SceneDelegate,
                        pxr::HdRenderParam*   RenderParam,
                        pxr::HdDirtyBits&     DirtyBits,
                        const pxr::TfToken&   ReprToken);

    void UpdateDrawItemGpuGeometry(HnRenderDelegate& RenderDelegate);
    void UpdateDrawItemGpuTopology(HnRenderDelegate& RenderDelegate);

    template <typename HandleDrawItemFuncType, typename HandleGeomSubsetDrawItemFuncType>
    void ProcessDrawItems(HandleDrawItemFuncType&&           HandleDrawItem,
                          HandleGeomSubsetDrawItemFuncType&& HandleGeomSubsetDrawItem);

    void UpdateCullMode(const float4x4&     Transform,
                        pxr::HdRenderParam* RenderParam);

    void Invalidate();

private:
    const Uint32       m_UID;
    const entt::entity m_Entity;

    struct Topology
    {
        size_t NumPoints       = 0;
        size_t NumFaceVaryings = 0;

        size_t ExpectedNumTriangleIndices = 0;
        size_t ExpectedNumEdgeIndices     = 0;
        size_t ExpectedNumPointIndices    = 0;

        struct Subset
        {
            pxr::HdGeomSubset::Type Type       = pxr::HdGeomSubset::TypeFaceSet;
            pxr::SdfPath            Id         = {};
            pxr::SdfPath            MaterialId = {};

            bool operator==(const Subset& rhs) const
            {
                return Type == rhs.Type && Id == rhs.Id && MaterialId == rhs.MaterialId;
            }
            bool operator!=(const Subset& rhs) const { return !(*this == rhs); }
        };
        std::vector<Subset> Subsets;

        size_t GetNumElements(bool HasFaceVaryingPrimvars) const
        {
            return HasFaceVaryingPrimvars ? NumFaceVaryings : NumPoints;
        }

        void Update(const pxr::HdMeshTopology& MeshTopology, const pxr::SdfPath& MeshId, const HnRenderDelegate* RenderDelegate);
        bool UpdateSubsets(const pxr::HdMeshTopology& MeshTopology);
    };
    Topology m_Topology;

    struct IndexData
    {
        std::vector<GeometrySubsetRange> Subsets;

        std::shared_ptr<HnGeometryPool::IndexHandle> Faces;
        std::shared_ptr<HnGeometryPool::IndexHandle> Edges;
        std::shared_ptr<HnGeometryPool::IndexHandle> Points;
    };
    IndexData m_IndexData;

    std::shared_ptr<HnGeometryPool::VertexHandle> m_VertexHandle;

    bool      m_HasFaceVaryingPrimvars = false;
    bool      m_IsDoubleSided          = false;
    CULL_MODE m_CullMode               = CULL_MODE_UNDEFINED;

    std::atomic<Uint32> m_GeometryVersion{0};
    std::atomic<Uint32> m_MaterialVersion{0};
    std::atomic<Uint32> m_SkinningPrimvarsVersion{0};
    Uint32              m_IndexPoolVersion  = ~0u;
    Uint32              m_VertexPoolVersion = ~0u;
    std::atomic<bool>   m_DrawItemGpuTopologyDirty{false};
    std::atomic<bool>   m_DrawItemGpuGeometryDirty{false};

    float4x4 m_SkelLocalToPrimLocal = float4x4::Identity();
};

} // namespace USD

} // namespace Diligent
