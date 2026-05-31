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

#include "RadientDrawList.hpp"
#include "RadientFrameRenderTargets.hpp"
#include "RadientLightList.hpp"
#include "RadientRenderResourceCache.hpp"

#include "PBR_Renderer.hpp"
#include "RefCntAutoPtr.hpp"

#include <array>
#include <memory>
#include <vector>

namespace Diligent
{

class RadientSceneDrawableCache;
struct RadientDrawableSlot;

struct RadientGeometryResourceCacheUseInfo
{
    GLTF::ResourceManager* pResourceMgr = nullptr;

    GLTF::ResourceManager::VertexLayoutKey VtxLayoutKey;

    std::array<TEXTURE_FORMAT, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> AtlasFormats{};

    RadientGeometryResourceCacheUseInfo() noexcept
    {
        AtlasFormats.fill(TEX_FORMAT_RGBA8_TYPELESS);
    }
};

struct RadientGeometryResourceCacheBindings
{
    Uint32 Version = ~0u;

    RefCntAutoPtr<IShaderResourceBinding> pSRB;
};

/// Shared renderer state used by geometry passes.
class RadientGeometryRenderer
{
public:
    RADIENT_STATUS Prepare(IRenderDevice* pDevice, IDeviceContext* pContext);
    RADIENT_STATUS BeginFrame(IRenderDevice*                   pDevice,
                              IDeviceContext*                  pContext,
                              const RadientLightList&          LightList,
                              GLTF::ResourceManager*           pResourceManager,
                              const RadientRenderAttribs&      Attribs,
                              const RadientFrameRenderTargets& Targets);
    void           EndFrame();

    PBR_Renderer*           GetRenderer() const { return m_pRenderer.get(); }
    IShaderResourceBinding* GetResourceCacheSRB() const { return m_CacheBindings.pSRB.RawPtr(); }
    PBR_Renderer::PSO_FLAGS GetBaseRenderFlags() const { return m_BaseRenderFlags; }

private:
    RADIENT_STATUS CreateRenderer(IRenderDevice*  pDevice,
                                  IDeviceContext* pContext);

    void InitializeResourceCacheUseInfo();

private:
    std::unique_ptr<PBR_Renderer> m_pRenderer;
    RefCntAutoPtr<IBuffer>        m_pFrameAttribsCB;

    RadientGeometryResourceCacheUseInfo  m_CacheUseInfo;
    RadientGeometryResourceCacheBindings m_CacheBindings;

    PBR_Renderer::PSO_FLAGS m_BaseRenderFlags = PBR_Renderer::PSO_FLAG_NONE;

    Uint32 m_FrameIndex = 0;
};

/// Mesh geometry render pass used by shadow and forward rendering stages.
class RadientGeometryPass
{
public:
    explicit RadientGeometryPass(bool EnableAsyncPipelineCompilation = true) noexcept;

    RADIENT_STATUS Prepare(RadientGeometryRenderer&         Renderer,
                           IRenderDevice*                   pDevice,
                           IDeviceContext*                  pContext,
                           const RadientSceneDrawableCache& DrawableCache,
                           const RadientFrameRenderTargets& Targets);
    RADIENT_STATUS Execute(RadientGeometryRenderer&         Renderer,
                           IRenderDevice*                   pDevice,
                           IDeviceContext*                  pContext,
                           const RadientDrawList&           DrawList,
                           const RadientSceneDrawableCache& DrawableCache,
                           const RadientFrameRenderTargets& Targets);

private:
    RADIENT_STATUS CreatePsoCaches(PBR_Renderer&           Renderer,
                                   PBR_Renderer::PSO_FLAGS BaseRenderFlags,
                                   TEXTURE_FORMAT          RTVFormat,
                                   TEXTURE_FORMAT          DSVFormat);

    struct DrawablePassData
    {
        const RadientDrawableSlot* pDrawable  = nullptr;
        Uint32                     Generation = 0;
        PBR_Renderer::PSO_FLAGS    PSOFlags   = PBR_Renderer::PSO_FLAG_NONE;
        IPipelineState*            pPSO       = nullptr;
    };

    void SyncDrawablePassData(PBR_Renderer&                    Renderer,
                              const RadientSceneDrawableCache& DrawableCache,
                              bool                             RebuildAll);
    void UpdateDrawablePassData(PBR_Renderer&              Renderer,
                                const RadientDrawableSlot& Drawable,
                                RadientDrawableID          DrawableID);
    void InvalidateDrawablePassData(RadientDrawableID DrawableID);

    void BuildSortedDrawableIDs(const RadientDrawList&           DrawList,
                                const RadientSceneDrawableCache& DrawableCache);

private:
    PBR_Renderer::PsoCacheAccessor m_PbrPSOCache;
    PBR_Renderer::PsoCacheAccessor m_WireframePSOCache;

    std::vector<DrawablePassData>  m_DrawablePassData;
    std::vector<RadientDrawableID> m_SortedDrawableIDs;

    PBR_Renderer::PSO_FLAGS m_RenderFlags = PBR_Renderer::PSO_FLAG_NONE;

    TEXTURE_FORMAT m_RTVFormat = TEX_FORMAT_UNKNOWN;
    TEXTURE_FORMAT m_DSVFormat = TEX_FORMAT_UNKNOWN;

    bool m_EnableAsyncPipelineCompilation = true;
};

} // namespace Diligent
