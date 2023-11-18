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


#include "HnMesh.hpp"
#include "HnTokens.hpp"
#include "HnMaterial.hpp"
#include "HnRenderDelegate.hpp"
#include "HnRenderParam.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"
#include "GLTFResourceManager.hpp"

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
                       Uint32              UID)
{
    return new HnMesh{typeId, id, UID};
}

HnMesh::HnMesh(pxr::TfToken const& typeId,
               pxr::SdfPath const& id,
               Uint32              UID) :
    m_UID{UID},
    pxr::HdMesh{id}
{
}

HnMesh::~HnMesh()
{
}

pxr::HdDirtyBits HnMesh::GetInitialDirtyBitsMask() const
{
    // Set all bits except the varying flag
    return pxr::HdChangeTracker::AllSceneDirtyBits & ~pxr::HdChangeTracker::Varying;
}

static pxr::TfToken ComputeMaterialTag(pxr::HdSceneDelegate* Delegate,
                                       const pxr::SdfPath&   MaterialId)
{
    if (const HnMaterial* Material = static_cast<const HnMaterial*>(Delegate->GetRenderIndex().GetSprim(pxr::HdPrimTypeTokens->material, MaterialId)))
    {
        return Material->GetTag();
    }

    return HnMaterialTagTokens->defaultTag;
}

void HnMesh::UpdateReprMaterialTags(pxr::HdSceneDelegate* SceneDelegate,
                                    pxr::HdRenderParam*   RenderParam)
{
    const pxr::TfToken        MeshMaterialTag = ComputeMaterialTag(SceneDelegate, GetMaterialId());
    const pxr::HdGeomSubsets& GeomSubsets     = m_Topology.GetGeomSubsets();
    const size_t              NumGeomSubsets  = GeomSubsets.size();
    for (auto const& token_repr : _reprs)
    {
        const pxr::TfToken&        Token = token_repr.first;
        pxr::HdReprSharedPtr       Repr  = token_repr.second;
        _MeshReprConfig::DescArray Descs = _GetReprDesc(Token);

        size_t DrawItemIdx       = 0;
        size_t GeomSubsetDescIdx = 0;
        for (const auto& Desc : Descs)
        {
            if (Desc.geomStyle == pxr::HdMeshGeomStyleInvalid)
                continue;

            {
                pxr::HdDrawItem* Item = Repr->GetDrawItem(DrawItemIdx++);
                Item->SetMaterialTag(MeshMaterialTag);
            }

            // Update geom subset draw items if they exist
            if (Desc.geomStyle != pxr::HdMeshGeomStylePoints)
            {
                for (size_t i = 0; i < NumGeomSubsets; ++i)
                {
                    if (pxr::HdDrawItem* Item = Repr->GetDrawItemForGeomSubset(GeomSubsetDescIdx, NumGeomSubsets, i))
                    {
                        const pxr::SdfPath& MaterialId = GeomSubsets[i].materialId;
                        Item->SetMaterialTag(ComputeMaterialTag(SceneDelegate, MaterialId));
                    }
                }
                ++GeomSubsetDescIdx;
            }
        }
    }
}


void HnMesh::Sync(pxr::HdSceneDelegate* Delegate,
                  pxr::HdRenderParam*   RenderParam,
                  pxr::HdDirtyBits*     DirtyBits,
                  const pxr::TfToken&   ReprToken)
{
    if (*DirtyBits == pxr::HdChangeTracker::Clean)
        return;

    bool UpdateMaterialTags = false;
    if (*DirtyBits & pxr::HdChangeTracker::DirtyMaterialId)
    {
        const pxr::SdfPath& MaterialId = Delegate->GetMaterialId(GetId());
        if (GetMaterialId() != MaterialId)
        {
            SetMaterialId(MaterialId);
        }
        UpdateMaterialTags = true;
    }
    if (*DirtyBits & (pxr::HdChangeTracker::DirtyDisplayStyle | pxr::HdChangeTracker::NewRepr))
    {
        UpdateMaterialTags = true;
    }

    const pxr::SdfPath& Id = GetId();
    if (Delegate != nullptr && DirtyBits != nullptr)
    {
        UpdateRepr(*Delegate, RenderParam, *DirtyBits, ReprToken);
    }

    if (*DirtyBits & pxr::HdChangeTracker::DirtyMaterialId)
    {
        m_MaterialId = Delegate->GetMaterialId(Id);
    }

    if (UpdateMaterialTags)
    {
        UpdateReprMaterialTags(Delegate, RenderParam);
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

void HnMesh::AddGeometrySubsetDrawItems(const pxr::HdMeshReprDesc& ReprDesc, size_t NumGeomSubsets, pxr::HdRepr& Repr)
{
    if (ReprDesc.geomStyle == pxr::HdMeshGeomStylePoints)
        return;

    for (size_t i = 0; i < NumGeomSubsets; ++i)
    {
        pxr::HdRepr::DrawItemUniquePtr Item = std::make_unique<pxr::HdDrawItem>(&_sharedData);
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

    _MeshReprConfig::DescArray ReprDescs      = _GetReprDesc(ReprToken);
    const size_t               NumGeomSubsets = m_Topology.GetGeomSubsets().size();
    for (const pxr::HdMeshReprDesc& Desc : ReprDescs)
    {
        if (Desc.geomStyle == pxr::HdMeshGeomStyleInvalid)
            continue;

        Repr.AddDrawItem(std::make_unique<pxr::HdDrawItem>(&_sharedData));
        AddGeometrySubsetDrawItems(Desc, NumGeomSubsets, Repr);
    }
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

    if (pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id))
    {
        UpdateTopology(SceneDelegate, RenderParam, DirtyBits, ReprToken);
    }
    if (pxr::HdChangeTracker::IsAnyPrimvarDirty(DirtyBits, Id))
    {
        m_VertexData = std::make_unique<VertexData>();
        if (UpdateVertexPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken))
        {
            // Collect face-varying primvar sources
            FaceSourcesMapType FaceSources;
            UpdateFaceVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken, FaceSources);

            // If there are neither vertex nor face-varying normals, generate smooth normals
            if (m_VertexData->Sources.find(pxr::HdTokens->normals) == m_VertexData->Sources.end() &&
                FaceSources.find(pxr::HdTokens->normals) == FaceSources.end())
            {
                GenerateSmoothNormals();
            }

            // If there are face-varying sources, we need to convert all vertex sources into face-varying sources
            if (!FaceSources.empty())
            {
                ConvertVertexPrimvarSources(std::move(FaceSources));
            }
        }
        UpdateConstantPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken);

        // Allocate space for vertex and index buffers.
        // Note that this only reserves space, but does not create any buffers.
        AllocatePooledResources(SceneDelegate, RenderParam);

        DirtyBits &= ~pxr::HdChangeTracker::DirtyPrimvar;
    }

    if (pxr::HdChangeTracker::IsTransformDirty(DirtyBits, Id))
    {
        pxr::GfMatrix4d Transform = SceneDelegate.GetTransform(Id);
        m_Transform               = float4x4::MakeMatrix(Transform.data());
    }
    if (pxr::HdChangeTracker::IsVisibilityDirty(DirtyBits, Id))
    {
        _sharedData.visible = SceneDelegate.GetVisible(Id);
    }

    DirtyBits &= ~pxr::HdChangeTracker::NewRepr;
}

void HnMesh::UpdateDrawItemsForGeometrySubsets(pxr::HdSceneDelegate& SceneDelegate,
                                               pxr::HdRenderParam*   RenderParam)
{
    // (Re)create geom subset draw items
    const size_t NumGeomSubsets = m_Topology.GetGeomSubsets().size();
    for (auto const& token_repr : _reprs)
    {
        const pxr::TfToken&        Token = token_repr.first;
        pxr::HdReprSharedPtr       Repr  = token_repr.second;
        _MeshReprConfig::DescArray Descs = _GetReprDesc(Token);

        // Clear all previous geom subset draw items.
        Repr->ClearGeomSubsetDrawItems();
        for (const pxr::HdMeshReprDesc& Desc : Descs)
        {
            if (Desc.geomStyle == pxr::HdMeshGeomStyleInvalid)
                continue;

            AddGeometrySubsetDrawItems(Desc, NumGeomSubsets, *Repr);
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
    }

    m_IndexData = std::make_unique<IndexData>();

    pxr::HdMeshUtil MeshUtil{&m_Topology, Id};
    pxr::VtIntArray PrimitiveParams;
    MeshUtil.ComputeTriangleIndices(&m_IndexData->TrianglesFaceIndices, &PrimitiveParams, nullptr);
    MeshUtil.EnumerateEdges(&m_IndexData->MeshEdgeIndices);
    m_NumFaceTriangles = static_cast<Uint32>(m_IndexData->TrianglesFaceIndices.size());
    m_NumEdges         = static_cast<Uint32>(m_IndexData->MeshEdgeIndices.size());

    DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
}

static std::shared_ptr<pxr::HdBufferSource> CreateBufferSource(const pxr::TfToken& Name, const pxr::VtValue& Data, size_t ExpectedNumElements)
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
        LOG_WARNING_MESSAGE("Primvar ", Name, " has only ", BufferSource->GetNumElements(),
                            " elements, while its topology expects at least ", ExpectedNumElements,
                            " elements. Skipping primvar.");
        return nullptr;
    }
    else if (BufferSource->GetNumElements() > ExpectedNumElements)
    {
        // If the primvar has more data than needed, we issue a warning,
        // but don't skip the primvar update. Truncate the buffer to
        // the expected length.
        LOG_WARNING_MESSAGE("Primvar ", Name, " has only ", BufferSource->GetNumElements(),
                            " elements, while its topology expects ", ExpectedNumElements,
                            " elements. Truncating.");
        BufferSource->Truncate(ExpectedNumElements);
    }

    return BufferSource;
}

bool HnMesh::UpdateVertexPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                  pxr::HdRenderParam*   RenderParam,
                                  pxr::HdDirtyBits&     DirtyBits,
                                  const pxr::TfToken&   ReprToken)
{
    VERIFY_EXPR(m_VertexData);
    const pxr::SdfPath& Id = GetId();

    const int NumPoints = m_Topology.GetNumPoints();

    pxr::HdPrimvarDescriptorVector VertexPrims = GetPrimvarDescriptors(&SceneDelegate, pxr::HdInterpolationVertex);
    for (const pxr::HdPrimvarDescriptor& PrimDesc : VertexPrims)
    {
        if (!pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
            continue;

        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        if (PrimValue.IsEmpty())
            continue;

        if (auto BufferSource = CreateBufferSource(PrimDesc.name, PrimValue, NumPoints))
        {
            m_VertexData->Sources.emplace(PrimDesc.name, std::move(BufferSource));
        }
        else if (BufferSource->GetName() == pxr::HdTokens->points)
        {
            LOG_WARNING_MESSAGE("Skipping prim ", Id, " because its points data is insufficient.");
            return false;
        }
    }
    return true;
}

void HnMesh::UpdateFaceVaryingPrimvars(pxr::HdSceneDelegate& SceneDelegate,
                                       pxr::HdRenderParam*   RenderParam,
                                       pxr::HdDirtyBits&     DirtyBits,
                                       const pxr::TfToken&   ReprToken,
                                       FaceSourcesMapType&   FaceSources)
{
    VERIFY_EXPR(m_VertexData);
    const pxr::SdfPath& Id = GetId();

    pxr::HdPrimvarDescriptorVector FaceVaryingPrims = GetPrimvarDescriptors(&SceneDelegate, pxr::HdInterpolationFaceVarying);
    for (const pxr::HdPrimvarDescriptor& PrimDesc : FaceVaryingPrims)
    {
        if (!pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
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
            if (auto BufferSource = CreateBufferSource(PrimDesc.name, TriangulatedPrimValue, size_t{m_NumFaceTriangles} * 3))
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
    VERIFY_EXPR(m_VertexData);
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
            if (ElementType == pxr::HdTypeFloatVec3)
            {
                memcpy(m_DisplayColor.Data(), Source->GetData(), sizeof(float3));
            }
            else
            {
                LOG_WARNING_MESSAGE("Unexpected type of ", PrimDesc.name, " primvar: ", ElementType);
            }
        }
        else if (PrimDesc.name == pxr::HdTokens->displayOpacity)
        {
            if (ElementType == pxr::HdTypeFloat)
            {
                memcpy(&m_DisplayColor.w, Source->GetData(), sizeof(float));
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

    auto points_it = m_VertexData->Sources.find(pxr::HdTokens->points);
    if (points_it == m_VertexData->Sources.end())
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

    if (auto BufferSource = CreateBufferSource(pxr::HdTokens->normals, pxr::VtValue{Normals}, PointsSource.GetNumElements()))
    {
        m_VertexData->Sources.emplace(pxr::HdTokens->normals, std::move(BufferSource));
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
    if (!m_IndexData || m_IndexData->TrianglesFaceIndices.empty())
    {
        // Need to regenerate triangle indices
        pxr::HdMeshUtil MeshUtil{&m_Topology, GetId()};
        pxr::VtIntArray PrimitiveParams;
        MeshUtil.ComputeTriangleIndices(&TrianglesFaceIndices, &PrimitiveParams, nullptr);
        if (TrianglesFaceIndices.empty())
            return;
    }
    const pxr::VtVec3iArray& Indices = !TrianglesFaceIndices.empty() ? TrianglesFaceIndices : m_IndexData->TrianglesFaceIndices;
    VERIFY(Indices.size() == m_NumFaceTriangles,
           "The number of indices is not consistent with the previously computed value. "
           "This may indicate that the topology was not updated during the last sync.");

    // Unpack vertex sources by unfolding triangle indices into linear list of vertices
    for (auto& source_it : m_VertexData->Sources)
    {
        auto& pSource = source_it.second;
        if (pSource == nullptr)
            continue;

        const auto*       pSrcData    = static_cast<const Uint8*>(pSource->GetData());
        const size_t      NumElements = pSource->GetNumElements();
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
        auto inserted = m_VertexData->Sources.emplace(face_source_it.first, std::move(face_source_it.second)).second;
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
    m_IndexData->TrianglesFaceIndices.resize(GetNumFaceTriangles());
    for (Uint32 i = 0; i < m_IndexData->TrianglesFaceIndices.size(); ++i)
    {
        pxr::GfVec3i& Tri{m_IndexData->TrianglesFaceIndices[i]};
        Tri[0] = i * 3 + 0;
        Tri[1] = i * 3 + 1;
        Tri[2] = i * 3 + 2;
    }

    // Update edge indices
    for (pxr::GfVec2i& Edge : m_IndexData->MeshEdgeIndices)
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
    m_IndexData->PointIndices.resize(GetNumPoints());
    for (size_t i = 0; i < m_IndexData->PointIndices.size(); ++i)
    {
        auto v_it                    = ReverseVertexMapping.find(i);
        m_IndexData->PointIndices[i] = v_it != ReverseVertexMapping.end() ? static_cast<Uint32>(v_it->second) : 0;
    }
}

void HnMesh::AllocatePooledResources(pxr::HdSceneDelegate& SceneDelegate,
                                     pxr::HdRenderParam*   RenderParam)
{
    HnRenderDelegate*      RenderDelegate = static_cast<HnRenderDelegate*>(SceneDelegate.GetRenderIndex().GetRenderDelegate());
    GLTF::ResourceManager& ResMgr         = RenderDelegate->GetResourceManager();

    if (m_IndexData && static_cast<const HnRenderParam*>(RenderParam)->GetUseIndexPool())
    {
        if (!m_IndexData->TrianglesFaceIndices.empty())
        {
            m_FaceIndexAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * GetNumFaceTriangles() * 3);
            m_FaceStartIndex      = m_FaceIndexAllocation->GetOffset() / sizeof(Uint32);
        }

        if (!m_IndexData->MeshEdgeIndices.empty())
        {
            m_EdgeIndexAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * GetNumEdges() * 2);
            m_EdgeStartIndex      = m_EdgeIndexAllocation->GetOffset() / sizeof(Uint32);
        }

        if (!m_IndexData->PointIndices.empty())
        {
            m_PointsIndexAllocation = ResMgr.AllocateIndices(sizeof(Uint32) * GetNumPoints());
            m_PointsStartIndex      = m_PointsIndexAllocation->GetOffset() / sizeof(Uint32);
        }
    }

    if (m_VertexData && !m_VertexData->Sources.empty() && static_cast<const HnRenderParam*>(RenderParam)->GetUseVertexPool())
    {
        // Allocate vertex buffers for face data
        Uint32 StartVertex = 0;
        {
            GLTF::ResourceManager::VertexLayoutKey VtxKey;
            VtxKey.Elements.reserve(m_VertexData->Sources.size());
            const auto NumVerts = m_VertexData->Sources.begin()->second->GetNumElements();
            for (auto& source_it : m_VertexData->Sources)
            {
                VERIFY(NumVerts == source_it.second->GetNumElements(), "Inconsistent number of elements in vertex data sources");
                const auto ElementType = source_it.second->GetTupleType().type;
                const auto ElementSize = HdDataSizeOfType(ElementType);

                m_VertexData->NameToPoolIndex[source_it.first] = static_cast<Uint32>(VtxKey.Elements.size());
                VtxKey.Elements.emplace_back(static_cast<Uint32>(ElementSize), BIND_VERTEX_BUFFER);
            }

            m_VertexAllocation = ResMgr.AllocateVertices(VtxKey, static_cast<Uint32>(NumVerts));
            VERIFY_EXPR(m_VertexAllocation);
            StartVertex = m_VertexAllocation->GetStartVertex();
        }

        // WebGL/GLES do not support base vertex, so we need to adjust indices.
        if (StartVertex != 0)
        {
            if (!m_IndexData->TrianglesFaceIndices.empty())
            {
                for (pxr::GfVec3i& Tri : m_IndexData->TrianglesFaceIndices)
                {
                    Tri[0] += StartVertex;
                    Tri[1] += StartVertex;
                    Tri[2] += StartVertex;
                }
            }

            if (!m_IndexData->MeshEdgeIndices.empty())
            {
                for (pxr::GfVec2i& Edge : m_IndexData->MeshEdgeIndices)
                {
                    Edge[0] += StartVertex;
                    Edge[1] += StartVertex;
                }
            }

            if (!m_IndexData->PointIndices.empty())
            {
                for (Uint32& Point : m_IndexData->PointIndices)
                {
                    Point += StartVertex;
                }
            }
            else
            {
                // If there are no point indices, we need to create them
                m_IndexData->PointIndices.resize(GetNumPoints());
                for (Uint32 i = 0; i < m_IndexData->PointIndices.size(); ++i)
                {
                    m_IndexData->PointIndices[i] = StartVertex + i;
                }
            }
        }
    }
}

void HnMesh::UpdateVertexBuffers(HnRenderDelegate& RenderDelegate)
{
    const RenderDeviceX_N& Device{RenderDelegate.GetDevice()};

    if (!m_VertexData)
    {
        UNEXPECTED("Vertex data is null");
        return;
    }

    for (auto source_it : m_VertexData->Sources)
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
        if (!m_VertexAllocation)
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
        }
        else
        {
            auto idx_it = m_VertexData->NameToPoolIndex.find(PrimName);
            if (idx_it != m_VertexData->NameToPoolIndex.end())
            {
                pBuffer = m_VertexAllocation->GetBuffer(idx_it->second);

                IDeviceContext* pCtx = RenderDelegate.GetDeviceContext();
                VERIFY_EXPR(m_VertexAllocation->GetVertexCount() == NumElements);
                pCtx->UpdateBuffer(pBuffer, m_VertexAllocation->GetStartVertex() * ElementSize, NumElements * ElementSize, pSource->GetData(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else
            {
                UNEXPECTED("Failed to find vertex buffer index for ", PrimName);
            }
        }

        m_VertexBuffers[source_it.first] = pBuffer;
    }

    m_VertexData.reset();
}

void HnMesh::UpdateIndexBuffer(HnRenderDelegate& RenderDelegate)
{
    VERIFY_EXPR(m_IndexData);

    auto PrepareIndexBuffer = [&](const char*           BufferName,
                                  const void*           pData,
                                  size_t                DataSize,
                                  IBufferSuballocation* pSuballocation) {
        const std::string Name = GetId().GetString() + " - " + BufferName;

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
            return Device.CreateBuffer(Desc, &InitData);
        }
        else
        {
            RefCntAutoPtr<IBuffer> pBuffer{pSuballocation->GetBuffer()};
            IDeviceContext*        pCtx = RenderDelegate.GetDeviceContext();
            VERIFY_EXPR(pSuballocation->GetSize() == DataSize);
            pCtx->UpdateBuffer(pBuffer, pSuballocation->GetOffset(), DataSize, pData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            return pBuffer;
        }
    };

    if (!m_IndexData->TrianglesFaceIndices.empty())
    {
        VERIFY_EXPR(GetNumFaceTriangles() == static_cast<size_t>(m_IndexData->TrianglesFaceIndices.size()));
        static_assert(sizeof(m_IndexData->TrianglesFaceIndices[0]) == sizeof(Uint32) * 3, "Unexpected triangle data size");
        m_pFaceIndexBuffer = PrepareIndexBuffer("Triangle Index Buffer",
                                                m_IndexData->TrianglesFaceIndices.data(),
                                                GetNumFaceTriangles() * sizeof(Uint32) * 3,
                                                m_FaceIndexAllocation);
    }

    if (!m_IndexData->MeshEdgeIndices.empty())
    {
        VERIFY_EXPR(GetNumEdges() == static_cast<Uint32>(m_IndexData->MeshEdgeIndices.size()));
        m_pEdgeIndexBuffer = PrepareIndexBuffer("Edge Index Buffer",
                                                m_IndexData->MeshEdgeIndices.data(),
                                                GetNumEdges() * sizeof(Uint32) * 2,
                                                m_EdgeIndexAllocation);
    }

    if (!m_IndexData->PointIndices.empty())
    {
        VERIFY_EXPR(GetNumPoints() == static_cast<Uint32>(m_IndexData->PointIndices.size()));
        m_pPointsIndexBuffer = PrepareIndexBuffer("Points Index Buffer",
                                                  m_IndexData->PointIndices.data(),
                                                  GetNumPoints() * sizeof(Uint32),
                                                  m_PointsIndexAllocation);
    }

    m_IndexData.reset();
}

void HnMesh::CommitGPUResources(HnRenderDelegate& RenderDelegate)
{
    if (m_IndexData)
    {
        UpdateIndexBuffer(RenderDelegate);
    }

    if (m_VertexData)
    {
        UpdateVertexBuffers(RenderDelegate);
    }
}

IBuffer* HnMesh::GetVertexBuffer(const pxr::TfToken& Name) const
{
    auto it = m_VertexBuffers.find(Name);
    return it != m_VertexBuffers.end() ? it->second.RawPtr() : nullptr;
}

} // namespace USD

} // namespace Diligent
