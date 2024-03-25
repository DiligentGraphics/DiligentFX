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
#include "GfTypeConversions.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"
#include "GLTFResourceManager.hpp"
#include "EngineMemory.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/vtBufferSource.h"
#include "pxr/imaging/hd/vertexAdjacency.h"
#include "pxr/imaging/hd/smoothNormals.h"

namespace Diligent
{

namespace USD
{

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

    bool UpdateMaterials = false;
    if (*DirtyBits & pxr::HdChangeTracker::DirtyMaterialId)
    {
        const pxr::SdfPath& MaterialId = Delegate->GetMaterialId(GetId());
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

    const pxr::SdfPath& Id = GetId();
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

    pxr::HdMeshUtil MeshUtil{&m_Topology, Id};
    pxr::VtIntArray PrimitiveParams;
    MeshUtil.ComputeTriangleIndices(&m_StagingIndexData->TrianglesFaceIndices, &PrimitiveParams, nullptr);
    MeshUtil.EnumerateEdges(&m_StagingIndexData->MeshEdgeIndices);
    m_IndexData.NumFaceTriangles = static_cast<Uint32>(m_StagingIndexData->TrianglesFaceIndices.size());
    m_IndexData.NumEdges         = static_cast<Uint32>(m_StagingIndexData->MeshEdgeIndices.size());

    DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
}

static std::shared_ptr<pxr::HdBufferSource> CreateBufferSource(const pxr::TfToken& Name, const pxr::VtValue& Data, size_t ExpectedNumElements, const pxr::SdfPath& MeshId)
{
    auto BufferSource = std::make_shared<pxr::HdVtBufferSource>(
        Name,
        Data,
        1,    // values per element
        false // whether doubles are supported or must be converted to floats
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
            AddPrimvarSource(ExtCompPrimDesc);
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
        m_Data(HdDataSizeOfType(TupleType.type) * NumElements)
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

void HnMesh::ConvertVertexPrimvarSources(FaceSourcesMapType&& FaceSources)
{
    pxr::VtVec3iArray TrianglesFaceIndices;
    if (!m_StagingIndexData || m_StagingIndexData->TrianglesFaceIndices.empty())
    {
        // Need to regenerate triangle indices
        pxr::HdMeshUtil MeshUtil{&m_Topology, GetId()};
        pxr::VtIntArray PrimitiveParams;
        MeshUtil.ComputeTriangleIndices(&TrianglesFaceIndices, &PrimitiveParams, nullptr);
        if (TrianglesFaceIndices.empty())
            return;
    }
    const pxr::VtVec3iArray& Indices = !TrianglesFaceIndices.empty() ? TrianglesFaceIndices : m_StagingIndexData->TrianglesFaceIndices;
    VERIFY(Indices.size() == m_IndexData.NumFaceTriangles,
           "The number of indices is not consistent with the previously computed value. "
           "This may indicate that the topology was not updated during the last sync.");

    // Unpack vertex sources by unfolding triangle indices into linear list of vertices
    for (auto& source_it : m_StagingVertexData->Sources)
    {
        auto& pSource = source_it.second;
        if (pSource == nullptr)
            continue;

        const auto*       pSrcData    = static_cast<const Uint8*>(pSource->GetData());
        const pxr::HdType ElementType = pSource->GetTupleType().type;
        const size_t      ElementSize = HdDataSizeOfType(ElementType);

        auto  FaceSource = std::make_shared<TriangulatedFaceBufferSource>(pSource->GetName(), pSource->GetTupleType(), Indices.size() * 3);
        auto& FaceData   = FaceSource->GetData();
        VERIFY_EXPR(FaceData.size() == Indices.size() * 3 * ElementSize);
        auto* pDstData = FaceData.data();
        for (size_t i = 0; i < Indices.size(); ++i)
        {
            const pxr::GfVec3i& Tri = Indices[i];
            for (size_t v = 0; v < 3; ++v)
            {
                const auto* pSrc = pSrcData + Tri[v] * ElementSize;
                memcpy(pDstData + (i * 3 + v) * ElementSize, pSrc, ElementSize);
            }
        }
        // Replace original vertex source with the triangulated face source
        pSource = std::move(FaceSource);
    }

    // Add face-varying sources
    for (auto& face_source_it : FaceSources)
    {
        auto inserted = m_StagingVertexData->Sources.emplace(face_source_it.first, std::move(face_source_it.second)).second;
        if (!inserted)
        {
            LOG_ERROR_MESSAGE("Failed to add face-varying source ", face_source_it.first, " to ", GetId(), " as vertex source with the same name already exists.");
        }
    }

    // Mapping from the original vertex index to the first occurrence of this vertex in the unfolded list
    //
    //  Verts:    A B C D E F
    //  Indices:  3 4 5 0 1 2
    //  Unfolded: D E F A B C
    //  Mapping:  0->3, 1->4, 2->5, 3->0, 4->1, 5->2
    std::unordered_map<size_t, size_t> ReverseVertexMapping;
    for (size_t i = 0; i < Indices.size(); ++i)
    {
        const pxr::GfVec3i& Tri = Indices[i];
        for (size_t v = 0; v < 3; ++v)
            ReverseVertexMapping.emplace(Tri[v], i * 3 + v);
    }

    // Replace original triangle indices with the list of unfolded face indices
    m_StagingIndexData->TrianglesFaceIndices.resize(GetNumFaceTriangles());
    for (Uint32 i = 0; i < m_StagingIndexData->TrianglesFaceIndices.size(); ++i)
    {
        pxr::GfVec3i& Tri{m_StagingIndexData->TrianglesFaceIndices[i]};
        Tri[0] = i * 3 + 0;
        Tri[1] = i * 3 + 1;
        Tri[2] = i * 3 + 2;
    }

    // Update edge indices
    for (pxr::GfVec2i& Edge : m_StagingIndexData->MeshEdgeIndices)
    {
        auto v0_it = ReverseVertexMapping.find(Edge[0]);
        auto v1_it = ReverseVertexMapping.find(Edge[1]);
        if (v0_it != ReverseVertexMapping.end() && v1_it != ReverseVertexMapping.end())
        {
            Edge[0] = static_cast<int>(v0_it->second);
            Edge[1] = static_cast<int>(v1_it->second);
        }
        else
        {
            Edge[0] = 0;
            Edge[1] = 0;
        }
    }

    // Create point indices
    m_StagingIndexData->PointIndices.resize(GetNumPoints());
    for (size_t i = 0; i < m_StagingIndexData->PointIndices.size(); ++i)
    {
        auto v_it                           = ReverseVertexMapping.find(i);
        m_StagingIndexData->PointIndices[i] = v_it != ReverseVertexMapping.end() ? static_cast<Uint32>(v_it->second) : 0;
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
                const auto ElementType = Source->GetTupleType().type;
                const auto ElementSize = HdDataSizeOfType(ElementType);

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
            if (!m_StagingIndexData->TrianglesFaceIndices.empty())
            {
                for (pxr::GfVec3i& Tri : m_StagingIndexData->TrianglesFaceIndices)
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
                m_StagingIndexData->PointIndices.resize(GetNumPoints());
                for (Uint32 i = 0; i < m_StagingIndexData->PointIndices.size(); ++i)
                {
                    m_StagingIndexData->PointIndices[i] = StartVertex + i;
                }
            }
        }
    }

    if (m_StagingIndexData && static_cast<const HnRenderParam*>(RenderParam)->GetUseIndexPool())
    {
        if (!m_StagingIndexData->TrianglesFaceIndices.empty())
        {
            m_IndexData.FaceAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * GetNumFaceTriangles() * 3);
            m_IndexData.FaceStartIndex = m_IndexData.FaceAllocation->GetOffset() / sizeof(Uint32);
        }

        if (!m_StagingIndexData->MeshEdgeIndices.empty())
        {
            m_IndexData.EdgeAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * GetNumEdges() * 2);
            m_IndexData.EdgeStartIndex = m_IndexData.EdgeAllocation->GetOffset() / sizeof(Uint32);
        }

        if (!m_StagingIndexData->PointIndices.empty())
        {
            m_IndexData.PointsAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * GetNumPoints());
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

        const auto NumElements = pSource->GetNumElements();
        const auto ElementType = pSource->GetTupleType().type;
        const auto ElementSize = HdDataSizeOfType(ElementType);
        if (PrimName == pxr::HdTokens->points)
            VERIFY(ElementType == pxr::HdTypeFloatVec3, "Unexpected vertex size");
        else if (PrimName == pxr::HdTokens->normals)
            VERIFY(ElementType == pxr::HdTypeFloatVec3, "Unexpected normal size");

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

    if (!m_StagingIndexData->TrianglesFaceIndices.empty())
    {
        VERIFY_EXPR(GetNumFaceTriangles() == static_cast<size_t>(m_StagingIndexData->TrianglesFaceIndices.size()));
        static_assert(sizeof(m_StagingIndexData->TrianglesFaceIndices[0]) == sizeof(Uint32) * 3, "Unexpected triangle data size");
        m_IndexData.Faces = PrepareIndexBuffer("Triangle Index Buffer",
                                               m_StagingIndexData->TrianglesFaceIndices.data(),
                                               GetNumFaceTriangles() * sizeof(Uint32) * 3,
                                               m_IndexData.FaceAllocation);
    }

    if (!m_StagingIndexData->MeshEdgeIndices.empty())
    {
        VERIFY_EXPR(GetNumEdges() == static_cast<Uint32>(m_StagingIndexData->MeshEdgeIndices.size()));
        m_IndexData.Edges = PrepareIndexBuffer("Edge Index Buffer",
                                               m_StagingIndexData->MeshEdgeIndices.data(),
                                               GetNumEdges() * sizeof(Uint32) * 2,
                                               m_IndexData.EdgeAllocation);
    }

    if (!m_StagingIndexData->PointIndices.empty())
    {
        VERIFY_EXPR(GetNumPoints() == static_cast<Uint32>(m_StagingIndexData->PointIndices.size()));
        m_IndexData.Points = PrepareIndexBuffer("Points Index Buffer",
                                                m_StagingIndexData->PointIndices.data(),
                                                GetNumPoints() * sizeof(Uint32),
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
            Geo.Positions = GetVertexBuffer(pxr::HdTokens->points);
            Geo.Normals   = GetVertexBuffer(pxr::HdTokens->normals);

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
    HnDrawItem::TopologyData FaceTopology{
        GetFaceIndexBuffer(),
        GetFaceStartIndex(),
        GetNumFaceTriangles() * 3,
    };
    HnDrawItem::TopologyData EdgeTopology{
        GetEdgeIndexBuffer(),
        GetEdgeStartIndex(),
        GetNumEdges() * 2,
    };
    HnDrawItem::TopologyData PointsTopology{
        GetPointsIndexBuffer(),
        GetPointsStartIndex(),
        GetNumPoints(),
    };

    ProcessDrawItems(
        [&](HnDrawItem& DrawItem) {
            if (m_Topology.GetGeomSubsets().empty())
            {
                DrawItem.SetFaces(FaceTopology);
                DrawItem.SetEdges(EdgeTopology);
                DrawItem.SetPoints(PointsTopology);
            }
            else
            {
                // Do not set topology if there are geometry subsets, so
                // that the render pass skips this draw item.
                DrawItem.SetFaces({});
                DrawItem.SetEdges({});
                DrawItem.SetPoints({});
            }
        },
        [&](const pxr::HdGeomSubset& Subset, HnDrawItem& DrawItem) {
            DrawItem.SetFaces(FaceTopology);
            DrawItem.SetEdges(EdgeTopology);
            DrawItem.SetPoints(PointsTopology);
        });
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
