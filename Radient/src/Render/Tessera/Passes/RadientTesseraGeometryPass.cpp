/*
 *  Copyright 2026 Diligent Graphics LLC
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

#include "Render/Tessera/Passes/RadientTesseraGeometryPass.hpp"

#include "Assets/RadientAssetStatus.hpp"
#include "Math/RadientMath.hpp"
#include "Render/RadientPBRRenderer.hpp"
#include "Render/Tessera/RadientTesseraDrawableCache.hpp"
#include "Render/Tessera/RadientTesseraSkinData.hpp"

#include "GraphicsAccessories.hpp"
#include "GLTF_PBR_Renderer.hpp"
#include "GLTFLoader.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace Diligent
{

namespace
{

TEXTURE_FORMAT GetTextureViewFormat(ITextureView* pView)
{
    if (pView == nullptr)
        return TEX_FORMAT_UNKNOWN;

    const TextureViewDesc& ViewDesc = pView->GetDesc();
    if (ViewDesc.Format != TEX_FORMAT_UNKNOWN)
        return ViewDesc.Format;

    ITexture* pTexture = pView->GetTexture();
    return pTexture != nullptr ? pTexture->GetDesc().Format : TEX_FORMAT_UNKNOWN;
}

bool RequiresOutputSRGBConversion(TEXTURE_FORMAT Format)
{
    return Format == TEX_FORMAT_RGBA8_UNORM ||
        Format == TEX_FORMAT_BGRA8_UNORM;
}

RADIENT_STATUS GetPipelineStatus(IPipelineState* pPSO)
{
    if (pPSO == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    switch (pPSO->GetStatus())
    {
        case PIPELINE_STATE_STATUS_READY:
            return RADIENT_STATUS_OK;

        case PIPELINE_STATE_STATUS_UNINITIALIZED:
        case PIPELINE_STATE_STATUS_COMPILING:
            return RADIENT_STATUS_PENDING;

        case PIPELINE_STATE_STATUS_FAILED:
            return RADIENT_STATUS_FAILED;

        default:
            UNEXPECTED("Unexpected pipeline state status");
            return RADIENT_STATUS_INVALID_OPERATION;
    }
}

PBR_Renderer::ALPHA_MODE ToPBRAlphaMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode)
{
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(RADIENT_MATERIAL_SURFACE_MODE_OPAQUE) == PBR_Renderer::ALPHA_MODE_OPAQUE, "Radient opaque surface mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(RADIENT_MATERIAL_SURFACE_MODE_MASKED) == PBR_Renderer::ALPHA_MODE_MASK, "Radient masked surface mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(RADIENT_MATERIAL_SURFACE_MODE_TRANSPARENT) == PBR_Renderer::ALPHA_MODE_BLEND, "Radient transparent surface mode must match PBR alpha mode");
    return static_cast<PBR_Renderer::ALPHA_MODE>(SurfaceMode);
}

void BindVertexPool(IVertexPool&    VertexPool,
                    IDeviceContext* pContext)
{
    const VertexPoolDesc& PoolDesc = VertexPool.GetDesc();

    std::array<IBuffer*, GLTF::ModelCreateInfo::MaxBuffers> pVBs; // Do not zero-initialize
    VERIFY(PoolDesc.NumElements <= pVBs.size(), "Too many vertex buffers in Radient vertex pool");
    for (Uint32 BufferIndex = 0; BufferIndex < PoolDesc.NumElements; ++BufferIndex)
        pVBs[BufferIndex] = VertexPool.GetBuffer(BufferIndex);

    pContext->SetVertexBuffers(0, PoolDesc.NumElements, pVBs.data(), nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
}

} // namespace

RadientTesseraGeometryPass::RadientTesseraGeometryPass(bool EnableAsyncPipelineCompilation) noexcept :
    m_EnableAsyncPipelineCompilation{EnableAsyncPipelineCompilation}
{
}

RADIENT_STATUS RadientTesseraGeometryPass::Prepare(RadientTesseraGeometryRenderer&    Renderer,
                                                   IRenderDevice*                     pDevice,
                                                   IDeviceContext*                    pContext,
                                                   const RadientTesseraDrawableCache& DrawableCache,
                                                   const RadientFrameRenderTargets&   Targets,
                                                   PBR_Renderer::DebugViewType        DebugView,
                                                   bool                               EnableIBL)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    PBR_Renderer* pRenderer = Renderer.GetRenderer();
    if (pRenderer == nullptr)
    {
        const RADIENT_STATUS PrepareStatus = Renderer.Prepare(pDevice, pContext);
        if (RADIENT_FAILED(PrepareStatus))
            return PrepareStatus;

        pRenderer = Renderer.GetRenderer();
    }
    if (pRenderer == nullptr)
        return RADIENT_STATUS_OK;

    if (m_NativeMultiDrawSupported == DEVICE_FEATURE_STATE_OPTIONAL)
        m_NativeMultiDrawSupported = pDevice->GetDeviceInfo().Features.NativeMultiDraw;

    bool RebuildDrawablePassData = false;

    if (m_DebugView != DebugView)
    {
        m_DebugView             = DebugView;
        RebuildDrawablePassData = true;
    }

    if (m_EnableIBL != EnableIBL)
    {
        m_EnableIBL             = EnableIBL;
        RebuildDrawablePassData = true;
    }

    std::array<TEXTURE_FORMAT, RadientFrameRenderTargets::GBUFFER_TARGET_COUNT> RTVFormats{};
    for (Uint32 TargetIndex = 0; TargetIndex < RadientFrameRenderTargets::GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        ITextureView* const pRTV = Targets.GetGBufferRTV(static_cast<RadientFrameRenderTargets::GBufferTarget>(TargetIndex));
        if (pRTV == nullptr)
            return RADIENT_STATUS_OK;

        RTVFormats[TargetIndex] = GetTextureViewFormat(pRTV);
    }

    const TEXTURE_FORMAT DSVFormat = GetTextureViewFormat(Targets.GetDepthDSV());
    if (m_RTVFormats != RTVFormats ||
        m_DSVFormat != DSVFormat ||
        m_UseReverseDepth != Targets.GetUseReverseDepth())
    {
        const RADIENT_STATUS Status = CreatePsoCaches(*pRenderer,
                                                      Renderer.GetBaseRenderFlags(),
                                                      RTVFormats,
                                                      DSVFormat,
                                                      Targets.GetUseReverseDepth());
        if (RADIENT_FAILED(Status))
            return Status;

        RebuildDrawablePassData = true;
    }

    SyncDrawablePassData(Renderer, DrawableCache, RebuildDrawablePassData);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraGeometryPass::Execute(RadientTesseraGeometryRenderer&    Renderer,
                                                   IRenderDevice*                     pDevice,
                                                   IDeviceContext*                    pContext,
                                                   IShaderResourceBinding*            pFrameSRB,
                                                   PBR_Renderer::ALPHA_MODE           AlphaMode,
                                                   const RadientTesseraDrawableCache& DrawableCache,
                                                   const RadientFrameRenderTargets&   Targets,
                                                   RadientTesseraFrameHistory&        FrameHistory)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (AlphaMode >= PBR_Renderer::ALPHA_MODE_NUM_MODES)
    {
        UNEXPECTED("Invalid material alpha mode");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    PBR_Renderer* const pRenderer = Renderer.GetRenderer();
    if (pRenderer == nullptr)
        return RADIENT_STATUS_OK;

    if (!m_PbrPSOCache)
    {
        const RADIENT_STATUS PrepareStatus = Prepare(Renderer,
                                                     pDevice,
                                                     pContext,
                                                     DrawableCache,
                                                     Targets,
                                                     m_DebugView,
                                                     m_EnableIBL);
        if (RADIENT_FAILED(PrepareStatus))
            return PrepareStatus;
    }
    if (!m_PbrPSOCache)
        return RADIENT_STATUS_OK;

    std::array<ITextureView*, RadientFrameRenderTargets::GBUFFER_TARGET_COUNT> GBufferRTVs{};
    for (Uint32 TargetIndex = 0; TargetIndex < RadientFrameRenderTargets::GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        GBufferRTVs[TargetIndex] = Targets.GetGBufferRTV(static_cast<RadientFrameRenderTargets::GBufferTarget>(TargetIndex));
        if (GBufferRTVs[TargetIndex] == nullptr)
            return RADIENT_STATUS_OUT_OF_DATE;
    }

    pContext->SetRenderTargets(RadientFrameRenderTargets::GBUFFER_TARGET_COUNT,
                               GBufferRTVs.data(),
                               Targets.GetDepthDSV(),
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const OrderedDrawableBatchMap& DrawableBatches = m_DrawableBatches[AlphaMode];
    if (DrawableBatches.empty())
        return RADIENT_STATUS_OK;

    VERIFY(pFrameSRB != nullptr, "Radient frame SRB is not initialized");
    if (pFrameSRB == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    IBuffer* const pPrimitiveAttribsCB = pRenderer->GetPBRPrimitiveAttribsCB();
    if (pPrimitiveAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const BufferDesc& PrimitiveAttribsBufferDesc = pPrimitiveAttribsCB->GetDesc();
    if (PrimitiveAttribsBufferDesc.Usage != USAGE_DYNAMIC)
    {
        UNEXPECTED("Radient primitive attributes buffer must use USAGE_DYNAMIC");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    const Uint32 PrimitiveArraySize      = std::max(pRenderer->GetSettings().PrimitiveArraySize, 1u);
    const Uint32 PrimitiveAttribsMaxSize = pRenderer->GetPBRPrimitiveAttribsSize(PBR_Renderer::PSO_FLAG_ALL);
    const Uint64 PrimitiveAttribsRange   = Uint64{PrimitiveAttribsMaxSize} * PrimitiveArraySize;
    if (PrimitiveAttribsMaxSize == 0 || PrimitiveAttribsBufferDesc.Size < PrimitiveAttribsRange)
    {
        UNEXPECTED("Radient primitive attributes buffer is too small");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    const Uint32 PrimitiveAttribsBufferSize    = static_cast<Uint32>(PrimitiveAttribsBufferDesc.Size);
    const Uint32 ConstantBufferOffsetAlignment = pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment;

    m_PendingDraws.clear();
    m_MultiDrawItems.clear();
    m_MultiDrawIndexedItems.clear();

    void*  pMappedPrimitiveData = nullptr;
    Uint32 AttribsBufferOffset  = 0;

    DrawState      State;
    RADIENT_STATUS Result = RADIENT_STATUS_OK;

    auto RenderPendingDraws = [&]() -> RADIENT_STATUS {
        if (pMappedPrimitiveData != nullptr)
        {
            pContext->UnmapBuffer(pPrimitiveAttribsCB, MAP_WRITE);
            pMappedPrimitiveData = nullptr;
        }

        const RADIENT_STATUS Status = this->RenderPendingDraws(pContext, pFrameSRB, State);
        m_PendingDraws.clear();
        m_MultiDrawItems.clear();
        m_MultiDrawIndexedItems.clear();
        AttribsBufferOffset = 0;
        return Status;
    };

    for (const auto& BatchEntry : DrawableBatches)
    {
        const DrawableBatch& Batch = BatchEntry.second;
        VERIFY(Batch.pPSO != nullptr && Batch.pVertexPool != nullptr,
               "Drawable batch contains invalid shared render state");
        if (Batch.pPSO == nullptr || Batch.pVertexPool == nullptr)
            continue;

        // Pipeline and SRB readiness are shared by the whole batch, so each
        // potentially expensive query is made once instead of once per drawable.
        const RADIENT_STATUS PipelineStatus = GetPipelineStatus(Batch.pPSO);

        Result = CombineDependencyStatus(Result, PipelineStatus);
        if (PipelineStatus != RADIENT_STATUS_OK)
            continue;

        if (Batch.MaterialSRB.GetSRB() == nullptr)
            continue;

        Uint32 MultiDrawCount = 0;
        for (const DrawableBatchItem& Drawable : Batch.Drawables)
        {
            const bool UsesSkinning = (Batch.PSOFlags & PBR_Renderer::PSO_FLAG_USE_JOINTS) != 0;
            if (Drawable.pWorldMatrix == nullptr ||
                Drawable.pEffectiveVisible == nullptr ||
                !*Drawable.pEffectiveVisible ||
                Drawable.ElementCount == 0 ||
                (UsesSkinning && (Drawable.pSkinData == nullptr || !Drawable.pSkinData->IsPrepared())))
            {
                continue;
            }

            if (MultiDrawCount == PrimitiveArraySize)
                MultiDrawCount = 0;

            if (MultiDrawCount == 0)
            {
                AttribsBufferOffset = AlignUp(AttribsBufferOffset, ConstantBufferOffsetAlignment);
                if (Uint64{AttribsBufferOffset} + PrimitiveAttribsRange > PrimitiveAttribsBufferSize)
                    Result = CombineDependencyStatus(Result, RenderPendingDraws());
            }

            const Uint32 PrimitiveAttribsOffset = AttribsBufferOffset;

            if (pMappedPrimitiveData == nullptr)
            {
                pContext->MapBuffer(pPrimitiveAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD, pMappedPrimitiveData);
                if (pMappedPrimitiveData == nullptr)
                {
                    UNEXPECTED("Unable to map PBR primitive attributes buffer");
                    return RADIENT_STATUS_FAILED;
                }
            }

            void* const            pPrimitiveAttribs = static_cast<Uint8*>(pMappedPrimitiveData) + PrimitiveAttribsOffset;
            const RadientMatrix4x4 PreviousTransform = FrameHistory.UpdateDrawableTransform(
                Drawable.DrawableID,
                Drawable.Generation,
                *Drawable.pWorldMatrix);
            const float4x4& NodeTransform     = reinterpret_cast<const float4x4&>(*Drawable.pWorldMatrix);
            const float4x4& PrevNodeTransform = reinterpret_cast<const float4x4&>(PreviousTransform);
            const float4    CustomData{};

            GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData AttribsData;
            AttribsData.PSOFlags       = Batch.PSOFlags;
            AttribsData.NodeMatrix     = &NodeTransform;
            AttribsData.PrevNodeMatrix = &PrevNodeTransform;
            AttribsData.CustomData     = &CustomData;
            AttribsData.CustomDataSize = sizeof(CustomData);
            if (UsesSkinning)
            {
                AttribsData.JointCount     = Drawable.pSkinData->GetJointCount();
                AttribsData.FirstJoint     = Drawable.pSkinData->GetFirstJoint();
                AttribsData.PrevFirstJoint = Drawable.pSkinData->GetPreviousFirstJoint();
            }

            void* const pPrimitiveAttribsEnd =
                GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(
                    pPrimitiveAttribs,
                    AttribsData,
                    !pRenderer->GetSettings().PackMatrixRowMajor,
                    pRenderer->GetSettings().UseSkinPreTransform,
                    pRenderer->GetSettings().VertexPosPackMode);
            const Uint32 PrimitiveAttribsSize =
                static_cast<Uint32>(static_cast<Uint8*>(pPrimitiveAttribsEnd) - static_cast<Uint8*>(pPrimitiveAttribs));
            if (PrimitiveAttribsSize != Batch.PrimitiveAttribsSize || PrimitiveAttribsSize > PrimitiveAttribsMaxSize)
            {
                pContext->UnmapBuffer(pPrimitiveAttribsCB, MAP_WRITE);
                pMappedPrimitiveData = nullptr;
                UNEXPECTED("PBR primitive attributes size is inconsistent with the bound buffer range");
                return RADIENT_STATUS_INVALID_OPERATION;
            }

            Uint32 FirstDrawItem = 0;
            if (Batch.IsIndexed)
            {
                FirstDrawItem = static_cast<Uint32>(m_MultiDrawIndexedItems.size());
                m_MultiDrawIndexedItems.push_back({
                    Drawable.ElementCount,
                    Drawable.FirstLocation,
                    Drawable.BaseVertex,
                });
            }
            else
            {
                FirstDrawItem = static_cast<Uint32>(m_MultiDrawItems.size());
                m_MultiDrawItems.push_back({
                    Drawable.ElementCount,
                    Drawable.FirstLocation,
                });
            }

            if (MultiDrawCount == 0)
            {
                m_PendingDraws.push_back({&Batch, FirstDrawItem, PrimitiveAttribsOffset, 1});
            }
            else
            {
                PendingDraw& Pending = m_PendingDraws.back();
                VERIFY(Pending.pBatch == &Batch && Pending.FirstDrawItem + Pending.DrawCount == FirstDrawItem,
                       "Pending multi-draw batch is not contiguous");
                ++Pending.DrawCount;
            }

            AttribsBufferOffset = PrimitiveAttribsOffset + Batch.PrimitiveAttribsSize;
            ++MultiDrawCount;
        }
    }

    const RADIENT_STATUS RenderStatus = RenderPendingDraws();
    return CombineDependencyStatus(Result, RenderStatus);
}

RADIENT_STATUS RadientTesseraGeometryPass::RenderPendingDraws(IDeviceContext*         pContext,
                                                              IShaderResourceBinding* pFrameSRB,
                                                              DrawState&              State)
{
    VERIFY(m_NativeMultiDrawSupported != DEVICE_FEATURE_STATE_OPTIONAL,
           "Native multi-draw support has not been initialized");
    const bool NativeMultiDrawSupported = m_NativeMultiDrawSupported == DEVICE_FEATURE_STATE_ENABLED;

    for (const PendingDraw& Pending : m_PendingDraws)
    {
        VERIFY(Pending.pBatch != nullptr, "Pending draw references a null batch");
        if (Pending.pBatch == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        const DrawableBatch& Batch = *Pending.pBatch;

        if (State.pVertexPool != Batch.pVertexPool)
        {
            State.pVertexPool = Batch.pVertexPool;
            VERIFY(State.pVertexPool != nullptr, "Pending batch references null vertex pool");
            if (State.pVertexPool != nullptr)
                BindVertexPool(*State.pVertexPool, pContext);
        }

        if (State.pPSO != Batch.pPSO)
        {
            State.pPSO = Batch.pPSO;

            if (State.pPSO != nullptr)
                pContext->SetPipelineState(State.pPSO);
        }

        if (!State.FrameSRBCommitted)
        {
            pContext->CommitShaderResources(pFrameSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            State.FrameSRBCommitted = true;
        }

        bool MaterialSRBChanged = false;
        if (State.MaterialId != Batch.MaterialId)
        {
            State.MaterialId = Batch.MaterialId;

            IShaderResourceBinding* const pMaterialSRB = Batch.MaterialSRB.GetSRB();
            MaterialSRBChanged                         = State.pMaterialSRB != pMaterialSRB;
            if (MaterialSRBChanged)
            {
                State.pMaterialSRB          = pMaterialSRB;
                State.pPrimitiveAttribsVar  = Batch.MaterialSRB.GetPrimitiveAttribsVariable();
                State.pMaterialAttribsVar   = Batch.MaterialSRB.GetMaterialAttribsVariable();
                State.MaterialAttribsOffset = ~Uint32{0};
            }
        }

        if (State.pPrimitiveAttribsVar == nullptr || State.pMaterialAttribsVar == nullptr)
        {
            UNEXPECTED("Material SRB does not contain the PBR attribute buffer variables");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        // One dynamic offset selects the contiguous primitive array consumed by
        // the entire multi-draw batch.
        State.pPrimitiveAttribsVar->SetBufferOffset(Pending.PrimitiveAttribsOffset);
        if (State.MaterialAttribsOffset != Batch.MaterialAttribsOffset)
        {
            State.pMaterialAttribsVar->SetBufferOffset(Batch.MaterialAttribsOffset);
            State.MaterialAttribsOffset = Batch.MaterialAttribsOffset;
        }
        if (MaterialSRBChanged)
            pContext->CommitShaderResources(State.pMaterialSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        VERIFY(Pending.DrawCount > 0, "Pending multi-draw batch is empty");
        if (Batch.IsIndexed)
        {
            VERIFY(Uint64{Pending.FirstDrawItem} + Pending.DrawCount <= m_MultiDrawIndexedItems.size(),
                   "Pending indexed multi-draw batch is invalid");
            const MultiDrawIndexedItem* const pDrawItems = m_MultiDrawIndexedItems.data() + Pending.FirstDrawItem;

            if (Pending.DrawCount > 1 && NativeMultiDrawSupported)
            {
                pContext->MultiDrawIndexed({Pending.DrawCount,
                                            pDrawItems,
                                            VT_UINT32,
                                            DRAW_FLAG_VERIFY_ALL});
            }
            else
            {
                for (Uint32 DrawIndex = 0; DrawIndex < Pending.DrawCount; ++DrawIndex)
                {
                    const MultiDrawIndexedItem& DrawItem = pDrawItems[DrawIndex];
                    DrawIndexedAttribs          DrawAttrs{DrawItem.NumIndices, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
                    if (DrawIndex > 0)
                        DrawAttrs.Flags |= DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT;
                    DrawAttrs.FirstIndexLocation    = DrawItem.FirstIndexLocation;
                    DrawAttrs.BaseVertex            = DrawItem.BaseVertex;
                    DrawAttrs.FirstInstanceLocation = DrawIndex;
                    pContext->DrawIndexed(DrawAttrs);
                }
            }
        }
        else
        {
            VERIFY(Uint64{Pending.FirstDrawItem} + Pending.DrawCount <= m_MultiDrawItems.size(),
                   "Pending multi-draw batch is invalid");
            const MultiDrawItem* const pDrawItems = m_MultiDrawItems.data() + Pending.FirstDrawItem;

            if (Pending.DrawCount > 1 && NativeMultiDrawSupported)
            {
                pContext->MultiDraw({Pending.DrawCount,
                                     pDrawItems,
                                     DRAW_FLAG_VERIFY_ALL});
            }
            else
            {
                for (Uint32 DrawIndex = 0; DrawIndex < Pending.DrawCount; ++DrawIndex)
                {
                    const MultiDrawItem& DrawItem = pDrawItems[DrawIndex];
                    DrawAttribs          DrawAttrs{DrawItem.NumVertices, DRAW_FLAG_VERIFY_ALL};
                    if (DrawIndex > 0)
                        DrawAttrs.Flags |= DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT;
                    DrawAttrs.StartVertexLocation   = DrawItem.StartVertexLocation;
                    DrawAttrs.FirstInstanceLocation = DrawIndex;
                    pContext->Draw(DrawAttrs);
                }
            }
        }
    }

    return RADIENT_STATUS_OK;
}

bool RadientTesseraGeometryPass::DrawableBatchKeyLess::operator()(const DrawableBatchKey& Lhs,
                                                                  const DrawableBatchKey& Rhs) const noexcept
{
    if (Lhs.PSOId != Rhs.PSOId)
        return Lhs.PSOId < Rhs.PSOId;

    if (Lhs.MaterialId != Rhs.MaterialId)
        return Lhs.MaterialId < Rhs.MaterialId;

    if (Lhs.VertexPoolId != Rhs.VertexPoolId)
        return Lhs.VertexPoolId < Rhs.VertexPoolId;

    return Lhs.IsIndexed < Rhs.IsIndexed;
}

void RadientTesseraGeometryPass::SyncDrawablePassData(RadientTesseraGeometryRenderer&    Renderer,
                                                      const RadientTesseraDrawableCache& DrawableCache,
                                                      bool                               RebuildAll)
{
    if (!m_PbrPSOCache)
        return;

    if (RebuildAll)
    {
        for (OrderedDrawableBatchMap& Batches : m_DrawableBatches)
            Batches.clear();
        m_DrawablePassData.clear();

        const std::array<PBR_Renderer::ALPHA_MODE, 3> AlphaModes =
            {
                PBR_Renderer::ALPHA_MODE_OPAQUE,
                PBR_Renderer::ALPHA_MODE_MASK,
                PBR_Renderer::ALPHA_MODE_BLEND,
            };

        for (const PBR_Renderer::ALPHA_MODE AlphaMode : AlphaModes)
        {
            for (const RadientDrawItem& DrawItem : DrawableCache.GetDrawList(AlphaMode).GetItems())
            {
                const RadientDrawableSlot* pDrawable = DrawableCache.GetDrawableSlot(DrawItem.DrawableID);
                if (pDrawable != nullptr)
                    UpdateDrawablePassData(Renderer, *pDrawable, DrawItem.DrawableID);
            }
        }
        return;
    }

    for (const RadientDrawableChange& Change : DrawableCache.GetDrawableChanges())
    {
        if (Change.Type == RadientDrawableChangeType::Removed)
        {
            InvalidateDrawablePassData(Change.DrawableID);
            continue;
        }

        if (const RadientDrawableSlot* pDrawable = DrawableCache.GetDrawableSlot(Change.DrawableID))
            UpdateDrawablePassData(Renderer, *pDrawable, Change.DrawableID);
        else
            InvalidateDrawablePassData(Change.DrawableID);
    }
}

void RadientTesseraGeometryPass::UpdateDrawablePassData(RadientTesseraGeometryRenderer& Renderer,
                                                        const RadientDrawableSlot&      Drawable,
                                                        RadientDrawableID               DrawableID)
{
    if (DrawableID == InvalidRadientDrawableID)
        return;

    if (DrawableID >= m_DrawablePassData.size())
        m_DrawablePassData.resize(static_cast<size_t>(DrawableID) + 1);

    InvalidateDrawablePassData(DrawableID);

    DrawablePassData&   PassData  = m_DrawablePassData[DrawableID];
    PBR_Renderer* const pRenderer = Renderer.GetRenderer();
    if (pRenderer == nullptr || !Drawable.MaterialData)
    {
        PassData = {};
        return;
    }

    const RadientTesseraMaterialData&   TesseraMaterialData = *Drawable.MaterialData;
    const RADIENT_MATERIAL_SURFACE_MODE SurfaceMode         = TesseraMaterialData.GetSurfaceMode();
    if (SurfaceMode >= RADIENT_MATERIAL_SURFACE_MODE_COUNT)
    {
        UNEXPECTED("Material has an invalid surface mode");
        return;
    }
    const PBR_Renderer::ALPHA_MODE AlphaMode = ToPBRAlphaMode(SurfaceMode);

    PBR_Renderer::PSO_FLAGS PSOFlags  = Drawable.VertexAttribFlags | TesseraMaterialData.GetMaterialPSOFlags();
    RadientTesseraSkinData* pSkinData = nullptr;
    if ((PSOFlags & PBR_Renderer::PSO_FLAG_USE_JOINTS) != 0)
    {
        pSkinData = Drawable.pSkinData;
        if (pSkinData == nullptr)
        {
            // A mesh containing joint attributes may also be instantiated by
            // an unskinned node. Render that instance in its authored pose.
            PSOFlags &= ~PBR_Renderer::PSO_FLAG_USE_JOINTS;
        }
        else if (!pSkinData->IsPrepared())
        {
            return;
        }
    }
    else if (Drawable.pSkinData != nullptr)
    {
        UNEXPECTED("A skinned drawable does not contain joint and weight vertex attributes");
    }
    PSOFlags |=
        PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS |
        PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
        PBR_Renderer::PSO_FLAG_COMPUTE_MOTION_VECTORS |
        PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB |
        PBR_Renderer::PSO_FLAG_USE_LIGHTS;
    if (m_EnableIBL)
        PSOFlags |= PBR_Renderer::PSO_FLAG_USE_IBL;
    PSOFlags &= m_RenderFlags;
    PSOFlags &= PBR_Renderer::GetEnabledPSOFlags(pRenderer->GetSettings());

    const RadientMaterialSRBLease&        MaterialSRB = TesseraMaterialData.GetMaterialSRB();
    const RadientTesseraBufferAllocation& MaterialBufferAllocation =
        TesseraMaterialData.GetMaterialBufferAllocation();
    if (!MaterialSRB || !MaterialBufferAllocation)
    {
        PassData = {};
        return;
    }
    const PBR_Renderer::StaticShaderTextureIdsArrayType& ShaderTextureIds =
        TesseraMaterialData.GetShaderTextureIds();

    const PBR_Renderer::PSOKey PsoKey{
        PBR_Renderer::RenderPassType::Main,
        PSOFlags,
        AlphaMode,
        TesseraMaterialData.IsDoubleSided() ? CULL_MODE_NONE : CULL_MODE_BACK,
        m_DebugView,
        PBR_Renderer::LoadingAnimationMode::None,
        0,
        &ShaderTextureIds,
    };

    PBR_Renderer::PsoCacheAccessor::GET_FLAGS GetFlags =
        PBR_Renderer::PsoCacheAccessor::GET_FLAG_CREATE_IF_NULL;
    if (m_EnableAsyncPipelineCompilation)
    {
        GetFlags |= PBR_Renderer::PsoCacheAccessor::GET_FLAG_ASYNC_COMPILE;
    }

    IPipelineState* const pPSO = m_PbrPSOCache.Get(PsoKey, GetFlags);
    if (pPSO == nullptr || Drawable.pVertexPool == nullptr)
    {
        UNEXPECTED("Unable to create drawable pass ordering data");
        return;
    }

    const DrawableBatchKey BatchKey = {
        pPSO->GetUniqueID(),
        TesseraMaterialData.GetUniqueID(),
        Drawable.pVertexPool->GetUniqueID(),
        Drawable.IsIndexed,
    };

    OrderedDrawableBatchMap& Batches      = m_DrawableBatches[AlphaMode];
    auto                     InsertResult = Batches.emplace(BatchKey, DrawableBatch{});
    DrawableBatch&           Batch        = InsertResult.first->second;
    if (InsertResult.second)
    {
        Batch.pPSO                  = pPSO;
        Batch.PSOFlags              = PSOFlags;
        Batch.PrimitiveAttribsSize  = pRenderer->GetPBRPrimitiveAttribsSize(PSOFlags);
        Batch.MaterialId            = BatchKey.MaterialId;
        Batch.pVertexPool           = Drawable.pVertexPool;
        Batch.MaterialAttribsOffset = MaterialBufferAllocation.GetOffset();
        Batch.IsIndexed             = Drawable.IsIndexed;
        Batch.MaterialSRB           = MaterialSRB;
    }
    else
    {
        VERIFY(Batch.pPSO == pPSO &&
                   Batch.PSOFlags == PSOFlags &&
                   Batch.MaterialId == BatchKey.MaterialId &&
                   Batch.pVertexPool == Drawable.pVertexPool &&
                   Batch.MaterialAttribsOffset == MaterialBufferAllocation.GetOffset() &&
                   Batch.IsIndexed == Drawable.IsIndexed,
               "Drawable batch key refers to inconsistent shared render state");
    }

    VERIFY(Batch.Drawables.size() < (std::numeric_limits<Uint32>::max)(),
           "Drawable batch contains too many items");

    PassData.AlphaMode      = AlphaMode;
    PassData.BatchKey       = BatchKey;
    PassData.BatchItemIndex = static_cast<Uint32>(Batch.Drawables.size());

    Batch.Drawables.push_back({
        Drawable.pWorldMatrix,
        Drawable.pEffectiveVisible,
        pSkinData,
        DrawableID,
        Drawable.Generation,
        Drawable.ElementCount,
        Drawable.IsIndexed ?
            Drawable.FirstIndexLocation + Drawable.FirstElement :
            Drawable.BaseVertex + Drawable.FirstElement,
        Drawable.IsIndexed ? Drawable.BaseVertex : 0,
    });
}

void RadientTesseraGeometryPass::InvalidateDrawablePassData(RadientDrawableID DrawableID)
{
    if (DrawableID < m_DrawablePassData.size())
    {
        DrawablePassData& PassData = m_DrawablePassData[DrawableID];
        if (PassData.BatchItemIndex != InvalidBatchItemIndex)
        {
            VERIFY(PassData.AlphaMode < PBR_Renderer::ALPHA_MODE_NUM_MODES, "Drawable has an invalid alpha mode");
            if (PassData.AlphaMode < PBR_Renderer::ALPHA_MODE_NUM_MODES)
            {
                OrderedDrawableBatchMap& Batches = m_DrawableBatches[PassData.AlphaMode];
                const auto               It      = Batches.find(PassData.BatchKey);
                VERIFY(It != Batches.end(), "Drawable batch is missing");
                if (It != Batches.end())
                {
                    std::vector<DrawableBatchItem>& Drawables = It->second.Drawables;
                    VERIFY(PassData.BatchItemIndex < Drawables.size() &&
                               Drawables[PassData.BatchItemIndex].DrawableID == DrawableID,
                           "Drawable batch index is invalid");
                    if (PassData.BatchItemIndex < Drawables.size())
                    {
                        const size_t LastItemIndex = Drawables.size() - 1;
                        if (PassData.BatchItemIndex != LastItemIndex)
                        {
                            Drawables[PassData.BatchItemIndex] = std::move(Drawables.back());

                            const RadientDrawableID MovedDrawableID = Drawables[PassData.BatchItemIndex].DrawableID;
                            VERIFY(MovedDrawableID < m_DrawablePassData.size(), "Moved drawable has an invalid ID");
                            if (MovedDrawableID < m_DrawablePassData.size())
                                m_DrawablePassData[MovedDrawableID].BatchItemIndex = PassData.BatchItemIndex;
                        }
                        Drawables.pop_back();
                    }

                    if (Drawables.empty())
                        Batches.erase(It);
                }
            }
        }
        PassData = {};
    }
}

RADIENT_STATUS RadientTesseraGeometryPass::CreatePsoCaches(PBR_Renderer&                                                                      Renderer,
                                                           PBR_Renderer::PSO_FLAGS                                                            BaseRenderFlags,
                                                           const std::array<TEXTURE_FORMAT, RadientFrameRenderTargets::GBUFFER_TARGET_COUNT>& RTVFormats,
                                                           TEXTURE_FORMAT                                                                     DSVFormat,
                                                           bool                                                                               UseReverseDepth)
{
    for (TEXTURE_FORMAT RTVFormat : RTVFormats)
    {
        if (RTVFormat == TEX_FORMAT_UNKNOWN)
            return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    GraphicsPipelineDesc GraphicsDesc;
    GraphicsDesc.NumRenderTargets = RadientFrameRenderTargets::GBUFFER_TARGET_COUNT;
    for (Uint32 TargetIndex = 0; TargetIndex < RadientFrameRenderTargets::GBUFFER_TARGET_COUNT; ++TargetIndex)
        GraphicsDesc.RTVFormats[TargetIndex] = RTVFormats[TargetIndex];
    GraphicsDesc.DSVFormat                  = DSVFormat;
    GraphicsDesc.DepthStencilDesc.DepthFunc = UseReverseDepth ? COMPARISON_FUNC_GREATER : COMPARISON_FUNC_LESS;

    GraphicsDesc.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsDesc.RasterizerDesc.FrontCounterClockwise = true;

    m_PbrPSOCache = Renderer.GetPsoCacheAccessor(GraphicsDesc);

    GraphicsDesc.RasterizerDesc.FillMode = FILL_MODE_WIREFRAME;
    m_WireframePSOCache                  = Renderer.GetPsoCacheAccessor(GraphicsDesc);

    m_RenderFlags = BaseRenderFlags;
    if (RequiresOutputSRGBConversion(RTVFormats[RadientFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR]))
        m_RenderFlags |= PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB;

    m_RTVFormats      = RTVFormats;
    m_DSVFormat       = DSVFormat;
    m_UseReverseDepth = UseReverseDepth;
    for (OrderedDrawableBatchMap& Batches : m_DrawableBatches)
        Batches.clear();
    m_DrawablePassData.clear();

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
