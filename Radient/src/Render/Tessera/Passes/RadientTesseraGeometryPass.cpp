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

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Math/RadientMath.hpp"
#include "Render/RadientPBRRenderer.hpp"
#include "Render/Tessera/RadientTesseraDrawableCache.hpp"

#include "GraphicsAccessories.hpp"
#include "GLTF_PBR_Renderer.hpp"
#include "GLTFLoader.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"
} // namespace HLSL

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

bool IsPipelineReady(IPipelineState* pPSO)
{
    return pPSO != nullptr && pPSO->GetStatus() == PIPELINE_STATE_STATUS_READY;
}

PBR_Renderer::ALPHA_MODE ToPBRAlphaMode(GLTF::Material::ALPHA_MODE AlphaMode)
{
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(GLTF::Material::ALPHA_MODE_OPAQUE) == PBR_Renderer::ALPHA_MODE_OPAQUE, "GLTF opaque alpha mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(GLTF::Material::ALPHA_MODE_MASK) == PBR_Renderer::ALPHA_MODE_MASK, "GLTF mask alpha mode must match PBR alpha mode");
    static_assert(static_cast<PBR_Renderer::ALPHA_MODE>(GLTF::Material::ALPHA_MODE_BLEND) == PBR_Renderer::ALPHA_MODE_BLEND, "GLTF blend alpha mode must match PBR alpha mode");
    return static_cast<PBR_Renderer::ALPHA_MODE>(AlphaMode);
}

template <typename ShaderStructType, typename HostStructType>
Uint8* WriteShaderAttribs(Uint8* pDstPtr, HostStructType* pSrc, const char* DebugName)
{
    static_assert(sizeof(ShaderStructType) == sizeof(HostStructType), "Size of HLSL and C++ structures must be the same");
    if (pSrc != nullptr)
    {
        std::memcpy(pDstPtr, pSrc, sizeof(ShaderStructType));
    }
    else
    {
        UNEXPECTED("Shader attribute ", DebugName, " is not initialized in the material");
        std::memset(pDstPtr, 0, sizeof(ShaderStructType));
    }
    static_assert(sizeof(ShaderStructType) % 16 == 0, "Size structure must be a multiple of 16");
    return pDstPtr + sizeof(ShaderStructType);
}

void* WritePBRMaterialShaderAttribs(void*                           pDstShaderAttribs,
                                    const PBR_Renderer::CreateInfo& Settings,
                                    PBR_Renderer::PSO_FLAGS         PSOFlags,
                                    const GLTF::Material&           Material)
{
    static_assert(static_cast<PBR_Renderer::PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_METALL_ROUGH) == PBR_Renderer::PBR_WORKFLOW_METALL_ROUGH, "GLTF metallic-roughness workflow must match PBR workflow");
    static_assert(static_cast<PBR_Renderer::PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_SPEC_GLOSS) == PBR_Renderer::PBR_WORKFLOW_SPEC_GLOSS, "GLTF specular-glossiness workflow must match PBR workflow");
    static_assert(static_cast<PBR_Renderer::PBR_WORKFLOW>(GLTF::Material::PBR_WORKFLOW_UNLIT) == PBR_Renderer::PBR_WORKFLOW_UNLIT, "GLTF unlit workflow must match PBR workflow");

    Uint8* pDstPtr = reinterpret_cast<Uint8*>(pDstShaderAttribs);

    pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialBasicAttribs>(pDstPtr, &Material.Attribs, "Basic Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_SHEEN)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialSheenAttribs>(pDstPtr, Material.Sheen.get(), "Sheen Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_ANISOTROPY)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialAnisotropyAttribs>(pDstPtr, Material.Anisotropy.get(), "Anisotropy Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_IRIDESCENCE)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialIridescenceAttribs>(pDstPtr, Material.Iridescence.get(), "Iridescence Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_TRANSMISSION)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialTransmissionAttribs>(pDstPtr, Material.Transmission.get(), "Transmission Attribs");

    if (PSOFlags & PBR_Renderer::PSO_FLAG_ENABLE_VOLUME)
        pDstPtr = WriteShaderAttribs<HLSL::PBRMaterialVolumeAttribs>(pDstPtr, Material.Volume.get(), "Volume Attribs");

    HLSL::PBRMaterialTextureAttribs* pDstTextures      = reinterpret_cast<HLSL::PBRMaterialTextureAttribs*>(pDstPtr);
    Uint32                           NumTextureAttribs = 0;
    PBR_Renderer::ProcessTexturAttribs(PSOFlags, [&](int CurrIndex, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
        const int SrcAttribIndex = Settings.TextureAttribIndices[AttribId];
        if (SrcAttribIndex < 0)
        {
            UNEXPECTED("Shader texture attribute ", Uint32{AttribId}, " is not initialized");
            return;
        }

        static_assert(sizeof(HLSL::PBRMaterialTextureAttribs) == sizeof(GLTF::Material::TextureShaderAttribs),
                      "The sizeof(HLSL::PBRMaterialTextureAttribs) is inconsistent with sizeof(GLTF::Material::TextureShaderAttribs)");
        std::memcpy(pDstTextures + CurrIndex, &Material.GetTextureAttrib(SrcAttribIndex), sizeof(HLSL::PBRMaterialTextureAttribs));
        ++NumTextureAttribs;
    });

    return pDstTextures + NumTextureAttribs;
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

void WriteMaterialAttribs(PBR_Renderer&           Renderer,
                          IDeviceContext*         pContext,
                          PBR_Renderer::PSO_FLAGS PSOFlags,
                          const GLTF::Material&   Material)
{
    void* pAttribsData = nullptr;
    pContext->MapBuffer(Renderer.GetPBRMaterialAttribsCB(), MAP_WRITE, MAP_FLAG_DISCARD, pAttribsData);
    if (pAttribsData == nullptr)
    {
        UNEXPECTED("Unable to map PBR material attribs buffer");
        return;
    }

    void* pEndPtr = WritePBRMaterialShaderAttribs(pAttribsData,
                                                  Renderer.GetSettings(),
                                                  PSOFlags,
                                                  Material);

    VERIFY(reinterpret_cast<uint8_t*>(pEndPtr) <= static_cast<uint8_t*>(pAttribsData) + Renderer.GetPBRMaterialAttribsCB()->GetDesc().Size,
           "Not enough space in the buffer to store material attributes");

    pContext->UnmapBuffer(Renderer.GetPBRMaterialAttribsCB(), MAP_WRITE);
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
                                                   const RadientFrameRenderTargets&   Targets)
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

    bool          RebuildDrawablePassData = false;
    ITextureView* pColorRTV               = Targets.GetColorRTV();
    if (pColorRTV == nullptr)
        return RADIENT_STATUS_OK;

    const TEXTURE_FORMAT RTVFormat = GetTextureViewFormat(pColorRTV);
    const TEXTURE_FORMAT DSVFormat = GetTextureViewFormat(Targets.GetDepthDSV());
    if (m_RTVFormat != RTVFormat ||
        m_DSVFormat != DSVFormat)
    {
        const RADIENT_STATUS Status = CreatePsoCaches(*pRenderer, Renderer.GetBaseRenderFlags(), RTVFormat, DSVFormat);
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
                                                   GLTF::Material::ALPHA_MODE         AlphaMode,
                                                   const RadientTesseraDrawableCache& DrawableCache,
                                                   const RadientFrameRenderTargets&   Targets)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    if (AlphaMode >= GLTF::Material::ALPHA_MODE_NUM_MODES)
    {
        UNEXPECTED("Invalid material alpha mode");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    PBR_Renderer* const pRenderer = Renderer.GetRenderer();
    if (pRenderer == nullptr)
        return RADIENT_STATUS_OK;

    if (!m_PbrPSOCache)
    {
        const RADIENT_STATUS PrepareStatus = Prepare(Renderer, pDevice, pContext, DrawableCache, Targets);
        if (RADIENT_FAILED(PrepareStatus))
            return PrepareStatus;
    }
    if (!m_PbrPSOCache)
        return RADIENT_STATUS_OK;

    ITextureView* pColorRTV = Targets.GetColorRTV();
    ITextureView* pDepthDSV = Targets.GetDepthDSV();
    pContext->SetRenderTargets(1, &pColorRTV, pDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const OrderedDrawableSet& OrderedDrawables = m_OrderedDrawables[AlphaMode];
    if (OrderedDrawables.empty())
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

    void*  pMappedPrimitiveData = nullptr;
    Uint32 AttribsBufferOffset  = 0;
    Uint32 MultiDrawCount       = 0;

    DrawState State;

    UniqueIdentifier CurrentPSOId      = 0;
    UniqueIdentifier CurrentMaterialId = 0;
    bool             IsCurrentPSOReady = false;
    bool             IsCurrentSRBReady = false;

    auto RenderPendingDraws = [&]() -> RADIENT_STATUS {
        if (pMappedPrimitiveData != nullptr)
        {
            pContext->UnmapBuffer(pPrimitiveAttribsCB, MAP_WRITE);
            pMappedPrimitiveData = nullptr;
        }

        const RADIENT_STATUS Status = this->RenderPendingDraws(*pRenderer, pContext, pFrameSRB, State);
        m_PendingDraws.clear();
        AttribsBufferOffset = 0;
        MultiDrawCount      = 0;
        return Status;
    };

    for (const DrawableSortKey& SortKey : OrderedDrawables)
    {
        const RadientDrawableID DrawableID = SortKey.DrawableID;
        VERIFY(DrawableID < m_DrawablePassData.size(), "Ordered drawable references invalid pass data");
        const DrawablePassData& PassData = m_DrawablePassData[DrawableID];
        VERIFY(PassData.pDrawable != nullptr &&
                   PassData.Generation == PassData.pDrawable->Generation &&
                   PassData.SortKey.DrawableID == DrawableID,
               "Ordered drawable references stale pass data");

        const RadientDrawableSlot& Drawable = *PassData.pDrawable;
        if (Drawable.pMaterial == nullptr ||
            Drawable.pVertexPool == nullptr ||
            Drawable.pWorldMatrix == nullptr ||
            Drawable.pEffectiveVisible == nullptr ||
            !*Drawable.pEffectiveVisible ||
            Drawable.ElementCount == 0)
        {
            continue;
        }

        if (CurrentPSOId != SortKey.PSOId)
        {
            CurrentPSOId      = SortKey.PSOId;
            IsCurrentPSOReady = IsPipelineReady(PassData.pPSO);
        }
        if (!IsCurrentPSOReady)
            continue;

        if (CurrentMaterialId != SortKey.MaterialId)
        {
            CurrentMaterialId = SortKey.MaterialId;
            IsCurrentSRBReady = PassData.MaterialSRB.GetSRB() != nullptr;
        }
        if (!IsCurrentSRBReady)
            continue;

        if (MultiDrawCount == PrimitiveArraySize)
            MultiDrawCount = 0;

        if (MultiDrawCount > 0)
        {
            PendingDraw&           FirstBatchDraw = m_PendingDraws[m_PendingDraws.size() - MultiDrawCount];
            const DrawableSortKey& FirstBatchKey  = m_DrawablePassData[FirstBatchDraw.DrawableID].SortKey;
            if (FirstBatchKey.PSOId == SortKey.PSOId &&
                FirstBatchKey.MaterialId == SortKey.MaterialId &&
                FirstBatchKey.VertexPoolId == SortKey.VertexPoolId &&
                FirstBatchKey.IsIndexed == SortKey.IsIndexed)
            {
                ++FirstBatchDraw.DrawCount;
            }
            else
            {
                MultiDrawCount = 0;
            }
        }

        if (MultiDrawCount == 0)
        {
            AttribsBufferOffset = AlignUp(AttribsBufferOffset, ConstantBufferOffsetAlignment);
            if (Uint64{AttribsBufferOffset} + PrimitiveAttribsRange > PrimitiveAttribsBufferSize)
            {
                const RADIENT_STATUS Status = RenderPendingDraws();
                if (RADIENT_FAILED(Status))
                    return Status;
            }
        }

        const Uint32 PrimitiveAttribsOffset = AttribsBufferOffset;

        if (pMappedPrimitiveData == nullptr)
        {
            pContext->MapBuffer(pPrimitiveAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD, pMappedPrimitiveData);
            if (pMappedPrimitiveData == nullptr)
            {
                UNEXPECTED("Unable to map PBR primitive attributes buffer");
                return RADIENT_STATUS_INVALID_OPERATION;
            }
        }

        void* const     pPrimitiveAttribs = static_cast<Uint8*>(pMappedPrimitiveData) + PrimitiveAttribsOffset;
        const float4x4& NodeTransform     = reinterpret_cast<const float4x4&>(*PassData.pDrawable->pWorldMatrix);
        const float4    CustomData{};

        GLTF_PBR_Renderer::PBRPrimitiveShaderAttribsData AttribsData;
        AttribsData.PSOFlags       = PassData.PSOFlags;
        AttribsData.NodeMatrix     = &NodeTransform;
        AttribsData.PrevNodeMatrix = &NodeTransform;
        AttribsData.CustomData     = &CustomData;
        AttribsData.CustomDataSize = sizeof(CustomData);

        void* const pPrimitiveAttribsEnd =
            GLTF_PBR_Renderer::WritePBRPrimitiveShaderAttribs(
                pPrimitiveAttribs,
                AttribsData,
                !pRenderer->GetSettings().PackMatrixRowMajor,
                pRenderer->GetSettings().UseSkinPreTransform);
        const Uint32 PrimitiveAttribsSize =
            static_cast<Uint32>(static_cast<Uint8*>(pPrimitiveAttribsEnd) - static_cast<Uint8*>(pPrimitiveAttribs));
        if (PrimitiveAttribsSize != PassData.PrimitiveAttribsSize || PrimitiveAttribsSize > PrimitiveAttribsMaxSize)
        {
            pContext->UnmapBuffer(pPrimitiveAttribsCB, MAP_WRITE);
            pMappedPrimitiveData = nullptr;
            UNEXPECTED("PBR primitive attributes size is inconsistent with the bound buffer range");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        m_PendingDraws.push_back({DrawableID, PrimitiveAttribsOffset, 1});
        AttribsBufferOffset = PrimitiveAttribsOffset + PassData.PrimitiveAttribsSize;
        ++MultiDrawCount;
    }

    return RenderPendingDraws();
}

RADIENT_STATUS RadientTesseraGeometryPass::RenderPendingDraws(PBR_Renderer&           Renderer,
                                                              IDeviceContext*         pContext,
                                                              IShaderResourceBinding* pFrameSRB,
                                                              DrawState&              State)
{
    VERIFY(m_NativeMultiDrawSupported != DEVICE_FEATURE_STATE_OPTIONAL,
           "Native multi-draw support has not been initialized");
    const bool NativeMultiDrawSupported = m_NativeMultiDrawSupported == DEVICE_FEATURE_STATE_ENABLED;

    size_t PendingDrawIndex = 0;
    while (PendingDrawIndex < m_PendingDraws.size())
    {
        const PendingDraw& Pending = m_PendingDraws[PendingDrawIndex];
        VERIFY(Pending.DrawableID < m_DrawablePassData.size(), "Pending drawable ID references invalid pass data");
        const DrawablePassData& PassData = m_DrawablePassData[Pending.DrawableID];
        VERIFY(PassData.pDrawable != nullptr &&
                   PassData.Generation == PassData.pDrawable->Generation &&
                   PassData.pPSO != nullptr,
               "Pending drawable ID references stale pass data");

        const RadientDrawableSlot& Drawable = *PassData.pDrawable;
        const GLTF::Material&      Material = *Drawable.pMaterial;

        if (State.pVertexPool != Drawable.pVertexPool)
        {
            State.pVertexPool = Drawable.pVertexPool;
            VERIFY(State.pVertexPool != nullptr, "Pending drawable references null vertex pool");
            if (State.pVertexPool != nullptr)
                BindVertexPool(*State.pVertexPool, pContext);
        }

        const PBR_Renderer::PSO_FLAGS PSOFlags = PassData.PSOFlags;
        if (State.pPSO != PassData.pPSO)
        {
            State.pPSO      = PassData.pPSO;
            State.pMaterial = nullptr;

            if (State.pPSO != nullptr)
                pContext->SetPipelineState(State.pPSO);
        }

        if (!State.FrameSRBCommitted)
        {
            pContext->CommitShaderResources(pFrameSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            State.FrameSRBCommitted = true;
        }

        const UniqueIdentifier MaterialId         = PassData.SortKey.MaterialId;
        const bool             MaterialSRBChanged = State.MaterialId != MaterialId;
        if (MaterialSRBChanged)
        {
            State.MaterialId           = MaterialId;
            State.pMaterialSRB         = PassData.MaterialSRB.GetSRB();
            State.pPrimitiveAttribsVar = PassData.MaterialSRB.GetPrimitiveAttribsVariable();
        }

        if (State.pPrimitiveAttribsVar == nullptr)
        {
            UNEXPECTED("Material SRB does not contain the PBR primitive attributes buffer variable");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        // One dynamic offset selects the contiguous primitive array consumed by
        // the entire multi-draw batch.
        State.pPrimitiveAttribsVar->SetBufferOffset(Pending.PrimitiveAttribsOffset);
        if (MaterialSRBChanged)
            pContext->CommitShaderResources(State.pMaterialSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        if (State.pMaterial != &Material)
        {
            WriteMaterialAttribs(Renderer, pContext, PSOFlags, Material);
            State.pMaterial = &Material;
        }

        VERIFY(Pending.DrawCount > 0 && PendingDrawIndex + Pending.DrawCount <= m_PendingDraws.size(),
               "Pending multi-draw batch is invalid");

#ifdef DILIGENT_DEBUG
        for (Uint32 DrawIndex = 1; DrawIndex < Pending.DrawCount; ++DrawIndex)
        {
            const PendingDraw& BatchDraw = m_PendingDraws[PendingDrawIndex + DrawIndex];
            VERIFY(BatchDraw.DrawableID < m_DrawablePassData.size(), "Pending batch references invalid pass data");
            const DrawablePassData& BatchPassData = m_DrawablePassData[BatchDraw.DrawableID];
            VERIFY(BatchPassData.SortKey.PSOId == PassData.SortKey.PSOId &&
                       BatchPassData.SortKey.MaterialId == PassData.SortKey.MaterialId &&
                       BatchPassData.SortKey.VertexPoolId == PassData.SortKey.VertexPoolId &&
                       BatchPassData.SortKey.IsIndexed == PassData.SortKey.IsIndexed,
                   "Multi-draw batch contains incompatible draw state");
        }
#endif

        if (Pending.DrawCount > 1 && Drawable.IsIndexed)
        {
            if (NativeMultiDrawSupported)
            {
                m_MultiDrawIndexedItems.resize(Pending.DrawCount);
                for (Uint32 DrawIndex = 0; DrawIndex < Pending.DrawCount; ++DrawIndex)
                {
                    const RadientDrawableSlot& BatchDrawable =
                        *m_DrawablePassData[m_PendingDraws[PendingDrawIndex + DrawIndex].DrawableID].pDrawable;
                    m_MultiDrawIndexedItems[DrawIndex] = {
                        BatchDrawable.ElementCount,
                        BatchDrawable.FirstIndexLocation + BatchDrawable.FirstElement,
                        BatchDrawable.BaseVertex,
                    };
                }
                pContext->MultiDrawIndexed({Pending.DrawCount,
                                            m_MultiDrawIndexedItems.data(),
                                            VT_UINT32,
                                            DRAW_FLAG_VERIFY_ALL});
            }
            else
            {
                for (Uint32 DrawIndex = 0; DrawIndex < Pending.DrawCount; ++DrawIndex)
                {
                    const RadientDrawableSlot& BatchDrawable =
                        *m_DrawablePassData[m_PendingDraws[PendingDrawIndex + DrawIndex].DrawableID].pDrawable;
                    DrawIndexedAttribs DrawAttrs{BatchDrawable.ElementCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
                    if (DrawIndex > 0)
                        DrawAttrs.Flags |= DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT;
                    DrawAttrs.FirstIndexLocation    = BatchDrawable.FirstIndexLocation + BatchDrawable.FirstElement;
                    DrawAttrs.BaseVertex            = BatchDrawable.BaseVertex;
                    DrawAttrs.FirstInstanceLocation = DrawIndex;
                    pContext->DrawIndexed(DrawAttrs);
                }
            }
        }
        else if (Pending.DrawCount > 1)
        {
            if (NativeMultiDrawSupported)
            {
                m_MultiDrawItems.resize(Pending.DrawCount);
                for (Uint32 DrawIndex = 0; DrawIndex < Pending.DrawCount; ++DrawIndex)
                {
                    const RadientDrawableSlot& BatchDrawable =
                        *m_DrawablePassData[m_PendingDraws[PendingDrawIndex + DrawIndex].DrawableID].pDrawable;
                    m_MultiDrawItems[DrawIndex] = {
                        BatchDrawable.ElementCount,
                        BatchDrawable.BaseVertex + BatchDrawable.FirstElement,
                    };
                }
                pContext->MultiDraw({Pending.DrawCount,
                                     m_MultiDrawItems.data(),
                                     DRAW_FLAG_VERIFY_ALL});
            }
            else
            {
                for (Uint32 DrawIndex = 0; DrawIndex < Pending.DrawCount; ++DrawIndex)
                {
                    const RadientDrawableSlot& BatchDrawable =
                        *m_DrawablePassData[m_PendingDraws[PendingDrawIndex + DrawIndex].DrawableID].pDrawable;
                    DrawAttribs DrawAttrs{BatchDrawable.ElementCount, DRAW_FLAG_VERIFY_ALL};
                    if (DrawIndex > 0)
                        DrawAttrs.Flags |= DRAW_FLAG_DYNAMIC_RESOURCE_BUFFERS_INTACT;
                    DrawAttrs.StartVertexLocation   = BatchDrawable.BaseVertex + BatchDrawable.FirstElement;
                    DrawAttrs.FirstInstanceLocation = DrawIndex;
                    pContext->Draw(DrawAttrs);
                }
            }
        }
        else if (Drawable.IsIndexed)
        {
            DrawIndexedAttribs DrawAttrs{Drawable.ElementCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL};
            DrawAttrs.FirstIndexLocation = Drawable.FirstIndexLocation + Drawable.FirstElement;
            DrawAttrs.BaseVertex         = Drawable.BaseVertex;
            pContext->DrawIndexed(DrawAttrs);
        }
        else
        {
            DrawAttribs DrawAttrs{Drawable.ElementCount, DRAW_FLAG_VERIFY_ALL};
            DrawAttrs.StartVertexLocation = Drawable.BaseVertex + Drawable.FirstElement;
            pContext->Draw(DrawAttrs);
        }

        PendingDrawIndex += Pending.DrawCount;
    }

    return RADIENT_STATUS_OK;
}

bool RadientTesseraGeometryPass::DrawableSortKeyLess::operator()(const DrawableSortKey& Lhs,
                                                                 const DrawableSortKey& Rhs) const noexcept
{
    if (Lhs.PSOId != Rhs.PSOId)
        return Lhs.PSOId < Rhs.PSOId;

    if (Lhs.MaterialId != Rhs.MaterialId)
        return Lhs.MaterialId < Rhs.MaterialId;

    if (Lhs.VertexPoolId != Rhs.VertexPoolId)
        return Lhs.VertexPoolId < Rhs.VertexPoolId;

    if (Lhs.IsIndexed != Rhs.IsIndexed)
        return Lhs.IsIndexed < Rhs.IsIndexed;

    return Lhs.DrawableID < Rhs.DrawableID;
}

void RadientTesseraGeometryPass::SyncDrawablePassData(RadientTesseraGeometryRenderer&    Renderer,
                                                      const RadientTesseraDrawableCache& DrawableCache,
                                                      bool                               RebuildAll)
{
    if (!m_PbrPSOCache)
        return;

    if (RebuildAll)
    {
        for (OrderedDrawableSet& Drawables : m_OrderedDrawables)
            Drawables.clear();
        m_DrawablePassData.clear();

        const std::array<GLTF::Material::ALPHA_MODE, 3> AlphaModes =
            {
                GLTF::Material::ALPHA_MODE_OPAQUE,
                GLTF::Material::ALPHA_MODE_MASK,
                GLTF::Material::ALPHA_MODE_BLEND,
            };

        for (const GLTF::Material::ALPHA_MODE AlphaMode : AlphaModes)
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

    const RadientTesseraMaterialData& TesseraMaterialData = *Drawable.MaterialData;
    const RadientMaterialRenderData&  MaterialData        = TesseraMaterialData.GetMaterialRenderData();
    if (!MaterialData)
    {
        PassData = {};
        return;
    }

    const GLTF::Material&            Material  = *MaterialData.pMaterial;
    const GLTF::Material::ALPHA_MODE AlphaMode = static_cast<GLTF::Material::ALPHA_MODE>(Material.Attribs.AlphaMode);
    if (AlphaMode >= GLTF::Material::ALPHA_MODE_NUM_MODES)
    {
        UNEXPECTED("Material has an invalid alpha mode");
        return;
    }

    PBR_Renderer::PSO_FLAGS PSOFlags = Drawable.VertexAttribFlags | TesseraMaterialData.GetMaterialPSOFlags();
    PSOFlags |=
        PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS |
        PBR_Renderer::PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM |
        PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB |
        PBR_Renderer::PSO_FLAG_USE_IBL |
        PBR_Renderer::PSO_FLAG_USE_LIGHTS;
    PSOFlags &= m_RenderFlags;
    PSOFlags &= PBR_Renderer::GetEnabledPSOFlags(pRenderer->GetSettings());

    const RadientMaterialSRBLease& MaterialSRB = TesseraMaterialData.GetMaterialSRB();
    if (!MaterialSRB)
    {
        PassData = {};
        return;
    }
    const PBR_Renderer::StaticShaderTextureIdsArrayType& ShaderTextureIds =
        TesseraMaterialData.GetShaderTextureIds();

    const PBR_Renderer::PSOKey PsoKey{
        PBR_Renderer::RenderPassType::Main,
        PSOFlags,
        ToPBRAlphaMode(AlphaMode),
        Material.DoubleSided ? CULL_MODE_NONE : CULL_MODE_BACK,
        PBR_Renderer::DebugViewType::None,
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

    PassData.pDrawable            = &Drawable;
    PassData.Generation           = Drawable.Generation;
    PassData.PSOFlags             = PSOFlags;
    PassData.PrimitiveAttribsSize = pRenderer->GetPBRPrimitiveAttribsSize(PSOFlags);
    PassData.AlphaMode            = AlphaMode;
    PassData.pPSO                 = pPSO;
    PassData.MaterialSRB          = MaterialSRB;
    PassData.SortKey              = {
        pPSO->GetUniqueID(),
        TesseraMaterialData.GetUniqueID(),
        Drawable.pVertexPool->GetUniqueID(),
        DrawableID,
        Drawable.IsIndexed,
    };

    const auto InsertResult = m_OrderedDrawables[AlphaMode].insert(PassData.SortKey);
    VERIFY(InsertResult.second, "Drawable is already present in the ordered pass data");
}

void RadientTesseraGeometryPass::InvalidateDrawablePassData(RadientDrawableID DrawableID)
{
    if (DrawableID < m_DrawablePassData.size())
    {
        DrawablePassData& PassData = m_DrawablePassData[DrawableID];
        if (PassData.SortKey.DrawableID != InvalidRadientDrawableID)
        {
            VERIFY(PassData.AlphaMode < GLTF::Material::ALPHA_MODE_NUM_MODES,
                   "Ordered drawable has an invalid alpha mode");
            if (PassData.AlphaMode < GLTF::Material::ALPHA_MODE_NUM_MODES)
            {
                OrderedDrawableSet& Drawables = m_OrderedDrawables[PassData.AlphaMode];
                const auto          It        = Drawables.find(PassData.SortKey);
                VERIFY(It != Drawables.end(), "Ordered drawable entry is missing");
                if (It != Drawables.end())
                    Drawables.erase(It);
            }
        }
        PassData = {};
    }
}

RADIENT_STATUS RadientTesseraGeometryPass::CreatePsoCaches(PBR_Renderer&           Renderer,
                                                           PBR_Renderer::PSO_FLAGS BaseRenderFlags,
                                                           TEXTURE_FORMAT          RTVFormat,
                                                           TEXTURE_FORMAT          DSVFormat)
{
    if (RTVFormat == TEX_FORMAT_UNKNOWN)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    GraphicsPipelineDesc GraphicsDesc;
    GraphicsDesc.NumRenderTargets = 1;
    GraphicsDesc.RTVFormats[0]    = RTVFormat;
    GraphicsDesc.DSVFormat        = DSVFormat;

    GraphicsDesc.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    GraphicsDesc.RasterizerDesc.FrontCounterClockwise = true;

    m_PbrPSOCache = Renderer.GetPsoCacheAccessor(GraphicsDesc);

    GraphicsDesc.RasterizerDesc.FillMode = FILL_MODE_WIREFRAME;
    m_WireframePSOCache                  = Renderer.GetPsoCacheAccessor(GraphicsDesc);

    m_RenderFlags = BaseRenderFlags;
    if (RequiresOutputSRGBConversion(RTVFormat))
        m_RenderFlags |= PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB;

    m_RTVFormat = RTVFormat;
    m_DSVFormat = DSVFormat;
    for (OrderedDrawableSet& Drawables : m_OrderedDrawables)
        Drawables.clear();
    m_DrawablePassData.clear();

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
