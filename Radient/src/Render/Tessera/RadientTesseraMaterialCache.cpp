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

#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientMaterialImpl.hpp"
#include "ThreadPool.hpp"

#include <exception>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

std::atomic<UniqueIdentifier> s_NextUniqueID{0};

PBR_Renderer::PSO_FLAGS GetSurfaceMaterialPSOFlags(const RadientSurfaceMaterialDefinitionDesc& Desc) noexcept
{
    const auto HasFeature = [&Desc](RADIENT_SURFACE_MATERIAL_FEATURE_FLAGS Feature) {
        return (Desc.Features & Feature) != RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_NONE;
    };

    // The unlit shading model is encoded in the material shader data workflow.
    // PSO_FLAG_UNSHADED bypasses material evaluation entirely and is reserved
    // for renderer overrides such as wireframe rendering.
    PBR_Renderer::PSO_FLAGS Flags = PBR_Renderer::PSO_FLAG_DEFAULT_TEXTURES;

    if (HasFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_CLEAR_COAT))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_CLEAR_COAT;
    if (HasFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_SHEEN))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_SHEEN;
    if (HasFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_ANISOTROPY))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_ANISOTROPY;
    if (HasFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_IRIDESCENCE))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_IRIDESCENCE;
    if (HasFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_TRANSMISSION))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_TRANSMISSION;
    if (HasFeature(RADIENT_SURFACE_MATERIAL_FEATURE_FLAG_VOLUME))
        Flags |= PBR_Renderer::PSO_FLAG_ALL_VOLUME;
    return Flags;
}

} // namespace

RadientTesseraMaterialData::RadientTesseraMaterialData(IRadientMaterialAsset*           pMaterial,
                                                       const RadientMaterialRenderData& MaterialData) :
    m_pMaterial{pMaterial},
    m_MaterialData{MaterialData},
    m_UniqueID{s_NextUniqueID.fetch_add(1, std::memory_order_relaxed) + 1}
{
    m_ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
}

RADIENT_STATUS RadientTesseraMaterialData::GetGPUResourceStatus() const noexcept
{
    RADIENT_STATUS Status = GetStatus();
    // The material aggregate covers the texture assets exposed through
    // m_MaterialData; renderer defaults that are not in this recipe are irrelevant.
    Status = CombineDependencyStatus(
        Status,
        RadientMaterialAssetManager::GetGPUResourceStatus(m_pMaterial));

    if (Status != RADIENT_STATUS_OK)
        return Status;

    IShaderResourceBinding* const pSRB = m_MaterialSRB.GetSRB();
    if (pSRB == nullptr)
        return RADIENT_STATUS_PENDING;

    // An SRB may intentionally retain the previous global material buffer
    // while a replacement is pending. Its generation records exactly how many
    // material allocations that bound buffer contains.
    return m_MaterialBufferAllocation.IsUploadedThrough(m_MaterialSRB.GetMaterialBufferGeneration()) ?
        RADIENT_STATUS_OK :
        RADIENT_STATUS_PENDING;
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
    RADIENT_MATERIAL_SURFACE_MODE                 SurfaceMode,
    Bool                                          IsDoubleSided,
    RadientMaterialSRBLease                       MaterialSRB,
    RadientTesseraMaterialBufferAllocation        MaterialBufferAllocation,
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds) noexcept
{
    VERIFY_EXPR(MaterialSRB);
    VERIFY_EXPR(MaterialBufferAllocation);
    m_MaterialPSOFlags         = MaterialPSOFlags;
    m_SurfaceMode              = SurfaceMode;
    m_IsDoubleSided            = IsDoubleSided;
    m_MaterialSRB              = std::move(MaterialSRB);
    m_MaterialBufferAllocation = std::move(MaterialBufferAllocation);
    m_ShaderTextureIds         = std::move(ShaderTextureIds);

    m_Status.store(RADIENT_STATUS_OK, std::memory_order_release);
}

void RadientTesseraMaterialData::PublishFailure(RADIENT_STATUS Status) noexcept
{
    VERIFY_EXPR(RADIENT_FAILED(Status));
    m_Status.store(Status, std::memory_order_release);
}

struct RadientTesseraMaterialCache::ProcessingContext
{
    std::shared_ptr<RadientMaterialSRBTable>               pMaterialSRBTable;
    std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> TextureAttribIndices{};
    Uint32                                                 MaterialTextureSlotCount = 0;
    PBR_Renderer::PSO_FLAGS                                EnabledMaterialPSOFlags  = PBR_Renderer::PSO_FLAG_NONE;
    RadientMaterialDefaultTextureBindings                  DefaultTextures;
    RadientTesseraMaterialBuffer                           MaterialBuffer;

    ProcessingContext(Uint32 ConstantBufferOffsetAlignment,
                      Uint32 MaxMaterialAttribsSize) :
        MaterialBuffer{{ConstantBufferOffsetAlignment, MaxMaterialAttribsSize}}
    {}
};

RadientTesseraMaterialCache::RadientTesseraMaterialCache(const CreateInfo& CI) :
    m_pProcessingContext{std::make_shared<ProcessingContext>(CI.ConstantBufferOffsetAlignment,
                                                             CI.MaxMaterialAttribsSize)}
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
                        Data->PublishFailure(RADIENT_STATUS_FAILED);
                    }
                    catch (...)
                    {
                        LOG_ERROR_MESSAGE("Failed to process Tessera material data");
                        Data->PublishFailure(RADIENT_STATUS_FAILED);
                    }
                    return ASYNC_TASK_STATUS_COMPLETE;
                });

        if (!ThreadPool.EnqueueTask(pTask))
            Data->PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
    }

    const RADIENT_STATUS Status = Data->GetStatus();
    return {std::move(Data), Status};
}

RADIENT_STATUS RadientTesseraMaterialCache::PrepareMaterialBuffer(IRenderDevice*  pDevice,
                                                                  IDeviceContext* pContext)
{
    return m_pProcessingContext->MaterialBuffer.Prepare(pDevice, pContext);
}

RADIENT_STATUS RadientTesseraMaterialCache::Prepare(
    Uint32                               TextureVersion,
    const ResolveTextureSRVCallbackType& ResolveTextureSRV,
    const CreateSRBCallbackType&         CreateSRB)
{
    const RadientTesseraMaterialBuffer& MaterialBuffer = m_pProcessingContext->MaterialBuffer;
    return Prepare(
        TextureVersion,
        MaterialBuffer.GetVersion(),
        MaterialBuffer.GetUploadedGeneration(),
        ResolveTextureSRV,
        CreateSRB);
}


RADIENT_STATUS RadientTesseraMaterialCache::Prepare(
    Uint32                               TextureVersion,
    Uint32                               MaterialBufferVersion,
    Uint64                               MaterialBufferGeneration,
    const ResolveTextureSRVCallbackType& ResolveTextureSRV,
    const CreateSRBCallbackType&         CreateSRB)
{
    return m_pProcessingContext->pMaterialSRBTable->Prepare(
        TextureVersion,
        MaterialBufferVersion,
        MaterialBufferGeneration,
        ResolveTextureSRV,
        CreateSRB);
}

IBuffer* RadientTesseraMaterialCache::GetMaterialBuffer() const noexcept
{
    return m_pProcessingContext->MaterialBuffer.GetBuffer();
}

Uint32 RadientTesseraMaterialCache::GetMaxMaterialAttribsSize() const noexcept
{
    return m_pProcessingContext->MaterialBuffer.GetMaxMaterialAttribsSize();
}

void RadientTesseraMaterialCache::ProcessMaterial(
    const std::shared_ptr<ProcessingContext>& pContext,
    RadientTesseraMaterialData&               Data)
{
    RefCntAutoPtr<IRadientMaterialInstance> pInstance = RadientMaterialAssetManager::GetInstance(Data.m_pMaterial);
    if (!pInstance)
    {
        Data.PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
        return;
    }

    IRadientMaterialDefinition* const pDefinitionInterface = pInstance->GetDefinition();
    if (pDefinitionInterface == nullptr ||
        pDefinitionInterface->GetDesc().Type != RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE)
    {
        Data.PublishFailure(RADIENT_STATUS_UNSUPPORTED);
        return;
    }

    const auto& SurfaceDesc =
        static_cast<const RadientSurfaceMaterialDefinitionDesc&>(pDefinitionInterface->GetDesc());
    const PBR_Renderer::PSO_FLAGS MaterialPSOFlags =
        GetSurfaceMaterialPSOFlags(SurfaceDesc) & pContext->EnabledMaterialPSOFlags;

    RefCntAutoPtr<IRadientSurfaceMaterialInstance> pSurfaceInstance{pInstance, IID_RadientSurfaceMaterialInstance};
    if (!pSurfaceInstance)
    {
        Data.PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
        return;
    }

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

    if (Status != RADIENT_STATUS_OK)
    {
        Data.PublishFailure(Status);
        return;
    }

    const auto* const pDefinition =
        static_cast<const RadientMaterialDefinitionImpl*>(pDefinitionInterface);
    if (pDefinition == nullptr)
    {
        Data.PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
        return;
    }

    const Uint32 MaterialAttribsSize = pDefinition->GetShaderDataSize();
    if (MaterialAttribsSize == 0 ||
        MaterialAttribsSize > pContext->MaterialBuffer.GetMaxMaterialAttribsSize())
    {
        Data.PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
        return;
    }

    std::vector<Uint8> MaterialAttribs(MaterialAttribsSize);
    pDefinition->WriteShaderData(*pInstance, MaterialAttribs.data());

    RadientTesseraMaterialBufferAllocation MaterialBufferAllocation =
        pContext->MaterialBuffer.Allocate(MaterialAttribs.data(), MaterialAttribsSize);
    if (!MaterialBufferAllocation)
    {
        Data.PublishFailure(RADIENT_STATUS_FAILED);
        return;
    }

    Data.PublishSuccess(MaterialPSOFlags,
                        pSurfaceInstance->GetSurfaceMode(),
                        pSurfaceInstance->IsDoubleSided(),
                        std::move(MaterialSRB),
                        std::move(MaterialBufferAllocation),
                        std::move(ShaderTextureIds));
}

} // namespace Diligent
