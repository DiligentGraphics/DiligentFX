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

#include "HnRenderPass.hpp"
#include "HnRenderPassState.hpp"
#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"
#include "HnDrawItem.hpp"
#include "HnTypeConversions.hpp"
#include "HnRenderParam.hpp"
#include "Computations/HnSkinningComputation.hpp"

#include <array>
#include <unordered_map>

#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/primvarSchema.h"

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

HnRenderPass::DrawListItem::DrawListItem(HnRenderDelegate& RenderDelegate,
                                         const HnDrawItem& Item) noexcept :
    DrawItem{Item},
    Mesh{Item.GetMesh()},
    MeshEntity{Mesh.GetEntity()},
    MeshUID{static_cast<float>(Mesh.GetUID())},
    RenderStateID{0},
    NumVertexBuffers{0}
{
    entt::registry& Registry = RenderDelegate.GetEcsRegistry();
    PrevTransform            = Registry.get<HnMesh::Components::Transform>(MeshEntity).Matrix;
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

    USD_Renderer&                   USDRenderer;
    const PBR_Renderer::CreateInfo& RendererSettings;

    IDeviceContext* const pCtx;

    const USD_Renderer::RenderPassType Type;
    const USD_Renderer::ALPHA_MODE     AlphaMode;

    const Uint32 ConstantBufferOffsetAlignment;
    const bool   NativeMultiDrawSupported;

    HnRenderPassStats Stats;

    RenderState(const HnRenderPass&      _RenderPass,
                const HnRenderPassState& _RPState) :
        RenderPass{_RenderPass},
        RPState{_RPState},
        RenderIndex{*RenderPass.GetRenderIndex()},
        RenderDelegate{*static_cast<HnRenderDelegate*>(RenderIndex.GetRenderDelegate())},
        RenderParam{*static_cast<const HnRenderParam*>(RenderDelegate.GetRenderParam())},
        USDRenderer{*RenderDelegate.GetUSDRenderer()},
        RendererSettings{USDRenderer.GetSettings()},
        pCtx{RenderDelegate.GetDeviceContext()},
        Type{RenderPass.m_Params.Type},
        AlphaMode{
            RenderPass.m_Params.AlphaMode != USD_Renderer::ALPHA_MODE_NUM_MODES ?
                RenderPass.m_Params.AlphaMode :
                MaterialTagToPbrAlphaMode(RenderPass.m_MaterialTag),
        },
        ConstantBufferOffsetAlignment{RenderDelegate.GetDevice()->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment},
        NativeMultiDrawSupported{RenderDelegate.GetDevice()->GetDeviceInfo().Features.NativeMultiDraw == DEVICE_FEATURE_STATE_ENABLED}
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

    void CommitMaterialSRB(const HnMaterial& Material, Uint32 PrimitiveAttribsOffset)
    {
        IShaderResourceBinding* pNewSRB = Material.GetSRB(PrimitiveAttribsOffset);
        if (pNewSRB == nullptr)
        {
            UNEXPECTED("Material SRB is null. This may happen if UpdateSRB was not called for this material.");
            return;
        }

        if (pNewSRB != this->pMaterialSRB)
        {
            this->MaterialBufferOffset = ~0u;
        }
        Material.ApplyMaterialAttribsBufferOffset(this->MaterialBufferOffset);

        if (pNewSRB == this->pMaterialSRB)
            return;

        if (pFrameSRB == nullptr)
        {
            pFrameSRB = RPState.GetFrameAttribsSRB();
            VERIFY_EXPR(pFrameSRB != nullptr);
            pCtx->CommitShaderResources(pFrameSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
        }

        if (Type == PBR_Renderer::RenderPassType::OITLayers && pRWOITLayersSRB == nullptr)
        {
            pRWOITLayersSRB = RPState.GetRWOITLayersSRB();
            VERIFY(pRWOITLayersSRB != nullptr, "RW OIT layers SRB is null. It should have been set in the render pass state by HnBeginOITPassTask::Execute().");
            pCtx->CommitShaderResources(pRWOITLayersSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
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
            GraphicsPipelineDesc GraphicsDesc = RenderPass.GetGraphicsDesc(RPState, USDRenderer, RenderDelegate.AllowPrimitiveRestart());

            PsoCache = USDRenderer.GetPsoCacheAccessor(GraphicsDesc);
            VERIFY_EXPR(PsoCache);
        }

        return PsoCache;
    }

private:
    IPipelineState*         pPSO            = nullptr;
    IShaderResourceBinding* pMaterialSRB    = nullptr;
    IShaderResourceBinding* pFrameSRB       = nullptr;
    IShaderResourceBinding* pRWOITLayersSRB = nullptr;

    IBuffer* pIndexBuffer = nullptr;

    Uint32 MaterialBufferOffset = ~0u;

    Uint32                                         NumVertexBuffers = 0;
    std::array<IBuffer*, VERTEX_BUFFER_SLOT_COUNT> ppVertexBuffers  = {};

    USD_Renderer::PsoCacheAccessor PsoCache;
};

GraphicsPipelineDesc HnRenderPass::GetGraphicsDesc(const HnRenderPassState& RPState, const USD_Renderer& USDRenderer, bool UseStripTopology) const
{
    GraphicsPipelineDesc GraphicsDesc = RPState.GetGraphicsPipelineDesc();

    // Set render targets write masks depending on the output flags
    for (USD_Renderer::USD_PSO_FLAGS Flags = USD_Renderer::USD_PSO_FLAG_ENABLE_ALL_OUTPUTS; Flags != USD_Renderer::PSO_FLAG_NONE;)
    {
        USD_Renderer::USD_PSO_FLAGS OutputFlag = ExtractLSB(Flags);
        if ((m_Params.UsdPsoFlags & OutputFlag) == 0)
        {
            const Uint32 RTIdx = USDRenderer.GetRenderTargetIndex(OutputFlag);
            VERIFY_EXPR(RTIdx != ~0u);
            if (RTIdx < GraphicsDesc.NumRenderTargets)
                GraphicsDesc.BlendDesc.RenderTargets[RTIdx].RenderTargetWriteMask = COLOR_MASK_NONE;
        }
    }

    switch (m_GeometryMode)
    {
        case HN_GEOMETRY_MODE_SOLID:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;

        case HN_GEOMETRY_MODE_MESH_EDGES:
            GraphicsDesc.PrimitiveTopology = UseStripTopology ? PRIMITIVE_TOPOLOGY_LINE_STRIP : PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;

        case HN_GEOMETRY_MODE_POINTS:
            GraphicsDesc.PrimitiveTopology = PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;

        default:
            UNEXPECTED("Unexpected render mode");
    }
    static_assert(HN_GEOMETRY_MODE_COUNT == 3, "Please handle the new geometry render mode in the switch above");

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

void HnRenderPass::WriteJointsDataBatch(RenderState& State, Uint32 BatchIdx, PBR_Renderer::PSO_FLAGS PSOFlags)
{
    IBuffer* const pJointsCB = State.USDRenderer.GetJointsBuffer();
    if (pJointsCB == nullptr)
    {
        UNEXPECTED("Joints buffer must not be null");
        return;
    }

    if (m_CurrDrawItemJointIdx >= m_DrawItemJoints.size())
    {
        UNEXPECTED("All joints data has been written");
        return;
    }
    VERIFY(BatchIdx >= m_DrawItemJoints[m_CurrDrawItemJointIdx].BatchIdx, "Batch indices must be monotonically increasing");
    while (m_CurrDrawItemJointIdx < m_DrawItemJoints.size() && BatchIdx > m_DrawItemJoints[m_CurrDrawItemJointIdx].BatchIdx)
    {
        ++m_CurrDrawItemJointIdx;
    }
    if (m_CurrDrawItemJointIdx == m_DrawItemJoints.size())
    {
        UNEXPECTED("Unable to find batch index ", BatchIdx);
        return;
    }
    VERIFY(m_DrawItemJoints[m_CurrDrawItemJointIdx].BatchIdx == BatchIdx, "Unexpected batch index");
    VERIFY(m_DrawItemJoints[m_CurrDrawItemJointIdx].BufferOffset == 0, "Joint data batch must start at offset 0");

    const BufferDesc& JointsBuffDesc = pJointsCB->GetDesc();

    Uint32 FrameNumber = static_cast<const HnRenderParam*>(State.RenderDelegate.GetRenderParam())->GetFrameNumber();

    Uint8* pJointsData = nullptr;
    if (JointsBuffDesc.Usage == USAGE_DYNAMIC)
    {
        State.pCtx->MapBuffer(pJointsCB, MAP_WRITE, MAP_FLAG_DISCARD, reinterpret_cast<PVoid&>(pJointsData));
        if (pJointsData == nullptr)
        {
            LOG_ERROR_MESSAGE("Failed to map joints data buffer '", JointsBuffDesc.Name, "'.");
            return;
        }
    }
    else
    {
        m_JointsData.resize(static_cast<size_t>(JointsBuffDesc.Size));
        pJointsData = m_JointsData.data();
    }

    Uint32 JointsDataSize = 0;
    while (m_CurrDrawItemJointIdx < m_DrawItemJoints.size())
    {
        DrawItemJointsData& Joints = m_DrawItemJoints[m_CurrDrawItemJointIdx];
        VERIFY(Joints.BatchIdx != ~0u && Joints.BufferOffset != ~0u, "Joint data is not initialized");
        if (Joints.BatchIdx != BatchIdx)
        {
            VERIFY(Joints.BatchIdx == BatchIdx + 1, "Batch indices must be monotonically increasing");
            break;
        }

        USD_Renderer::WriteSkinningDataAttribs WriteSkinningAttribs{
            PSOFlags,
            Joints.JointCount,
            reinterpret_cast<const float4x4*>(Joints.SkinComp->GetXforms().data()),
            reinterpret_cast<const float4x4*>(Joints.SkinComp->GetPrevFrameXforms(FrameNumber).data()),
        };

        JointsDataSize = Joints.BufferOffset + Joints.DataSize;
        VERIFY_EXPR(JointsDataSize <= JointsBuffDesc.Size);
        void* pDataEnd = State.USDRenderer.WriteSkinningData(pJointsData + Joints.BufferOffset, WriteSkinningAttribs);
        VERIFY_EXPR(JointsDataSize >= static_cast<Uint32>(static_cast<const Uint8*>(pDataEnd) - pJointsData));

        ++m_CurrDrawItemJointIdx;
    }

    if (JointsBuffDesc.Usage == USAGE_DYNAMIC)
    {
        State.pCtx->UnmapBuffer(pJointsCB, MAP_WRITE);
    }
    else
    {
        State.pCtx->UpdateBuffer(pJointsCB, 0, JointsDataSize, m_JointsData.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        StateTransitionDesc Barrier{pJointsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
        State.pCtx->TransitionResourceStates(1, &Barrier);
    }
}

HnRenderPass::EXECUTE_RESULT HnRenderPass::Execute(HnRenderPassState& RPState, const pxr::TfTokenVector& Tags)
{
    UpdateDrawList(Tags);
    if (m_DrawList.empty())
        return EXECUTE_RESULT_OK;

    RenderState State{*this, RPState};

    std::string DebugGroupName = std::string{"Render Pass - "} +
        USD_Renderer::GetRenderPassTypeString(m_Params.Type) + " - " +
        m_MaterialTag.GetString() + " - " +
        HnRenderPassParams::GetSelectionTypeString(m_Params.Selection);
    if (m_Params.AlphaMode != USD_Renderer::ALPHA_MODE_NUM_MODES)
    {
        DebugGroupName += " - ";
        DebugGroupName += USD_Renderer::GetAlphaModeString(m_Params.AlphaMode);
    }
    ScopedDebugGroup DebugGroup{State.pCtx, DebugGroupName.c_str()};

    RPState.Commit(State.pCtx);

    {
        PBR_Renderer::DebugViewType DebugView = HnViewModeToDebugViewType(State.RenderParam.GetViewMode());
        if (m_DebugView != DebugView)
        {
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;
            m_DebugView = DebugView;
        }
    }

    {
        HN_GEOMETRY_MODE GeometryMode = State.RenderParam.GetGeometryMode();
        if (m_GeometryMode != GeometryMode)
        {
            m_GeometryMode = GeometryMode;
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO | DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA;
            // Reset fallback PSO so that it is updated in UpdateDrawListGPUResources
            m_FallbackPSO = nullptr;
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

    if (m_DepthCompareFunc != RPState.GetDepthFunc())
    {
        m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;
        m_DepthCompareFunc = RPState.GetDepthFunc();
    }

    if (IRenderStateCache* pStateCache = State.RenderDelegate.GetRenderStateCache())
    {
        Uint32 ReloadVersion = pStateCache->GetReloadVersion();
        if (m_RenderStateCacheReloadVersion != ReloadVersion)
        {
            // Make sure all PSOs are added to m_PendingPSOs as they may be in compiling state
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;
            m_RenderStateCacheReloadVersion = ReloadVersion;
        }
    }

    {
        const Uint32 MaterialVersion    = State.RenderParam.GetAttribVersion(HnRenderParam::GlobalAttrib::Material);
        const Uint32 MeshCullingVersion = State.RenderParam.GetAttribVersion(HnRenderParam::GlobalAttrib::MeshCulling);
        if (m_GlobalAttribVersions.Material != MaterialVersion ||
            m_GlobalAttribVersions.MeshCulling != MeshCullingVersion)
        {
            // Attributes of some material have changed. We don't know which meshes may be affected,
            // so we need to process the entire draw list.

            // Also update draw list items when mesh culling changes as it affects the PSO.

            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_PSO;

            m_GlobalAttribVersions.Material    = MaterialVersion;
            m_GlobalAttribVersions.MeshCulling = MeshCullingVersion;
        }

        const Uint32 MeshResourceCacheVersion = HnMesh::GetCacheResourceVersion(State.RenderDelegate);
        if (m_GlobalAttribVersions.MeshResourceCache != MeshResourceCacheVersion)
        {
            m_GlobalAttribVersions.MeshResourceCache = MeshResourceCacheVersion;
            m_DrawListItemsDirtyFlags |= DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA;
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

    if (m_DrawListItemsDirtyFlags != DRAW_LIST_ITEM_DIRTY_FLAG_NONE)
    {
        // Draw list GPU resources have not been updated. This may only happen on the first frame.
        VERIFY(State.RenderParam.GetFrameNumber() <= 1, "Draw list items should always be updated by UpdateDrawListGPUResources except for the first frame.");
        return EXECUTE_RESULT_SKIPPED;
    }

    // Check the status of the pending PSOs
    if (!m_PendingPSOs.empty())
    {
        size_t NumPSOsReady = 0;
        for (auto& it : m_PendingPSOs)
        {
            if (!it.second)
            {
                it.second = (it.first->GetStatus() == PIPELINE_STATE_STATUS_READY);
                if (!it.second)
                {
                    // Note that in WebGL, checking pipeline status may block calling thread.
                    // If at least one PSO is not ready, do not check the rest.
                    break;
                }
            }

            ++NumPSOsReady;
        }
        if (NumPSOsReady == m_PendingPSOs.size())
        {
            m_PendingPSOs.clear();
        }
    }

    // Wait until all PSOs are ready and textures are loaded
    m_UseFallbackPSO = false;
    if (!m_PendingPSOs.empty() || State.RenderDelegate.GetTextureRegistry().GetNumTexturesLoading() > 0)
    {
        if (m_Params.Type == PBR_Renderer::RenderPassType::Main &&
            (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT) != 0 &&
            m_FallbackPSO != nullptr &&
            m_FallbackPSO->GetStatus() == PIPELINE_STATE_STATUS_READY)
        {
            m_UseFallbackPSO = true;
        }
        else
        {
            return EXECUTE_RESULT_SKIPPED;
        }
    }

    IBuffer* const pPrimitiveAttribsCB = State.RenderDelegate.GetPrimitiveAttribsCB();
    VERIFY_EXPR(pPrimitiveAttribsCB != nullptr);
    const bool PackMatrixRowMajor = State.RendererSettings.PackMatrixRowMajor;

    const BufferDesc& AttribsBuffDesc = pPrimitiveAttribsCB->GetDesc();

    m_PendingDrawItems.clear();
    m_PendingDrawItems.reserve(m_DrawList.size());
    void*  pMappedPrimitiveData = nullptr;
    Uint32 AttribsBufferOffset  = 0;

    Uint32 CurrJointDataBatch     = ~0u;
    Uint32 CurrJointsBufferOffset = ~0u;
    m_CurrDrawItemJointIdx        = 0;

    if (AttribsBuffDesc.Usage != USAGE_DYNAMIC)
    {
        m_PrimitiveAttribsData.resize(static_cast<size_t>(AttribsBuffDesc.Size));
    }

    auto FlushPendingDraws = [&]() {
        VERIFY_EXPR(AttribsBufferOffset > 0);
        VERIFY_EXPR(AttribsBuffDesc.Usage == USAGE_DYNAMIC || AttribsBufferOffset <= m_PrimitiveAttribsData.size());
        if (AttribsBuffDesc.Usage == USAGE_DYNAMIC)
        {
            if (pMappedPrimitiveData != nullptr)
            {
                State.pCtx->UnmapBuffer(pPrimitiveAttribsCB, MAP_WRITE);
            }
            pMappedPrimitiveData = nullptr;
        }
        else
        {
            State.pCtx->UpdateBuffer(pPrimitiveAttribsCB, 0, AttribsBufferOffset, m_PrimitiveAttribsData.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            StateTransitionDesc Barrier{pPrimitiveAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            State.pCtx->TransitionResourceStates(1, &Barrier);
        }
        AttribsBufferOffset = 0;

        RenderPendingDrawItems(State);
        VERIFY_EXPR(m_PendingDrawItems.empty());
    };

    const Uint32 PrimitiveArraySize = !m_UseFallbackPSO ?
        std::max(State.RendererSettings.PrimitiveArraySize, 1u) :
        1u; // Fallback PSO uses flags that are not consistent with material SRB flags.
            // Hence the size of the shader primitive data is different and we can't use multi-draw.
    m_ScratchSpace.resize(sizeof(MultiDrawIndexedItem) * State.RendererSettings.PrimitiveArraySize);

    entt::registry& Registry = State.RenderDelegate.GetEcsRegistry();

    // Note: accessing components through a view is faster than accessing them through the registry.
    auto MeshAttribsView = Registry.view<const HnMesh::Components::Transform,
                                         const HnMesh::Components::DisplayColor,
                                         const HnMesh::Components::Visibility,
                                         const HnMesh::Components::Skinning>();

    Uint32 MultiDrawCount = 0;
    for (size_t item_idx = 0; item_idx < m_ValidDrawItemCount; ++item_idx)
    {
        DrawListItem& ListItem = m_DrawList[item_idx];
        VERIFY(ListItem.DrawItem.IsValid(), "All valid draw items must be grouped at the beginning of the list by UpdateDrawListGPUResources");

        if (!ListItem)
            continue;

        const auto& MeshAttribs = MeshAttribsView.get<const HnMesh::Components::Transform,
                                                      const HnMesh::Components::DisplayColor,
                                                      const HnMesh::Components::Visibility>(ListItem.MeshEntity);

        const HnMesh::Components::Transform& Transform    = std::get<0>(MeshAttribs);
        const float4&                        DisplayColor = std::get<1>(MeshAttribs).Val;
        const bool                           MeshVisibile = std::get<2>(MeshAttribs).Val;
        const PBR_Renderer::PSO_FLAGS        PSOFlags     = m_UseFallbackPSO ? GetFallbackPSOFlags() : ListItem.PSOFlags;

        const HnMesh::Components::Skinning* pSkinningData = (PSOFlags & PBR_Renderer::PSO_FLAG_USE_JOINTS) ?
            &MeshAttribsView.get<const HnMesh::Components::Skinning>(ListItem.MeshEntity) :
            nullptr;
        VERIFY(pSkinningData == nullptr || ListItem.JointsIdx != ~0u, "Joints index must be valid if skinning data is present");

        const DrawItemJointsData& Joints = (PSOFlags & PBR_Renderer::PSO_FLAG_USE_JOINTS) != 0 && ListItem.JointsIdx != ~0u ?
            m_DrawItemJoints[ListItem.JointsIdx] :
            DrawItemJointsData{};
        VERIFY(((pSkinningData != nullptr ? pSkinningData->Computation->GetXformsHash() : 0) ==
                (Joints.SkinComp != nullptr ? Joints.SkinComp->GetXformsHash() : 0)),
               "Skinning xforms must match the joints data");

        if (!MeshVisibile)
            continue;

        if (MultiDrawCount == PrimitiveArraySize)
            MultiDrawCount = 0;

        if (Joints)
        {
            if (State.RendererSettings.JointsBufferMode == USD_Renderer::JOINTS_BUFFER_MODE_UNIFORM &&
                Joints.BufferOffset != CurrJointsBufferOffset)
            {
                // Start a new multi-draw batch if the joints buffer offset changes
                MultiDrawCount         = 0;
                CurrJointsBufferOffset = Joints.BufferOffset;
            }

            if (Joints.BatchIdx != CurrJointDataBatch)
            {
                VERIFY(CurrJointDataBatch == ~0u || Joints.BatchIdx > CurrJointDataBatch, "Joint data batch indices must be increasing");
                if (CurrJointDataBatch != ~0u)
                {
                    // Joints buffer needs to be updated - flush pending draws
                    MultiDrawCount      = 0;
                    AttribsBufferOffset = AttribsBuffDesc.Size;
                }
                else
                {
                    // Joints buffer needs to be updated, but previous draw items do not use it.
                }
            }
        }

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
                            FirstMultiDrawItem.ListItem.pMaterial == ListItem.pMaterial);
                VERIFY_EXPR(AttribsBufferOffset + ListItem.ShaderAttribsDataSize <= AttribsBuffDesc.Size);

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
            AttribsBufferOffset = AlignUp(AttribsBufferOffset, State.ConstantBufferOffsetAlignment);

            // Note that the actual attribs size may be smaller than the range, but we need
            // to check for the entire range to avoid errors because this range is set in
            // the shader variable in the SRB.
            if (AttribsBufferOffset + ListItem.PrimitiveAttribsBufferRange > AttribsBuffDesc.Size)
            {
                // The buffer is full. Render the pending items and start filling the buffer from the beginning.
                FlushPendingDraws();
            }
        }

        if (Joints && Joints.BatchIdx != CurrJointDataBatch)
        {
            // Write next joints data batch after flushing pending draws
            WriteJointsDataBatch(State, Joints.BatchIdx, PSOFlags);
            VERIFY(CurrJointDataBatch == ~0u || Joints.BatchIdx > CurrJointDataBatch, "Joint data batch indices must be increasing");
            CurrJointDataBatch = Joints.BatchIdx;
        }

        void* pCurrPrimitive = nullptr;
        if (AttribsBuffDesc.Usage == USAGE_DYNAMIC)
        {
            if (pMappedPrimitiveData == nullptr)
            {
                State.pCtx->MapBuffer(pPrimitiveAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD, pMappedPrimitiveData);
                if (pMappedPrimitiveData == nullptr)
                {
                    LOG_ERROR_MESSAGE("Failed to map primitive attribs buffer '", AttribsBuffDesc.Name, "'.");
                    break;
                }
            }
            pCurrPrimitive = reinterpret_cast<Uint8*>(pMappedPrimitiveData) + AttribsBufferOffset;
        }
        else
        {
            VERIFY_EXPR(AttribsBufferOffset + ListItem.ShaderAttribsDataSize <= m_PrimitiveAttribsData.size());
            pCurrPrimitive = &m_PrimitiveAttribsData[AttribsBufferOffset];
        }

        // Write current primitive attributes
        GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData AttribsData{
            PSOFlags,
            &Transform.Matrix,
            &ListItem.PrevTransform,
            Joints.JointCount,
            Joints.FirstJoint,
            &Transform.PosScale,
            &Transform.PosBias,
            pSkinningData ? reinterpret_cast<const float4x4*>(pSkinningData->GeomBindXform.Data()) : nullptr,
            pSkinningData ? reinterpret_cast<const float4x4*>(pSkinningData->GeomBindXform.Data()) : nullptr,
            &DisplayColor,
            &ListItem.MeshUID,        // CustomData
            sizeof(ListItem.MeshUID), // CustomDataSize
        };
        GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(pCurrPrimitive, AttribsData,
                                                          /*TransposeMatrices = */ !PackMatrixRowMajor,
                                                          State.RendererSettings.UseSkinPreTransform);

        ListItem.PrevTransform = Transform.Matrix;

        Uint32 JointsBufferOffset = ~0u;
        if (Joints)
        {
            JointsBufferOffset = (State.RendererSettings.JointsBufferMode == USD_Renderer::JOINTS_BUFFER_MODE_UNIFORM) ?
                Joints.BufferOffset :
                0;
        }

        m_PendingDrawItems.push_back(PendingDrawItem{
            ListItem,
            AttribsBufferOffset,
            JointsBufferOffset,
        });

        AttribsBufferOffset += ListItem.ShaderAttribsDataSize;
        ++MultiDrawCount;
    }
#ifdef DILIGENT_DEBUG
    for (size_t i = m_ValidDrawItemCount; i < m_DrawList.size(); ++i)
    {
        VERIFY(!m_DrawList[i].DrawItem.IsValid(), "All invalid draw items must be grouped at the end of the list by UpdateDrawListGPUResources");
    }
#endif
    if (AttribsBufferOffset != 0)
    {
        FlushPendingDraws();
    }

    RPState.UpdateStats(State.Stats);

    return m_UseFallbackPSO ? EXECUTE_RESULT_FALLBACK : EXECUTE_RESULT_OK;
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
                // Add all draw items, including invalid ones, to the draw list.
                // An item may be currently invalid if it was skipped during the last sync,
                // but may become valid soon. When this happens, mesh geometry version will
                // be updated, and items in the list will be updated, but the list itself
                // will not be recreated.
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

void HnRenderPass::UpdateDrawListJoints(HnRenderDelegate& RenderDelegate)
{
    // Each skinned draw item references joints data.
    // Joints data is organized in batches that fit into the joints buffer.
    //
    //              |             Batch 0             | |          Batch 1         |
    //              |                                 | |                          |
    //  Draw Items  [  0  ][  0  ][ -1  ][  1  ][  1  ] [  0  ][  0  ][ -1  ][  1  ]
    //                 |      |             |      |       |      |             |
    //                 |.----'  .-----------'------'       |      |             |
    //                 |       |      .--------------------'------'             |
    //                 |       |     |      .-----------------------------------'
    //                 V       V     V      V
    // Joints Data  [     ][     ][     ][     ]
    //              |  upload 0  ||  upload 1  |

    m_DrawItemJoints.clear();

    const USD_Renderer&             USDRenderer      = *RenderDelegate.GetUSDRenderer();
    const PBR_Renderer::CreateInfo& RendererSettings = USDRenderer.GetSettings();

    if (RendererSettings.MaxJointCount == 0)
        return;

    IBuffer* pJointsCB = USDRenderer.GetJointsBuffer();
    if (pJointsCB == nullptr)
    {
        UNEXPECTED("Joints buffer must not be null");
        return;
    }
    const BufferDesc& JointsBuffDesc = pJointsCB->GetDesc();

    const Uint32 JointsBufferOffsetAlignment = (RendererSettings.JointsBufferMode == USD_Renderer::JOINTS_BUFFER_MODE_UNIFORM) ?
        RenderDelegate.GetDevice()->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment :
        sizeof(float4x4);

    auto MeshSkinningView = RenderDelegate.GetEcsRegistry().view<const HnMesh::Components::Skinning>();

    Uint32                             CurrBufferOffset = 0;
    Uint32                             CurrBatchIdx     = 0;
    std::unordered_map<size_t, Uint32> XformsHashToJointDataIdx;
    for (DrawListItem& ListItem : m_DrawList)
    {
        if (!ListItem.DrawItem.IsValid() || !ListItem.DrawItem.GetGeometryData().Joints)
            continue;

        const HnMesh::Components::Skinning& Skinning = MeshSkinningView.get<const HnMesh::Components::Skinning>(ListItem.MeshEntity);
        const HnSkinningComputation*        SkinComp = Skinning.Computation;
        if (SkinComp == nullptr)
            continue;

        size_t XformsHash = SkinComp->GetXformsHash();
        VERIFY_EXPR(XformsHash != 0);

        // Note that we rely on the XformsHash computed at the time when the draw list is updated.
        // In theory it is possible that different xforms have the same hash at one time and
        // different hashes at another time, but this is highly unlikely.
        auto XformsHashIt = XformsHashToJointDataIdx.find(XformsHash);
        if (XformsHashIt != XformsHashToJointDataIdx.end())
        {
            ListItem.JointsIdx = XformsHashIt->second;
            continue;
        }

        DrawItemJointsData Joints;

        Joints.JointCount = std::min(static_cast<Uint32>(SkinComp->GetXforms().size()), RendererSettings.MaxJointCount);
        // Reserve max space that may be needed for the joints data.
        Joints.DataSize = USDRenderer.GetJointsDataSize(Joints.JointCount, PBR_Renderer::PSO_FLAG_ALL);

        Uint32 JointsDataRange = 0;
        if (RendererSettings.JointsBufferMode == USD_Renderer::JOINTS_BUFFER_MODE_UNIFORM)
        {
            // RenderPBR.vsh
            //
            //  struct SkinnigData
            //  {
            //  #   if COMPUTE_MOTION_VECTORS
            //          float4x4 Joints[MAX_JOINT_COUNT * 2];
            //  #   else
            //          float4x4 Joints[MAX_JOINT_COUNT];
            //  #   endif
            //  };
            //
            // Joints data range is the size of the entire structure with the maximum number of joints.
            JointsDataRange = USDRenderer.GetJointsDataSize(RendererSettings.MaxJointCount, PBR_Renderer::PSO_FLAG_ALL);
        }
        else if (RendererSettings.JointsBufferMode == USD_Renderer::JOINTS_BUFFER_MODE_STRUCTURED)
        {
            // RenderPBR.vsh
            //
            // StructuredBuffer<float4x4> g_JointTransforms;
            //
            // Joints data range is the size of the actual data.
            JointsDataRange = Joints.DataSize;
        }
        else
        {
            UNEXPECTED("Unexpected joints buffer mode");
        }

        if (CurrBufferOffset + JointsDataRange > JointsBuffDesc.Size)
        {
            // There is not enough space for the new joint transforms - start new batch.
            CurrBufferOffset = 0;
            ++CurrBatchIdx;
            XformsHashToJointDataIdx.clear();
        }

        Joints.BatchIdx     = CurrBatchIdx;
        Joints.BufferOffset = CurrBufferOffset;

        // In structured buffer mode, we use the first joint index.
        // In uniform buffer mode, we use the joint buffer offset.
        Joints.FirstJoint = (RendererSettings.JointsBufferMode == USD_Renderer::JOINTS_BUFFER_MODE_STRUCTURED) ?
            CurrBufferOffset / sizeof(float4x4) :
            0;

        Joints.SkinComp = SkinComp;

        ListItem.JointsIdx = static_cast<Uint32>(m_DrawItemJoints.size());
        XformsHashToJointDataIdx.emplace(XformsHash, ListItem.JointsIdx);
        m_DrawItemJoints.push_back(Joints);

        CurrBufferOffset = AlignUp(CurrBufferOffset + Joints.DataSize, JointsBufferOffsetAlignment);
    }
}

PBR_Renderer::PSO_FLAGS HnRenderPass::GetFallbackPSOFlags() const
{
    return (m_GeometryMode == HN_GEOMETRY_MODE_SOLID ?
                PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS :
                PBR_Renderer::PSO_FLAG_UNSHADED) |
        static_cast<PBR_Renderer::PSO_FLAGS>(m_Params.UsdPsoFlags);
}

void HnRenderPass::UpdateDrawListGPUResources(RenderState& State)
{
    if (m_DrawListItemsDirtyFlags & DRAW_LIST_ITEM_DIRTY_FLAG_PSO)
    {
        m_PendingPSOs.clear();
    }

    if (m_FallbackPSO == nullptr &&
        m_Params.Type == PBR_Renderer::RenderPassType::Main &&
        (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT) != 0 &&
        m_GeometryMode == HN_GEOMETRY_MODE_SOLID)
    {
        const PBR_Renderer::PSOKey FallbackPSOKey{
            PBR_Renderer::RenderPassType::Main,
            GetFallbackPSOFlags(),
            PBR_Renderer::ALPHA_MODE_OPAQUE,
            CULL_MODE_NONE,
            PBR_Renderer::DebugViewType::None,
            PBR_Renderer::LoadingAnimationMode::Always,
        };
        m_FallbackPSO = State.GePsoCache().Get(FallbackPSOKey, PBR_Renderer::PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL);
    }

    if (State.RenderParam.GetFrameNumber() <= 1)
    {
        // Do not initialize draw items on the first frame to allow
        // post-processing shaders to be compiled first.
        return;
    }

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
                                             State.Item.pMaterial);
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
            if (Hash != rhs.Hash)
                return false;

            // clang-format off
            if (Item.pPSO             != rhs.Item.pPSO ||
                Item.IndexBuffer      != rhs.Item.IndexBuffer ||
                Item.NumVertexBuffers != rhs.Item.NumVertexBuffers ||
                Item.pMaterial        != rhs.Item.pMaterial)
                return false;
            // clang-format on

            for (Uint32 i = 0; i < Item.NumVertexBuffers; ++i)
            {
                if (Item.VertexBuffers[i] != rhs.Item.VertexBuffers[i])
                    return false;
            }

            return true;
        }
    };
    std::unordered_map<DrawListItemRenderState, Uint32, DrawListItemRenderState::Hasher> DrawListItemRenderStateIDs;

    bool DrawListDirty   = false;
    m_ValidDrawItemCount = 0;
    for (DrawListItem& ListItem : m_DrawList)
    {
        if (!ListItem.DrawItem.IsValid())
        {
            // Skip invalid draw items.
            continue;
        }

        DRAW_LIST_ITEM_DIRTY_FLAGS DrawItemGPUResDirtyFlags = m_DrawListItemsDirtyFlags;

        const Uint32 Version = ListItem.Mesh.GetGeometryVersion() + ListItem.Mesh.GetMaterialVersion();
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
        ++m_ValidDrawItemCount;
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
                      if (!Item0.DrawItem.IsValid() || !Item1.DrawItem.IsValid())
                      {
                          // Group all invalid items at the end of the list
                          Item0PrecedesItem1 = Item0.DrawItem.IsValid() && !Item1.DrawItem.IsValid();
                      }
                      else
                      {
                          // Sort by PSO first, then by material SRB, then by material, and finally by entity ID
                          if (Item0.pPSO < Item1.pPSO)
                              Item0PrecedesItem1 = true;
                          else if (Item0.pPSO > Item1.pPSO)
                              Item0PrecedesItem1 = false;
                          else if (Item0.pMaterial->GetSRB() < Item1.pMaterial->GetSRB())
                              Item0PrecedesItem1 = true;
                          else if (Item0.pMaterial->GetSRB() > Item1.pMaterial->GetSRB())
                              Item0PrecedesItem1 = false;
                          else if (Item0.pMaterial < Item1.pMaterial)
                              Item0PrecedesItem1 = true;
                          else if (Item0.pMaterial > Item1.pMaterial)
                              Item0PrecedesItem1 = false;
                          else
                              Item0PrecedesItem1 = Item0.MeshEntity < Item1.MeshEntity;
                      }

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

#ifdef DILIGENT_DEBUG
        for (size_t i = 0; i < m_ValidDrawItemCount; ++i)
        {
            VERIFY(m_DrawList[i].DrawItem.IsValid(), "All valid draw items must be grouped at the beginning of the list");
        }
        for (size_t i = m_ValidDrawItemCount; i < m_DrawList.size(); ++i)
        {
            VERIFY(!m_DrawList[i].DrawItem.IsValid(), "All invalid draw items must be grouped at the end of the list");
        }
#endif

        UpdateDrawListJoints(State.RenderDelegate);
    }

    m_DrawListItemsDirtyFlags = DRAW_LIST_ITEM_DIRTY_FLAG_NONE;
}

HnRenderPass::SupportedVertexInputsMapType HnRenderPass::GetSupportedVertexInputs(const HnMaterial* Material)
{
    SupportedVertexInputsMapType SupportedInputs{
        {pxr::HdTokens->points, pxr::HdPrimvarSchemaTokens->point},
        {pxr::HdTokens->normals, pxr::HdPrimvarSchemaTokens->normal},
        {pxr::HdTokens->displayColor, pxr::HdPrimvarSchemaTokens->color},
    };
    if (Material != nullptr)
    {
        const auto& TexCoordSets = Material->GetTextureCoordinateSets();
        for (const auto& TexCoordSet : TexCoordSets)
        {
            if (!TexCoordSet.PrimVarName.IsEmpty())
                SupportedInputs.emplace(TexCoordSet.PrimVarName, pxr::HdPrimvarSchemaTokens->textureCoordinate);
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

        const HnMaterial* pMaterial = DrawItem.GetMaterial();
        VERIFY(pMaterial != nullptr, "Material must not be null");
        if (pMaterial->GetSRB() == nullptr)
        {
            // Use fallback material until the material is initialized.
            // When all textures are loaded, the render delegate will make the global material
            // attrib dirty, and the material will be reinitialized.
            pMaterial = State.RenderDelegate.GetFallbackMaterial();
            VERIFY(pMaterial != nullptr, "Fallback material must not be null");
            VERIFY(pMaterial->GetSRB() != nullptr, "Fallback material must be initialized");
        }
        ListItem.pMaterial = pMaterial;

        auto& PsoCache = State.GePsoCache();
        VERIFY_EXPR(PsoCache);

        PBR_Renderer::PSO_FLAGS& PSOFlags{ListItem.PSOFlags};
        PSOFlags = static_cast<PBR_Renderer::PSO_FLAGS>(m_Params.UsdPsoFlags);

        const HnDrawItem::GeometryData& Geo      = DrawItem.GetGeometryData();
        const CULL_MODE                 CullMode = ListItem.Mesh.GetCullMode();

        // Use the material's texture indexing ID as the user value in the PSO key.
        // The USD renderer will use this ID to return the indexing.
        const auto ShaderTextureIndexingId = pMaterial->GetStaticShaderTextureIndexingId();

        PBR_Renderer::PsoCacheAccessor::GET_FLAGS GetPSOFlags = PBR_Renderer::PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL;
        if (State.RenderParam.GetConfig().AsyncShaderCompilation)
            GetPSOFlags |= PBR_Renderer::PsoCacheAccessor::GET_FLAG_ASYNC_COMPILE;

        if (Geo.Joints != nullptr)
            PSOFlags |= PBR_Renderer::PSO_FLAG_USE_JOINTS;

        if (m_GeometryMode == HN_GEOMETRY_MODE_SOLID)
        {
            if ((m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT) != 0)
            {
                if (Geo.Normals != nullptr)
                    PSOFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS;
                if (Geo.VertexColors != nullptr)
                    PSOFlags |= PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS;
            }
            if (Geo.TexCoords[0] != nullptr)
                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD0;
            if (Geo.TexCoords[1] != nullptr)
                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXCOORD1;

            if ((m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_MOTION_VECTORS_OUTPUT) != 0)
            {
                PSOFlags |= PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS;
            }

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
                    PSOFlags |= (MaterialPSOFlags & (PBR_Renderer::PSO_FLAG_USE_COLOR_MAP | PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM));
                }

                VERIFY(((m_Params.AlphaMode != USD_Renderer::ALPHA_MODE_NUM_MODES ? m_Params.AlphaMode : pMaterial->GetMaterialData().Attribs.AlphaMode) == State.AlphaMode ||
                        pMaterial->GetId().IsEmpty()),
                       "Alpha mode derived from the material tag is not consistent with the alpha mode in the shader attributes. "
                       "This may indicate an issue in how alpha mode is determined in the material, or (less likely) an issue in Rprim sorting by Hydra.");
            }

            if (State.RenderParam.GetConfig().TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
                PSOFlags |= PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS;

            if (State.USDRenderer.GetSettings().EnableShadows &&
                (m_Params.UsdPsoFlags & USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT) != 0 &&
                State.RenderParam.GetUseShadows())
                PSOFlags |= PBR_Renderer::PSO_FLAG_ENABLE_SHADOWS;

            const PBR_Renderer::LoadingAnimationMode LoadingAnimationMode = (m_FallbackPSO != nullptr) ?
                PBR_Renderer::LoadingAnimationMode::Transitioning :
                PBR_Renderer::LoadingAnimationMode::None;

            const PBR_Renderer::PSOKey PSOKey{
                m_Params.Type,
                PSOFlags,
                static_cast<PBR_Renderer::ALPHA_MODE>(State.AlphaMode),
                CullMode,
                m_DebugView,
                LoadingAnimationMode,
                ShaderTextureIndexingId,
            };
            ListItem.pPSO = PsoCache.Get(PSOKey, GetPSOFlags);
            // PSOKey may have cleared some flags - get updated flags
            PSOFlags = PSOKey.GetFlags();
        }
        else if (m_GeometryMode == HN_GEOMETRY_MODE_MESH_EDGES ||
                 m_GeometryMode == HN_GEOMETRY_MODE_POINTS)
        {
            VERIFY_EXPR(m_Params.Type == PBR_Renderer::RenderPassType::Main);
            PSOFlags |= PBR_Renderer::PSO_FLAG_UNSHADED;
            const PBR_Renderer::PSOKey PSOKey{
                m_Params.Type,
                PSOFlags,
                CullMode,
                PBR_Renderer::DebugViewType::None,
                PBR_Renderer::LoadingAnimationMode::None,
                ShaderTextureIndexingId,
            };
            ListItem.pPSO = PsoCache.Get(PSOKey, GetPSOFlags);
            // PSOKey may have cleared some flags - get updated flags
            PSOFlags = PSOKey.GetFlags();
        }
        else
        {
            UNEXPECTED("Unexpected render mode");
        }

        ListItem.ShaderAttribsDataSize       = StaticCast<Uint16>(State.USDRenderer.GetPBRPrimitiveAttribsSize(PSOFlags));
        ListItem.PrimitiveAttribsBufferRange = StaticCast<Uint16>(pMaterial->GetPBRPrimitiveAttribsBufferRange());
        VERIFY(ListItem.ShaderAttribsDataSize <= ListItem.PrimitiveAttribsBufferRange,
               "Attribs data size (", ListItem.ShaderAttribsDataSize, ") computed from the PSO flags exceeds the attribs buffer range (",
               ListItem.PrimitiveAttribsBufferRange, ") computed from material PSO flags. The latter is used by HnMaterial to set the buffer range.");

        // Note: some PSOs (e.g. shadow) may not use the full range of the material attribs buffer.
        VERIFY(ListItem.pMaterial->GetPBRMaterialAttribsSize() >= State.USDRenderer.GetPBRMaterialAttribsSize(PSOFlags),
               "Material attribs size is smaller than required by the PSO flags");
        VERIFY_EXPR(ListItem.pPSO != nullptr);
        m_PendingPSOs.emplace(ListItem.pPSO, false);
    }

    if (DirtyFlags & DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA)
    {
        const HnDrawItem::GeometryData& Geo = DrawItem.GetGeometryData();

        static_assert(HN_GEOMETRY_MODE_COUNT == 3, "Please handle the new geometry render mode here");

        // Input layout is defined by HnRenderDelegate when creating USD renderer.
        ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_POSITIONS]     = Geo.Positions;
        ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_VERTEX_JOINTS] = Geo.Joints;
        if (m_GeometryMode == HN_GEOMETRY_MODE_SOLID)
        {
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_NORMALS]       = Geo.Normals;
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_TEX_COORDS0]   = Geo.TexCoords[0];
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_TEX_COORDS1]   = Geo.TexCoords[1];
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_VERTEX_COLORS] = Geo.VertexColors;

            // It is OK if some buffers are null
            ListItem.NumVertexBuffers = VERTEX_BUFFER_SLOT_COUNT;
        }
        else if (m_GeometryMode == HN_GEOMETRY_MODE_MESH_EDGES ||
                 m_GeometryMode == HN_GEOMETRY_MODE_POINTS)
        {
            // Only positions and joints are used
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_NORMALS]       = nullptr;
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_TEX_COORDS0]   = nullptr;
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_TEX_COORDS1]   = nullptr;
            ListItem.VertexBuffers[VERTEX_BUFFER_SLOT_VERTEX_COLORS] = nullptr;

            ListItem.NumVertexBuffers = (Geo.Joints ? VERTEX_BUFFER_SLOT_VERTEX_JOINTS : VERTEX_BUFFER_SLOT_POSITIONS) + 1;
        }
        else
        {
            UNEXPECTED("Unexpected render mode");
        }

        const HnDrawItem::TopologyData* Topology = nullptr;
        switch (m_GeometryMode)
        {
            case HN_GEOMETRY_MODE_SOLID:
                Topology = &DrawItem.GetFaces();
                break;

            case HN_GEOMETRY_MODE_MESH_EDGES:
                Topology = &DrawItem.GetEdges();
                break;

            case HN_GEOMETRY_MODE_POINTS:
                Topology = &DrawItem.GetPoints();
                break;

            default:
                UNEXPECTED("Unexpected render mode");
        }

        if (Topology != nullptr)
        {
            ListItem.IndexBuffer = Topology->IndexBuffer;
            ListItem.StartIndex  = Topology->StartIndex;
            ListItem.NumVertices = Topology->NumVertices;
            ListItem.StartVertex = Topology->StartVertex;
        }
        else
        {
            ListItem.IndexBuffer = nullptr;
            ListItem.StartIndex  = 0;
            ListItem.NumVertices = 0;
            ListItem.StartVertex = 0;
        }
    }
}

void HnRenderPass::RenderPendingDrawItems(RenderState& State)
{
    size_t item_idx           = 0;
    Uint32 JointsBufferOffset = ~0u;
    while (item_idx < m_PendingDrawItems.size())
    {
        const PendingDrawItem& PendingItem = m_PendingDrawItems[item_idx];
        const DrawListItem&    ListItem    = PendingItem.ListItem;

        State.SetPipelineState(m_UseFallbackPSO ? m_FallbackPSO : ListItem.pPSO);

        if (State.RendererSettings.JointsBufferMode == USD_Renderer::JOINTS_BUFFER_MODE_UNIFORM)
        {
            if (PendingItem.JointsBufferOffset != ~0u && PendingItem.JointsBufferOffset != JointsBufferOffset)
            {
                JointsBufferOffset = PendingItem.JointsBufferOffset;
                ListItem.pMaterial->SetJointsBufferOffset(JointsBufferOffset);
            }
        }
        else
        {
            // In structured buffer mode, we use the first joint index, so we do not need to set the joint buffer offset.
        }
        State.CommitMaterialSRB(*ListItem.pMaterial, PendingItem.PrimitiveAttribsOffset);

        State.SetIndexBuffer(ListItem.IndexBuffer);
        State.SetVertexBuffers(ListItem.VertexBuffers.data(), ListItem.NumVertexBuffers);

        if (PendingItem.DrawCount > 1)
        {
#ifdef DILIGENT_DEBUG
            VERIFY_EXPR(item_idx + PendingItem.DrawCount <= m_PendingDrawItems.size());
            for (size_t i = 1; i < PendingItem.DrawCount; ++i)
            {
                const PendingDrawItem& BatchItem     = m_PendingDrawItems[item_idx + i];
                const DrawListItem&    BatchListItem = BatchItem.ListItem;
                // clang-format off
                VERIFY_EXPR(BatchListItem.RenderStateID    == ListItem.RenderStateID &&
                            BatchListItem.pPSO             == ListItem.pPSO &&
                            BatchListItem.IndexBuffer      == ListItem.IndexBuffer &&
                            BatchListItem.NumVertexBuffers == ListItem.NumVertexBuffers &&
                            BatchListItem.VertexBuffers    == ListItem.VertexBuffers &&
                            BatchListItem.pMaterial->GetSRB() == ListItem.pMaterial->GetSRB() &&
                            BatchItem.JointsBufferOffset == PendingItem.JointsBufferOffset);
                // clang-format on
            }
            VERIFY_EXPR(m_ScratchSpace.size() >= PendingItem.DrawCount * (ListItem.IndexBuffer != nullptr ? sizeof(MultiDrawIndexedItem) : sizeof(MultiDrawItem)));
#endif

            if (ListItem.IndexBuffer != nullptr)
            {
                if (State.NativeMultiDrawSupported)
                {
                    MultiDrawIndexedItem* pMultiDrawItems = reinterpret_cast<MultiDrawIndexedItem*>(m_ScratchSpace.data());
                    for (size_t i = 0; i < PendingItem.DrawCount; ++i)
                    {
                        const DrawListItem& BatchItem = m_PendingDrawItems[item_idx + i].ListItem;
                        pMultiDrawItems[i]            = {BatchItem.NumVertices, BatchItem.StartIndex, BatchItem.StartVertex};
                    }
                    State.pCtx->MultiDrawIndexed({PendingItem.DrawCount, pMultiDrawItems, VT_UINT32, DRAW_FLAG_VERIFY_ALL});
                }
                else
                {
                    for (size_t i = 0; i < PendingItem.DrawCount; ++i)
                    {
                        // When native multi-draw is not supported, we pass primitive ID as instance ID.
                        const DrawListItem& BatchItem = m_PendingDrawItems[item_idx + i].ListItem;
                        DrawIndexedAttribs  Attribs{BatchItem.NumVertices, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
                        if (i > 0)
                        {
                            Attribs.Flags |= DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT;
                        }
                        Attribs.FirstIndexLocation    = BatchItem.StartIndex;
                        Attribs.FirstInstanceLocation = i;
                        Attribs.BaseVertex            = BatchItem.StartVertex;
                        State.pCtx->DrawIndexed(Attribs);
                    }
                }
            }
            else
            {
                if (State.NativeMultiDrawSupported)
                {
                    MultiDrawItem* pMultiDrawItems = reinterpret_cast<MultiDrawItem*>(m_ScratchSpace.data());
                    for (size_t i = 0; i < PendingItem.DrawCount; ++i)
                    {
                        const DrawListItem& BatchItem = m_PendingDrawItems[item_idx + i].ListItem;
                        pMultiDrawItems[i]            = {BatchItem.NumVertices, BatchItem.StartVertex};
                    }
                    State.pCtx->MultiDraw({PendingItem.DrawCount, pMultiDrawItems, DRAW_FLAG_VERIFY_ALL});
                }
                else
                {
                    // When native multi-draw is not supported, we pass primitive ID as instance ID.
                    for (size_t i = 0; i < PendingItem.DrawCount; ++i)
                    {
                        const DrawListItem& BatchItem = m_PendingDrawItems[item_idx + i].ListItem;
                        DrawAttribs         Attribs{BatchItem.NumVertices, DRAW_FLAG_VERIFY_ALL};
                        if (i > 0)
                        {
                            Attribs.Flags |= DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT;
                        }
                        Attribs.FirstInstanceLocation = i;
                        Attribs.StartVertexLocation   = BatchItem.StartVertex;
                        State.pCtx->Draw(Attribs);
                    }
                }
            }
        }
        else
        {
            constexpr Uint32 NumInstances = 1;
            if (ListItem.IndexBuffer != nullptr)
            {
                State.pCtx->DrawIndexed({ListItem.NumVertices, VT_UINT32, DRAW_FLAG_VERIFY_ALL, NumInstances, ListItem.StartIndex, ListItem.StartVertex});
            }
            else
            {
                State.pCtx->Draw({ListItem.NumVertices, DRAW_FLAG_VERIFY_ALL, NumInstances, ListItem.StartVertex});
            }
        }

        item_idx += PendingItem.DrawCount;
    }

    State.Stats.NumDrawItems += static_cast<Uint32>(m_PendingDrawItems.size());

    m_PendingDrawItems.clear();
}

} // namespace USD

} // namespace Diligent
