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

#include "HnRenderPass.hpp"
#include "HnRenderPassState.hpp"
#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"
#include "HnDrawItem.hpp"
#include "HnTypeConversions.hpp"
#include "HnRenderParam.hpp"

#include <array>
#include <unordered_map>

#include "pxr/imaging/hd/renderIndex.h"

#include "USD_Renderer.hpp"
#include "GLTF_PBR_Renderer.hpp"
#include "MapHelper.hpp"
#include "ScopedDebugGroup.hpp"
#include "HashUtils.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"

} // namespace HLSL

namespace USD
{

const char* HnRenderPassParams::GetSelectionTypeString(SelectionType Type)
{
    switch (Type)
    {
        case SelectionType::All:
            return "All";
        case SelectionType::Unselected:
            return "Unselected";
        case SelectionType::Selected:
            return "Selected";
        default:
            UNEXPECTED("Unexpected selection type");
            return "Unknown";
    }
}

pxr::HdRenderPassSharedPtr HnRenderPass::Create(pxr::HdRenderIndex*           pIndex,
                                                const pxr::HdRprimCollection& Collection)
{
    return pxr::HdRenderPassSharedPtr{new HnRenderPass{pIndex, Collection}};
}

HnRenderPass::DrawListItem::DrawListItem(HnRenderDelegate& RenderDelegate, const HnDrawItem& Item) noexcept :
    DrawItem{Item},
    Mesh{Item.GetMesh()},
    Material{*Item.GetMaterial()},
    MeshEntity{Mesh.GetEntity()},
    MeshUID{static_cast<float>(Mesh.GetUID())},
    RenderStateID{0},
    NumVertexBuffers{0}
{
    entt::registry& Registry = RenderDelegate.GetEcsRegistry();
    PrevTransform            = Registry.get<HnMesh::Components::Transform>(MeshEntity).Val;
}

HnRenderPass::HnRenderPass(pxr::HdRenderIndex*           pIndex,
                           const pxr::HdRprimCollection& Collection) :
    pxr::HdRenderPass{pIndex, Collection}
{
}

struct HnRenderPass::RenderState
{
    const HnRenderPass&       RenderPass;
    const HnRenderPassState&  RPState;
    const pxr::HdRenderIndex& RenderIndex;
    HnRenderDelegate&         RenderDelegate;
    const HnRenderParam&      RenderParam;
    USD_Renderer&             USDRenderer;

    IDeviceContext* const pCtx;

    const USD_Renderer::ALPHA_MODE AlphaMode;

    const Uint32 ConstantBufferOffsetAlignment;

    RenderState(const HnRenderPass&      _RenderPass,
                const HnRenderPassState& _RPState) :
        RenderPass{_RenderPass},
        RPState{_RPState},
        RenderIndex{*RenderPass.GetRenderIndex()},
        RenderDelegate{*static_cast<HnRenderDelegate*>(RenderIndex.GetRenderDelegate())},
        RenderParam{*static_cast<const HnRenderParam*>(RenderDelegate.GetRenderParam())},
        USDRenderer{*RenderDelegate.GetUSDRenderer()},
        pCtx{RenderDelegate.GetDeviceContext()},
        AlphaMode{MaterialTagToPbrAlphaMode(RenderPass.m_MaterialTag)},
        ConstantBufferOffsetAlignment{RenderDelegate.GetDevice()->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment}
    {
    }

    void SetPipelineState(IPipelineState* pNewPSO)
    {
        VERIFY_EXPR(pNewPSO != nullptr);
        if (pNewPSO == nullptr || pNewPSO == pPSO)
            return;

        pCtx->SetPipelineState(pNewPSO);
        pPSO = pNewPSO;
    }

    void CommitShaderResources(IShaderResourceBinding* pNewSRB)
    {
        VERIFY_EXPR(pNewSRB != nullptr);
        if (pNewSRB == nullptr || pNewSRB == this->pMaterialSRB)
            return;

        if (pFrameSRB == nullptr)
        {
            pFrameSRB = RPState.GetFrameAttribsSRB();
            VERIFY_EXPR(pFrameSRB != nullptr);
            pCtx->CommitShaderResources(pFrameSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
        }

        pCtx->CommitShaderResources(pNewSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
        pMaterialSRB = pNewSRB;
    }

    void SetIndexBuffer(IBuffer* pNewIndexBuffer)
    {
        if (pNewIndexBuffer == nullptr || pNewIndexBuffer == pIndexBuffer)
            return;

        pIndexBuffer = pNewIndexBuffer;
        pCtx->SetIndexBuffer(pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    }

    void SetVertexBuffers(IBuffer* const* ppBuffers, Uint32 NumBuffers)
    {
        VERIFY_EXPR(NumBuffers <= ppVertexBuffers.size());
        bool SetBuffers = false;
        if (NumVertexBuffers != NumBuffers)
        {
            SetBuffers       = true;
            NumVertexBuffers = NumBuffers;
        }
        for (Uint32 i = 0; i < NumBuffers; ++i)
        {
            if (ppBuffers[i] != nullptr && ppBuffers[i] != ppVertexBuffers[i])
            {
                SetBuffers         = true;
                ppVertexBuffers[i] = ppBuffers[i];
            }
        }

        if (SetBuffers)
        {
            pCtx->SetVertexBuffers(0, NumBuffers, ppBuffers, nullptr, RESOURCE_STATE_TRANSITION_MODE_VERIFY, SET_VERTEX_BUFFERS_FLAG_RESET);
        }
    }

    USD_Renderer::PsoCacheAccessor& GePsoCache()
    {
        if (!PsoCache)
        {
            GraphicsPipelineDesc GraphicsDesc = RenderPass.GetGraphicsDesc(RPState);

            PsoCache = USDRenderer.GetPsoCacheAccessor(GraphicsDesc);
            VERIFY_EXPR(PsoCache);
        }

        return PsoCache;
    }

private:
    IPipelineState*         pPSO         = nullptr;
    IShaderResourceBinding* pMaterialSRB = nullptr;
    IShaderResourceBinding* pFrameSRB    = nullptr;

    IBuffer* pIndexBuffer = nullptr;

    Uint32                  NumVertexBuffers = 0;
    std::array<IBuffer*, 4> ppVertexBuffers  = {};

    USD_Renderer::PsoCacheAccessor PsoCache;
};

GraphicsPipelineDesc HnRenderPass::GetGraphicsDesc(const HnRenderPassState& RPState) const
{
    GraphicsPipelineDesc GraphicsDesc = RPState.GetGraphicsPipelineDesc();
    if ((m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS) == 0)
    {
        for (Uint32 i = 0; i < GraphicsDesc.NumRenderTargets; ++i)
            GraphicsDesc.RTVFormats[i] = TEX_FORMAT_UNKNOWN;
        GraphicsDesc.NumRenderTargets = 0;
    }

    switch (m_RenderMode)
    {
        case HN_RENDER_MODE_SOLID:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;

        case HN_RENDER_MODE_MESH_EDGES:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;

        case HN_RENDER_MODE_POINTS:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;

        default:
            UNEXPECTED("Unexpected render mode");
    }
    static_assert(HN_RENDER_MODE_COUNT == 3, "Please handle the new render mode in the switch above");

    return GraphicsDesc;
}

void HnRenderPass::_Execute(const pxr::HdRenderPassStateSharedPtr& RPState,
                            const pxr::TfTokenVector&              Tags)
{
    // Render pass state is initialized by HnBeginFrameTask, and
    // passed from the render Rprims task.
    if (!RPState)
    {
        UNEXPECTED("Render pass state should not be null");
        return;
    }

    Execute(*static_cast<HnRenderPassState*>(RPState.get()), Tags);
}

void HnRenderPass::Execute(HnRenderPassState& RPState, const pxr::TfTokenVector& Tags)
{
    UpdateDrawList(Tags);
    if (m_DrawList.empty())
        return;

    RenderState State{*this, RPState};

    const std::string DebugGroupName = std::string{"Render Pass - "} + m_MaterialTag.GetString() + " - " + HnRenderPassParams::GetSelectionTypeString(m_Params.Selection);
    ScopedDebugGroup  DebugGroup{State.pCtx, DebugGroupName.c_str()};

    RPState.Commit(State.pCtx);

    {
        PBR_Renderer::DebugViewType DebugView = State.RenderParam.GetDebugView();
        if (m_DebugView != DebugView)
        {
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;
            m_DebugView = DebugView;
        }
    }

    {
        HN_RENDER_MODE RenderMode = State.RenderParam.GetRenderMode();
        if (m_RenderMode != RenderMode)
        {
            m_RenderMode = RenderMode;
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO | DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA;
        }
    }

    {
        bool UseShadows = State.RenderParam.GetUseShadows();
        if (m_UseShadows != UseShadows)
        {
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;
            m_UseShadows = UseShadows;
        }
    }

    {
        const Uint32 MaterialVersion = State.RenderParam.GetAttribVersion(HnRenderParam::GlobalAttrib::Material);
        if (m_GlobalAttribVersions.Material != MaterialVersion)
        {
            // Attributes of some material have changed. We don't know which meshes may be affected,
            // so we need to process the entire draw list.
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;
            m_GlobalAttribVersions.Material = MaterialVersion;
        }

        // If either mesh material or mesh geometry changes, call UpdateDrawListGPUResources(), but
        // don't set the dirty flags in the m_DrawListItemsDirtyFlags. The UpdateDrawListGPUResources()
        // method will check the version of each individual mesh and only update those that have changed.
        const Uint32 MeshMaterialVersion = State.RenderParam.GetAttribVersion(HnRenderParam::GlobalAttrib::MeshMaterial);
        const Uint32 MeshGeometryVersion = State.RenderParam.GetAttribVersion(HnRenderParam::GlobalAttrib::MeshGeometry);
        if (m_DrawListItemsDirtyFlags != DRAW_LIST_ITEM_DIRTY_FLAG_NONE ||
            m_GlobalAttribVersions.MeshGeometry != MeshGeometryVersion ||
            m_GlobalAttribVersions.MeshMaterial != MeshMaterialVersion)
        {
            UpdateDrawListGPUResources(State);

            m_GlobalAttribVersions.MeshGeometry = MeshGeometryVersion;
            m_GlobalAttribVersions.MeshMaterial = MeshMaterialVersion;
        }
    }

    IBuffer* const pPrimitiveAttribsCB = State.RenderDelegate.GetPrimitiveAttribsCB();
    VERIFY_EXPR(pPrimitiveAttribsCB != nullptr);

    const BufferDesc& AttribsBuffDesc = pPrimitiveAttribsCB->GetDesc();

    m_PendingDrawItems.clear();
    m_PendingDrawItems.reserve(m_DrawList.size());
    void*  pMappedBufferData = nullptr;
    Uint32 CurrOffset        = 0;

    if (AttribsBuffDesc.Usage != USAGE_DYNAMIC)
    {
        m_PrimitiveAttribsData.resize(static_cast<size_t>(AttribsBuffDesc.Size));
    }

    auto FlushPendingDraws = [&]() {
        VERIFY_EXPR(CurrOffset > 0);
        if (AttribsBuffDesc.Usage == USAGE_DYNAMIC)
        {
            VERIFY_EXPR(pMappedBufferData != nullptr);
            State.pCtx->UnmapBuffer(pPrimitiveAttribsCB, MAP_WRITE);
            pMappedBufferData = nullptr;
        }
        else
        {
            State.pCtx->UpdateBuffer(pPrimitiveAttribsCB, 0, m_PrimitiveAttribsData.size(), m_PrimitiveAttribsData.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            StateTransitionDesc Barrier{pPrimitiveAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            State.pCtx->TransitionResourceStates(1, &Barrier);
        }
        RenderPendingDrawItems(State);
        VERIFY_EXPR(m_PendingDrawItems.empty());
        CurrOffset = 0;
    };

    const Uint32 PrimitiveArraySize = std::max(State.USDRenderer.GetSettings().PrimitiveArraySize, 1u);
    m_ScratchSpace.resize(sizeof(MultiDrawIndexedItem) * State.USDRenderer.GetSettings().PrimitiveArraySize);

    entt::registry& Registry = State.RenderDelegate.GetEcsRegistry();

    // Note: accessing components through a view is faster than accessing them through the registry.
    auto MeshAttribsView = Registry.view<const HnMesh::Components::Transform,
                                         const HnMesh::Components::DisplayColor,
                                         const HnMesh::Components::Visibility>();

    Uint32 MultiDrawCount = 0;
    for (DrawListItem& ListItem : m_DrawList)
    {
        if (!ListItem)
            continue;

        const auto& MeshAttribs = MeshAttribsView.get<const HnMesh::Components::Transform,
                                                      const HnMesh::Components::DisplayColor,
                                                      const HnMesh::Components::Visibility>(ListItem.MeshEntity);

        const float4x4& Transform    = std::get<0>(MeshAttribs).Val;
        const float4&   DisplayColor = std::get<1>(MeshAttribs).Val;
        const bool      MeshVisibile = std::get<2>(MeshAttribs).Val;

        if (!MeshVisibile)
            continue;

        if (MultiDrawCount == PrimitiveArraySize)
            MultiDrawCount = 0;

        if (MultiDrawCount > 0)
        {
            // Check if the current item can be batched with the previous ones
            auto& FirstMultiDrawItem = m_PendingDrawItems[m_PendingDrawItems.size() - MultiDrawCount];
            VERIFY_EXPR(FirstMultiDrawItem.DrawCount == MultiDrawCount);

            // If any of the state changes, multi-draw is not possible
            if (FirstMultiDrawItem.ListItem.RenderStateID == ListItem.RenderStateID)
            {
                VERIFY_EXPR(FirstMultiDrawItem.ListItem.pPSO == ListItem.pPSO &&
                            FirstMultiDrawItem.ListItem.IndexBuffer == ListItem.IndexBuffer &&
                            FirstMultiDrawItem.ListItem.NumVertexBuffers == ListItem.NumVertexBuffers &&
                            FirstMultiDrawItem.ListItem.VertexBuffers == ListItem.VertexBuffers &&
                            FirstMultiDrawItem.ListItem.Material.GetSRB() == ListItem.Material.GetSRB());
                VERIFY_EXPR(CurrOffset + ListItem.ShaderAttribsDataSize <= AttribsBuffDesc.Size);

                ++FirstMultiDrawItem.DrawCount;
            }
            else
            {
                MultiDrawCount = 0;
            }
        }

        if (MultiDrawCount == 0)
        {
            // Align the offset by the constant buffer offset alignment
            CurrOffset = AlignUp(CurrOffset, State.ConstantBufferOffsetAlignment);

            // Note that the actual attribs size may be smaller than the range, but we need
            // to check for the entire range to avoid errors because this range is set in
            // the shader variable in the SRB.
            if (CurrOffset + ListItem.ShaderAttribsBufferRange > AttribsBuffDesc.Size)
            {
                // The buffer is full. Render the pending items and start filling the buffer from the beginning.
                FlushPendingDraws();
            }
        }

        void* pCurrPrimitive = nullptr;
        if (AttribsBuffDesc.Usage == USAGE_DYNAMIC)
        {
            if (pMappedBufferData == nullptr)
            {
                State.pCtx->MapBuffer(pPrimitiveAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD, pMappedBufferData);
                if (pMappedBufferData == nullptr)
                {
                    UNEXPECTED("Failed to map the primitive attributes buffer");
                    break;
                }
            }
            pCurrPrimitive = reinterpret_cast<Uint8*>(pMappedBufferData) + CurrOffset;
        }
        else
        {
            VERIFY_EXPR(CurrOffset + ListItem.ShaderAttribsDataSize <= m_PrimitiveAttribsData.size());
            pCurrPrimitive = &m_PrimitiveAttribsData[CurrOffset];
        }

        // Write current primitive attributes
        float4 CustomData{
            ListItem.MeshUID,
            m_Params.Selection == HnRenderPassParams::SelectionType::Selected ? 1.f : 0.f,
            0,
            0,
        };

        HLSL::PBRMaterialBasicAttribs* pDstMaterialBasicAttribs = nullptr;

        GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData AttribsData{
            ListItem.PSOFlags,
            &Transform,
            &ListItem.PrevTransform,
            0,
            &CustomData,
            sizeof(CustomData),
            &pDstMaterialBasicAttribs,
        };
        // Note: if the material changes in the mesh, the mesh material version and/or
        //       global material version will be updated, and the draw list item GPU
        //       resources will be updated.
        const GLTF::Material& MaterialData = ListItem.Material.GetMaterialData();
        GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(pCurrPrimitive, AttribsData, State.USDRenderer.GetSettings().TextureAttribIndices, MaterialData);

        pDstMaterialBasicAttribs->BaseColorFactor = MaterialData.Attribs.BaseColorFactor * DisplayColor;

        ListItem.PrevTransform = Transform;

        m_PendingDrawItems.push_back(PendingDrawItem{ListItem, CurrOffset});

        CurrOffset += ListItem.ShaderAttribsDataSize;
        ++MultiDrawCount;
    }
    if (CurrOffset != 0)
    {
        FlushPendingDraws();
    }

    m_DrawListItemsDirtyFlags = DRAW_LIST_ITEM_DIRTY_FLAG_NONE;
}

void HnRenderPass::_MarkCollectionDirty()
{
    // Force any cached data based on collection to be refreshed.
    m_GlobalAttribVersions.Collection = ~0u;
}

void HnRenderPass::SetParams(const HnRenderPassParams& Params)
{
    if (m_Params.UsdPsoFlags != Params.UsdPsoFlags)
        m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;

    m_Params = Params;
}

void HnRenderPass::UpdateDrawList(const pxr::TfTokenVector& RenderTags)
{
    pxr::HdRenderIndex*  pRenderIndex    = GetRenderIndex();
    HnRenderDelegate*    pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());
    const HnRenderParam* pRenderParam    = static_cast<const HnRenderParam*>(pRenderDelegate->GetRenderParam());
    if (pRenderParam == nullptr)
    {
        UNEXPECTED("Render param is null");
        return;
    }

    bool DrawListDirty = false;
    if (pRenderParam->GetSelectedPrimId() != m_SelectedPrimId)
    {
        m_SelectedPrimId = pRenderParam->GetSelectedPrimId();
        // Only update draw list, but not the draw items list
        DrawListDirty = true;
    }

    const pxr::HdRprimCollection& Collection  = GetRprimCollection();
    const pxr::HdChangeTracker&   Tracker     = pRenderIndex->GetChangeTracker();
    const pxr::TfToken&           MaterialTag = Collection.GetMaterialTag();

    const unsigned int CollectionVersion          = Tracker.GetCollectionVersion(Collection.GetName());
    const unsigned int RprimRenderTagVersion      = Tracker.GetRenderTagVersion();
    const unsigned int TaskRenderTagsVersion      = Tracker.GetTaskRenderTagsVersion();
    const unsigned int GeomSubsetDrawItemsVersion = pRenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::GeometrySubsetDrawItems);

    const bool CollectionChanged          = (m_GlobalAttribVersions.Collection != CollectionVersion);
    const bool RprimRenderTagChanged      = (m_GlobalAttribVersions.RprimRenderTag != RprimRenderTagVersion);
    const bool MaterialTagChanged         = (m_MaterialTag != MaterialTag);
    const bool GeomSubsetDrawItemsChanged = (m_GlobalAttribVersions.GeomSubsetDrawItems != GeomSubsetDrawItemsVersion);

    bool TaskRenderTagsChanged = false;
    if (m_GlobalAttribVersions.TaskRenderTags != TaskRenderTagsVersion)
    {
        m_GlobalAttribVersions.TaskRenderTags = TaskRenderTagsVersion;
        if (m_RenderTags != RenderTags)
        {
            m_RenderTags          = RenderTags;
            TaskRenderTagsChanged = true;
        }
    }

    // Do not update draw items list when selection changes as GetDrawItems() is an expensive call.
    if (CollectionChanged ||
        RprimRenderTagChanged ||
        MaterialTagChanged ||
        GeomSubsetDrawItemsChanged ||
        TaskRenderTagsChanged)
    {
        m_DrawItems.clear();
        //const HnRenderParam* const RenderParam = static_cast<HnRenderParam*>(pRenderIndex->GetRenderDelegate()->GetRenderParam());
        //if (RenderParam->HasMaterialTag(Collection.GetMaterialTag()))
        {
            m_DrawItems = pRenderIndex->GetDrawItems(Collection, RenderTags);
            // GetDrawItems() uses multithreading, so the order of draw items is not deterministic.
            std::sort(m_DrawItems.begin(), m_DrawItems.end());
        }
        //else
        //{
        //    // There is no prim with the desired material tag.
        //    m_DrawItems.clear()
        //}
        DrawListDirty = true;
    }

    if (DrawListDirty)
    {
        m_DrawList.clear();
        for (const pxr::HdDrawItem* pDrawItem : m_DrawItems)
        {
            if (pDrawItem == nullptr)
                continue;

            const pxr::SdfPath& RPrimID = pDrawItem->GetRprimID();
            const pxr::HdRprim* pRPrim  = pRenderIndex->GetRprim(RPrimID);
            if (pRPrim == nullptr)
                continue;

            const bool IsSelected = RPrimID.HasPrefix(m_SelectedPrimId);
            if ((m_Params.Selection == HnRenderPassParams::SelectionType::All) ||
                (m_Params.Selection == HnRenderPassParams::SelectionType::Selected && IsSelected) ||
                (m_Params.Selection == HnRenderPassParams::SelectionType::Unselected && !IsSelected))
            {
                const HnDrawItem& DrawItem = static_cast<const HnDrawItem&>(*pDrawItem);
                if (DrawItem.IsValid())
                    m_DrawList.push_back(DrawListItem{*pRenderDelegate, DrawItem});
            }
        }

        m_DrawListItemsDirtyFlags = DRAW_LIST_ITEM_DIRTY_FLAG_ALL;
    }

    m_GlobalAttribVersions.Collection          = CollectionVersion;
    m_GlobalAttribVersions.RprimRenderTag      = RprimRenderTagVersion;
    m_MaterialTag                              = MaterialTag;
    m_GlobalAttribVersions.GeomSubsetDrawItems = GeomSubsetDrawItemsVersion;
}

void HnRenderPass::UpdateDrawListGPUResources(RenderState& State)
{
    struct DrawListItemRenderState
    {
        const DrawListItem& Item;
        mutable size_t      Hash = 0;

        struct Hasher
        {
            size_t operator()(const DrawListItemRenderState& State) const
            {
                if (State.Hash == 0)
                {
                    State.Hash = ComputeHash(State.Item.pPSO,
                                             State.Item.IndexBuffer,
                                             State.Item.NumVertexBuffers,
                                             State.Item.Material.GetSRB());
                    for (Uint32 i = 0; i < State.Item.NumVertexBuffers; ++i)
                    {
                        HashCombine(State.Hash, State.Item.VertexBuffers[i]);
                    }
                }
                return State.Hash;
            }
        };

        bool operator==(const DrawListItemRenderState& rhs) const
        {
            return (Item.pPSO == rhs.Item.pPSO &&
                    Item.IndexBuffer == rhs.Item.IndexBuffer &&
                    Item.NumVertexBuffers == rhs.Item.NumVertexBuffers &&
                    Item.Material.GetSRB() == rhs.Item.Material.GetSRB() &&
                    Item.VertexBuffers == rhs.Item.VertexBuffers);
        }
    };
    std::unordered_map<DrawListItemRenderState, Uint32, DrawListItemRenderState::Hasher> DrawListItemRenderStateIDs;

    bool DrawListDirty = false;
    for (DrawListItem& ListItem : m_DrawList)
    {
        auto DrawItemGPUResDirtyFlags = m_DrawListItemsDirtyFlags;

        const auto Version = ListItem.Mesh.GetGeometryVersion() + ListItem.Mesh.GetMaterialVersion();
        if (ListItem.Version != Version)
        {
            DrawItemGPUResDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO | DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA;
            ListItem.Version = Version;
        }
        if (DrawItemGPUResDirtyFlags != DRAW_LIST_ITEM_DIRTY_FLAG_NONE)
        {
            UpdateDrawListItemGPUResources(ListItem, State, DrawItemGPUResDirtyFlags);
            DrawListDirty = true;
        }

        // Assign a unique ID to the combination of render states used to render the draw item.
        // We have to do this after we update the draw item GPU resources.
        ListItem.RenderStateID = DrawListItemRenderStateIDs.emplace(DrawListItemRenderState{ListItem}, static_cast<Uint32>(DrawListItemRenderStateIDs.size())).first->second;
    }

    if (DrawListDirty)
    {
        m_RenderOrder.resize(m_DrawList.size());
        for (Uint32 i = 0; i < m_RenderOrder.size(); ++i)
            m_RenderOrder[i] = i;

        bool DrawOrderDirty = false;

        std::sort(m_RenderOrder.begin(), m_RenderOrder.end(),
                  [this, &DrawOrderDirty](Uint32 i0, Uint32 i1) {
                      const DrawListItem& Item0 = m_DrawList[i0];
                      const DrawListItem& Item1 = m_DrawList[i1];

                      bool Item0PrecedesItem1;
                      // Sort by PSO first, then by material SRB, then by entity ID
                      if (Item0.pPSO < Item1.pPSO)
                          Item0PrecedesItem1 = true;
                      else if (Item0.pPSO > Item1.pPSO)
                          Item0PrecedesItem1 = false;
                      else if (Item0.Material.GetSRB() < Item1.Material.GetSRB())
                          Item0PrecedesItem1 = true;
                      else if (Item0.Material.GetSRB() > Item1.Material.GetSRB())
                          Item0PrecedesItem1 = false;
                      else
                          Item0PrecedesItem1 = Item0.MeshEntity < Item1.MeshEntity;

                      if ((i0 < i1) != Item0PrecedesItem1)
                          DrawOrderDirty = true;

                      return Item0PrecedesItem1;
                  });

        if (DrawOrderDirty)
        {
            std::vector<DrawListItem> SortedDrawList;
            SortedDrawList.reserve(m_DrawList.size());
            for (size_t i = 0; i < m_RenderOrder.size(); ++i)
            {
                SortedDrawList.emplace_back(m_DrawList[m_RenderOrder[i]]);
            }
            m_DrawList.swap(SortedDrawList);
        }
        else
        {
#ifdef DILIGENT_DEBUG
            for (size_t i = 0; i < m_RenderOrder.size(); ++i)
            {
                VERIFY_EXPR(m_RenderOrder[i] == i);
            }
#endif
        }
    }
}

HnRenderPass::SupportedVertexInputsSetType HnRenderPass::GetSupportedVertexInputs(const HnMaterial* Material)
{
    SupportedVertexInputsSetType SupportedInputs{{pxr::HdTokens->points, pxr::HdTokens->normals}};
    if (Material != nullptr)
    {
        const auto& TexCoordSets = Material->GetTextureCoordinateSets();
        for (const auto& TexCoordSet : TexCoordSets)
        {
            if (!TexCoordSet.PrimVarName.IsEmpty())
                SupportedInputs.emplace(TexCoordSet.PrimVarName);
        }
    }

    return SupportedInputs;
}

PBR_Renderer::PSO_FLAGS HnRenderPass::GetMaterialPSOFlags(const HnMaterial& Material)
{
    const GLTF::Material& MaterialData = Material.GetMaterialData();

    PBR_Renderer::PSO_FLAGS PSOFlags =
        PBR_Renderer::PSO_FLAG_USE_COLOR_MAP |
        PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP |
        PBR_Renderer::PSO_FLAG_USE_METALLIC_MAP |
        PBR_Renderer::PSO_FLAG_USE_ROUGHNESS_MAP |
        PBR_Renderer::PSO_FLAG_USE_AO_MAP |
        PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP;

    PSOFlags |= PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS;

    MaterialData.ProcessActiveTextureAttibs(
        [&PSOFlags](Uint32, const GLTF::Material::TextureShaderAttribs& TexAttrib, int) //
        {
            if (TexAttrib.UVScaleAndRotation != float2x2::Identity() ||
                TexAttrib.UBias != 0 ||
                TexAttrib.VBias != 0)
            {
                PSOFlags |= PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM;
                return false;
            }
            return true;
        });

    if (MaterialData.HasClearcoat)
    {
        PSOFlags |= PBR_Renderer::PSO_FLAG_ENABLE_CLEAR_COAT;
    }

    return PSOFlags;
}

void HnRenderPass::UpdateDrawListItemGPUResources(DrawListItem& ListItem, RenderState& State, DRAW_LIST_ITEM_DIRTY_FLAGS DirtyFlags)
{
    const HnDrawItem& DrawItem = ListItem.DrawItem;
    if (DirtyFlags & DRAW_LIST_ITEM_DIRTY_FLAG_PSO)
    {
        ListItem.pPSO = nullptr;

        auto& PsoCache = State.GePsoCache();
        VERIFY_EXPR(PsoCache);

        auto& PSOFlags = ListItem.PSOFlags;
        PSOFlags       = static_cast<PBR_Renderer::PSO_FLAGS>(m_Params.UsdPsoFlags);

        const HnDrawItem::GeometryData& Geo       = DrawItem.GetGeometryData();
        const HnMaterial*               pMaterial = DrawItem.GetMaterial();
        VERIFY(pMaterial != nullptr, "Material is null");

        const bool IsDoubleSided = ListItem.Mesh.GetIsDoubleSided();

        // Use the material's texture indexing ID as the user value in the PSO key.
        // The USD renderer will use this ID to return the indexing.
        const auto ShaderTextureIndexingId = pMaterial->GetStaticShaderTextureIndexingId();

        if (m_RenderMode == HN_RENDER_MODE_SOLID)
        {
            if (Geo.Normals != nullptr && (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT) != 0)
                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
            if (Geo.TexCoords[0] != nullptr)
                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
            if (Geo.TexCoords[1] != nullptr)
                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;

            if (pMaterial != nullptr)
            {
                const PBR_Renderer::PSO_FLAGS MaterialPSOFlags = GetMaterialPSOFlags(*pMaterial);
                if (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT)
                {
                    PSOFlags |= MaterialPSOFlags | PBR_Renderer::PSO_FLAG_USE_IBL | PBR_Renderer::PSO_FLAG_USE_LIGHTS;
                }
                else
                {
                    // Color map is needed for alpha-masked materials
                    PSOFlags |= (MaterialPSOFlags & (PBR_Renderer::PSO_FLAG_USE_COLOR_MAP & PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM));
                }

                VERIFY(pMaterial->GetMaterialData().Attribs.AlphaMode == State.AlphaMode || pMaterial->GetId().IsEmpty(),
                       "Alpha mode derived from the material tag is not consistent with the alpha mode in the shader attributes. "
                       "This may indicate an issue in how alpha mode is determined in the material, or (less likely) an issue in Rprim sorting by Hydra.");
            }

            if (State.RenderParam.GetTextureBindingMode() == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS;

            if (State.USDRenderer.GetSettings().EnableShadows &&
                (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT) != 0 &&
                State.RenderParam.GetUseShadows())
                PSOFlags |= PBR_Renderer::PSO_FLAG_ENABLE_SHADOWS;

            ListItem.pPSO = PsoCache.Get({PSOFlags, static_cast<PBR_Renderer::ALPHA_MODE>(State.AlphaMode), IsDoubleSided, m_DebugView, ShaderTextureIndexingId}, true);
        }
        else if (m_RenderMode == HN_RENDER_MODE_MESH_EDGES ||
                 m_RenderMode == HN_RENDER_MODE_POINTS)
        {
            PSOFlags |= PBR_Renderer::PSO_FLAG_UNSHADED;
            ListItem.pPSO = PsoCache.Get({PSOFlags, IsDoubleSided, PBR_Renderer::DebugViewType::None, ShaderTextureIndexingId}, true);
        }
        else
        {
            UNEXPECTED("Unexpected render mode");
        }

        ListItem.ShaderAttribsDataSize    = State.USDRenderer.GetPBRPrimitiveAttribsSize(PSOFlags);
        ListItem.ShaderAttribsBufferRange = pMaterial->GetPBRPrimitiveAttribsBufferRange();
        VERIFY(ListItem.ShaderAttribsDataSize <= ListItem.ShaderAttribsBufferRange,
               "Attribs data size (", ListItem.ShaderAttribsDataSize, ") computed from the PSO flags exceeds the attribs buffer range (",
               ListItem.ShaderAttribsBufferRange, ") computed from material PSO flags. The latter is used by HnMaterial to set the buffer range.");

        VERIFY_EXPR(ListItem.pPSO != nullptr);
    }

    if (DirtyFlags & DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA)
    {
        const HnDrawItem::GeometryData& Geo = DrawItem.GetGeometryData();

        ListItem.VertexBuffers = {Geo.Positions, Geo.Normals, Geo.TexCoords[0], Geo.TexCoords[1]};

        const HnDrawItem::TopologyData* Topology = nullptr;
        switch (m_RenderMode)
        {
            case HN_RENDER_MODE_SOLID:
                Topology                  = &DrawItem.GetFaces();
                ListItem.NumVertexBuffers = 4;
                break;

            case HN_RENDER_MODE_MESH_EDGES:
                Topology                  = &DrawItem.GetEdges();
                ListItem.NumVertexBuffers = 1; // Only positions are used
                break;

            case HN_RENDER_MODE_POINTS:
                Topology                  = &DrawItem.GetPoints();
                ListItem.NumVertexBuffers = 1; // Only positions are used
                break;

            default:
                UNEXPECTED("Unexpected render mode");
        }

        if (Topology != nullptr)
        {
            ListItem.IndexBuffer = Topology->IndexBuffer;
            ListItem.StartIndex  = Topology->StartIndex;
            ListItem.NumVertices = Topology->NumVertices;
        }
        else
        {
            ListItem.IndexBuffer = nullptr;
            ListItem.StartIndex  = 0;
            ListItem.NumVertices = 0;
        }
    }
}

void HnRenderPass::RenderPendingDrawItems(RenderState& State)
{
    size_t item_idx = 0;
    while (item_idx < m_PendingDrawItems.size())
    {
        const PendingDrawItem& PendingItem = m_PendingDrawItems[item_idx];
        const DrawListItem&    ListItem    = PendingItem.ListItem;

        State.SetPipelineState(ListItem.pPSO);

        IShaderResourceBinding* pSRB = ListItem.Material.GetSRB(PendingItem.BufferOffset);
        VERIFY(pSRB != nullptr, "Material SRB is null. This may happen if UpdateSRB was not called for this material.");
        State.CommitShaderResources(pSRB);

        State.SetIndexBuffer(ListItem.IndexBuffer);
        State.SetVertexBuffers(ListItem.VertexBuffers.data(), ListItem.NumVertexBuffers);

        if (PendingItem.DrawCount > 1)
        {
#ifdef DILIGENT_DEBUG
            VERIFY_EXPR(item_idx + PendingItem.DrawCount <= m_PendingDrawItems.size());
            for (size_t i = 1; i < PendingItem.DrawCount; ++i)
            {
                const auto& BatchItem = m_PendingDrawItems[item_idx + i].ListItem;
                VERIFY_EXPR(BatchItem.RenderStateID == ListItem.RenderStateID &&
                            BatchItem.pPSO == ListItem.pPSO &&
                            BatchItem.IndexBuffer == ListItem.IndexBuffer &&
                            BatchItem.NumVertexBuffers == ListItem.NumVertexBuffers &&
                            BatchItem.VertexBuffers == ListItem.VertexBuffers &&
                            BatchItem.DrawItem.GetMaterial()->GetSRB() == ListItem.DrawItem.GetMaterial()->GetSRB());
            }
            VERIFY_EXPR(m_ScratchSpace.size() >= PendingItem.DrawCount * (ListItem.IndexBuffer != nullptr ? sizeof(MultiDrawIndexedItem) : sizeof(MultiDrawItem)));
#endif

            if (ListItem.IndexBuffer != nullptr)
            {
                MultiDrawIndexedItem* pMultiDrawItems = reinterpret_cast<MultiDrawIndexedItem*>(m_ScratchSpace.data());
                for (size_t i = 0; i < PendingItem.DrawCount; ++i)
                {
                    const auto& BatchItem = m_PendingDrawItems[item_idx + i].ListItem;
                    pMultiDrawItems[i]    = {BatchItem.NumVertices, BatchItem.StartIndex, 0};
                }
                State.pCtx->MultiDrawIndexed({PendingItem.DrawCount, pMultiDrawItems, VT_UINT32, DRAW_FLAG_VERIFY_ALL});
            }
            else
            {
                MultiDrawItem* pMultiDrawItems = reinterpret_cast<MultiDrawItem*>(m_ScratchSpace.data());
                for (size_t i = 0; i < PendingItem.DrawCount; ++i)
                {
                    const auto& BatchItem = m_PendingDrawItems[item_idx + i].ListItem;
                    pMultiDrawItems[i]    = {BatchItem.NumVertices, 0};
                }
                State.pCtx->MultiDraw({PendingItem.DrawCount, pMultiDrawItems, DRAW_FLAG_VERIFY_ALL});
            }
        }
        else
        {
            if (ListItem.IndexBuffer != nullptr)
            {
                constexpr Uint32 NumInstances = 1;
                State.pCtx->DrawIndexed({ListItem.NumVertices, VT_UINT32, DRAW_FLAG_VERIFY_ALL, NumInstances, ListItem.StartIndex});
            }
            else
            {
                State.pCtx->Draw({ListItem.NumVertices, DRAW_FLAG_VERIFY_ALL});
            }
        }

        item_idx += PendingItem.DrawCount;
    }

    m_PendingDrawItems.clear();
}

} // namespace USD

} // namespace Diligent
