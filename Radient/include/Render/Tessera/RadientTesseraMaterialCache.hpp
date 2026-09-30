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

#include "Assets/RadientMaterialAssetManager.hpp"
#include "Assets/RadientMaterialChangeTracker.hpp"
#include "Render/RadientMaterialSRBTable.hpp"
#include "Render/Tessera/RadientTesseraBufferSuballocator.hpp"
#include "UniqueIdentifier.hpp"
#include "WeakValueHashMap.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace Diligent
{

struct IThreadPool;
class RadientMaterialDefinitionImpl;

namespace RadientMaterialDetail
{
class MaterialStorage;
}

/// Tessera-specific material data initially produced by a worker task. Shader
/// data and surface state are refreshed on the render thread without changing bindings.
/// The retained material asset keeps the borrowed RadientMaterialAssetView
/// alive. The logical SRB lease is stable even while its GPU SRB is pending.
class RadientTesseraMaterialData final
{
public:
    RadientTesseraMaterialData(IRadientMaterialAsset*          pMaterial,
                               const RadientMaterialAssetView& MaterialView);

    RADIENT_STATUS GetStatus() const noexcept
    {
        return m_Status.load(std::memory_order_acquire);
    }

    UniqueIdentifier GetUniqueID() const noexcept
    {
        return m_UniqueID;
    }

    /// Reports aggregate Tessera GPU readiness. The source material tracks its
    /// selected texture dependencies; Tessera additionally waits for initial
    /// shader-data validation and upload, and the SRB prepared for the logical
    /// lease. This method is render-thread-only.
    RADIENT_STATUS GetGPUResourceStatus() const noexcept;

    const RadientMaterialAssetView& GetMaterialView() const noexcept
    {
        return m_MaterialView;
    }

    const RadientMaterialSRBLease& GetMaterialSRB() const noexcept
    {
        return m_MaterialSRB;
    }

    const RadientTesseraBufferAllocation& GetMaterialBufferAllocation() const noexcept
    {
        return m_MaterialBufferAllocation;
    }

    const PBR_Renderer::StaticShaderTextureIdsArrayType& GetShaderTextureIds() const noexcept
    {
        return m_ShaderTextureIds;
    }

    PBR_Renderer::PSO_FLAGS GetMaterialPSOFlags() const noexcept
    {
        return m_MaterialPSOFlags;
    }

    RADIENT_MATERIAL_SURFACE_MODE GetSurfaceMode() const noexcept
    {
        return m_SurfaceMode;
    }

    Bool IsDoubleSided() const noexcept
    {
        return m_IsDoubleSided;
    }

    /// Changes only when preparation updates the cached surface mode or double-sided flag.
    Uint64 GetRenderStateRevision() const noexcept
    {
        return m_RenderStateRevision;
    }

private:
    bool TryScheduleProcessing() noexcept;

    void PublishSuccess(PBR_Renderer::PSO_FLAGS                       MaterialPSOFlags,
                        RadientMaterialSRBLease                       MaterialSRB,
                        RadientTesseraBufferAllocation                MaterialBufferAllocation,
                        PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds,
                        Uint64                                        ShaderDataVersion) noexcept;

    void PublishFailure(RADIENT_STATUS Status) noexcept;

    RefCntAutoPtr<IRadientMaterialAsset> m_pMaterial;
    RadientMaterialAssetView             m_MaterialView;
    RadientMaterialSRBLease              m_MaterialSRB;
    RadientTesseraBufferAllocation       m_MaterialBufferAllocation;

    // The retained asset owns both objects; resolving these once avoids
    // interface queries when checking shader-data changes.
    const RadientMaterialDetail::MaterialStorage* const m_pMaterialStorage;
    const RadientMaterialDefinitionImpl*                m_pDefinition       = nullptr;
    Uint64                                              m_ShaderDataVersion = 0;

    // Only the render thread reads/writes these fields. Worker publication alone
    // cannot make a record GPU-ready: shader and surface state must first be refreshed.
    // Keep the observed asset version separate from the effective cached-state revision:
    // edits reverted before preparation must not invalidate dependent drawables.
    Uint64 m_ObservedRenderStateVersion = 0;
    Uint64 m_RenderStateRevision        = 0;
    bool   m_InitialDataValidated       = false;

    const UniqueIdentifier                        m_UniqueID;
    PBR_Renderer::StaticShaderTextureIdsArrayType m_ShaderTextureIds{};
    PBR_Renderer::PSO_FLAGS                       m_MaterialPSOFlags = PBR_Renderer::PSO_FLAG_NONE;
    RADIENT_MATERIAL_SURFACE_MODE                 m_SurfaceMode      = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Bool                                          m_IsDoubleSided    = False;

    std::atomic<RADIENT_STATUS> m_Status{RADIENT_STATUS_PENDING};
    std::atomic_bool            m_ProcessingScheduled{false};

    friend class RadientTesseraMaterialCache;
};

using RadientTesseraMaterialDataMap =
    WeakValueHashMap<IRadientMaterialAsset*, RadientTesseraMaterialData>;

struct RadientTesseraMaterialResolveResult
{
    // Once processing is scheduled, pending results retain their state so it
    // is not repeated before the renderer checks the status again.
    RadientTesseraMaterialDataMap::ValueHandle Data;
    RADIENT_STATUS                             Status = RADIENT_STATUS_INVALID_ARGUMENT;
};

/// Cache of Tessera material binding data. Resolve() schedules at most one
/// processing task for each material and texture-flag configuration. The cache
/// owns the logical SRB entries and prepares their GPU SRBs on the render thread.
class RadientTesseraMaterialCache final
{
public:
    using ResolveTextureSRVCallbackType = RadientMaterialSRBTable::ResolveTextureSRVCallbackType;
    using CreateSRBCallbackType =
        std::function<RefCntAutoPtr<IShaderResourceBinding>(ITextureView* const*, Uint32)>;

    struct CreateInfo
    {
        Uint32 MaterialTextureSlotCount = 0;
        /// Material and texture flags enabled by the renderer configuration.
        /// Resolve() further restricts optional groups using the surface definition.
        PBR_Renderer::PSO_FLAGS               EnabledMaterialPSOFlags = PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES;
        RadientMaterialDefaultTextureBindings DefaultTextures;
        Uint32                                ConstantBufferOffsetAlignment = 0;
        Uint32                                MaxMaterialAttribsSize        = 0;
    };

    explicit RadientTesseraMaterialCache(const CreateInfo& CI);
    ~RadientTesseraMaterialCache();

    RadientTesseraMaterialCache(const RadientTesseraMaterialCache&) = delete;
    RadientTesseraMaterialCache& operator=(const RadientTesseraMaterialCache&) = delete;
    RadientTesseraMaterialCache(RadientTesseraMaterialCache&&)                 = delete;
    RadientTesseraMaterialCache& operator=(RadientTesseraMaterialCache&&) = delete;

    /// Returns or schedules Tessera data for the material. On the first call,
    /// the material and its selected texture GPU resources must report OK.
    /// This method obtains the immutable render data from the asset and must be
    /// called from the render thread. Material PSO flags are derived by the
    /// worker task.
    RadientTesseraMaterialResolveResult Resolve(IThreadPool&           ThreadPool,
                                                IRadientMaterialAsset* pMaterial);

    /// Refreshes changed material shader data and surface state, creates or grows the shared
    /// material buffer, and uploads pending records. Existing allocations and
    /// bindings are preserved. This method must be called from the render thread.
    RADIENT_STATUS PrepareMaterialBuffer(IRenderDevice*  pDevice,
                                         IDeviceContext* pContext);

    /// Creates pending material SRBs and refreshes existing SRBs after texture
    /// resources change. This method uses the material buffer snapshot owned
    /// by this cache and must only be called from the render thread.
    RADIENT_STATUS Prepare(Uint32                               TextureVersion,
                           const ResolveTextureSRVCallbackType& ResolveTextureSRV,
                           const CreateSRBCallbackType&         CreateSRB);

    /// Testing-only overload that supplies a synthetic material buffer
    /// snapshot. Production code must use the overload above so the snapshot
    /// always comes from the cache-owned material buffer.
    RADIENT_STATUS Prepare(Uint32                               TextureVersion,
                           Uint32                               MaterialBufferVersion,
                           Uint64                               MaterialBufferGeneration,
                           const ResolveTextureSRVCallbackType& ResolveTextureSRV,
                           const CreateSRBCallbackType&         CreateSRB);

    IBuffer* GetMaterialBuffer() const noexcept;
    Uint32   GetMaxMaterialAttribsSize() const noexcept;

    /// Changes when preparation updates a cached surface mode or double-sided flag.
    /// Shader-only edits do not advance this render-thread revision.
    Uint64 GetRenderStateRevision() const noexcept
    {
        return m_RenderStateRevision;
    }

private:
    struct ProcessingContext;

    static void ProcessMaterial(const std::shared_ptr<ProcessingContext>& pContext,
                                RadientTesseraMaterialData&               Data);

    RADIENT_STATUS RefreshMaterialData();

    struct MaterialTracker
    {
        std::shared_ptr<RadientMaterialDetail::MaterialChangeTracker> pTracker;
        Uint64                                                        Revision = 0;
    };

    std::shared_ptr<ProcessingContext>              m_pProcessingContext;
    RadientTesseraMaterialDataMap                   m_MaterialData;
    std::vector<MaterialTracker>                    m_MaterialTrackers;
    RadientTesseraMaterialDataMap::ValueHandleArray m_RefreshMaterials;
    Uint64                                          m_MaterialProcessingRevision = 0;
    Uint64                                          m_RenderStateRevision        = 0;
    bool                                            m_MaterialDataRefreshPending = false;
};

} // namespace Diligent
