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

    const bool TopologyDirty   = pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id);
    const bool AnyPrimvarDirty = pxr::HdChangeTracker::IsAnyPrimvarDirty(DirtyBits, Id);

    bool IndexDataDirty = TopologyDirty;
    if (TopologyDirty)
    {
        UpdateTopology(SceneDelegate, RenderParam, DirtyBits, ReprToken);
        DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
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

    if (IndexDataDirty)
    {
        UpdateIndexData();
    }

    if (AnyPrimvarDirty)
    {
        m_StagingVertexData = std::make_unique<StagingVertexData>();
        UpdateVertexAndVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, VertexPrimvarsInfo);

        if (m_StagingVertexData->Sources.find(pxr::HdTokens->points) != m_StagingVertexData->Sources.end() ||
            GetVertexBuffer(pxr::HdTokens->points) != nullptr)
        {
            // Collect face-varying primvar sources
            UpdateFaceVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, FacePrimvarsInfo);

            // If there are neither vertex nor face-varying normals, generate smooth normals
            if (m_StagingVertexData->Sources.find(pxr::HdTokens->normals) == m_StagingVertexData->Sources.end() &&
                !m_StagingVertexData->Points.IsEmpty())
            {
                GenerateSmoothNormals();
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

    if ((IndexDataDirty || AnyPrimvarDirty) && RenderParam != nullptr)
    {
        static_cast<HnRenderParam*>(RenderParam)->MakeAttribDirty(HnRenderParam::GlobalAttrib::MeshGeometry);
        ++m_GeometryVersion;
    }

    if (pxr::HdChangeTracker::IsTransformDirty(DirtyBits, Id))
    {
        entt::registry& Registry  = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate())->GetEcsRegistry();
        float4x4&       Transform = Registry.get<Components::Transform>(m_Entity).Val;

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

    DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
}

bool HnMesh::AddStagingBufferSourceForPrimvar(const pxr::TfToken&  Name,
                                              pxr::VtValue         Primvar,
                                              pxr::HdInterpolation Interpolation,
                                              int                  ValuesPerElement)
{
    if (!m_StagingVertexData)
    {
        UNEXPECTED("Staging vertex data is not initialized");
        return false;
    }

    if (Primvar.IsEmpty())
        return false;

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

    m_StagingVertexData->Sources.emplace(Name, std::move(BufferSource));

    return true;
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

bool HnMesh::AddJointInfluencesStagingBufferSource(const pxr::VtValue& NumInfluencesPerComponentVal,
                                                   const pxr::VtValue& InfluencesVal)
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

    return AddStagingBufferSourceForPrimvar(HnTokens->joints, pxr::VtValue{std::move(Joints)}, pxr::HdInterpolationVertex, 2);
}

void HnMesh::GetPrimvarsInfo(pxr::HdSceneDelegate& SceneDelegate,
                             pxr::HdDirtyBits&     DirtyBits,
                             PrimvarsInfo&         VertexPrimvarsInfo,
                             PrimvarsInfo&         FacePrimvarsInfo) const
{
    const pxr::SdfPath& Id = GetId();

    const HnRenderPass::SupportedVertexInputsSetType SupportedPrimvars = GetSupportedPrimvars(SceneDelegate.GetRenderIndex(), GetMaterialId(), m_Topology);

    auto UpdatePrimvarsInfo = [&](const pxr::HdInterpolation Interpolation,
                                  PrimvarsInfo&              PrimvarsInfo) {
        pxr::HdPrimvarDescriptorVector PrimVarDescs = GetPrimvarDescriptors(&SceneDelegate, Interpolation);
        for (const pxr::HdPrimvarDescriptor& PrimDesc : PrimVarDescs)
        {
            if (SupportedPrimvars.find(PrimDesc.name) == SupportedPrimvars.end())
                continue;

            ++PrimvarsInfo.Count;

            if (pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
            {
                PrimvarsInfo.Dirty.push_back(PrimDesc);
            }
        }
    };

    for (pxr::HdInterpolation Interpolation : {pxr::HdInterpolationVertex, pxr::HdInterpolationVarying})
    {
        UpdatePrimvarsInfo(Interpolation, VertexPrimvarsInfo);

        pxr::HdExtComputationPrimvarDescriptorVector CompPrimvarsDescs = SceneDelegate.GetExtComputationPrimvarDescriptors(Id, Interpolation);
        for (const pxr::HdExtComputationPrimvarDescriptor& ExtCompPrimDesc : CompPrimvarsDescs)
        {
            // Skinnig ext computation is created by the skeleton adapter
            VertexPrimvarsInfo.ExtComp.push_back(ExtCompPrimDesc);
        }
    }

    UpdatePrimvarsInfo(pxr::HdInterpolationFaceVarying, FacePrimvarsInfo);
}

void HnMesh::UpdateSkinningPrimvars(pxr::HdSceneDelegate&                         SceneDelegate,
                                    pxr::HdRenderParam*                           RenderParam,
                                    pxr::HdDirtyBits&                             DirtyBits,
                                    const pxr::TfToken&                           ReprToken,
                                    const pxr::HdExtComputationPrimvarDescriptor& SkinningCompPrimDesc)
{
    const pxr::HdRenderIndex& RenderIndex   = SceneDelegate.GetRenderIndex();
    const pxr::SdfPath&       SkinnigCompId = SkinningCompPrimDesc.sourceComputationId;
    const HnExtComputation*   SkinningComp  = static_cast<const HnExtComputation*>(RenderIndex.GetSprim(pxr::HdPrimTypeTokens->extComputation, SkinnigCompId));
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

    entt::registry&       Registry     = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate())->GetEcsRegistry();
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

        m_StagingVertexData->Points = RestPointsVal;

        AddStagingBufferSourceForPrimvar(SkinningCompPrimDesc.name, std::move(RestPointsVal), SkinningCompPrimDesc.interpolation);
        AddJointInfluencesStagingBufferSource(NumInfluencesPerComponentVal, InfluencesVal);

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
                                            const PrimvarsInfo&   VertexPrimvarsInfo)
{
    VERIFY_EXPR(m_StagingVertexData);

    for (const pxr::HdPrimvarDescriptor& PrimDesc : VertexPrimvarsInfo.Dirty)
    {
        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        if (PrimValue.IsEmpty())
            return;

        if (PrimDesc.name == pxr::HdTokens->points)
            m_StagingVertexData->Points = PrimValue;

        AddStagingBufferSourceForPrimvar(PrimDesc.name, std::move(PrimValue), PrimDesc.interpolation);
    }

    for (const pxr::HdExtComputationPrimvarDescriptor& ExtCompPrimDesc : VertexPrimvarsInfo.ExtComp)
    {
        // Skinnig ext computation is created by the skeleton adapter
        if (ExtCompPrimDesc.sourceComputationOutputName == HnSkinningPrivateTokens->skinnedPoints)
        {
            UpdateSkinningPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, ExtCompPrimDesc);
        }
    }
}

void HnMesh::UpdateFaceVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                       pxr::HdRenderParam*   RenderParam,
                                       pxr::HdDirtyBits&     DirtyBits,
                                       const pxr::TfToken&   ReprToken,
                                       const PrimvarsInfo&   FacePrimvarsInfo)
{
    VERIFY_EXPR(m_StagingVertexData);
    for (const pxr::HdPrimvarDescriptor& PrimDesc : FacePrimvarsInfo.Dirty)
    {
        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        AddStagingBufferSourceForPrimvar(PrimDesc.name, std::move(PrimValue), PrimDesc.interpolation);
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

    if (!m_StagingVertexData->Points.IsHolding<pxr::VtVec3fArray>())
    {
        LOG_ERROR_MESSAGE("Skipping smooth normal generation for ", GetId(), " because its points data is not float3.");
        return;
    }

    const pxr::VtVec3fArray& Points = m_StagingVertexData->Points.UncheckedGet<pxr::VtVec3fArray>();

    pxr::VtVec3fArray Normals = pxr::Hd_SmoothNormals::ComputeSmoothNormals(&Adjacency, static_cast<int>(Points.size()), Points.data());
    if (Normals.size() != Points.size())
    {
        LOG_ERROR_MESSAGE("Failed to generate smooth normals for ", GetId(), ". Expected ", Points.size(), " normals, got ", Normals.size(), ".");
        return;
    }

    AddStagingBufferSourceForPrimvar(pxr::HdTokens->normals, pxr::VtValue{std::move(Normals)}, pxr::HdInterpolationVertex);
}

void HnMesh::UpdateIndexData()
{
    m_StagingIndexData = std::make_unique<StagingIndexData>();

    HnMeshUtils     MeshUtils{m_Topology, GetId()};
    pxr::VtIntArray SubsetStart;
    MeshUtils.Triangulate(!m_HasFaceVaryingPrimvars, m_StagingIndexData->FaceIndices, SubsetStart);
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

    m_StagingIndexData->EdgeIndices = MeshUtils.ComputeEdgeIndices(!m_HasFaceVaryingPrimvars);
    m_IndexData.NumFaceTriangles    = static_cast<Uint32>(m_StagingIndexData->FaceIndices.size());
    m_IndexData.NumEdges            = static_cast<Uint32>(m_StagingIndexData->EdgeIndices.size());
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
            if (!m_StagingIndexData->FaceIndices.empty())
            {
                for (pxr::GfVec3i& Tri : m_StagingIndexData->FaceIndices)
                {
                    Tri[0] += StartVertex;
                    Tri[1] += StartVertex;
                    Tri[2] += StartVertex;
                }
            }

            if (!m_StagingIndexData->EdgeIndices.empty())
            {
                for (pxr::GfVec2i& Edge : m_StagingIndexData->EdgeIndices)
                {
                    Edge[0] += StartVertex;
                    Edge[1] += StartVertex;
                }
            }

            if (!m_StagingIndexData->PointIndices.empty())
            {
                for (int& Point : m_StagingIndexData->PointIndices)
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
        if (!m_StagingIndexData->FaceIndices.empty())
        {
            m_IndexData.FaceAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * m_IndexData.NumFaceTriangles * 3);
            m_IndexData.FaceStartIndex = m_IndexData.FaceAllocation->GetOffset() / sizeof(Uint32);
        }

        if (!m_StagingIndexData->EdgeIndices.empty())
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

    if (!m_StagingIndexData->FaceIndices.empty())
    {
        VERIFY_EXPR(m_IndexData.NumFaceTriangles == static_cast<size_t>(m_StagingIndexData->FaceIndices.size()));
        static_assert(sizeof(m_StagingIndexData->FaceIndices[0]) == sizeof(Uint32) * 3, "Unexpected triangle data size");
        m_IndexData.Faces = PrepareIndexBuffer("Triangle Index Buffer",
                                               m_StagingIndexData->FaceIndices.data(),
                                               m_IndexData.NumFaceTriangles * sizeof(Uint32) * 3,
                                               m_IndexData.FaceAllocation);
    }

    if (!m_StagingIndexData->EdgeIndices.empty())
    {
        VERIFY_EXPR(m_IndexData.NumEdges == static_cast<Uint32>(m_StagingIndexData->EdgeIndices.size()));
        m_IndexData.Edges = PrepareIndexBuffer("Edge Index Buffer",
                                               m_StagingIndexData->EdgeIndices.data(),
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
