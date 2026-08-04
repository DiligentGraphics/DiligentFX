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

#pragma once

#include "Render/RadientDrawList.hpp"
#include "Render/RadientFrameRenderTargets.hpp"
#include "Render/RadientPBRRenderer.hpp"
#include "Render/Tessera/RadientTesseraGeometryRenderer.hpp"

#include "GLTFLoader.hpp"
#include "RefCntAutoPtr.hpp"
#include "UniqueIdentifier.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127) // conditional expression is constant
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/btree_map.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <array>
#include <vector>

namespace Diligent
{

class RadientTesseraDrawableCache;
struct RadientDrawableSlot;

/// Mesh geometry render pass used by shadow and forward rendering stages.
class RadientTesseraGeometryPass
{
public:
    explicit RadientTesseraGeometryPass(bool EnableAsyncPipelineCompilation = true) noexcept;

    RADIENT_STATUS Prepare(RadientTesseraGeometryRenderer&    Renderer,
                           IRenderDevice*                     pDevice,
                           IDeviceContext*                    pContext,
                           const RadientTesseraDrawableCache& DrawableCache,
                           const RadientFrameRenderTargets&   Targets);
    RADIENT_STATUS Execute(RadientTesseraGeometryRenderer&    Renderer,
                           IRenderDevice*                     pDevice,
                           IDeviceContext*                    pContext,
                           IShaderResourceBinding*            pFrameSRB,
                           GLTF::Material::ALPHA_MODE         AlphaMode,
                           const RadientTesseraDrawableCache& DrawableCache,
                           const RadientFrameRenderTargets&   Targets,
                           RadientTesseraFrameHistory&        FrameHistory);

private:
    static constexpr Uint32 InvalidBatchItemIndex = ~Uint32{0};

    RADIENT_STATUS CreatePsoCaches(PBR_Renderer&                                                                      Renderer,
                                   PBR_Renderer::PSO_FLAGS                                                            BaseRenderFlags,
                                   const std::array<TEXTURE_FORMAT, RadientFrameRenderTargets::GBUFFER_TARGET_COUNT>& RTVFormats,
                                   TEXTURE_FORMAT                                                                     DSVFormat,
                                   bool                                                                               UseReverseDepth);

    struct DrawableBatchKey
    {
        UniqueIdentifier PSOId        = 0;
        UniqueIdentifier MaterialId   = 0;
        UniqueIdentifier VertexPoolId = 0;
        bool             IsIndexed    = false;
    };

    struct DrawableBatchKeyLess
    {
        bool operator()(const DrawableBatchKey& Lhs, const DrawableBatchKey& Rhs) const noexcept;
    };

    struct DrawableBatchItem
    {
        // Only fields consumed by Execute() are retained here, keeping the hot
        // per-batch traversal compact and independent of the full drawable slot.
        const RadientMatrix4x4* pWorldMatrix      = nullptr;
        const Bool*             pEffectiveVisible = nullptr;

        RadientDrawableID DrawableID    = InvalidRadientDrawableID;
        Uint32            Generation    = 0;
        Uint32            ElementCount  = 0;
        Uint32            FirstLocation = 0;
        Uint32            BaseVertex    = 0;
    };

    struct DrawableBatch
    {
        IPipelineState*         pPSO                 = nullptr;
        PBR_Renderer::PSO_FLAGS PSOFlags             = PBR_Renderer::PSO_FLAG_NONE;
        Uint32                  PrimitiveAttribsSize = 0;

        UniqueIdentifier MaterialId            = 0;
        IVertexPool*     pVertexPool           = nullptr;
        Uint32           MaterialAttribsOffset = ~Uint32{0};
        bool             IsIndexed             = false;

        RadientMaterialSRBLease        MaterialSRB;
        std::vector<DrawableBatchItem> Drawables;
    };

    struct DrawablePassData
    {
        GLTF::Material::ALPHA_MODE AlphaMode = GLTF::Material::ALPHA_MODE_NUM_MODES;
        DrawableBatchKey           BatchKey;
        Uint32                     BatchItemIndex = InvalidBatchItemIndex;
    };

    struct PendingDraw
    {
        const DrawableBatch* pBatch                 = nullptr;
        Uint32               FirstDrawItem          = 0;
        Uint32               PrimitiveAttribsOffset = 0;
        Uint32               DrawCount              = 1;
    };

    struct DrawState
    {
        UniqueIdentifier         MaterialId            = 0;
        IShaderResourceBinding*  pMaterialSRB          = nullptr;
        IShaderResourceVariable* pPrimitiveAttribsVar  = nullptr;
        IShaderResourceVariable* pMaterialAttribsVar   = nullptr;
        IPipelineState*          pPSO                  = nullptr;
        IVertexPool*             pVertexPool           = nullptr;
        Uint32                   MaterialAttribsOffset = ~Uint32{0};
        bool                     FrameSRBCommitted     = false;
    };

    RADIENT_STATUS RenderPendingDraws(IDeviceContext*         pContext,
                                      IShaderResourceBinding* pFrameSRB,
                                      DrawState&              State);

    void SyncDrawablePassData(RadientTesseraGeometryRenderer&    Renderer,
                              const RadientTesseraDrawableCache& DrawableCache,
                              bool                               RebuildAll);
    void UpdateDrawablePassData(RadientTesseraGeometryRenderer& Renderer,
                                const RadientDrawableSlot&      Drawable,
                                RadientDrawableID               DrawableID);
    void InvalidateDrawablePassData(RadientDrawableID DrawableID);

private:
    using OrderedDrawableBatchMap = absl::btree_map<DrawableBatchKey, DrawableBatch, DrawableBatchKeyLess>;

    PBR_Renderer::PsoCacheAccessor m_PbrPSOCache;
    PBR_Renderer::PsoCacheAccessor m_WireframePSOCache;

    std::vector<DrawablePassData> m_DrawablePassData;

    // Incremental drawable changes only move affected records between batches.
    // Execute traverses each compact drawable vector without sorting every frame.
    std::array<OrderedDrawableBatchMap, GLTF::Material::ALPHA_MODE_NUM_MODES> m_DrawableBatches;
    std::vector<PendingDraw>                                                  m_PendingDraws;
    std::vector<MultiDrawItem>                                                m_MultiDrawItems;
    std::vector<MultiDrawIndexedItem>                                         m_MultiDrawIndexedItems;

    PBR_Renderer::PSO_FLAGS m_RenderFlags = PBR_Renderer::PSO_FLAG_NONE;

    std::array<TEXTURE_FORMAT, RadientFrameRenderTargets::GBUFFER_TARGET_COUNT> m_RTVFormats{};
    TEXTURE_FORMAT                                                              m_DSVFormat                      = TEX_FORMAT_UNKNOWN;
    bool                                                                        m_UseReverseDepth                = false;
    bool                                                                        m_EnableAsyncPipelineCompilation = true;
    DEVICE_FEATURE_STATE                                                        m_NativeMultiDrawSupported       = DEVICE_FEATURE_STATE_OPTIONAL;
};

} // namespace Diligent
