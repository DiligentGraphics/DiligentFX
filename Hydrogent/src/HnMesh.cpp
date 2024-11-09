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
#include "pxr/imaging/hd/vertexAdjacency.h"
#include "pxr/imaging/hd/smoothNormals.h"
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
    const pxr::HdGeomSubsets& GeomSubsets    = m_Topology.GetGeomSubsets();
    const size_t              NumGeomSubsets = GeomSubsets.size();
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
                        HandleGeomSubsetDrawItem(GeomSubsets[i], static_cast<HnDrawItem&>(*Item));
                    }
                }
                ++GeomSubsetDescIdx;
            }
        }
    }
}

void HnMesh::UpdateReprMaterials(pxr::HdSceneDelegate* SceneDelegate,
                                 pxr::HdRenderParam*   RenderParam)
{
    const pxr::HdRenderIndex& RenderIndex = SceneDelegate->GetRenderIndex();

    ProcessDrawItems(
        [&](HnDrawItem& DrawItem) {
            SetDrawItemMaterial(RenderIndex, DrawItem, GetMaterialId());
        },
        [&](const pxr::HdGeomSubset& Subset, HnDrawItem& DrawItem) {
            SetDrawItemMaterial(RenderIndex, DrawItem, Subset.materialId);
        });
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
    }
    if (*DirtyBits & (pxr::HdChangeTracker::DirtyDisplayStyle | pxr::HdChangeTracker::NewRepr))
    {
        UpdateMaterials = true;
    }
    bool UpdateCullMode = pxr::HdChangeTracker::IsTransformDirty(*DirtyBits, Id) || m_CullMode == CULL_MODE_UNDEFINED;


    HnRenderDelegate* RenderDelegate      = static_cast<HnRenderDelegate*>(Delegate->GetRenderIndex().GetRenderDelegate());
    HnGeometryPool&   GeometryPool        = RenderDelegate->GetGeometryPool();
    const Int64       PendingGeometrySize = GeometryPool.GetPendingVertexDataSize() + GeometryPool.GetPendingIndexDataSize();
    const Uint64      GeometryLoadBudget  = static_cast<HnRenderParam*>(RenderParam)->GetGeometryLoadBudget();
    bool              ReprUpdated         = false;
    if (GeometryLoadBudget == 0 || static_cast<Uint64>(PendingGeometrySize) < GeometryLoadBudget)
    {
        UpdateRepr(*Delegate, RenderParam, *DirtyBits, ReprToken);
        ReprUpdated = true;
    }

    if (UpdateMaterials)
    {
        UpdateReprMaterials(Delegate, RenderParam);
    }

    const bool DirtyDoubleSided = (*DirtyBits & pxr::HdChangeTracker::DirtyDoubleSided) != 0;
    if (DirtyDoubleSided)
    {
        m_IsDoubleSided = Delegate->GetDoubleSided(Id);
        UpdateCullMode  = true;
    }

    if (UpdateCullMode)
    {
        CULL_MODE CullMode = CULL_MODE_UNDEFINED;
        if (m_IsDoubleSided)
        {
            CullMode = CULL_MODE_NONE;
        }
        else
        {
            CullMode = Delegate->GetTransform(Id).IsRightHanded() ? CULL_MODE_BACK : CULL_MODE_FRONT;
        }

        if (m_CullMode != CullMode)
        {
            m_CullMode = CullMode;
            static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshCulling);
        }
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

void HnMesh::AddGeometrySubsetDrawItems(const pxr::HdMeshReprDesc& ReprDesc, pxr::HdRepr& Repr)
{
    if (ReprDesc.geomStyle == pxr::HdMeshGeomStyleInvalid ||
        ReprDesc.geomStyle == pxr::HdMeshGeomStylePoints)
        return;

    const size_t NumGeomSubsets = m_Topology.GetGeomSubsets().size();
    for (size_t i = 0; i < NumGeomSubsets; ++i)
    {
        pxr::HdRepr::DrawItemUniquePtr Item = std::make_unique<HnDrawItem>(_sharedData, *this);
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

    // Note that geometry susbset items may change later.
    for (const pxr::HdMeshReprDesc& Desc : ReprDescs)
    {
        AddGeometrySubsetDrawItems(Desc, Repr);
    }
}

void HnMesh::Invalidate()
{
    m_Topology     = {};
    m_VertexHandle = {};
    m_IndexData    = {};
}

struct HnMesh::StagingIndexData
{
    pxr::VtVec3iArray FaceIndices;
    pxr::VtIntArray   EdgeIndices;
    pxr::VtIntArray   PointIndices;
};

struct HnMesh::StagingVertexData
{
    pxr::VtValue Points;

    // Use map to keep buffer sources sorted by name
    std::map<pxr::TfToken, std::shared_ptr<pxr::HdBufferSource>> Sources;
};

void HnMesh::UpdateRepr(pxr::HdSceneDelegate& SceneDelegate,
                        pxr::HdRenderParam*   RenderParam,
                        pxr::HdDirtyBits&     DirtyBits,
                        const pxr::TfToken&   ReprToken)
{
    const pxr::HdReprSharedPtr& CurrRepr = _GetRepr(ReprToken);
    if (!CurrRepr)
        return;

    HnRenderDelegate* RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate());

    const pxr::SdfPath& Id = GetId();

    const bool TopologyDirty   = pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id);
    const bool AnyPrimvarDirty = pxr::HdChangeTracker::IsAnyPrimvarDirty(DirtyBits, Id);

    bool IndexDataDirty = TopologyDirty;
    if (TopologyDirty)
    {
        UpdateTopology(SceneDelegate, RenderParam, DirtyBits, ReprToken);
        DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
        m_IndexData    = {};
        m_VertexHandle = {};
    }

    PrimvarsInfo VertexPrimvarsInfo;
    PrimvarsInfo FacePrimvarsInfo;
    if (TopologyDirty || AnyPrimvarDirty)
    {
        GetPrimvarsInfo(SceneDelegate, DirtyBits, VertexPrimvarsInfo, FacePrimvarsInfo);
        bool HasFaceVaryingPrimvars = FacePrimvarsInfo.Count > 0;
        if (m_HasFaceVaryingPrimvars != HasFaceVaryingPrimvars)
        {
            m_HasFaceVaryingPrimvars = HasFaceVaryingPrimvars;
            IndexDataDirty           = true;
        }
    }

    StagingVertexData StagingVerts;
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
                GenerateSmoothNormals(*RenderDelegate, StagingVerts);
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

    const bool UseNativeStartVertex = static_cast<HnRenderParam*>(RenderParam)->GetUseNativeStartVertex();

    // When native start vertex is not supported, start vertex needs to be baked into the index data.
    Uint32 BakedStartVertex = (!UseNativeStartVertex && m_VertexHandle) ? m_VertexHandle->GetStartVertex() : 0;
    if (!StagingVerts.Sources.empty())
    {
        HnGeometryPool& GeometryPool = RenderDelegate->GetGeometryPool();
        // When native start vertex is not supported, start vertex needs to be baked into the index data, and
        // we need to know the start vertex now, so we have to disallow pool allocation reuse (with pool allocation reuse
        // enabled, the allocation initialization is delayed until the GeometryPool.Commit() is called later).
        GeometryPool.AllocateVertices(Id.GetString(), StagingVerts.Sources, m_VertexHandle, /*DisallowPoolAllocationReuse = */ !UseNativeStartVertex);

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
        UpdateIndexData(StagingInds, StagingVerts.Points);

        HnGeometryPool& GeometryPool = RenderDelegate->GetGeometryPool();

        GeometryPool.AllocateIndices(Id.GetString() + " - faces", pxr::VtValue::Take(StagingInds.FaceIndices), BakedStartVertex, m_IndexData.Faces);
        GeometryPool.AllocateIndices(Id.GetString() + " - edges", pxr::VtValue::Take(StagingInds.EdgeIndices), BakedStartVertex, m_IndexData.Edges);
        GeometryPool.AllocateIndices(Id.GetString() + " - points", pxr::VtValue::Take(StagingInds.PointIndices), BakedStartVertex, m_IndexData.Points);

        m_DrawItemGpuTopologyDirty.store(true);
    }

    if ((IndexDataDirty || AnyPrimvarDirty) && RenderParam != nullptr)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshGeometry);
        ++m_GeometryVersion;
    }

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
            AddGeometrySubsetDrawItems(Desc, *Repr);
        }
    }
}

void HnMesh::UpdateTopology(pxr::HdSceneDelegate& SceneDelegate,
                            pxr::HdRenderParam*   RenderParam,
                            pxr::HdDirtyBits&     DirtyBits,
                            const pxr::TfToken&   ReprToken)
{
    const pxr::SdfPath& Id = GetId();
    VERIFY_EXPR(pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id));

    pxr::HdMeshTopology Topology           = HdMesh::GetMeshTopology(&SceneDelegate);
    bool                GeomSubsetsChanged = Topology.GetGeomSubsets() != m_Topology.GetGeomSubsets();

    m_Topology = Topology;
    if (GeomSubsetsChanged)
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
            HnMeshUtils  MeshUtils{m_Topology, GetId()};
            pxr::GfVec3f UnpackScale, UnpackBias;
            Primvar = MeshUtils.PackVertexPositions(Primvar, UnpackScale, UnpackBias);

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
            HnMeshUtils MeshUtils{m_Topology, GetId()};
            Primvar = MeshUtils.PackVertexNormals(Primvar);
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
        HnMeshUtils MeshUtils{m_Topology, GetId()};
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

    const size_t ExpectedNumElements = m_HasFaceVaryingPrimvars ? m_Topology.GetNumFaceVaryings() : m_Topology.GetNumPoints();

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

static HnRenderPass::SupportedVertexInputsMapType GetSupportedPrimvars(
    const pxr::HdRenderIndex&  RenderIndex,
    const pxr::SdfPath&        MaterialId,
    const pxr::HdMeshTopology& Topology)
{
    HnRenderPass::SupportedVertexInputsMapType SupportedPrimvars =
        HnRenderPass::GetSupportedVertexInputs(static_cast<const HnMaterial*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->material, MaterialId)));

    {
        const pxr::HdGeomSubsets GeomSubsets = Topology.GetGeomSubsets();
        for (const pxr::HdGeomSubset& Subset : GeomSubsets)
        {
            HnRenderPass::SupportedVertexInputsMapType SubsetPrimvars =
                HnRenderPass::GetSupportedVertexInputs(static_cast<const HnMaterial*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->material, Subset.materialId)));
            SupportedPrimvars.insert(SubsetPrimvars.begin(), SubsetPrimvars.end());
        }
    }

    return SupportedPrimvars;
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
                                           const pxr::HdPrimvarDescriptor& PrimDesc)
{
    if (pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
    {
        Dirty.emplace(Name, PrimDesc);
    }

    ++Count;
}

void HnMesh::GetPrimvarsInfo(pxr::HdSceneDelegate& SceneDelegate,
                             pxr::HdDirtyBits&     DirtyBits,
                             PrimvarsInfo&         VertexPrimvarsInfo,
                             PrimvarsInfo&         FacePrimvarsInfo) const
{
    const pxr::SdfPath& Id = GetId();

    HnRenderPass::SupportedVertexInputsMapType SupportedPrimvars = GetSupportedPrimvars(SceneDelegate.GetRenderIndex(), GetMaterialId(), m_Topology);

    std::unordered_map<pxr::TfToken, pxr::HdPrimvarDescriptor, pxr::TfToken::HashFunctor> SkippedPrimvarsByRole;

    auto UpdatePrimvarsInfo = [&](const pxr::HdInterpolation Interpolation,
                                  PrimvarsInfo&              PrimvarsInfo) {
        pxr::HdPrimvarDescriptorVector PrimVarDescs = GetPrimvarDescriptors(&SceneDelegate, Interpolation);
        for (const pxr::HdPrimvarDescriptor& PrimDesc : PrimVarDescs)
        {
            if (SupportedPrimvars.find(PrimDesc.name) == SupportedPrimvars.end())
            {
                if (PrimDesc.role == pxr::HdPrimvarSchemaTokens->point ||
                    PrimDesc.role == pxr::HdPrimvarSchemaTokens->normal ||
                    PrimDesc.role == pxr::HdPrimvarSchemaTokens->textureCoordinate)
                {
                    SkippedPrimvarsByRole.emplace(PrimDesc.role, PrimDesc);
                }
                continue;
            }

            PrimvarsInfo.AddDirtyPrimvar(DirtyBits, Id, PrimDesc.name, PrimDesc);
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
                (PrimDesc.interpolation == pxr::HdInterpolationFaceVarying ? FacePrimvarsInfo : VertexPrimvarsInfo).AddDirtyPrimvar(DirtyBits, Id, Name, PrimDesc);
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

                SkinningData.GeomBindXform = GeomBindXformVal.IsHolding<pxr::GfMatrix4f>() ?
                    ToFloat4x4(GeomBindXformVal.UncheckedGet<pxr::GfMatrix4f>()) :
                    float4x4::Identity();
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
            if (NormalsPrimvar.GetArraySize() == m_Topology.GetNumPoints())
                AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, pxr::HdTokens->normals, std::move(NormalsPrimvar), pxr::HdInterpolationVertex);
            else if (NormalsPrimvar.GetArraySize() == m_Topology.GetNumFaceVaryings())
                AddStagingBufferSourceForPrimvar(RenderDelegate, StagingVerts, pxr::HdTokens->normals, std::move(NormalsPrimvar), pxr::HdInterpolationFaceVarying);
        }

        m_SkinningPrimvarsVersion = SkinningPrimvarsVersion;
    }

    if (const HnSkinningComputation* SkinningCompImpl = SkinningComp->GetImpl<HnSkinningComputation>())
    {
        SkinningData.Xforms     = &SkinningCompImpl->GetXforms();
        SkinningData.XformsHash = SkinningCompImpl->GetXformsHash();

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

void HnMesh::GenerateSmoothNormals(HnRenderDelegate& RenderDelegate, StagingVertexData& StagingVerts)
{
    pxr::Hd_VertexAdjacency Adjacency;
    Adjacency.BuildAdjacencyTable(&m_Topology);
    if (Adjacency.GetNumPoints() == 0)
    {
        LOG_WARNING_MESSAGE("Skipping smooth normal generation for ", GetId(), " because its adjacency information is empty.");
        return;
    }

    if (!StagingVerts.Points.IsHolding<pxr::VtVec3fArray>())
    {
        LOG_ERROR_MESSAGE("Skipping smooth normal generation for ", GetId(), " because its points data is not float3.");
        return;
    }

    const pxr::VtVec3fArray& Points = StagingVerts.Points.UncheckedGet<pxr::VtVec3fArray>();

    pxr::VtVec3fArray Normals = pxr::Hd_SmoothNormals::ComputeSmoothNormals(&Adjacency, static_cast<int>(Points.size()), Points.data());
    if (Normals.size() != Points.size())
    {
        LOG_ERROR_MESSAGE("Failed to generate smooth normals for ", GetId(), ". Expected ", Points.size(), " normals, got ", Normals.size(), ".");
        return;
    }

    AddStagingBufferSourceForPrimvar(&RenderDelegate, StagingVerts, pxr::HdTokens->normals, pxr::VtValue::Take(Normals), pxr::HdInterpolationVertex);
}

void HnMesh::UpdateIndexData(StagingIndexData& StagingInds, const pxr::VtValue& Points)
{
    HnMeshUtils     MeshUtils{m_Topology, GetId()};
    pxr::VtIntArray SubsetStart;
    MeshUtils.Triangulate(!m_HasFaceVaryingPrimvars, &Points, StagingInds.FaceIndices, SubsetStart);
    if (!m_Topology.GetGeomSubsets().empty())
    {
        VERIFY_EXPR(SubsetStart.size() == m_Topology.GetGeomSubsets().size() + 1);
        m_IndexData.Subsets.reserve(SubsetStart.size() - 1);
        for (size_t i = 0; i < SubsetStart.size() - 1; ++i)
        {
            const Uint32 StartTri = static_cast<Uint32>(SubsetStart[i]);
            const Uint32 EndTri   = static_cast<Uint32>(SubsetStart[i + 1]);
            m_IndexData.Subsets.emplace_back(GeometrySubsetRange{StartTri * 3, (EndTri - StartTri) * 3});
        }
    }
    else
    {
        m_IndexData.Subsets.clear();
    }

    StagingInds.EdgeIndices  = MeshUtils.ComputeEdgeIndices(!m_HasFaceVaryingPrimvars, true);
    StagingInds.PointIndices = MeshUtils.ComputePointIndices(m_HasFaceVaryingPrimvars);
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
    const Uint32         StartVertex  = pRenderParam->GetUseNativeStartVertex() ? m_VertexHandle->GetStartVertex() : 0;

    Uint32 SubsetIdx = 0;
    ProcessDrawItems(
        [&](HnDrawItem& DrawItem) {
            if (m_Topology.GetGeomSubsets().empty())
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

            // Render edgesand points for the entire mesh at once
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
        [&](const pxr::HdGeomSubset& Subset, HnDrawItem& DrawItem) {
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
    if (m_DrawItemGpuTopologyDirty.load())
    {
        UpdateDrawItemGpuTopology(RenderDelegate);
    }

    if (m_DrawItemGpuGeometryDirty.load())
    {
        UpdateDrawItemGpuGeometry(RenderDelegate);
    }
}

IBuffer* HnMesh::GetVertexBuffer(const pxr::TfToken& Name) const
{
    return m_VertexHandle ? m_VertexHandle->GetBuffer(Name) : nullptr;
}

} // namespace USD

} // namespace Diligent
