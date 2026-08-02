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

#include "Render/RadientMaterialSRBTable.hpp"
#include "Render/Tessera/RadientTesseraMaterialBuffer.hpp"
#include "UniqueIdentifier.hpp"
#include "WeakValueHashMap.hpp"

#include <atomic>
#include <functional>
#include <memory>

namespace Diligent
{

struct IThreadPool;

/// Immutable Tessera-specific material data produced by a worker task. The
/// retained material asset keeps the borrowed RadientMaterialRenderData view
/// alive. The logical SRB lease is stable even while its GPU SRB is pending.
class RadientTesseraMaterialData final
{
public:
    RadientTesseraMaterialData(IRadientMaterialAsset*           pMaterial,
                               const RadientMaterialRenderData& MaterialData);

    RADIENT_STATUS GetStatus() const noexcept
    {
        return m_Status.load(std::memory_order_acquire);
    }

    UniqueIdentifier GetUniqueID() const noexcept
    {
        return m_UniqueID;
    }

    /// Reports aggregate Tessera GPU readiness. The source material tracks its
    /// selected texture dependencies; Tessera additionally waits for the SRB
    /// prepared for the logical lease. This method is render-thread-only.
    RADIENT_STATUS GetGPUResourceStatus() const noexcept;

    const RadientMaterialRenderData& GetMaterialRenderData() const noexcept
    {
        return m_MaterialData;
    }

    const RadientMaterialSRBLease& GetMaterialSRB() const noexcept
    {
        return m_MaterialSRB;
    }

    const RadientTesseraMaterialBufferAllocation& GetMaterialBufferAllocation() const noexcept
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

private:
    bool TryScheduleProcessing() noexcept;

    void PublishSuccess(PBR_Renderer::PSO_FLAGS                       MaterialPSOFlags,
                        RadientMaterialSRBLease                       MaterialSRB,
                        RadientTesseraMaterialBufferAllocation        MaterialBufferAllocation,
                        PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds) noexcept;

    void PublishFailure(RADIENT_STATUS Status) noexcept;

    RefCntAutoPtr<IRadientMaterialAsset>   m_pMaterial;
    RadientMaterialRenderData              m_MaterialData;
    RadientMaterialSRBLease                m_MaterialSRB;
    RadientTesseraMaterialBufferAllocation m_MaterialBufferAllocation;

    const UniqueIdentifier                        m_UniqueID;
    PBR_Renderer::StaticShaderTextureIdsArrayType m_ShaderTextureIds{};
    PBR_Renderer::PSO_FLAGS                       m_MaterialPSOFlags = PBR_Renderer::PSO_FLAG_NONE;

    std::atomic<RADIENT_STATUS> m_Status{RADIENT_STATUS_PENDING};
    std::atomic_bool            m_ProcessingScheduled{false};

    friend class RadientTesseraMaterialCache;
};

using RadientTesseraMaterialDataMap =
    WeakValueHashMap<IRadientMaterialAsset*, RadientTesseraMaterialData>;

struct RadientTesseraMaterialResolveResult
{
    // Pending results retain their state so processing is not repeated before
    // the renderer checks the status again.
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
        std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> TextureAttribIndices{};
        Uint32                                                 MaterialTextureSlotCount = 0;
        /// Material and texture flags enabled by the renderer configuration.
        /// Resolve() further restricts extension groups using the GLTF material.
        PBR_Renderer::PSO_FLAGS               EnabledMaterialPSOFlags = PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES;
        RadientMaterialDefaultTextureBindings DefaultTextures;
        Uint32                                ConstantBufferOffsetAlignment = 0;
        Uint32                                MaxMaterialAttribsSize        = 0;
    };

    explicit RadientTesseraMaterialCache(const CreateInfo& CI);
    ~RadientTesseraMaterialCache();

    RadientTesseraMaterialCache(const RadientTesseraMaterialCache&)            = delete;
    RadientTesseraMaterialCache& operator=(const RadientTesseraMaterialCache&) = delete;
    RadientTesseraMaterialCache(RadientTesseraMaterialCache&&)                 = delete;
    RadientTesseraMaterialCache& operator=(RadientTesseraMaterialCache&&)      = delete;

    /// Returns or schedules Tessera data for the material. This method obtains
    /// the immutable render data from the asset and must be called from the
    /// render thread. Material PSO flags are derived by the worker task.
    RadientTesseraMaterialResolveResult Resolve(IThreadPool&           ThreadPool,
                                                IRadientMaterialAsset* pMaterial);

    /// Creates or grows the shared material buffer and uploads worker-produced
    /// material records. This method must be called from the render thread.
    RADIENT_STATUS PrepareMaterialBuffer(IRenderDevice*  pDevice,
                                         IDeviceContext* pContext);

    /// Creates pending material SRBs and refreshes existing SRBs after texture
    /// resources change. This method must be called from the render thread.
    RADIENT_STATUS Prepare(Uint32                               TextureVersion,
                           const ResolveTextureSRVCallbackType& ResolveTextureSRV,
                           const CreateSRBCallbackType&         CreateSRB);

    IBuffer* GetMaterialBuffer() const noexcept;
    Uint32   GetMaterialBufferVersion() const noexcept;
    Uint32   GetMaxMaterialAttribsSize() const noexcept;

private:
    struct ProcessingContext;

    static void ProcessMaterial(const std::shared_ptr<ProcessingContext>& pContext,
                                RadientTesseraMaterialData&               Data);

    std::shared_ptr<ProcessingContext> m_pProcessingContext;
    RadientTesseraMaterialDataMap      m_MaterialData;
};

} // namespace Diligent
