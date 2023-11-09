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

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"

#include "pxr/base/gf/vec2f.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/vtBufferSource.h"

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
            UpdateFaceVaryingPrimvars(SceneDelegate, RenderParam, DirtyBits, ReprToken);
            if (!m_VertexData->FaceSources.empty())
            {
                ConvertVertexPrimvarSources();
                // Face data is not indexed
                m_IndexData->TrianglesFaceIndices.clear();
            }
        }
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
            m_VertexData->VertexSources.emplace(PrimDesc.name, std::move(BufferSource));
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
                                       const pxr::TfToken&   ReprToken)
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
                m_VertexData->FaceSources.emplace(PrimDesc.name, std::move(BufferSource));
            }
        }
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

void HnMesh::ConvertVertexPrimvarSources()
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

    for (auto source_it : m_VertexData->VertexSources)
    {
        const auto& pSource = source_it.second;
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
        m_VertexData->FaceSources.emplace(source_it.first, std::move(FaceSource));
    }
}

void HnMesh::UpdateVertexBuffers(const RenderDeviceX_N& Device)
{
    if (!m_VertexData)
    {
        UNEXPECTED("Vertex data is null");
        return;
    }

    auto CreateBuffer = [&](const pxr::HdBufferSource* pSource, const pxr::TfToken& PrimName) {
        if (pSource == nullptr)
            return RefCntAutoPtr<IBuffer>{};

        const auto NumElements = pSource->GetNumElements();
        const auto ElementType = pSource->GetTupleType().type;
        const auto ElementSize = HdDataSizeOfType(ElementType);
        if (PrimName == pxr::HdTokens->points)
            VERIFY(ElementType == pxr::HdTypeFloatVec3, "Unexpected vertex size");
        else if (PrimName == pxr::HdTokens->normals)
            VERIFY(ElementType == pxr::HdTypeFloatVec3, "Unexpected normal size");

        const auto Name = GetId().GetString() + " - " + PrimName.GetString();
        BufferDesc Desc{
            Name.c_str(),
            NumElements * ElementSize,
            BIND_VERTEX_BUFFER,
            USAGE_IMMUTABLE,
        };

        BufferData InitData{pSource->GetData(), Desc.Size};
        return Device.CreateBuffer(Desc, &InitData);
    };

    auto points_it = m_VertexData->VertexSources.find(pxr::HdTokens->points);
    if (points_it == m_VertexData->VertexSources.end())
    {
        LOG_ERROR_MESSAGE("Vertex data does not contain points");
        return;
    }

    m_pPointsVertexBuffer = CreateBuffer(points_it->second.get(), pxr::HdTokens->points);

    if (!m_VertexData->FaceSources.empty())
    {
        for (auto source_it : m_VertexData->FaceSources)
        {
            m_FaceVertexBuffers[source_it.first] = CreateBuffer(source_it.second.get(), source_it.first);
        }
    }
    else
    {
        for (auto source_it : m_VertexData->VertexSources)
        {
            m_FaceVertexBuffers[source_it.first] = source_it.first == pxr::HdTokens->points ?
                m_pPointsVertexBuffer :
                CreateBuffer(source_it.second.get(), source_it.first);
        }
    }

    m_VertexData.reset();
}

void HnMesh::UpdateIndexBuffer(const RenderDeviceX_N& Device)
{
    VERIFY_EXPR(m_IndexData);

    if (!m_IndexData->TrianglesFaceIndices.empty())
    {
        const auto Name = GetId().GetString() + " - Triangle Index Buffer";

        VERIFY_EXPR(m_NumFaceTriangles == static_cast<size_t>(m_IndexData->TrianglesFaceIndices.size()));
        static_assert(sizeof(m_IndexData->TrianglesFaceIndices[0]) == sizeof(Uint32) * 3, "Unexpected triangle data size");

        BufferDesc Desc{
            Name.c_str(),
            m_NumFaceTriangles * sizeof(Uint32) * 3,
            BIND_INDEX_BUFFER,
            USAGE_IMMUTABLE,
        };

        BufferData InitData{m_IndexData->TrianglesFaceIndices.data(), Desc.Size};
        m_pFaceIndexBuffer = Device.CreateBuffer(Desc, &InitData);
    }

    if (!m_IndexData->MeshEdgeIndices.empty())
    {
        const auto Name = GetId().GetString() + " - Edge Index Buffer";

        VERIFY_EXPR(m_NumEdges == static_cast<Uint32>(m_IndexData->MeshEdgeIndices.size()));

        BufferDesc Desc{
            Name.c_str(),
            m_NumEdges * sizeof(Uint32) * 2,
            BIND_INDEX_BUFFER,
            USAGE_IMMUTABLE,
        };

        BufferData InitData{m_IndexData->MeshEdgeIndices.data(), Desc.Size};
        m_pEdgeIndexBuffer = Device.CreateBuffer(Desc, &InitData);
    }

    m_IndexData.reset();
}

void HnMesh::CommitGPUResources(IRenderDevice* pDevice)
{
    if (m_IndexData)
    {
        UpdateIndexBuffer(pDevice);
    }

    if (m_VertexData)
    {
        UpdateVertexBuffers(pDevice);
    }
}

IBuffer* HnMesh::GetFaceVertexBuffer(const pxr::TfToken& Name) const
{
    auto it = m_FaceVertexBuffers.find(Name);
    return it != m_FaceVertexBuffers.end() ? it->second.RawPtr() : nullptr;
}

} // namespace USD

} // namespace Diligent
