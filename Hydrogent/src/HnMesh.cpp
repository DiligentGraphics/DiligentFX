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
#include "GfTypeConversions.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"
#include "GLTFResourceManager.hpp"
#include "EngineMemory.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/tf/smallVector.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/vtBufferSource.h"
#include "pxr/imaging/hd/vertexAdjacency.h"
#include "pxr/imaging/hd/smoothNormals.h"
#include "pxr/usd/usdSkel/tokens.h"

namespace Diligent
{

namespace USD
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    HnSkinningPrivateTokens,
    (skinnedPoints)
    (numInfluencesPerComponent)
    (influences)
    (skinningXforms)
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

    if (Delegate != nullptr && DirtyBits != nullptr)
    {
        UpdateRepr(*Delegate, RenderParam, *DirtyBits, ReprToken);
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
            CullMode = Delegate->GetTransform(Id).GetDeterminant() > 0 ? CULL_MODE_BACK : CULL_MODE_FRONT;
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
    m_StagingVertexData.reset();
    m_StagingIndexData.reset();
    m_Topology   = {};
    m_VertexData = {};
    m_IndexData  = {};
}

void HnMesh::UpdateRepr(pxr::HdSceneDelegate& SceneDelegate,
                        pxr::HdRenderParam*   RenderParam,
                        pxr::HdDirtyBits&     DirtyBits,
                        const pxr::TfToken&   ReprToken)
{
    const pxr::HdReprSharedPtr& CurrRepr = _GetRepr(ReprToken);
    if (!CurrRepr)
        return;

    const pxr::SdfPath& Id = GetId();

    const bool TopologyDirty = pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id);
    if (TopologyDirty)
    {
        UpdateTopology(SceneDelegate, RenderParam, DirtyBits, ReprToken);
        DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
    }

    const bool AnyPrimvarDirty = pxr::HdChangeTracker::IsAnyPrimvarDirty(DirtyBits, Id);
    if (AnyPrimvarDirty)
    {
        m_StagingVertexData = std::make_unique<StagingVertexData>();
        UpdateVertexAndVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken);

        if (m_StagingVertexData->Sources.find(pxr::HdTokens->points) != m_StagingVertexData->Sources.end())
        {
            // Collect face-varying primvar sources
            FaceSourcesMapType FaceSources;
            UpdateFaceVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, FaceSources);

            // If there are neither vertex nor face-varying normals, generate smooth normals
            if (m_StagingVertexData->Sources.find(pxr::HdTokens->normals) == m_StagingVertexData->Sources.end() &&
                FaceSources.find(pxr::HdTokens->normals) == FaceSources.end())
            {
                GenerateSmoothNormals();
            }

            // If there are face-varying sources, we need to convert all vertex sources into face-varying sources
            if (!FaceSources.empty())
            {
                ConvertVertexPrimvarSources(std::move(FaceSources));
            }

            UpdateConstantPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken);

            // Allocate space for vertex and index buffers.
            // Note that this only reserves space, but does not create any buffers.
            AllocatePooledResources(SceneDelegate, RenderParam);
        }
        else
        {
            LOG_WARNING_MESSAGE("Unable to find required primvar 'points' for rprim ", Id, ".");

            Invalidate();
        }

        DirtyBits &= ~pxr::HdChangeTracker::DirtyPrimvar;
    }

    if ((TopologyDirty || AnyPrimvarDirty) && RenderParam != nullptr)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshGeometry);
        ++m_GeometryVersion;
    }

    if (pxr::HdChangeTracker::IsTransformDirty(DirtyBits, Id))
    {
        entt::registry& Registry  = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate())->GetEcsRegistry();
        float4x4&       Transform = Registry.get<Components::Transform>(m_Entity).Val;

        auto NewTransform = ToFloat4x4(SceneDelegate.GetTransform(Id));
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
            entt::registry& Registry = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate())->GetEcsRegistry();
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

    m_StagingIndexData = std::make_unique<StagingIndexData>();

    m_StagingIndexData->Faces = ComputeTriangleFaceIndices();
    pxr::HdMeshUtil MeshUtil{&m_Topology, Id};
    MeshUtil.EnumerateEdges(&m_StagingIndexData->MeshEdgeIndices);
    m_IndexData.Subsets          = m_StagingIndexData->Faces.Subsets;
    m_IndexData.NumFaceTriangles = static_cast<Uint32>(m_StagingIndexData->Faces.Indices.size());
    m_IndexData.NumEdges         = static_cast<Uint32>(m_StagingIndexData->MeshEdgeIndices.size());

    DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
}

static std::shared_ptr<pxr::HdBufferSource> CreateBufferSource(const pxr::TfToken& Name,
                                                               const pxr::VtValue& Data,
                                                               int                 ValuesPerElement,
                                                               size_t              ExpectedNumElements,
                                                               const pxr::SdfPath& MeshId)
{
    if (Data.IsEmpty())
        return nullptr;

    auto BufferSource = std::make_shared<pxr::HdVtBufferSource>(
        Name,
        Data,
        ValuesPerElement, // values per element
        false             // whether doubles are supported or must be converted to floats
    );

    if (BufferSource->GetNumElements() == 0)
        return nullptr;

    // Verify primvar length - it is alright to have more data than we index into.
    if (BufferSource->GetNumElements() < ExpectedNumElements)
    {
        LOG_WARNING_MESSAGE("Primvar '", Name, "' in mesh ", MeshId, " has only ", BufferSource->GetNumElements(),
                            " elements, while its topology expects at least ", ExpectedNumElements,
                            " elements. Skipping primvar.");
        return nullptr;
    }
    else if (BufferSource->GetNumElements() > ExpectedNumElements)
    {
        // If the primvar has more data than needed, we issue a warning,
        // but don't skip the primvar update. Truncate the buffer to
        // the expected length.
        LOG_WARNING_MESSAGE("Primvar '", Name, "' in mesh ", MeshId, " has ", BufferSource->GetNumElements(),
                            " elements, while its topology expects ", ExpectedNumElements,
                            " elements. Truncating.");
        BufferSource->Truncate(ExpectedNumElements);
    }

    return BufferSource;
}

static std::shared_ptr<pxr::HdBufferSource> CreateBufferSource(const pxr::TfToken& Name,
                                                               const pxr::VtValue& Data,
                                                               size_t              ExpectedNumElements,
                                                               const pxr::SdfPath& MeshId)
{
    return CreateBufferSource(Name, Data, 1, ExpectedNumElements, MeshId);
}

static HnRenderPass::SupportedVertexInputsSetType GetSupportedPrimvars(
    const pxr::HdRenderIndex&  RenderIndex,
    const pxr::SdfPath&        MaterialId,
    const pxr::HdMeshTopology& Topology)
{
    HnRenderPass::SupportedVertexInputsSetType SupportedPrimvars =
        HnRenderPass::GetSupportedVertexInputs(static_cast<const HnMaterial*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->material, MaterialId)));

    {
        const pxr::HdGeomSubsets GeomSubsets = Topology.GetGeomSubsets();
        for (const pxr::HdGeomSubset& Subset : GeomSubsets)
        {
            HnRenderPass::SupportedVertexInputsSetType SubsetPrimvars =
                HnRenderPass::GetSupportedVertexInputs(static_cast<const HnMaterial*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->material, Subset.materialId)));
            SupportedPrimvars.insert(SubsetPrimvars.begin(), SubsetPrimvars.end());
        }
    }

    return SupportedPrimvars;
}

std::shared_ptr<pxr::HdBufferSource> HnMesh::CreateJointInfluencesBufferSource(pxr::HdSceneDelegate&   SceneDelegate,
                                                                               const HnExtComputation* SkinningComp)
{
    const pxr::SdfPath& Id = GetId();

    int NumInfluencesPerComponent = 0;

    pxr::VtVec2fArray Influences;

    // NB: use influences from the skinnig computation, not from the primvars because
    //     the computation may have performed necessary reindexing of primvar indices.
    const pxr::HdExtComputationInputDescriptorVector& ComputationInputs = SkinningComp->GetComputationInputs();
    for (const pxr::HdExtComputationInputDescriptor& CompInput : ComputationInputs)
    {
        if (CompInput.name == HnSkinningPrivateTokens->numInfluencesPerComponent)
        {
            pxr::VtValue NumInfluencesPerComponentVal = SceneDelegate.GetExtComputationInput(CompInput.sourceComputationId, CompInput.name);
            if (!NumInfluencesPerComponentVal.IsHolding<int>())
            {
                LOG_ERROR_MESSAGE("Number of influences per component of mesh ", Id, " is of type ", NumInfluencesPerComponentVal.GetTypeName(), ", but int is expected");
                return {};
            }
            NumInfluencesPerComponent = NumInfluencesPerComponentVal.Get<int>();
        }
        else if (CompInput.name == HnSkinningPrivateTokens->influences)
        {
            pxr::VtValue InfluencesVal = SceneDelegate.GetExtComputationInput(CompInput.sourceComputationId, CompInput.name);
            if (!InfluencesVal.IsHolding<pxr::VtVec2fArray>())
            {
                LOG_ERROR_MESSAGE("Influences of mesh ", Id, " are of type ", InfluencesVal.GetTypeName(), ", but VtVec2fArray is expected");
                return {};
            }
            Influences = InfluencesVal.Get<pxr::VtVec2fArray>();
        }
    }

    if (NumInfluencesPerComponent <= 0)
    {
        return {};
    }

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

    return CreateBufferSource(HnTokens->joints, pxr::VtValue{Joints}, 2, m_Topology.GetNumPoints(), Id);
}

void HnMesh::UpdateSkinningPrimvars(pxr::HdSceneDelegate&                         SceneDelegate,
                                    pxr::HdRenderParam*                           RenderParam,
                                    pxr::HdDirtyBits&                             DirtyBits,
                                    const pxr::TfToken&                           ReprToken,
                                    const pxr::HdExtComputationPrimvarDescriptor& SkinningCompPrimDesc)
{
    const pxr::SdfPath&     SkinnigCompId = SkinningCompPrimDesc.sourceComputationId;
    const HnExtComputation* SkinningComp  = static_cast<const HnExtComputation*>(SceneDelegate.GetRenderIndex().GetSprim(pxr::HdPrimTypeTokens->extComputation, SkinnigCompId));
    if (SkinningComp == nullptr)
    {
        LOG_ERROR_MESSAGE("Unable to find skinning computation ", SkinnigCompId);
        return;
    }

    const pxr::SdfPath& Id = GetId();

    // When animation is played, the points primvar is marked as dirty each frame.
    // We only need to really update it if any other primvar is dirty or topology is dirty.
    if (pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, SkinningCompPrimDesc.name) &&
        ((DirtyBits & pxr::HdChangeTracker::DirtyPrimvar) || m_StagingIndexData))
    {
        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, SkinningCompPrimDesc.name);
        if (auto BufferSource = CreateBufferSource(SkinningCompPrimDesc.name, PrimValue, m_Topology.GetNumPoints(), Id))
        {
            m_StagingVertexData->Sources.emplace(SkinningCompPrimDesc.name, std::move(BufferSource));
        }
    }

    if (pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, pxr::UsdSkelTokens->primvarsSkelJointIndices) ||
        pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, pxr::UsdSkelTokens->primvarsSkelJointWeights))
    {
        if (auto BufferSource = CreateJointInfluencesBufferSource(SceneDelegate, SkinningComp))
        {
            m_StagingVertexData->Sources.emplace(HnTokens->joints, std::move(BufferSource));
        }
    }

    {
        pxr::VtValue SkinningXformsVal = SceneDelegate.GetExtComputationInput(SkinningCompPrimDesc.sourceComputationId, HnSkinningPrivateTokens->skinningXforms);
        if (SkinningXformsVal.IsHolding<pxr::VtMatrix4fArray>())
        {
            m_SkinningXforms = SkinningXformsVal.Get<pxr::VtMatrix4fArray>();
        }
        else
        {
            LOG_ERROR_MESSAGE("Skinning transforms of mesh ", Id, " are of type ", SkinningXformsVal.GetTypeName(), ", but VtMatrix4fArray is expected");
        }
    }
}

void HnMesh::UpdateVertexAndVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                            pxr::HdRenderParam*   RenderParam,
                                            pxr::HdDirtyBits&     DirtyBits,
                                            const pxr::TfToken&   ReprToken)
{
    VERIFY_EXPR(m_StagingVertexData);
    const pxr::SdfPath& Id = GetId();

    const int NumPoints = m_Topology.GetNumPoints();

    const HnRenderPass::SupportedVertexInputsSetType SupportedPrimvars = GetSupportedPrimvars(SceneDelegate.GetRenderIndex(), GetMaterialId(), m_Topology);

    auto AddPrimvarSource = [&](const pxr::HdPrimvarDescriptor& PrimDesc) {
        if (!pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
            return;

        // Skip unsupported primvars
        if (SupportedPrimvars.find(PrimDesc.name) == SupportedPrimvars.end())
            return;

        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        if (PrimValue.IsEmpty())
            return;

        if (auto BufferSource = CreateBufferSource(PrimDesc.name, PrimValue, NumPoints, Id))
        {
            m_StagingVertexData->Sources.emplace(PrimDesc.name, std::move(BufferSource));
        }
    };

    for (pxr::HdInterpolation Interpolation : {pxr::HdInterpolationVertex, pxr::HdInterpolationVarying})
    {
        pxr::HdPrimvarDescriptorVector PrimVarDescs = GetPrimvarDescriptors(&SceneDelegate, Interpolation);
        for (const pxr::HdPrimvarDescriptor& PrimDesc : PrimVarDescs)
        {
            AddPrimvarSource(PrimDesc);
        }

        pxr::HdExtComputationPrimvarDescriptorVector CompPrimvarsDescs = SceneDelegate.GetExtComputationPrimvarDescriptors(Id, Interpolation);
        for (const pxr::HdExtComputationPrimvarDescriptor& ExtCompPrimDesc : CompPrimvarsDescs)
        {
            // Skinnig ext computation is created by the skeleton adapter
            if (ExtCompPrimDesc.sourceComputationOutputName == HnSkinningPrivateTokens->skinnedPoints)
            {
                UpdateSkinningPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, ExtCompPrimDesc);
            }
        }
    }
}

void HnMesh::UpdateFaceVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                       pxr::HdRenderParam*   RenderParam,
                                       pxr::HdDirtyBits&     DirtyBits,
                                       const pxr::TfToken&   ReprToken,
                                       FaceSourcesMapType&   FaceSources)
{
    VERIFY_EXPR(m_StagingVertexData);
    const pxr::SdfPath& Id = GetId();

    const HnRenderPass::SupportedVertexInputsSetType SupportedPrimvars = GetSupportedPrimvars(SceneDelegate.GetRenderIndex(), GetMaterialId(), m_Topology);

    pxr::HdPrimvarDescriptorVector FaceVaryingPrims = GetPrimvarDescriptors(&SceneDelegate, pxr::HdInterpolationFaceVarying);
    for (const pxr::HdPrimvarDescriptor& PrimDesc : FaceVaryingPrims)
    {
        if (!pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
            continue;

        // Skip unsupported primvars
        if (SupportedPrimvars.find(PrimDesc.name) == SupportedPrimvars.end())
            continue;

        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        if (PrimValue.IsEmpty())
            continue;

        auto PrimVarSource = std::make_shared<pxr::HdVtBufferSource>(
            PrimDesc.name,
            PrimValue,
            1,    // values per element
            false // whether doubles are supported or must be converted to floats
        );

        if (PrimVarSource->GetNumElements() == 0)
            continue;

        pxr::VtValue    TriangulatedPrimValue;
        pxr::HdMeshUtil MeshUtil{&m_Topology, Id};
        // The method outputs a linear non-indexed list of values.
        // If Indices is the list of indices produced by MeshUtil.ComputeTriangleIndices, then
        // values in the output are indexed by i rather than Indices[i].
        if (MeshUtil.ComputeTriangulatedFaceVaryingPrimvar(PrimVarSource->GetData(),
                                                           PrimVarSource->GetNumElements(),
                                                           PrimVarSource->GetTupleType().type,
                                                           &TriangulatedPrimValue))
        {
            if (auto BufferSource = CreateBufferSource(PrimDesc.name, TriangulatedPrimValue, size_t{m_IndexData.NumFaceTriangles} * 3, Id))
            {
                FaceSources.emplace(PrimDesc.name, std::move(BufferSource));
            }
        }
    }
}

void HnMesh::UpdateConstantPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                    pxr::HdRenderParam*   RenderParam,
                                    pxr::HdDirtyBits&     DirtyBits,
                                    const pxr::TfToken&   ReprToken)
{
    VERIFY_EXPR(m_StagingVertexData);
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

void HnMesh::GenerateSmoothNormals()
{
    pxr::Hd_VertexAdjacency Adjacency;
    Adjacency.BuildAdjacencyTable(&m_Topology);
    if (Adjacency.GetNumPoints() == 0)
    {
        LOG_WARNING_MESSAGE("Skipping smooth normal generation for ", GetId(), " because its adjacency information is empty.");
        return;
    }

    auto points_it = m_StagingVertexData->Sources.find(pxr::HdTokens->points);
    if (points_it == m_StagingVertexData->Sources.end())
    {
        LOG_ERROR_MESSAGE("Skipping smooth normal generation for ", GetId(), " because its points data is missing.");
        return;
    }

    const pxr::HdBufferSource& PointsSource = *points_it->second;
    if (PointsSource.GetTupleType().type != pxr::HdTypeFloatVec3)
    {
        LOG_ERROR_MESSAGE("Skipping smooth normal generation for ", GetId(), " because its points data is not float3.");
        return;
    }

    pxr::VtVec3fArray Normals = pxr::Hd_SmoothNormals::ComputeSmoothNormals(&Adjacency, static_cast<int>(PointsSource.GetNumElements()), static_cast<const pxr::GfVec3f*>(PointsSource.GetData()));
    if (Normals.size() != PointsSource.GetNumElements())
    {
        LOG_ERROR_MESSAGE("Failed to generate smooth normals for ", GetId(), ". Expected ", PointsSource.GetNumElements(), " normals, got ", Normals.size(), ".");
        return;
    }

    if (auto BufferSource = CreateBufferSource(pxr::HdTokens->normals, pxr::VtValue{Normals}, PointsSource.GetNumElements(), GetId()))
    {
        m_StagingVertexData->Sources.emplace(pxr::HdTokens->normals, std::move(BufferSource));
    }
}

namespace
{

class TriangulatedFaceBufferSource final : public pxr::HdBufferSource
{
public:
    TriangulatedFaceBufferSource(const pxr::TfToken& Name,
                                 pxr::HdTupleType    TupleType,
                                 size_t              NumElements) :
        m_Name{Name},
        m_TupleType{TupleType},
        m_NumElements{NumElements},
        m_Data(HdDataSizeOfType(TupleType.type) * TupleType.count * NumElements)
    {
    }

    virtual TfToken const& GetName() const override
    {
        return m_Name;
    }

    virtual void const* GetData() const override
    {
        return m_Data.data();
    }

    virtual size_t ComputeHash() const override
    {
        UNEXPECTED("This is not supposed to be called");
        return 0;
    }

    virtual size_t GetNumElements() const override
    {
        return m_NumElements;
    }

    virtual pxr::HdTupleType GetTupleType() const override
    {
        return m_TupleType;
    }

    virtual void GetBufferSpecs(pxr::HdBufferSpecVector* specs) const override
    {
        UNEXPECTED("This is not supposed to be called");
    }

    virtual bool Resolve() override final
    {
        return true;
    }

    virtual bool _CheckValid() const override final
    {
        return !m_Data.empty();
    }

    std::vector<Uint8>& GetData()
    {
        return m_Data;
    }

private:
    pxr::TfToken       m_Name;
    pxr::HdTupleType   m_TupleType;
    size_t             m_NumElements;
    std::vector<Uint8> m_Data;
};

} // namespace

HnMesh::TriangleFaceIndexData HnMesh::ComputeTriangleFaceIndices()
{
    TriangleFaceIndexData FaceIndexData;

    pxr::HdMeshUtil MeshUtil{&m_Topology, GetId()};
    pxr::VtIntArray PrimitiveParams;
    MeshUtil.ComputeTriangleIndices(&FaceIndexData.Indices, &PrimitiveParams, nullptr);

    if (!m_Topology.GetGeomSubsets().empty())
    {
        const size_t FaceCount = m_Topology.GetFaceVertexCounts().size();
        // PrimitiveParams is the mapping from the triangle index to the original face index.
        //            +--------+---------------+
        //           /| \      |\      |      /
        //          / |  \  B  | \  C  |     /
        //         /  |   \    |  \    |    /
        //        /   |    \   |   \   | C /
        //       / A  |  B  \  | C  \  |  /
        //      /     |      \ |     \ | /
        //     /      |       \|      \|/
        //    +-------+--------+-------+
        //
        //  0 -> A
        //  1 -> B, 2 -> B
        //  3 -> C, 4 -> C, 5 -> C
        //
        // We need the reverse mapping:
        //  A -> {0}
        //  B -> {1, 2}
        //  C -> {3, 4, 5}
        std::vector<pxr::TfSmallVector<int, 4>> FaceIdxToTriIdx(FaceCount);
        for (int i = 0; i < PrimitiveParams.size(); ++i)
        {
            int FaceIndex = pxr::HdMeshUtil::DecodeFaceIndexFromCoarseFaceParam(PrimitiveParams[i]);
            if (FaceIndex >= FaceCount)
            {
                LOG_ERROR_MESSAGE("Invalid face index ", FaceIndex, " decoded from primitive param for triangle ", i, " in ", GetId(), " mesh. Expected value in the range [0, ", FaceCount, ").");
                continue;
            }
            FaceIdxToTriIdx[FaceIndex].push_back(i);
        }

        pxr::VtVec3iArray Indices;

        ProcessDrawItems(
            [](HnDrawItem& DrawItem) {},
            [&](const pxr::HdGeomSubset& Subset, HnDrawItem& DrawItem) {
                const Uint32 FirstTri = static_cast<Uint32>(Indices.size());
                for (int FaceIndex : Subset.indices)
                {
                    if (FaceIndex >= FaceCount)
                    {
                        LOG_ERROR_MESSAGE("Invalid face index ", FaceIndex, " for subset ", Subset.id, " in ", GetId(), " mesh. Expected value in the range [0, ", FaceCount, ").");
                        continue;
                    }

                    for (int TriIndex : FaceIdxToTriIdx[FaceIndex])
                    {
                        Indices.push_back(FaceIndexData.Indices[TriIndex]);
                        FaceIndexData.FaceReordering.emplace_back(TriIndex * 3 + 0, TriIndex * 3 + 1, TriIndex * 3 + 2);
                    }
                }
                const Uint32 NumTris = static_cast<Uint32>(Indices.size() - FirstTri);
                FaceIndexData.Subsets.emplace_back(GeometrySubsetRange{FirstTri * 3, NumTris * 3});
            });
        FaceIndexData.Indices = std::move(Indices);
    }

    return FaceIndexData;
}

static std::shared_ptr<pxr::HdBufferSource> ReindexBufferSource(const pxr::HdBufferSource& SrcSource,
                                                                const pxr::VtVec3iArray&   Indices)
{
    const Uint8*           pSrcData    = static_cast<const Uint8*>(SrcSource.GetData());
    const pxr::HdTupleType ElementType = SrcSource.GetTupleType();
    const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;

    std::shared_ptr<TriangulatedFaceBufferSource> ReindexedSource =
        std::make_shared<TriangulatedFaceBufferSource>(SrcSource.GetName(), SrcSource.GetTupleType(), Indices.size() * 3);

    std::vector<Uint8>& DstData = ReindexedSource->GetData();
    VERIFY_EXPR(DstData.size() == Indices.size() * 3 * ElementSize);
    Uint8* pDst = DstData.data();
    for (size_t i = 0; i < Indices.size(); ++i)
    {
        const pxr::GfVec3i& Tri = Indices[i];
        for (size_t v = 0; v < 3; ++v)
        {
            const auto* pSrc = pSrcData + Tri[v] * ElementSize;
            memcpy(pDst + (i * 3 + v) * ElementSize, pSrc, ElementSize);
        }
    }

    return ReindexedSource;
}

void HnMesh::ConvertVertexPrimvarSources(FaceSourcesMapType&& FaceSources)
{
    TriangleFaceIndexData Faces;
    if (!m_StagingIndexData || !m_StagingIndexData->Faces)
    {
        // Need to regenerate triangle indices
        Faces = ComputeTriangleFaceIndices();
        if (!Faces)
            return;

        VERIFY(Faces.Subsets.size() == m_IndexData.Subsets.size(),
               "The number of subsets is not consistent with the previously computed value. "
               "This may indicate that the topology was not updated during the last sync.");
    }
    const pxr::VtVec3iArray& Indices        = Faces ? Faces.Indices : m_StagingIndexData->Faces.Indices;
    const pxr::VtVec3iArray& FaceReordering = Faces ? Faces.FaceReordering : m_StagingIndexData->Faces.FaceReordering;

    VERIFY(Indices.size() == m_IndexData.NumFaceTriangles,
           "The number of indices is not consistent with the previously computed value. "
           "This may indicate that the topology was not updated during the last sync.");

    // Vertex data is indexed by triangle indices produced by MeshUtil.ComputeTriangleIndices.
    // However, triangulation of face-varying primvars produced by MeshUtil.ComputeTriangulatedFaceVaryingPrimvar
    // is non-indexed. It contains one value per index, rather than one value per vertex.
    // We need to unfold vertex data to match the face-varying data.

    // Unpack vertex sources by unfolding triangle indices into linear list of vertices
    int NumVerts = 0;
    for (auto& source_it : m_StagingVertexData->Sources)
    {
        auto& pSource = source_it.second;
        if (pSource == nullptr)
            continue;
        NumVerts = std::max(NumVerts, static_cast<int>(pSource->GetNumElements()));

        // Replace original vertex source with the triangulated face source
        pSource = ReindexBufferSource(*pSource, Indices);
    }

    // Add face-varying sources
    for (auto& face_source_it : FaceSources)
    {
        auto it_inserted = m_StagingVertexData->Sources.emplace(face_source_it.first, std::move(face_source_it.second));
        if (!it_inserted.second)
        {
            LOG_ERROR_MESSAGE("Failed to add face-varying source ", face_source_it.first, " to ", GetId(), " as vertex source with the same name already exists.");
            continue;
        }

        if (!FaceReordering.empty() && it_inserted.first->second)
        {
            // Reorder face-varying data to match the geometry subset face order
            it_inserted.first->second = ReindexBufferSource(*it_inserted.first->second, FaceReordering);
        }
    }

    // Mapping from the original vertex index to the first occurrence of this vertex in the unfolded list
    //
    //  Verts:    A B C D E F
    //  Indices:  3 4 5 0 1 2
    //  Unfolded: D E F A B C
    //  Mapping:  0->3, 1->4, 2->5, 3->0, 4->1, 5->2
    std::vector<int> ReverseVertexMapping(NumVerts, -1);
    for (int i = 0; i < Indices.size(); ++i)
    {
        const pxr::GfVec3i& Tri = Indices[i];
        for (int v = 0; v < 3; ++v)
        {
            if (Tri[v] >= NumVerts)
            {
                LOG_ERROR_MESSAGE("Invalid vertex index ", Tri[v], " in triangle ", i, " in ", GetId(), " mesh. Expected value in the range [0, ", NumVerts, ").");
                continue;
            }
            int& MappedIndex = ReverseVertexMapping[Tri[v]];
            if (MappedIndex < 0)
                MappedIndex = i * 3 + v;
        }
    }

    // Replace original triangle indices with the list of unfolded face indices
    m_StagingIndexData->Faces.Indices.resize(m_IndexData.NumFaceTriangles);
    for (Uint32 i = 0; i < m_StagingIndexData->Faces.Indices.size(); ++i)
    {
        pxr::GfVec3i& Tri{m_StagingIndexData->Faces.Indices[i]};
        Tri[0] = i * 3 + 0;
        Tri[1] = i * 3 + 1;
        Tri[2] = i * 3 + 2;
    }

    // Update edge indices
    for (pxr::GfVec2i& Edge : m_StagingIndexData->MeshEdgeIndices)
    {
        if (Edge[0] >= NumVerts || Edge[1] >= NumVerts)
        {
            LOG_ERROR_MESSAGE("Invalid vertex index in edge ", Edge[0], " - ", Edge[1], " in ", GetId(), " mesh. Expected value in the range [0, ", NumVerts, ").");
            continue;
        }

        int v0 = ReverseVertexMapping[Edge[0]];
        int v1 = ReverseVertexMapping[Edge[1]];
        if (v0 >= 0 && v1 >= 0)
        {
            Edge[0] = v0;
            Edge[1] = v1;
        }
        else
        {
            Edge[0] = 0;
            Edge[1] = 0;
        }
    }

    // Create point indices
    m_StagingIndexData->PointIndices.resize(m_Topology.GetNumPoints());
    for (size_t i = 0; i < m_StagingIndexData->PointIndices.size(); ++i)
    {
        if (i < NumVerts)
        {
            m_StagingIndexData->PointIndices[i] = ReverseVertexMapping[i];
        }
    }
}

void HnMesh::AllocatePooledResources(pxr::HdSceneDelegate& SceneDelegate,
                                     pxr::HdRenderParam*   RenderParam)
{
    HnRenderDelegate*      RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate());
    GLTF::ResourceManager& ResMgr         = RenderDelegate->GetResourceManager();

    if (m_StagingVertexData && !m_StagingVertexData->Sources.empty() && static_cast<const HnRenderParam*>(RenderParam)->GetUseVertexPool())
    {
        if (m_StagingIndexData)
        {
            // The topology has changed: release the existing allocation
            m_VertexData.PoolAllocation.Release();
            m_VertexData.NameToPoolIndex.clear();
        }

        // Allocate vertex buffers for face data
        const size_t NumVerts = m_StagingVertexData->Sources.begin()->second->GetNumElements();
        if (!m_VertexData.PoolAllocation)
        {
            GLTF::ResourceManager::VertexLayoutKey VtxKey;
            VtxKey.Elements.reserve(m_StagingVertexData->Sources.size());
            for (const auto& source_it : m_StagingVertexData->Sources)
            {
                const pxr::TfToken&                         Name   = source_it.first;
                const std::shared_ptr<pxr::HdBufferSource>& Source = source_it.second;
                VERIFY(NumVerts == Source->GetNumElements(), "Inconsistent number of elements in vertex data sources");
                const pxr::HdTupleType ElementType = Source->GetTupleType();
                const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;

                m_VertexData.NameToPoolIndex[Name] = static_cast<Uint32>(VtxKey.Elements.size());
                VtxKey.Elements.emplace_back(static_cast<Uint32>(ElementSize), BIND_VERTEX_BUFFER);
            }

            m_VertexData.PoolAllocation = ResMgr.AllocateVertices(VtxKey, static_cast<Uint32>(NumVerts));
            VERIFY_EXPR(m_VertexData.PoolAllocation);
        }
        else
        {
#ifdef DILIGENT_DEBUG
            VERIFY_EXPR(!m_StagingIndexData);
            VERIFY(m_VertexData.PoolAllocation->GetVertexCount() == NumVerts,
                   "The number of vertices has changed, but staging index data is null indicating that the topology has not changed. This is unexpected.");
            for (const auto& source_it : m_StagingVertexData->Sources)
            {
                const pxr::TfToken& Name = source_it.first;
                VERIFY(m_VertexData.NameToPoolIndex.find(Name) != m_VertexData.NameToPoolIndex.end(), "Failed to find vertex buffer index for ", Name,
                       ". This is unexpected as when a new buffer is added, the topology is expected to change.");
            }
#endif
        }

        // WebGL/GLES do not support base vertex, so we need to adjust indices.
        const Uint32 StartVertex = m_VertexData.PoolAllocation->GetStartVertex();
        if (m_StagingIndexData && StartVertex != 0)
        {
            if (!m_StagingIndexData->Faces.Indices.empty())
            {
                for (pxr::GfVec3i& Tri : m_StagingIndexData->Faces.Indices)
                {
                    Tri[0] += StartVertex;
                    Tri[1] += StartVertex;
                    Tri[2] += StartVertex;
                }
            }

            if (!m_StagingIndexData->MeshEdgeIndices.empty())
            {
                for (pxr::GfVec2i& Edge : m_StagingIndexData->MeshEdgeIndices)
                {
                    Edge[0] += StartVertex;
                    Edge[1] += StartVertex;
                }
            }

            if (!m_StagingIndexData->PointIndices.empty())
            {
                for (Uint32& Point : m_StagingIndexData->PointIndices)
                {
                    Point += StartVertex;
                }
            }
            else
            {
                // If there are no point indices, we need to create them
                m_StagingIndexData->PointIndices.resize(m_Topology.GetNumPoints());
                for (Uint32 i = 0; i < m_StagingIndexData->PointIndices.size(); ++i)
                {
                    m_StagingIndexData->PointIndices[i] = StartVertex + i;
                }
            }
        }
    }

    if (m_StagingIndexData && static_cast<const HnRenderParam*>(RenderParam)->GetUseIndexPool())
    {
        if (!m_StagingIndexData->Faces.Indices.empty())
        {
            m_IndexData.FaceAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * m_IndexData.NumFaceTriangles * 3);
            m_IndexData.FaceStartIndex = m_IndexData.FaceAllocation->GetOffset() / sizeof(Uint32);
        }

        if (!m_StagingIndexData->MeshEdgeIndices.empty())
        {
            m_IndexData.EdgeAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * m_IndexData.NumEdges * 2);
            m_IndexData.EdgeStartIndex = m_IndexData.EdgeAllocation->GetOffset() / sizeof(Uint32);
        }

        if (!m_StagingIndexData->PointIndices.empty())
        {
            m_IndexData.PointsAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * m_Topology.GetNumPoints());
            m_IndexData.PointsStartIndex = m_IndexData.PointsAllocation->GetOffset() / sizeof(Uint32);
        }
    }
}

void HnMesh::UpdateVertexBuffers(HnRenderDelegate& RenderDelegate)
{
    const RenderDeviceX_N& Device{RenderDelegate.GetDevice()};

    if (!m_StagingVertexData)
    {
        UNEXPECTED("Vertex data is null");
        return;
    }

    for (auto source_it : m_StagingVertexData->Sources)
    {
        const pxr::HdBufferSource* pSource = source_it.second.get();
        if (pSource == nullptr)
            continue;
        const pxr::TfToken& PrimName = source_it.first;

        const size_t           NumElements = pSource->GetNumElements();
        const pxr::HdTupleType ElementType = pSource->GetTupleType();
        const size_t           ElementSize = HdDataSizeOfType(ElementType.type) * ElementType.count;
        if (PrimName == pxr::HdTokens->points)
            VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected vertex element type");
        else if (PrimName == pxr::HdTokens->normals)
            VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected normal element type");
        else if (PrimName == pxr::HdTokens->displayColor)
            VERIFY(ElementType.type == pxr::HdTypeFloatVec3 && ElementType.count == 1, "Unexpected vertex color element type");
        else if (PrimName == HnTokens->joints)
            VERIFY(ElementType.type == pxr::HdTypeFloatVec4 && ElementType.count == 2, "Unexpected joints element type");

        RefCntAutoPtr<IBuffer> pBuffer;
        if (!m_VertexData.PoolAllocation)
        {
            const auto BufferName = GetId().GetString() + " - " + PrimName.GetString();
            BufferDesc Desc{
                BufferName.c_str(),
                NumElements * ElementSize,
                BIND_VERTEX_BUFFER,
                USAGE_IMMUTABLE,
            };

            BufferData InitData{pSource->GetData(), Desc.Size};
            pBuffer = Device.CreateBuffer(Desc, &InitData);

            StateTransitionDesc Barrier{pBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_VERTEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            RenderDelegate.GetDeviceContext()->TransitionResourceStates(1, &Barrier);
        }
        else
        {
            auto idx_it = m_VertexData.NameToPoolIndex.find(PrimName);
            if (idx_it != m_VertexData.NameToPoolIndex.end())
            {
                pBuffer = m_VertexData.PoolAllocation->GetBuffer(idx_it->second);

                IDeviceContext* pCtx = RenderDelegate.GetDeviceContext();
                VERIFY_EXPR(m_VertexData.PoolAllocation->GetVertexCount() == NumElements);
                pCtx->UpdateBuffer(pBuffer, m_VertexData.PoolAllocation->GetStartVertex() * ElementSize, NumElements * ElementSize, pSource->GetData(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else
            {
                UNEXPECTED("Failed to find vertex buffer index for ", PrimName);
            }
        }

        m_VertexData.Buffers[source_it.first] = pBuffer;
    }

    m_StagingVertexData.reset();
}

void HnMesh::UpdateIndexBuffer(HnRenderDelegate& RenderDelegate)
{
    VERIFY_EXPR(m_StagingIndexData);

    auto PrepareIndexBuffer = [&](const char*           BufferName,
                                  const void*           pData,
                                  size_t                DataSize,
                                  IBufferSuballocation* pSuballocation) {
        const std::string Name = GetId().GetString() + " - " + BufferName;

        IDeviceContext* pCtx = RenderDelegate.GetDeviceContext();
        if (pSuballocation == nullptr)
        {
            BufferDesc Desc{
                Name.c_str(),
                DataSize,
                BIND_INDEX_BUFFER,
                USAGE_IMMUTABLE,
            };
            BufferData InitData{pData, Desc.Size};

            const RenderDeviceX_N& Device{RenderDelegate.GetDevice()};
            RefCntAutoPtr<IBuffer> pBuffer = Device.CreateBuffer(Desc, &InitData);

            StateTransitionDesc Barrier{pBuffer, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_INDEX_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            pCtx->TransitionResourceStates(1, &Barrier);

            return pBuffer;
        }
        else
        {
            RefCntAutoPtr<IBuffer> pBuffer{pSuballocation->GetBuffer()};
            VERIFY_EXPR(pSuballocation->GetSize() == DataSize);
            pCtx->UpdateBuffer(pBuffer, pSuballocation->GetOffset(), DataSize, pData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            return pBuffer;
        }
    };

    if (!m_StagingIndexData->Faces.Indices.empty())
    {
        VERIFY_EXPR(m_IndexData.NumFaceTriangles == static_cast<size_t>(m_StagingIndexData->Faces.Indices.size()));
        static_assert(sizeof(m_StagingIndexData->Faces.Indices[0]) == sizeof(Uint32) * 3, "Unexpected triangle data size");
        m_IndexData.Faces = PrepareIndexBuffer("Triangle Index Buffer",
                                               m_StagingIndexData->Faces.Indices.data(),
                                               m_IndexData.NumFaceTriangles * sizeof(Uint32) * 3,
                                               m_IndexData.FaceAllocation);
    }

    if (!m_StagingIndexData->MeshEdgeIndices.empty())
    {
        VERIFY_EXPR(m_IndexData.NumEdges == static_cast<Uint32>(m_StagingIndexData->MeshEdgeIndices.size()));
        m_IndexData.Edges = PrepareIndexBuffer("Edge Index Buffer",
                                               m_StagingIndexData->MeshEdgeIndices.data(),
                                               m_IndexData.NumEdges * sizeof(Uint32) * 2,
                                               m_IndexData.EdgeAllocation);
    }

    if (!m_StagingIndexData->PointIndices.empty())
    {
        VERIFY_EXPR(m_Topology.GetNumPoints() == static_cast<int>(m_StagingIndexData->PointIndices.size()));
        m_IndexData.Points = PrepareIndexBuffer("Points Index Buffer",
                                                m_StagingIndexData->PointIndices.data(),
                                                m_Topology.GetNumPoints() * sizeof(Uint32),
                                                m_IndexData.PointsAllocation);
    }

    m_StagingIndexData.reset();
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
}

void HnMesh::UpdateDrawItemGpuTopology()
{
    Uint32 SubsetIdx = 0;
    ProcessDrawItems(
        [&](HnDrawItem& DrawItem) {
            if (m_Topology.GetGeomSubsets().empty())
            {
                DrawItem.SetFaces({
                    m_IndexData.Faces,
                    m_IndexData.FaceStartIndex,
                    m_IndexData.NumFaceTriangles * 3,
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
                m_IndexData.Edges,
                m_IndexData.EdgeStartIndex,
                m_IndexData.NumEdges * 2,
            });

            DrawItem.SetPoints({
                m_IndexData.Points,
                m_IndexData.PointsStartIndex,
                static_cast<Uint32>(m_Topology.GetNumPoints()),
            });
        },
        [&](const pxr::HdGeomSubset& Subset, HnDrawItem& DrawItem) {
            const GeometrySubsetRange& SubsetRange = m_IndexData.Subsets[SubsetIdx++];
            DrawItem.SetFaces({
                m_IndexData.Faces,
                m_IndexData.FaceStartIndex + SubsetRange.StartIndex,
                SubsetRange.NumIndices,
            });
            // Do not set edges and points for subsets
            DrawItem.SetEdges({});
            DrawItem.SetPoints({});
        });
    VERIFY_EXPR(m_IndexData.Subsets.size() == SubsetIdx);
}

void HnMesh::CommitGPUResources(HnRenderDelegate& RenderDelegate)
{
    if (m_StagingIndexData)
    {
        UpdateIndexBuffer(RenderDelegate);
        UpdateDrawItemGpuTopology();
    }

    if (m_StagingVertexData)
    {
        UpdateVertexBuffers(RenderDelegate);
        UpdateDrawItemGpuGeometry(RenderDelegate);
    }
}

IBuffer* HnMesh::GetVertexBuffer(const pxr::TfToken& Name) const
{
    auto it = m_VertexData.Buffers.find(Name);
    return it != m_VertexData.Buffers.end() ? it->second.RawPtr() : nullptr;
}

} // namespace USD

} // namespace Diligent
