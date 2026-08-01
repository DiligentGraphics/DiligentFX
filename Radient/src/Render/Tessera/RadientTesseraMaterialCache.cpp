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

#include "Render/Tessera/RadientTesseraMaterialCache.hpp"

#include "ThreadPool.hpp"

#include <exception>
#include <utility>

namespace Diligent
{

RadientTesseraMaterialData::RadientTesseraMaterialData(IRadientMaterialAsset*           pMaterial,
                                                       const RadientMaterialRenderData& MaterialData) :
    m_pMaterial{pMaterial},
    m_MaterialData{MaterialData}
{
    m_ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
}

bool RadientTesseraMaterialData::TryScheduleProcessing() noexcept
{
    bool Expected = false;
    return m_ProcessingScheduled.compare_exchange_strong(Expected, true,
                                                         std::memory_order_acq_rel,
                                                         std::memory_order_relaxed);
}

void RadientTesseraMaterialData::PublishSuccess(
    PBR_Renderer::PSO_FLAGS                       MaterialPSOFlags,
    RadientMaterialSRBLease                       MaterialSRB,
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds) noexcept
{
    VERIFY_EXPR(MaterialSRB);
    m_MaterialPSOFlags = MaterialPSOFlags;
    m_MaterialSRB      = std::move(MaterialSRB);
    m_ShaderTextureIds = std::move(ShaderTextureIds);

    m_Status.store(RADIENT_STATUS_OK, std::memory_order_release);
}

void RadientTesseraMaterialData::PublishFailure(RADIENT_STATUS Status) noexcept
{
    VERIFY_EXPR(RADIENT_FAILED(Status));
    m_Status.store(Status, std::memory_order_release);
}

namespace
{

PBR_Renderer::PSO_FLAGS GetMaterialPSOFlags(const GLTF::Material&   Material,
                                            PBR_Renderer::PSO_FLAGS EnabledFlags)
{
    PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES;

    if (Material.HasClearcoat)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_CLEAR_COAT |
            PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_MAP |
            PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP |
            PBR_Renderer::PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP;
    }
    if (Material.Sheen)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_SHEEN |
            PBR_Renderer::PSO_FLAG_USE_SHEEN_COLOR_MAP |
            PBR_Renderer::PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP;
    }
    if (Material.Anisotropy)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_ANISOTROPY |
            PBR_Renderer::PSO_FLAG_USE_ANISOTROPY_MAP;
    }
    if (Material.Iridescence)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_IRIDESCENCE |
            PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_MAP |
            PBR_Renderer::PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP;
    }
    if (Material.Transmission)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_TRANSMISSION |
            PBR_Renderer::PSO_FLAG_USE_TRANSMISSION_MAP;
    }
    if (Material.Volume)
    {
        Flags |=
            PBR_Renderer::PSO_FLAG_ENABLE_VOLUME |
            PBR_Renderer::PSO_FLAG_USE_THICKNESS_MAP;
    }

    return static_cast<PBR_Renderer::PSO_FLAGS>(Flags & EnabledFlags);
}

} // namespace

struct RadientTesseraMaterialCache::ProcessingContext
{
    std::shared_ptr<RadientMaterialSRBTable>               pMaterialSRBTable;
    std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> TextureAttribIndices{};
    Uint32                                                 MaterialTextureSlotCount = 0;
    PBR_Renderer::PSO_FLAGS                                EnabledMaterialPSOFlags  = PBR_Renderer::PSO_FLAG_NONE;
    RadientMaterialDefaultTextureBindings                  DefaultTextures;
};

RadientTesseraMaterialCache::RadientTesseraMaterialCache(const CreateInfo& CI) :
    m_pProcessingContext{std::make_shared<ProcessingContext>()}
{
    if (CI.MaterialTextureSlotCount == 0 ||
        CI.MaterialTextureSlotCount > PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT)
    {
        LOG_ERROR_AND_THROW("Radient Tessera material texture slot count must be between 1 and ",
                            Uint32{PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT});
    }
    if (!CI.DefaultTextures)
        LOG_ERROR_AND_THROW("Radient Tessera default material texture bindings must be initialized");

    m_pProcessingContext->pMaterialSRBTable        = std::make_shared<RadientMaterialSRBTable>();
    m_pProcessingContext->TextureAttribIndices     = CI.TextureAttribIndices;
    m_pProcessingContext->MaterialTextureSlotCount = CI.MaterialTextureSlotCount;
    m_pProcessingContext->EnabledMaterialPSOFlags  = CI.EnabledMaterialPSOFlags;
    m_pProcessingContext->DefaultTextures          = CI.DefaultTextures;
}

RadientTesseraMaterialCache::~RadientTesseraMaterialCache() = default;

RadientTesseraMaterialResolveResult RadientTesseraMaterialCache::Resolve(IThreadPool&           ThreadPool,
                                                                         IRadientMaterialAsset* pMaterial)
{
    if (pMaterial == nullptr)
        return {};

    RadientTesseraMaterialDataMap::ValueHandle Data = m_MaterialData.Get(pMaterial);
    if (!Data)
    {
        const RadientMaterialRenderData MaterialData = RadientMaterialAssetManager::GetRenderData(pMaterial);
        if (!MaterialData)
            return {};

        Data = m_MaterialData.GetOrInsert(pMaterial, pMaterial, MaterialData);
    }
    if (!Data)
        return {};

    if (Data->TryScheduleProcessing())
    {
        RadientTesseraMaterialDataMap::ValueHandle TaskData = m_MaterialData.Get(pMaterial);
        VERIFY_EXPR(TaskData);

        RefCntAutoPtr<IAsyncTask> pTask =
            CreateAsyncWorkTask(
                [pContext = m_pProcessingContext, Data = std::move(TaskData)](Uint32) mutable {
                    try
                    {
                        ProcessMaterial(pContext, *Data);
                    }
                    catch (const std::exception& Error)
                    {
                        LOG_ERROR_MESSAGE("Failed to process Tessera material data: ", Error.what());
                        Data->PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
                    }
                    catch (...)
                    {
                        LOG_ERROR_MESSAGE("Failed to process Tessera material data");
                        Data->PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
                    }
                    return ASYNC_TASK_STATUS_COMPLETE;
                });

        if (!ThreadPool.EnqueueTask(pTask))
            Data->PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
    }

    const RADIENT_STATUS Status = Data->GetStatus();
    return {std::move(Data), Status};
}

RADIENT_STATUS RadientTesseraMaterialCache::Prepare(
    Uint32                               TextureVersion,
    const ResolveTextureSRVCallbackType& ResolveTextureSRV,
    const CreateSRBCallbackType&         CreateSRB)
{
    return m_pProcessingContext->pMaterialSRBTable->Prepare(
        TextureVersion,
        ResolveTextureSRV,
        CreateSRB);
}

void RadientTesseraMaterialCache::ProcessMaterial(
    const std::shared_ptr<ProcessingContext>& pContext,
    RadientTesseraMaterialData&               Data)
{
    const PBR_Renderer::PSO_FLAGS MaterialPSOFlags =
        GetMaterialPSOFlags(*Data.m_MaterialData.pMaterial,
                            pContext->EnabledMaterialPSOFlags);

    RadientMaterialSRBLease                       MaterialSRB;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;

    const RADIENT_STATUS Status =
        pContext->pMaterialSRBTable->Acquire(
            Data.m_MaterialData,
            pContext->TextureAttribIndices,
            MaterialPSOFlags,
            pContext->MaterialTextureSlotCount,
            pContext->DefaultTextures,
            MaterialSRB,
            ShaderTextureIds);

    if (Status == RADIENT_STATUS_OK)
        Data.PublishSuccess(MaterialPSOFlags, std::move(MaterialSRB), std::move(ShaderTextureIds));
    else
        Data.PublishFailure(Status);
}

} // namespace Diligent
