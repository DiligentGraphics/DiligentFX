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


#include "HnMesh.hpp"
#include "HnTokens.hpp"
#include "HnMaterial.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderParam.hpp"
#include "HnRenderPass.hpp"
#include "HnDrawItem.hpp"
#include "HnExtComputation.hpp"
#include "HnMeshUtils.hpp"
#include "HnGeometryPool.hpp"
#include "Computations/HnSkinningComputation.hpp"
#include "GfTypeConversions.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"
#include "GLTFResourceManager.hpp"
#include "EngineMemory.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/tf/smallVector.h"
#include "pxr/imaging/hd/vtBufferSource.h"
#include "pxr/imaging/hd/primvarSchema.h"

namespace Diligent
{

namespace USD
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnSkinningPrivateTokens,
    (restPoints)
    (skinnedPoints)
    (numInfluencesPerComponent)
    (influences)
    (geomBindXform)
);
// clang-format on

HnMesh* HnMesh::Create(pxr::TfToken const& typeId,
                       pxr::SdfPath const& id,
                       HnRenderDelegate&   RenderDelegate,
                       Uint32              UID,
                       entt::entity        Entity)
{
    return new HnMesh{typeId, id, RenderDelegate, UID, Entity};
}

HnMesh::HnMesh(pxr::TfToken const& typeId,
               pxr::SdfPath const& id,
               HnRenderDelegate&   RenderDelegate,
               Uint32              UID,
               entt::entity        Entity) :
    pxr::HdMesh{id},
    m_UID{UID},
    m_Entity{Entity}
{
    entt::registry& Regisgtry = RenderDelegate.GetEcsRegistry();
    Regisgtry.emplace<Components::Transform>(m_Entity);
    Regisgtry.emplace<Components::DisplayColor>(m_Entity);
    Regisgtry.emplace<Components::Visibility>(m_Entity, _sharedData.visible);
    Regisgtry.emplace<Components::Skinning>(m_Entity);
}

HnMesh::~HnMesh()
{
}

pxr::HdDirtyBits HnMesh::GetInitialDirtyBitsMask() const
{
    // Set all bits except the varying flag
    return pxr::HdChangeTracker::AllSceneDirtyBits & ~pxr::HdChangeTracker::Varying;
}

static void SetDrawItemMaterial(const pxr::HdRenderIndex& RenderIndex, HnDrawItem& DrawItem, const pxr::SdfPath& MaterialId)
{
    const pxr::HdSprim* pMaterial = RenderIndex.GetSprim(pxr::HdPrimTypeTokens->material, MaterialId);
    if (pMaterial == nullptr)
    {
        pMaterial = RenderIndex.GetFallbackSprim(pxr::HdPrimTypeTokens->material);
        VERIFY(pMaterial != nullptr, "Unable to get fallback material. This is unexpected as default material is initialized in the render delegate.");
    }
    if (pMaterial != nullptr)
    {
        DrawItem.SetMaterial(*static_cast<const HnMaterial*>(pMaterial));
    }
}


template <typename HandleDrawItemFuncType, typename HandleGeomSubsetDrawItemFuncType>
void HnMesh::ProcessDrawItems(HandleDrawItemFuncType&&           HandleDrawItem,
                              HandleGeomSubsetDrawItemFuncType&& HandleGeomSubsetDrawItem)
{
    const size_t NumGeomSubsets = m_Topology.Subsets.size();
    for (auto const& token_repr : _reprs)
    {
        const pxr::TfToken&        Token = token_repr.first;
        pxr::HdReprSharedPtr       Repr  = token_repr.second;
        _MeshReprConfig::DescArray Descs = _GetReprDesc(Token);

        size_t DrawItemIdx       = 0;
        size_t GeomSubsetDescIdx = 0;
        for (const pxr::HdMeshReprDesc& Desc : Descs)
        {
            if (Desc.geomStyle == pxr::HdMeshGeomStyleInvalid)
                continue;

            {
                pxr::HdDrawItem* Item = Repr->GetDrawItem(DrawItemIdx++);
                HandleDrawItem(static_cast<HnDrawItem&>(*Item));
            }

            // Update geom subset draw items if they exist
            if (Desc.geomStyle != pxr::HdMeshGeomStylePoints)
            {
                for (size_t i = 0; i < NumGeomSubsets; ++i)
                {
                    if (pxr::HdDrawItem* Item = Repr->GetDrawItemForGeomSubset(GeomSubsetDescIdx, NumGeomSubsets, i))
                    {
                        HandleGeomSubsetDrawItem(m_Topology.Subsets[i], static_cast<HnDrawItem&>(*Item));
                    }
                }
                ++GeomSubsetDescIdx;
            }
        }
    }
}

void HnMesh::UpdateReprMaterials(const pxr::HdRenderIndex& RenderIndex,
                                 pxr::HdRenderParam*       RenderParam)
{
    ProcessDrawItems(
        [&](HnDrawItem& DrawItem) {
            SetDrawItemMaterial(RenderIndex, DrawItem, GetMaterialId());
        },
        [&](const Topology::Subset& Subset, HnDrawItem& DrawItem) {
            SetDrawItemMaterial(RenderIndex, DrawItem, Subset.MaterialId);
        });
}

void HnMesh::UpdateCullMode(const float4x4&     Transform,
                            pxr::HdRenderParam* RenderParam)
{
    CULL_MODE CullMode = CULL_MODE_UNDEFINED;
    if (m_IsDoubleSided)
    {
        CullMode = CULL_MODE_NONE;
    }
    else
    {
        const float Det = Transform.Determinant();
        // Do NOT use Delegate->GetTransform() as it is very expensive and we have the matrix already
        //CullMode = Delegate->GetTransform(Id).IsRightHanded() ? CULL_MODE_BACK : CULL_MODE_FRONT;
        CullMode = Det > 0 ? CULL_MODE_BACK : CULL_MODE_FRONT;
    }

    if (m_CullMode != CullMode)
    {
        m_CullMode = CullMode;
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshCulling);
    }
}

void HnMesh::Sync(pxr::HdSceneDelegate* Delegate,
                  pxr::HdRenderParam*   RenderParam,
                  pxr::HdDirtyBits*     DirtyBits,
                  const pxr::TfToken&   ReprToken)
{
    if (*DirtyBits == pxr::HdChangeTracker::Clean)
        return;

    const pxr::SdfPath& Id = GetId();

    bool UpdateMaterials = false;
    if (*DirtyBits & pxr::HdChangeTracker::DirtyMaterialId)
    {
        const pxr::SdfPath& MaterialId = Delegate->GetMaterialId(Id);
        if (GetMaterialId() != MaterialId)
        {
            SetMaterialId(MaterialId);
        }
        UpdateMaterials = true;
        *DirtyBits &= ~pxr::HdChangeTracker::DirtyMaterialId;
    }

    if ((*DirtyBits & pxr::HdChangeTracker::DirtyDisplayStyle) != 0)
    {
        UpdateMaterials = true;
        *DirtyBits &= ~pxr::HdChangeTracker::DirtyDisplayStyle;
    }

    const bool ReprUpdated = UpdateRepr(*Delegate, RenderParam, *DirtyBits, ReprToken);

    // Update materials after the repr is updated, so that the draw items are created
    if (UpdateMaterials)
    {
        UpdateReprMaterials(Delegate->GetRenderIndex(), RenderParam);
    }

    const bool DirtyDoubleSided = (*DirtyBits & pxr::HdChangeTracker::DirtyDoubleSided) != 0;
    if (DirtyDoubleSided)
    {
        m_IsDoubleSided = Delegate->GetDoubleSided(Id);
        *DirtyBits &= ~pxr::HdChangeTracker::DirtyDoubleSided;
    }

    if (DirtyDoubleSided || m_CullMode == CULL_MODE_UNDEFINED)
    {
        HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(Delegate->GetRenderIndex().GetRenderDelegate());
        entt::registry&   Registry       = RenderDelegate->GetEcsRegistry();
        const float4x4&   Transform      = Registry.get<Components::Transform>(m_Entity).Matrix;
        UpdateCullMode(Transform, RenderParam);
    }

    if ((DirtyDoubleSided || UpdateMaterials) && RenderParam != nullptr)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshMaterial);
        ++m_MaterialVersion;
    }

    if (!ReprUpdated)
    {
        // We can't mark the prim as dirty here, because OpenUSD marks the prim as clean after Sync() is called.
        // We mark the prim as dirty in HnBeginFrameTask::Prepare
        static_cast<HnRenderParam*>(RenderParam)->AddDirtyRPrim(Id, *DirtyBits);
    }

    *DirtyBits &= ~pxr::HdChangeTracker::AllSceneDirtyBits;
}

pxr::TfTokenVector const& HnMesh::GetBuiltinPrimvarNames() const
{
    static const pxr::TfTokenVector names{};
    return names;
}

pxr::HdDirtyBits HnMesh::_PropagateDirtyBits(pxr::HdDirtyBits bits) const
{
    return bits;
}

void HnMesh::AddGeometrySubsetDrawItems(const pxr::HdRenderIndex& RenderIndex, const pxr::HdMeshReprDesc& ReprDesc, pxr::HdRepr& Repr)
{
    if (ReprDesc.geomStyle == pxr::HdMeshGeomStyleInvalid ||
        ReprDesc.geomStyle == pxr::HdMeshGeomStylePoints)
        return;

    for (const Topology::Subset& Subset : m_Topology.Subsets)
    {
        std::unique_ptr<HnDrawItem> Item = std::make_unique<HnDrawItem>(_sharedData, *this);
        SetDrawItemMaterial(RenderIndex, *Item, Subset.MaterialId);
        Repr.AddGeomSubsetDrawItem(std::move(Item));
    }
}

void HnMesh::_InitRepr(const pxr::TfToken& ReprToken, pxr::HdDirtyBits* DirtyBits)
{
    auto it = std::find_if(_reprs.begin(), _reprs.end(), _ReprComparator(ReprToken));
    if (it != _reprs.end())
        return;

    _reprs.emplace_back(ReprToken, std::make_shared<pxr::HdRepr>());
    auto& Repr = *_reprs.back().second;

    // set dirty bit to say we need to sync a new repr
    *DirtyBits |= pxr::HdChangeTracker::NewRepr;

    _MeshReprConfig::DescArray ReprDescs = _GetReprDesc(ReprToken);
    // All main draw items must be added before any geometry subset draw item.
    for (const pxr::HdMeshReprDesc& Desc : ReprDescs)
    {
        if (Desc.geomStyle == pxr::HdMeshGeomStyleInvalid)
            continue;
        Repr.AddDrawItem(std::make_unique<HnDrawItem>(_sharedData, *this));
    }

    // Geometry susbset items will be added after the topology is updated.
}

void HnMesh::Invalidate()
{
    m_Topology     = {};
    m_VertexHandle = {};
    m_IndexData    = {};
}

class HnMesh::HdMeshTopologyWrapper
{
public:
    HdMeshTopologyWrapper(pxr::HdSceneDelegate& SceneDelegate,
                          const pxr::HdMesh&    Mesh) :
        m_SceneDelegate{SceneDelegate},
        m_Mesh{Mesh}
    {
    }

    pxr::HdMeshTopology& Get()
    {
        if (m_Topology.GetNumPoints() == 0)
        {
            m_Topology = m_Mesh.GetMeshTopology(&m_SceneDelegate);
        }
        return m_Topology;
    }

    operator pxr::HdMeshTopology &()
    {
        return Get();
    }

private:
    pxr::HdSceneDelegate& m_SceneDelegate;
    const pxr::HdMesh&    m_Mesh;
    pxr::HdMeshTopology   m_Topology;
};

struct HnMesh::StagingIndexData
{
    pxr::VtVec3iArray FaceIndices;
    pxr::VtIntArray   EdgeIndices;
    pxr::VtIntArray   PointIndices;
};

struct HnMesh::StagingVertexData
{
    HdMeshTopologyWrapper& MeshTopology;

    pxr::VtValue Points;

    // Use map to keep buffer sources sorted by name
    std::map<pxr::TfToken, std::shared_ptr<pxr::HdBufferSource>> Sources;
};

static size_t GetPrimvarElementSize(const HnRenderDelegate* RenderDelegate, const pxr::TfToken& Name, const pxr::TfToken& Role)
{
    if (Name == pxr::HdTokens->points ||
        Role == pxr::HdPrimvarSchemaTokens->point)
    {
        return RenderDelegate->GetUSDRenderer()->GetSettings().VertexPosPackMode == PBR_Renderer::VERTEX_POS_PACK_MODE_64_BIT ?
            sizeof(uint2) : // Vertex positions are packed into two 32-bit uints, see PBR_Renderer::VERTEX_POS_PACK_MODE_64_BIT
            sizeof(float3);
    }
    else if (Name == pxr::HdTokens->normals ||
             Role == pxr::HdPrimvarSchemaTokens->normal)
    {
        return RenderDelegate->GetUSDRenderer()->GetSettings().PackVertexNormals ?
            sizeof(Uint32) : // Normals are packed into a single uint
            sizeof(float3);
    }
    else if (Name == pxr::HdTokens->displayColor ||
             Role == pxr::HdPrimvarSchemaTokens->color)
    {
        return RenderDelegate->GetUSDRenderer()->GetSettings().PackVertexColors ?
            sizeof(Uint32) :
            sizeof(float3);
    }
    else if (Role == pxr::HdPrimvarSchemaTokens->textureCoordinate)
    {
        return sizeof(float2);
    }
    else
    {
        UNEXPECTED("Unrecognized primvar ", Name, " with role ", Role,
                   ". This likely indicated that HnRenderPass::GetSupportedVertexInputs "
                   " supports a vertex input that is not handled here.");
        return 0;
    }
}

bool HnMesh::UpdateRepr(pxr::HdSceneDelegate& SceneDelegate,
                        pxr::HdRenderParam*   RenderParam,
                        pxr::HdDirtyBits&     DirtyBits,
                        const pxr::TfToken&   ReprToken)
{
    const pxr::HdReprSharedPtr& CurrRepr = _GetRepr(ReprToken);
    if (!CurrRepr)
        return true;

    HnRenderDelegate* RenderDelegate      = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate());
    HnGeometryPool&   GeometryPool        = RenderDelegate->GetGeometryPool();
    const Int64       PendingGeometrySize = GeometryPool.GetPendingVertexDataSize() + GeometryPool.GetPendingIndexDataSize() + GeometryPool.GetReservedDataSize();
    const Uint64      GeometryLoadBudget  = static_cast<HnRenderParam*>(RenderParam)->GetConfig().GeometryLoadBudget;
    if (GeometryLoadBudget > 0 && static_cast<Uint64>(PendingGeometrySize) > GeometryLoadBudget)
    {
        // Pending geometry size exceeds the budget, skip updating the repr
        return false;
    }

    const pxr::SdfPath& Id = GetId();

    const bool TopologyDirty   = pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id);
    const bool AnyPrimvarDirty = pxr::HdChangeTracker::IsAnyPrimvarDirty(DirtyBits, Id);

    HdMeshTopologyWrapper MeshTopology{SceneDelegate, *this};

    bool IndexDataDirty = TopologyDirty || !m_IndexData.Faces;
    if (TopologyDirty)
    {
        UpdateTopology(SceneDelegate, RenderParam, DirtyBits, ReprToken, MeshTopology);
        DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
        m_IndexData    = {};
        m_VertexHandle = {};
    }

    PrimvarsInfo VertexPrimvarsInfo;
    PrimvarsInfo FacePrimvarsInfo;
    size_t       ExpectedVertexDataSize = 0;
    if (TopologyDirty || AnyPrimvarDirty)
    {
        GetPrimvarsInfo(SceneDelegate, DirtyBits, VertexPrimvarsInfo, FacePrimvarsInfo);
        bool HasFaceVaryingPrimvars = FacePrimvarsInfo.Count > 0;
        if (m_HasFaceVaryingPrimvars != HasFaceVaryingPrimvars)
        {
            m_HasFaceVaryingPrimvars = HasFaceVaryingPrimvars;
            IndexDataDirty           = true;
        }

        const size_t ExpectedNumElements = m_Topology.GetNumElements(m_HasFaceVaryingPrimvars);
        for (const PrimvarsInfo& Primvars : {VertexPrimvarsInfo, FacePrimvarsInfo})
        {
            for (const auto& it : Primvars.Dirty)
            {
                const pxr::HdPrimvarDescriptor& PrimDesc    = it.second;
                const size_t                    ElementSize = GetPrimvarElementSize(RenderDelegate, PrimDesc.name, PrimDesc.role);
                ExpectedVertexDataSize += ExpectedNumElements * ElementSize;
            }
        }
    }

    const size_t ExpectedTriangleIndexDataSize = m_Topology.ExpectedNumTriangleIndices * sizeof(Uint32);
    const size_t ExpectedEdgeIndexDataSize     = m_Topology.ExpectedNumEdgeIndices * sizeof(Uint32);
    const size_t ExpectedPointIndexDataSize    = m_Topology.ExpectedNumPointIndices * sizeof(Uint32);

    size_t ExpectedGeometryDataSize = ExpectedVertexDataSize;
    if (IndexDataDirty)
    {
        ExpectedGeometryDataSize +=
            (ExpectedTriangleIndexDataSize +
             ExpectedEdgeIndexDataSize +
             ExpectedPointIndexDataSize);
    }

    HnGeometryPool::ReservedSpace ReservedSpace = GeometryPool.ReserveSpace(ExpectedGeometryDataSize);
    if (GeometryLoadBudget > 0 && ReservedSpace.GetTotalPendingSize() > GeometryLoadBudget)
    {
        if (ReservedSpace.GetTotalPendingSize() == ExpectedGeometryDataSize)
        {
            LOG_WARNING_MESSAGE("Syncing rprim ", Id, " requires ", ExpectedGeometryDataSize,
                                " bytes of geometry data, which exceeds the budget ", GeometryLoadBudget, ".");
        }
        else
        {
            // Reserved data size exceeds the budget, skip updating the repr
            return false;
        }
    }

    StagingVertexData StagingVerts{MeshTopology};
    if (AnyPrimvarDirty)
    {
        UpdateVertexAndVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, VertexPrimvarsInfo, StagingVerts);

        if (StagingVerts.Sources.find(pxr::HdTokens->points) != StagingVerts.Sources.end() ||
            GetVertexBuffer(pxr::HdTokens->points) != nullptr)
        {
            // Collect face-varying primvar sources
            UpdateFaceVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, FacePrimvarsInfo, StagingVerts);

            // If there are neither vertex nor face-varying normals, generate smooth normals
            if (StagingVerts.Sources.find(pxr::HdTokens->normals) == StagingVerts.Sources.end() &&
                GetVertexBuffer(pxr::HdTokens->normals) == nullptr &&
                !StagingVerts.Points.IsEmpty())
            {
                HnMeshUtils  MeshUtils{MeshTopology, Id};
                pxr::VtValue Normals = MeshUtils.ComputeSmoothNormals(StagingVerts.Points);
                if (!Normals.IsEmpty())
                {
                    AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, pxr::HdTokens->normals, pxr::VtValue::Take(Normals), pxr::HdInterpolationVertex);
                }
            }

            UpdateConstantPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken);
        }
        else
        {
            LOG_WARNING_MESSAGE("Unable to find required primvar 'points' for rprim ", Id, ".");

            Invalidate();
        }

        DirtyBits &= ~pxr::HdChangeTracker::DirtyPrimvar;
    }

    const bool UseNativeStartVertex = static_cast<HnRenderParam*>(RenderParam)->GetConfig().UseNativeStartVertex;

    // When native start vertex is not supported, start vertex needs to be baked into the index data.
    Uint32 BakedStartVertex = (!UseNativeStartVertex && m_VertexHandle) ? m_VertexHandle->GetStartVertex() : 0;
    if (!StagingVerts.Sources.empty())
    {
        // When native start vertex is not supported, start vertex needs to be baked into the index data, and
        // we need to know the start vertex now, so we have to disallow pool allocation reuse (with pool allocation reuse
        // enabled, the allocation initialization is delayed until the GeometryPool.Commit() is called later).
        GeometryPool.AllocateVertices(Id.GetString(), StagingVerts.Sources, m_VertexHandle, /*DisallowPoolAllocationReuse = */ !UseNativeStartVertex);
        ReservedSpace.Release(ExpectedVertexDataSize);

        if (!UseNativeStartVertex)
        {
            Uint32 StartVertex = m_VertexHandle->GetStartVertex();
            if (StartVertex != BakedStartVertex)
            {
                BakedStartVertex = StartVertex;
                // Start vertex has changed, need to update index data
                IndexDataDirty = true;
            }
        }

        m_DrawItemGpuGeometryDirty.store(true);
    }

    if (IndexDataDirty && m_VertexHandle)
    {
        StagingIndexData StagingInds;
        UpdateIndexData(StagingInds, MeshTopology, StagingVerts.Points, RenderDelegate->AllowPrimitiveRestart());

        GeometryPool.AllocateIndices(Id.GetString() + " - faces", pxr::VtValue::Take(StagingInds.FaceIndices), BakedStartVertex, m_IndexData.Faces);
        ReservedSpace.Release(ExpectedTriangleIndexDataSize);

        GeometryPool.AllocateIndices(Id.GetString() + " - edges", pxr::VtValue::Take(StagingInds.EdgeIndices), BakedStartVertex, m_IndexData.Edges);
        ReservedSpace.Release(ExpectedEdgeIndexDataSize);

        GeometryPool.AllocateIndices(Id.GetString() + " - points", pxr::VtValue::Take(StagingInds.PointIndices), BakedStartVertex, m_IndexData.Points);
        ReservedSpace.Release(ExpectedPointIndexDataSize);

        m_DrawItemGpuTopologyDirty.store(true);
    }

    if ((m_DrawItemGpuGeometryDirty.load() || m_DrawItemGpuTopologyDirty.load()) && RenderParam != nullptr)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshGeometry);
        ++m_GeometryVersion;
    }

    // Note that m_SkelLocalToPrimLocal is set by UpdateSkinningPrimvars, so transform
    // should be updated after the primvars are synced.
    if (pxr::HdChangeTracker::IsTransformDirty(DirtyBits, Id))
    {
        entt::registry& Registry  = RenderDelegate->GetEcsRegistry();
        float4x4&       Transform = Registry.get<Components::Transform>(m_Entity).Matrix;

        float4x4 NewTransform = m_SkelLocalToPrimLocal * ToFloat4x4(SceneDelegate.GetTransform(Id));
        if (Transform != NewTransform)
        {
            Transform = NewTransform;
            if (RenderParam != nullptr)
            {
                static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshTransform);
            }
            UpdateCullMode(Transform, RenderParam);
        }

        DirtyBits &= ~pxr::HdChangeTracker::DirtyTransform;
    }

    if (pxr::HdChangeTracker::IsVisibilityDirty(DirtyBits, Id))
    {
        bool Visible = SceneDelegate.GetVisible(Id);
        if (_sharedData.visible != Visible)
        {
            _sharedData.visible = SceneDelegate.GetVisible(Id);
            if (RenderParam != nullptr)
            {
                static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshVisibility);
            }
            entt::registry& Registry = RenderDelegate->GetEcsRegistry();
            Registry.replace<Components::Visibility>(m_Entity, _sharedData.visible);
        }

        DirtyBits &= ~pxr::HdChangeTracker::DirtyVisibility;
    }

    DirtyBits &= ~pxr::HdChangeTracker::NewRepr;

    return true;
}

void HnMesh::UpdateDrawItemsForGeometrySubsets(pxr::HdSceneDelegate& SceneDelegate,
                                               pxr::HdRenderParam*   RenderParam)
{
    // (Re)create geom subset draw items
    for (auto const& token_repr : _reprs)
    {
        const pxr::TfToken&        Token = token_repr.first;
        pxr::HdReprSharedPtr       Repr  = token_repr.second;
        _MeshReprConfig::DescArray Descs = _GetReprDesc(Token);

        // Clear all previous geom subset draw items.
        Repr->ClearGeomSubsetDrawItems();
        for (const pxr::HdMeshReprDesc& Desc : Descs)
        {
            AddGeometrySubsetDrawItems(SceneDelegate.GetRenderIndex(), Desc, *Repr);
        }
    }
}

void HnMesh::Topology::Update(const pxr::HdMeshTopology& MeshTopology,
                              const pxr::SdfPath&        MeshId,
                              const HnRenderDelegate*    RenderDelegate)
{
    NumPoints       = static_cast<size_t>(MeshTopology.GetNumPoints());
    NumFaceVaryings = static_cast<size_t>(MeshTopology.GetNumFaceVaryings());

    HnMeshUtils MeshUtils{MeshTopology, MeshId};

    const HnMeshUtils::GET_TOTAL_INDEX_COUNT_FLAGS CountEdgesFlag = RenderDelegate->AllowPrimitiveRestart() ?
        HnMeshUtils::GET_TOTAL_INDEX_COUNT_FLAG_EDGES_STRIP :
        HnMeshUtils::GET_TOTAL_INDEX_COUNT_FLAG_EDGES_LIST;

    ExpectedNumTriangleIndices = MeshUtils.GetTotalIndexCount(HnMeshUtils::GET_TOTAL_INDEX_COUNT_FLAG_TRIANGLES);
    ExpectedNumEdgeIndices     = MeshUtils.GetTotalIndexCount(CountEdgesFlag);
    ExpectedNumPointIndices    = MeshUtils.GetTotalIndexCount(HnMeshUtils::GET_TOTAL_INDEX_COUNT_FLAG_POINTS);
}

bool HnMesh::Topology::UpdateSubsets(const pxr::HdMeshTopology& MeshTopology)
{
    const pxr::HdGeomSubsets GeomSubsets = MeshTopology.GetGeomSubsets();

    bool GeomSubsetsChanged = Subsets.size() != GeomSubsets.size();
    Subsets.resize(GeomSubsets.size());
    for (size_t i = 0; i < GeomSubsets.size(); ++i)
    {
        const pxr::HdGeomSubset& SrcSubset = GeomSubsets[i];

        Subset NewSubset{
            SrcSubset.type,
            SrcSubset.id,
            SrcSubset.materialId,
        };
        if (NewSubset != Subsets[i])
        {
            Subsets[i]         = std::move(NewSubset);
            GeomSubsetsChanged = true;
        }
    }

    return GeomSubsetsChanged;
}

void HnMesh::UpdateTopology(pxr::HdSceneDelegate&  SceneDelegate,
                            pxr::HdRenderParam*    RenderParam,
                            pxr::HdDirtyBits&      DirtyBits,
                            const pxr::TfToken&    ReprToken,
                            HdMeshTopologyWrapper& MeshTopology)
{
    const pxr::SdfPath& Id = GetId();
    VERIFY_EXPR(pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id));

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate());
    m_Topology.Update(MeshTopology, Id, RenderDelegate);

    if (m_Topology.UpdateSubsets(MeshTopology))
    {
        UpdateDrawItemsForGeometrySubsets(SceneDelegate, RenderParam);
        if (RenderParam != nullptr)
        {
            static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::GeometrySubsetDrawItems);
        }
    }

    DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
}

void HnMesh::PreprocessPrimvar(HnRenderDelegate* RenderDelegate, const pxr::TfToken& Name, pxr::VtValue& Primvar)
{
    if (Name == pxr::HdTokens->points)
    {
        VERIFY_EXPR(RenderDelegate != nullptr);
        if (RenderDelegate != nullptr && RenderDelegate->GetUSDRenderer()->GetSettings().VertexPosPackMode == PBR_Renderer::VERTEX_POS_PACK_MODE_64_BIT)
        {
            pxr::GfVec3f UnpackScale, UnpackBias;
            Primvar = HnMeshUtils::PackVertexPositions(GetId(), Primvar, UnpackScale, UnpackBias);

            entt::registry&        Registry  = RenderDelegate->GetEcsRegistry();
            Components::Transform& Transform = Registry.get<Components::Transform>(m_Entity);
            Transform.PosScale               = ToFloat3(UnpackScale);
            Transform.PosBias                = ToFloat3(UnpackBias);
        }
    }
    else if (Name == pxr::HdTokens->normals)
    {
        VERIFY_EXPR(RenderDelegate != nullptr);
        if (RenderDelegate != nullptr && RenderDelegate->GetUSDRenderer()->GetSettings().PackVertexNormals)
        {
            Primvar = HnMeshUtils::PackVertexNormals(GetId(), Primvar);
        }
    }
    else if (Name == pxr::HdTokens->displayColor)
    {
        VERIFY_EXPR(RenderDelegate != nullptr);
        if (RenderDelegate != nullptr && RenderDelegate->GetUSDRenderer()->GetSettings().PackVertexColors)
        {
            Primvar = HnMeshUtils::PackVertexColors(GetId(), Primvar);
        }
    }
}

bool HnMesh::AddStagingBufferSourceForPrimvar(HnRenderDelegate*    RenderDelegate,
                                              StagingVertexData&   StagingVerts,
                                              const pxr::TfToken&  Name,
                                              pxr::VtValue         Primvar,
                                              pxr::HdInterpolation Interpolation,
                                              int                  ValuesPerElement)
{
    if (Primvar.IsEmpty())
        return false;

    PreprocessPrimvar(RenderDelegate, Name, Primvar);

    pxr::VtValue  FaceVaryingPrimvar;
    pxr::VtValue* pSrcPrimvar = &Primvar;
    if ((Interpolation == pxr::HdInterpolationVertex || Interpolation == pxr::HdInterpolationVarying) && m_HasFaceVaryingPrimvars)
    {
        HnMeshUtils MeshUtils{StagingVerts.MeshTopology, GetId()};
        FaceVaryingPrimvar = MeshUtils.ConvertVertexPrimvarToFaceVarying(Primvar, ValuesPerElement);
        pSrcPrimvar        = &FaceVaryingPrimvar;
    }

    if (pSrcPrimvar == nullptr || pSrcPrimvar->IsEmpty())
        return false;

    auto BufferSource = std::make_shared<pxr::HdVtBufferSource>(
        Name,
        std::move(*pSrcPrimvar),
        ValuesPerElement, // values per element
        false             // whether doubles are supported or must be converted to floats
    );

    if (BufferSource->GetNumElements() == 0)
        return false;

    const size_t ExpectedNumElements = m_Topology.GetNumElements(m_HasFaceVaryingPrimvars);

    // Verify primvar length - it is alright to have more data than we index into.
    if (BufferSource->GetNumElements() < ExpectedNumElements)
    {
        LOG_WARNING_MESSAGE("Primvar '", Name, "' in mesh ", GetId(), " has only ", BufferSource->GetNumElements(),
                            " elements, while its topology expects at least ", ExpectedNumElements,
                            " elements. Skipping primvar.");
        return false;
    }
    else if (BufferSource->GetNumElements() > ExpectedNumElements)
    {
        // If the primvar has more data than needed, we issue a warning,
        // but don't skip the primvar update. Truncate the buffer to
        // the expected length.
        LOG_WARNING_MESSAGE("Primvar '", Name, "' in mesh ", GetId(), " has ", BufferSource->GetNumElements(),
                            " elements, while its topology expects ", ExpectedNumElements,
                            " elements. Truncating.");
        BufferSource->Truncate(ExpectedNumElements);
    }

    StagingVerts.Sources.emplace(Name, std::move(BufferSource));

    return true;
}

bool HnMesh::AddJointInfluencesStagingBufferSource(const pxr::VtValue& NumInfluencesPerComponentVal,
                                                   const pxr::VtValue& InfluencesVal,
                                                   StagingVertexData&  StagingVerts)
{
    const pxr::SdfPath& Id = GetId();
    if (!NumInfluencesPerComponentVal.IsHolding<int>())
    {
        LOG_ERROR_MESSAGE("Number of influences per component of mesh ", Id, " is of type ", NumInfluencesPerComponentVal.GetTypeName(), ", but int is expected");
        return false;
    }
    if (!InfluencesVal.IsHolding<pxr::VtVec2fArray>())
    {
        LOG_ERROR_MESSAGE("Influences of mesh ", Id, " are of type ", InfluencesVal.GetTypeName(), ", but VtVec2fArray is expected");
        return false;
    }

    const int NumInfluencesPerComponent = NumInfluencesPerComponentVal.UncheckedGet<int>();
    if (NumInfluencesPerComponent <= 0)
    {
        return false;
    }

    pxr::VtVec2fArray Influences = InfluencesVal.UncheckedGet<pxr::VtVec2fArray>();

    // 'influences' input of skinnig computation contains pairs of joint indices and weights.
    // Our shader always uses 4 joints packed into the following structure:
    // struct JointInfluence
    // {
    //     float4 Indices;
    //     float4 Weights;
    // };


    // We can't use pxr::VtArray<JointInfluence> because JointInfluence is not the type recognized
    // by HdVtBufferSource.
    const size_t      NumJoints = Influences.size() / NumInfluencesPerComponent;
    pxr::VtVec4fArray Joints(NumJoints * 2, pxr::GfVec4f{0});

    if (NumInfluencesPerComponent > 4)
    {
        // Sort influences by weight.
        for (size_t i = 0; i < NumJoints; ++i)
        {
            std::sort(Influences.begin() + i * NumInfluencesPerComponent,
                      Influences.begin() + (i + 1) * NumInfluencesPerComponent,
                      [](const pxr::GfVec2f& a, const pxr::GfVec2f& b) {
                          return a[1] > b[1];
                      });
            // Renormalize for the top 4 influences.
            float TotalWeight = (Influences[i * NumInfluencesPerComponent + 0][1] +
                                 Influences[i * NumInfluencesPerComponent + 1][1] +
                                 Influences[i * NumInfluencesPerComponent + 2][1] +
                                 Influences[i * NumInfluencesPerComponent + 3][1]);
            TotalWeight       = std::max(TotalWeight, 1e-5f);
            Influences[i * NumInfluencesPerComponent + 0][1] /= TotalWeight;
            Influences[i * NumInfluencesPerComponent + 1][1] /= TotalWeight;
            Influences[i * NumInfluencesPerComponent + 2][1] /= TotalWeight;
            Influences[i * NumInfluencesPerComponent + 3][1] /= TotalWeight;
        }
    }

    // Keep only the top 4 influences.
    for (size_t i = 0; i < NumJoints; ++i)
    {
        for (int j = 0; j < std::min(NumInfluencesPerComponent, 4); ++j)
        {
            Joints[i * 2 + 0][j] = Influences[i * NumInfluencesPerComponent + j][0];
            Joints[i * 2 + 1][j] = Influences[i * NumInfluencesPerComponent + j][1];
        }
    }

    return AddStagingBufferSourceForPrimvar(nullptr, StagingVerts, HnTokens->joints, pxr::VtValue::Take(Joints), pxr::HdInterpolationVertex, 2);
}

void HnMesh::PrimvarsInfo::AddDirtyPrimvar(pxr::HdDirtyBits&               DirtyBits,
                                           const pxr::SdfPath&             Id,
                                           const pxr::TfToken&             Name,
                                           const pxr::HdPrimvarDescriptor& PrimDesc,
                                           const pxr::TfToken&             Role)
{
    if (pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
    {
        auto it_inserted               = Dirty.emplace(Name, PrimDesc);
        it_inserted.first->second.role = Role;
    }

    ++Count;
}

void HnMesh::GetPrimvarsInfo(pxr::HdSceneDelegate& SceneDelegate,
                             pxr::HdDirtyBits&     DirtyBits,
                             PrimvarsInfo&         VertexPrimvarsInfo,
                             PrimvarsInfo&         FacePrimvarsInfo) const
{
    const pxr::SdfPath&       Id          = GetId();
    const pxr::HdRenderIndex& RenderIndex = SceneDelegate.GetRenderIndex();

    // Collect supported primvars from the materials in the mesh and its subsets.
    HnRenderPass::SupportedVertexInputsMapType SupportedPrimvars =
        HnRenderPass::GetSupportedVertexInputs(static_cast<const HnMaterial*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->material, GetMaterialId())));
    for (const Topology::Subset& Subset : m_Topology.Subsets)
    {
        HnRenderPass::SupportedVertexInputsMapType SubsetPrimvars =
            HnRenderPass::GetSupportedVertexInputs(static_cast<const HnMaterial*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->material, Subset.MaterialId)));
        SupportedPrimvars.insert(SubsetPrimvars.begin(), SubsetPrimvars.end());
    }

    std::unordered_map<pxr::TfToken, pxr::HdPrimvarDescriptor, pxr::TfToken::HashFunctor> SkippedPrimvarsByRole;

    auto UpdatePrimvarsInfo = [&](const pxr::HdInterpolation Interpolation,
                                  PrimvarsInfo&              PrimvarsInfo) {
        pxr::HdPrimvarDescriptorVector PrimVarDescs = GetPrimvarDescriptors(&SceneDelegate, Interpolation);
        for (const pxr::HdPrimvarDescriptor& PrimDesc : PrimVarDescs)
        {
            auto PrimvarIt = SupportedPrimvars.find(PrimDesc.name);
            if (PrimvarIt == SupportedPrimvars.end())
            {
                if (PrimDesc.role == pxr::HdPrimvarSchemaTokens->point ||
                    PrimDesc.role == pxr::HdPrimvarSchemaTokens->normal ||
                    PrimDesc.role == pxr::HdPrimvarSchemaTokens->textureCoordinate)
                {
                    SkippedPrimvarsByRole.emplace(PrimDesc.role, PrimDesc);
                }
                continue;
            }

            // Use role provided by HnRenderPass::GetSupportedVertexInputs, not the role from the primvar descriptor.
            const pxr::TfToken& Role = PrimvarIt->second;
            PrimvarsInfo.AddDirtyPrimvar(DirtyBits, Id, PrimDesc.name, PrimDesc, Role);
            SupportedPrimvars.erase(PrimDesc.name);
        }
    };

    bool HasSkinningComputation = false;
    for (pxr::HdInterpolation Interpolation : {pxr::HdInterpolationVertex, pxr::HdInterpolationVarying})
    {
        UpdatePrimvarsInfo(Interpolation, VertexPrimvarsInfo);

        pxr::HdExtComputationPrimvarDescriptorVector CompPrimvarsDescs = SceneDelegate.GetExtComputationPrimvarDescriptors(Id, Interpolation);
        for (const pxr::HdExtComputationPrimvarDescriptor& ExtCompPrimDesc : CompPrimvarsDescs)
        {
            // Skinnig ext computation is created by the skeleton adapter
            VertexPrimvarsInfo.ExtComp.push_back(ExtCompPrimDesc);
            if (ExtCompPrimDesc.sourceComputationOutputName == HnSkinningPrivateTokens->skinnedPoints)
            {
                HasSkinningComputation = true;
            }
        }
    }

    UpdatePrimvarsInfo(pxr::HdInterpolationFaceVarying, FacePrimvarsInfo);

    if (!SupportedPrimvars.empty() && !SkippedPrimvarsByRole.empty())
    {
        // Try to find a primvar that matches the role of the skipped primvar.
        for (const auto& it : SupportedPrimvars)
        {
            const pxr::TfToken& Name   = it.first;
            const pxr::TfToken& Role   = it.second;
            const auto          PrimIt = SkippedPrimvarsByRole.find(Role);
            if (PrimIt != SkippedPrimvarsByRole.end())
            {
                const pxr::HdPrimvarDescriptor& PrimDesc = PrimIt->second;
                LOG_WARNING_MESSAGE("Primvar '", PrimDesc.name, "' in mesh ", Id, " is not recognized by the material, but matches primvar '",
                                    Name, "' by role '", PrimDesc.role, "'.");
                (PrimDesc.interpolation == pxr::HdInterpolationFaceVarying ? FacePrimvarsInfo : VertexPrimvarsInfo).AddDirtyPrimvar(DirtyBits, Id, Name, PrimDesc, Role);
            }
        }
    }

    if (HasSkinningComputation && FacePrimvarsInfo.Count == 0)
    {
        // Skeleton Adapter hides normals, so we try to get them directly from the scene delegate.
        // However, there is no other way to check if the primvar exists than to try to get it,
        // which is not very efficient. We need to get the primvar to know its interpolation.
        // If normals turn out to be the only face-varying primvar, then vertex primvars that may have
        // been added before would need to be converted to face-varying as well.
        // To avoid these complications, we always convert primvars to face-varying if there is a skinning computation.
        FacePrimvarsInfo.Count = 1;
    }
}

void HnMesh::UpdateSkinningPrimvars(pxr::HdSceneDelegate&                         SceneDelegate,
                                    pxr::HdRenderParam*                           RenderParam,
                                    pxr::HdDirtyBits&                             DirtyBits,
                                    const pxr::TfToken&                           ReprToken,
                                    const pxr::HdExtComputationPrimvarDescriptor& SkinningCompPrimDesc,
                                    StagingVertexData&                            StagingVerts)
{
    const pxr::HdRenderIndex& RenderIndex    = SceneDelegate.GetRenderIndex();
    HnRenderDelegate*         RenderDelegate = static_cast<HnRenderDelegate*>(RenderIndex.GetRenderDelegate());
    const pxr::SdfPath&       SkinnigCompId  = SkinningCompPrimDesc.sourceComputationId;
    const HnExtComputation*   SkinningComp   = static_cast<const HnExtComputation*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->extComputation, SkinnigCompId));
    if (SkinningComp == nullptr)
    {
        LOG_ERROR_MESSAGE("Unable to find skinning computation ", SkinnigCompId);
        return;
    }

    Uint32 SkinningPrimvarsVersion = 0;

    // When animation is played, the points primvar is marked as dirty each frame.
    // To detect if skinning primvars are truly dirty, we need to check the version of the skinning computation inputs.
    const pxr::HdExtComputationInputDescriptorVector& ComputationInputs = SkinningComp->GetComputationInputs();
    for (const pxr::HdExtComputationInputDescriptor& CompInput : ComputationInputs)
    {
        const pxr::SdfPath      InputAggregatorCompId = CompInput.sourceComputationId;
        const HnExtComputation* InputAggregatorComp   = static_cast<const HnExtComputation*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->extComputation, CompInput.sourceComputationId));
        if (InputAggregatorComp == nullptr)
        {
            LOG_ERROR_MESSAGE("Unable to find skinning input aggregator computation ", InputAggregatorCompId, " for input ", CompInput.name, " of skinning computation ", SkinnigCompId);
            return;
        }
        SkinningPrimvarsVersion += InputAggregatorComp->GetSceneInputsVersion();
    }

    entt::registry&       Registry     = RenderDelegate->GetEcsRegistry();
    Components::Skinning& SkinningData = Registry.get<Components::Skinning>(m_Entity);

    if (SkinningPrimvarsVersion != m_SkinningPrimvarsVersion)
    {
        pxr::VtValue RestPointsVal;
        pxr::VtValue NumInfluencesPerComponentVal;
        pxr::VtValue InfluencesVal;
        for (const pxr::HdExtComputationInputDescriptor& CompInput : ComputationInputs)
        {
            if (CompInput.name == HnSkinningPrivateTokens->restPoints)
            {
                RestPointsVal = SceneDelegate.GetExtComputationInput(CompInput.sourceComputationId, CompInput.name);
            }
            else if (CompInput.name == HnSkinningPrivateTokens->numInfluencesPerComponent)
            {
                NumInfluencesPerComponentVal = SceneDelegate.GetExtComputationInput(CompInput.sourceComputationId, CompInput.name);
            }
            else if (CompInput.name == HnSkinningPrivateTokens->influences)
            {
                InfluencesVal = SceneDelegate.GetExtComputationInput(CompInput.sourceComputationId, CompInput.name);
            }
            else if (CompInput.name == HnSkinningPrivateTokens->geomBindXform)
            {
                pxr::VtValue GeomBindXformVal = SceneDelegate.GetExtComputationInput(CompInput.sourceComputationId, CompInput.name);

                if (GeomBindXformVal.IsHolding<pxr::GfMatrix4f>())
                {
                    const pxr::GfMatrix4f& GeomBindXform = GeomBindXformVal.UncheckedGet<pxr::GfMatrix4f>();
                    SkinningData.GeomBindXform           = ToFloat4x4(GeomBindXform);
                }
                else
                {
                    SkinningData.GeomBindXform = float4x4::Identity();
                }
            }
        }

        if (RestPointsVal.IsEmpty())
        {
            LOG_ERROR_MESSAGE("Unable to find rest points for skinning computation ", SkinnigCompId);
            return;
        }
        if (NumInfluencesPerComponentVal.IsEmpty())
        {
            LOG_ERROR_MESSAGE("Unable to find number of influences per component for skinning computation ", SkinnigCompId);
            return;
        }
        if (InfluencesVal.IsEmpty())
        {
            LOG_ERROR_MESSAGE("Unable to find influences for skinning computation ", SkinnigCompId);
            return;
        }

        StagingVerts.Points = RestPointsVal;

        AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, SkinningCompPrimDesc.name, std::move(RestPointsVal), SkinningCompPrimDesc.interpolation);
        AddJointInfluencesStagingBufferSource(NumInfluencesPerComponentVal, InfluencesVal, StagingVerts);

        // Skeleton adapter hides normals, so try to get them directly from the scene delegate
        pxr::VtValue NormalsPrimvar = GetPrimvar(&SceneDelegate, pxr::HdTokens->normals);
        if (!NormalsPrimvar.IsEmpty() && NormalsPrimvar.IsArrayValued())
        {
            // There is no way to get the interpolation of the normals primvar from the scene delegate,
            // so rely on the number of elements.
            if (NormalsPrimvar.GetArraySize() == m_Topology.NumPoints)
                AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, pxr::HdTokens->normals, std::move(NormalsPrimvar), pxr::HdInterpolationVertex);
            else if (NormalsPrimvar.GetArraySize() == m_Topology.NumFaceVaryings)
                AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, pxr::HdTokens->normals, std::move(NormalsPrimvar), pxr::HdInterpolationFaceVarying);
        }

        m_SkinningPrimvarsVersion = SkinningPrimvarsVersion;
    }

    if (const HnSkinningComputation* SkinningCompImpl = SkinningComp->GetImpl<HnSkinningComputation>())
    {
        SkinningData.Computation = SkinningCompImpl;

        const float4x4& SkelLocalToPrimLocal = SkinningCompImpl->GetSkelLocalToPrimLocal();
        if (SkelLocalToPrimLocal != m_SkelLocalToPrimLocal)
        {
            m_SkelLocalToPrimLocal = SkelLocalToPrimLocal;
            DirtyBits |= pxr::HdChangeTracker::DirtyTransform;
        }
    }
    else
    {
        DEV_ERROR("Skinning computation ", SkinnigCompId, " does not have a valid implementation");
    }
}

void HnMesh::UpdateVertexAndVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                            pxr::HdRenderParam*   RenderParam,
                                            pxr::HdDirtyBits&     DirtyBits,
                                            const pxr::TfToken&   ReprToken,
                                            const PrimvarsInfo&   VertexPrimvarsInfo,
                                            StagingVertexData&    StagingVerts)
{
    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate());

    for (const auto& it : VertexPrimvarsInfo.Dirty)
    {
        const pxr::TfToken&             Name     = it.first;
        const pxr::HdPrimvarDescriptor& PrimDesc = it.second;

        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        if (PrimValue.IsEmpty())
            continue;

        if (PrimDesc.name == pxr::HdTokens->points)
            StagingVerts.Points = PrimValue;

        AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, Name, std::move(PrimValue), PrimDesc.interpolation);
    }

    for (const pxr::HdExtComputationPrimvarDescriptor& ExtCompPrimDesc : VertexPrimvarsInfo.ExtComp)
    {
        // Skinnig ext computation is created by the skeleton adapter
        if (ExtCompPrimDesc.sourceComputationOutputName == HnSkinningPrivateTokens->skinnedPoints)
        {
            UpdateSkinningPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, ExtCompPrimDesc, StagingVerts);
        }
    }
}

void HnMesh::UpdateFaceVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                       pxr::HdRenderParam*   RenderParam,
                                       pxr::HdDirtyBits&     DirtyBits,
                                       const pxr::TfToken&   ReprToken,
                                       const PrimvarsInfo&   FacePrimvarsInfo,
                                       StagingVertexData&    StagingVerts)
{
    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate());

    for (const auto& it : FacePrimvarsInfo.Dirty)
    {
        const pxr::TfToken&             Name     = it.first;
        const pxr::HdPrimvarDescriptor& PrimDesc = it.second;

        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, Name, std::move(PrimValue), PrimDesc.interpolation);
    }
}

void HnMesh::UpdateConstantPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                    pxr::HdRenderParam*   RenderParam,
                                    pxr::HdDirtyBits&     DirtyBits,
                                    const pxr::TfToken&   ReprToken)
{
    const pxr::SdfPath& Id = GetId();

    pxr::HdPrimvarDescriptorVector ConstantPrims = GetPrimvarDescriptors(&SceneDelegate, pxr::HdInterpolationConstant);
    for (const pxr::HdPrimvarDescriptor& PrimDesc : ConstantPrims)
    {
        if (!pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
            continue;

        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        if (PrimValue.IsEmpty())
            continue;

        auto Source = std::make_shared<pxr::HdVtBufferSource>(
            PrimDesc.name,
            PrimValue,
            1,    // values per element
            false // whether doubles are supported or must be converted to floats
        );
        if (Source->GetNumElements() == 0)
            continue;

        const pxr::HdType ElementType = Source->GetTupleType().type;
        if (PrimDesc.name == pxr::HdTokens->displayColor)
        {
            entt::registry& Registry     = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate())->GetEcsRegistry();
            float4&         DisplayColor = Registry.get<Components::DisplayColor>(m_Entity).Val;

            if (ElementType == pxr::HdTypeFloatVec3)
            {
                memcpy(DisplayColor.Data(), Source->GetData(), sizeof(float3));
            }
            else
            {
                LOG_WARNING_MESSAGE("Unexpected type of ", PrimDesc.name, " primvar: ", ElementType);
            }
        }
        else if (PrimDesc.name == pxr::HdTokens->displayOpacity)
        {
            entt::registry& Registry = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate())->GetEcsRegistry();
            float&          Opacity  = Registry.get<Components::DisplayColor>(m_Entity).Val.w;

            if (ElementType == pxr::HdTypeFloat)
            {
                memcpy(&Opacity, Source->GetData(), sizeof(float));
            }
            else
            {
                LOG_WARNING_MESSAGE("Unexpected type of ", PrimDesc.name, " primvar: ", ElementType);
            }
        }
    }
}

void HnMesh::UpdateIndexData(StagingIndexData& StagingInds, const pxr::HdMeshTopology& MeshTopology, const pxr::VtValue& Points, bool UseStripTopology)
{
    HnMeshUtils     MeshUtils{MeshTopology, GetId()};
    pxr::VtIntArray SubsetStart;
    MeshUtils.Triangulate(!m_HasFaceVaryingPrimvars, &Points, StagingInds.FaceIndices, SubsetStart);
    m_IndexData.Subsets.clear();
    if (!m_Topology.Subsets.empty())
    {
        VERIFY_EXPR(SubsetStart.size() == m_Topology.Subsets.size() + 1);
        m_IndexData.Subsets.reserve(SubsetStart.size() - 1);
        for (size_t i = 0; i < SubsetStart.size() - 1; ++i)
        {
            const Uint32 StartTri = static_cast<Uint32>(SubsetStart[i]);
            const Uint32 EndTri   = static_cast<Uint32>(SubsetStart[i + 1]);
            m_IndexData.Subsets.emplace_back(GeometrySubsetRange{StartTri * 3, (EndTri - StartTri) * 3});
        }
    }

    StagingInds.EdgeIndices  = MeshUtils.ComputeEdgeIndices(!m_HasFaceVaryingPrimvars, UseStripTopology);
    StagingInds.PointIndices = MeshUtils.ComputePointIndices(m_HasFaceVaryingPrimvars);

    VERIFY_EXPR(StagingInds.FaceIndices.size() * 3 <= m_Topology.ExpectedNumTriangleIndices);
    VERIFY_EXPR(StagingInds.EdgeIndices.size() <= m_Topology.ExpectedNumEdgeIndices);
    VERIFY_EXPR(StagingInds.PointIndices.size() <= m_Topology.ExpectedNumPointIndices);
}

void HnMesh::UpdateDrawItemGpuGeometry(HnRenderDelegate& RenderDelegate)
{
    for (auto& it : _reprs)
    {
        pxr::HdRepr& Repr          = *it.second;
        const size_t DrawItemCount = Repr.GetDrawItems().size();
        for (size_t item = 0; item < DrawItemCount; ++item)
        {
            HnDrawItem& DrawItem = *static_cast<HnDrawItem*>(Repr.GetDrawItem(item));

            HnDrawItem::GeometryData Geo;
            Geo.Positions    = GetVertexBuffer(pxr::HdTokens->points);
            Geo.Normals      = GetVertexBuffer(pxr::HdTokens->normals);
            Geo.VertexColors = GetVertexBuffer(pxr::HdTokens->displayColor);
            Geo.Joints       = GetVertexBuffer(HnTokens->joints);

            // Our shader currently supports two texture coordinate sets.
            // Gather vertex buffers for both sets.
            if (const HnMaterial* pMaterial = DrawItem.GetMaterial())
            {
                const auto& TexCoordSets = pMaterial->GetTextureCoordinateSets();
                for (size_t i = 0; i < TexCoordSets.size(); ++i)
                {
                    const auto& TexCoordSet = TexCoordSets[i];
                    if (!TexCoordSet.PrimVarName.IsEmpty())
                    {
                        Geo.TexCoords[i] = GetVertexBuffer(TexCoordSet.PrimVarName);
                        if (!Geo.TexCoords[i])
                        {
                            LOG_ERROR_MESSAGE("Failed to find texture coordinates vertex buffer '", TexCoordSet.PrimVarName.GetText(), "' in mesh '", GetId().GetText(), "'");
                        }
                    }
                }
            }

            DrawItem.SetGeometryData(std::move(Geo));
        }
    }
    m_DrawItemGpuGeometryDirty.store(false);
}

void HnMesh::UpdateDrawItemGpuTopology(HnRenderDelegate& RenderDelegate)
{
    const HnRenderParam* pRenderParam = static_cast<const HnRenderParam*>(RenderDelegate.GetRenderParam());
    const Uint32         StartVertex  = pRenderParam->GetConfig().UseNativeStartVertex ? m_VertexHandle->GetStartVertex() : 0;

    Uint32 SubsetIdx = 0;
    ProcessDrawItems(
        [&](HnDrawItem& DrawItem) {
            if (m_IndexData.Subsets.empty())
            {
                DrawItem.SetFaces({
                    m_IndexData.Faces->GetBuffer(),
                    m_IndexData.Faces->GetStartIndex(),
                    m_IndexData.Faces->GetNumIndices(),
                    StartVertex,
                });
            }
            else
            {
                // Do not set topology if there are geometry subsets, so
                // that the render pass skips this draw item.
                DrawItem.SetFaces({});
            }

            // Render edges and points for the entire mesh at once
            DrawItem.SetEdges({
                m_IndexData.Edges->GetBuffer(),
                m_IndexData.Edges->GetStartIndex(),
                m_IndexData.Edges->GetNumIndices(),
                StartVertex,
            });

            DrawItem.SetPoints({
                m_IndexData.Points->GetBuffer(),
                m_IndexData.Points->GetStartIndex(),
                m_IndexData.Points->GetNumIndices(),
                StartVertex,
            });
        },
        [&](const Topology::Subset&, HnDrawItem& DrawItem) {
            const GeometrySubsetRange& SubsetRange = m_IndexData.Subsets[SubsetIdx++];
            DrawItem.SetFaces({
                m_IndexData.Faces->GetBuffer(),
                m_IndexData.Faces->GetStartIndex() + SubsetRange.StartIndex,
                SubsetRange.NumIndices,
                StartVertex,
            });
            // Do not set edges and points for subsets
            DrawItem.SetEdges({});
            DrawItem.SetPoints({});
        });
    VERIFY_EXPR(m_IndexData.Subsets.size() == SubsetIdx);
    m_DrawItemGpuTopologyDirty.store(false);
}

void HnMesh::CommitGPUResources(HnRenderDelegate& RenderDelegate)
{
    const GLTF::ResourceManager& ResMgr            = RenderDelegate.GetResourceManager();
    const Uint32                 IndexPoolVersion  = ResMgr.GetIndexBufferVersion();
    const Uint32                 VertexPoolVersion = ResMgr.GetVertexPoolsVersion();
    if (m_VertexHandle)
    {
        if (m_IndexPoolVersion != IndexPoolVersion)
        {
            // Update index buffer
            m_DrawItemGpuTopologyDirty.store(true);
        }

        if (m_VertexPoolVersion != VertexPoolVersion)
        {
            // Update vertex buffers
            m_DrawItemGpuGeometryDirty.store(true);
        }
    }
    else
    {
        // The mesh has not been initialized yet.
        // m_DrawItemGpuGeometryDirty and m_DrawItemGpuTopologyDirty will be set
        // by UpdateRepr() when the mesh is initialized.
    }

    if (m_DrawItemGpuTopologyDirty.load())
    {
        UpdateDrawItemGpuTopology(RenderDelegate);
    }

    if (m_DrawItemGpuGeometryDirty.load())
    {
        UpdateDrawItemGpuGeometry(RenderDelegate);
    }

    m_IndexPoolVersion  = IndexPoolVersion;
    m_VertexPoolVersion = VertexPoolVersion;
}

IBuffer* HnMesh::GetVertexBuffer(const pxr::TfToken& Name) const
{
    return m_VertexHandle ? m_VertexHandle->GetBuffer(Name) : nullptr;
}

Uint32 HnMesh::GetCacheResourceVersion(const HnRenderDelegate& RenderDelegate)
{
    const GLTF::ResourceManager& ResMgr = RenderDelegate.GetResourceManager();
    return ResMgr.GetIndexBufferVersion() + ResMgr.GetVertexPoolsVersion();
}

} // namespace USD

} // namespace Diligent
