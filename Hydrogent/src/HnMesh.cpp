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

#include "DebugUtilities.hpp"
#include "GraphicsTypesX.hpp"

#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/vtBufferSource.h"

namespace Diligent
{

namespace USD
{

std::shared_ptr<HnMesh> HnMesh::Create(pxr::TfToken const& typeId, pxr::SdfPath const& id)
{
    return std::shared_ptr<HnMesh>(new HnMesh{typeId, id});
}

HnMesh::HnMesh(pxr::TfToken const& typeId,
               pxr::SdfPath const& id) :
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

void HnMesh::Sync(pxr::HdSceneDelegate* Delegate,
                  pxr::HdRenderParam*   RenderParam,
                  pxr::HdDirtyBits*     DirtyBits,
                  const pxr::TfToken&   ReprToken)
{
    if (*DirtyBits == pxr::HdChangeTracker::Clean)
        return;

    if (Delegate != nullptr && DirtyBits != nullptr)
    {
        UpdateRepr(*Delegate, RenderParam, *DirtyBits, ReprToken);
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
    for (const pxr::HdMeshReprDesc& Desc : ReprDescs)
    {
        if (Desc.geomStyle == pxr::HdMeshGeomStyleInvalid)
            continue;

        Repr.AddDrawItem(std::make_unique<pxr::HdDrawItem>(&_sharedData));
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

    UpdateTopology(SceneDelegate, RenderParam, DirtyBits, ReprToken);
    UpdateVertexPrims(SceneDelegate, RenderParam, DirtyBits, ReprToken);

    _MeshReprConfig::DescArray ReprDescs = _GetReprDesc(ReprToken);

    // Iterate through all reprdescs for the current repr to figure out if any
    // of them requires smooth normals or flat normals. If either (or both)
    // are required, we will calculate them once and clean the bits.
    bool RequireSmoothNormals = false;
    bool RequireFlatNormals   = false;
    for (const pxr::HdMeshReprDesc& Desc : ReprDescs)
    {
        if (Desc.flatShadingEnabled)
        {
            RequireFlatNormals = true;
        }
        else
        {
            RequireSmoothNormals = true;
        }
    }

    // For each relevant draw item, update dirty buffer sources.
    int DrawItemIndex       = 0;
    int GeomSubsetDescIndex = 0;
    for (const pxr::HdMeshReprDesc& Desc : ReprDescs)
    {
        if (Desc.geomStyle == pxr::HdMeshGeomStyleInvalid)
            continue;

        pxr::HdDrawItem& DrawItem = *CurrRepr->GetDrawItem(DrawItemIndex++);

        if (pxr::HdChangeTracker::IsDirty(DirtyBits))
        {
            UpdateDrawItem(SceneDelegate,
                           RenderParam,
                           DrawItem,
                           DirtyBits,
                           ReprToken,
                           CurrRepr,
                           Desc,
                           RequireSmoothNormals,
                           RequireFlatNormals,
                           GeomSubsetDescIndex);
        }

        if (Desc.geomStyle != pxr::HdMeshGeomStylePoints)
        {
            GeomSubsetDescIndex++;
        }
    }

    DirtyBits &= ~pxr::HdChangeTracker::NewRepr;
}


void HnMesh::UpdateDrawItem(pxr::HdSceneDelegate&       SceneDelegate,
                            pxr::HdRenderParam*         RenderParam,
                            pxr::HdDrawItem&            DrawItem,
                            pxr::HdDirtyBits&           DirtyBits,
                            const pxr::TfToken&         ReprToken,
                            const pxr::HdReprSharedPtr& Repr,
                            const pxr::HdMeshReprDesc&  Desc,
                            bool                        RequireSmoothNormals,
                            bool                        RequireFlatNormals,
                            int                         GeomSubsetDescIndex)
{
}

void HnMesh::UpdateTopology(pxr::HdSceneDelegate& SceneDelegate,
                            pxr::HdRenderParam*   RenderParam,
                            pxr::HdDirtyBits&     DirtyBits,
                            const pxr::TfToken&   ReprToken)
{
    const pxr::SdfPath& Id = GetId();
    if (!pxr::HdChangeTracker::IsTopologyDirty(DirtyBits, Id))
        return;

    m_Topology = HdMesh::GetMeshTopology(&SceneDelegate);

    m_IndexData = std::make_unique<IndexData>();

    pxr::HdMeshUtil MeshUtil{&m_Topology, Id};
    MeshUtil.ComputeTriangleIndices(
        &m_IndexData->TrianglesFaceIndices,
        &m_IndexData->PrimitiveParam,
        &m_IndexData->TrianglesEdgeIndices);

    DirtyBits &= ~pxr::HdChangeTracker::DirtyTopology;
}

void HnMesh::UpdateVertexPrims(pxr::HdSceneDelegate& SceneDelegate,
                               pxr::HdRenderParam*   RenderParam,
                               pxr::HdDirtyBits&     DirtyBits,
                               const pxr::TfToken&   ReprToken)
{
    const pxr::SdfPath& Id = GetId();
    if (!pxr::HdChangeTracker::IsAnyPrimvarDirty(DirtyBits, Id))
        return;

    const int NumPoints = m_Topology.GetNumPoints();

    pxr::HdPrimvarDescriptorVector VertexPrims = GetPrimvarDescriptors(&SceneDelegate, pxr::HdInterpolationVertex);
    for (const pxr::HdPrimvarDescriptor& PrimDesc : VertexPrims)
    {
        if (!pxr::HdChangeTracker::IsPrimvarDirty(DirtyBits, Id, PrimDesc.name))
            continue;

        pxr::VtValue PrimValue = GetPrimvar(&SceneDelegate, PrimDesc.name);
        if (PrimValue.IsEmpty())
            continue;

        auto BufferSource = std::make_shared<pxr::HdVtBufferSource>(
            PrimDesc.name,
            PrimValue,
            1,    // values per element
            false // whether doubles are supported or must be converted to floats
        );

        if (BufferSource->GetNumElements() == 0)
        {
            if (BufferSource->GetName() == pxr::HdTokens->points)
                return;
            else
                continue;
        }

        // Verify primvar length - it is alright to have more data than we index into.
        if (BufferSource->GetNumElements() < static_cast<size_t>(NumPoints))
        {
            LOG_WARNING_MESSAGE("Vertex primvar ", PrimDesc.name, " has only ", BufferSource->GetNumElements(),
                                " elements, while its topology expects at least ", NumPoints,
                                " elements. Skipping primvar update.");

            if (PrimDesc.name == pxr::HdTokens->points)
            {
                LOG_WARNING_MESSAGE("Skipping prim ", Id, " because its points data is insufficient.");
                return;
            }

            continue;
        }
        else if (BufferSource->GetNumElements() > static_cast<size_t>(NumPoints))
        {
            // If the primvar has more data than needed, we issue a warning,
            // but don't skip the primvar update. Truncate the buffer to
            // the expected length.
            LOG_WARNING_MESSAGE("Vertex primvar ", PrimDesc.name, " has only ", BufferSource->GetNumElements(),
                                " elements, while its topology expects at least ", NumPoints, " elements.");

            BufferSource->Truncate(NumPoints);
        }

        m_BufferSources.emplace(PrimDesc.name, std::move(BufferSource));
    }

    DirtyBits &= ~pxr::HdChangeTracker::DirtyPrimvar;
}

void HnMesh::UpdateVertexBuffer(const RenderDeviceX_N& Device)
{
    VERIFY_EXPR(!m_BufferSources.empty());

    const auto Name = GetId().GetString() + " - Vertex Buffer";

    const auto NumVerts = static_cast<size_t>(m_Topology.GetNumPoints());
    BufferDesc Desc{
        Name.c_str(),
        NumVerts * sizeof(GpuVertex),
        BIND_VERTEX_BUFFER,
        USAGE_IMMUTABLE,
    };
    std::vector<GpuVertex> Vertices(NumVerts);

    auto PopulateElement = [&](const pxr::TfToken Name, Uint32 ElementOffset, Uint32 ElementSize) {
        const auto source_it = m_BufferSources.find(Name);
        if (source_it == m_BufferSources.end())
            return;

        auto pSource = source_it->second.get();
        if (pSource == nullptr)
            return;

        VERIFY_EXPR(pSource->GetNumElements() == NumVerts);

        const auto DataType = pSource->GetTupleType().type;
        if (HdDataSizeOfType(DataType) != ElementSize)
        {
            UNEXPECTED("Data source element size ", HdDataSizeOfType(DataType), " does not match the expected element size ", ElementSize);
            return;
        }

        const auto* pSrcData = static_cast<const Uint8*>(pSource->GetData());
        auto*       pDstData = Vertices.data();
        for (Uint32 i = 0; i < NumVerts; ++i)
        {
            std::memcpy(reinterpret_cast<Uint8*>(pDstData + i) + ElementOffset,
                        pSrcData + ElementSize * i,
                        ElementSize);
        }
    };

    PopulateElement(pxr::HdTokens->points, offsetof(GpuVertex, Pos), sizeof(float) * 3);
    PopulateElement(pxr::HdTokens->normals, offsetof(GpuVertex, Normal), sizeof(float) * 3);
    PopulateElement(HnTokens->st0, offsetof(GpuVertex, UV), sizeof(float) * 2);

    BufferData InitData{Vertices.data(), Desc.Size};
    m_pVertexBuffer = Device.CreateBuffer(Desc, &InitData);

    m_BufferSources.clear();
}

void HnMesh::UpdateIndexBuffer(const RenderDeviceX_N& Device)
{
    VERIFY_EXPR(m_IndexData);

    {
        const auto Name = GetId().GetString() + " - Triangle Index Buffer";

        m_NumTriangles = static_cast<size_t>(m_IndexData->TrianglesFaceIndices.size());
        static_assert(sizeof(m_IndexData->TrianglesFaceIndices[0]) == sizeof(Uint32) * 3, "Unexpected triangle data size");

        BufferDesc Desc{
            Name.c_str(),
            m_NumTriangles * sizeof(Uint32) * 3,
            BIND_INDEX_BUFFER,
            USAGE_IMMUTABLE,
        };

        BufferData InitData{m_IndexData->TrianglesFaceIndices.data(), Desc.Size};
        m_pTriangleIndexBuffer = Device.CreateBuffer(Desc, &InitData);
    }

    {
        const auto Name = GetId().GetString() + " - Edge Index Buffer";

        m_NumEdges = static_cast<size_t>(m_IndexData->TrianglesEdgeIndices.size()) / 2;

        BufferDesc Desc{
            Name.c_str(),
            m_NumEdges * sizeof(Uint32) * 2,
            BIND_INDEX_BUFFER,
            USAGE_IMMUTABLE,
        };

        BufferData InitData{m_IndexData->TrianglesEdgeIndices.data(), Desc.Size};
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

    if (!m_BufferSources.empty())
    {
        UpdateVertexBuffer(pDevice);
    }
}

} // namespace USD

} // namespace Diligent
