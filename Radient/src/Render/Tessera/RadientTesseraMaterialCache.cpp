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
#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "RadientStandardMaterialParameters.h"
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
    if (Desc.ShadingModel == RADIENT_SURFACE_SHADING_MODEL_UNLIT)
        return PBR_Renderer::PSO_FLAG_USE_COLOR_MAP;

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

const char* GetSharedLitTextureParameterName(PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) noexcept
{
    switch (TextureAttribId)
    {
        case PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL:
            return RadientStandardMaterialNormalTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION:
            return RadientStandardMaterialOcclusionTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE:
            return RadientStandardMaterialEmissiveTextureName;
        default:
            return nullptr;
    }
}

const char* GetMetallicRoughnessTextureParameterName(PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) noexcept
{
    switch (TextureAttribId)
    {
        case PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR:
            return RadientStandardMaterialBaseColorTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC:
            return RadientStandardMaterialMetallicRoughnessTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT:
            return RadientStandardMaterialClearCoatTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS:
            return RadientStandardMaterialClearCoatRoughnessTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL:
            return RadientStandardMaterialClearCoatNormalTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_COLOR:
            return RadientStandardMaterialSheenColorTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS:
            return RadientStandardMaterialSheenRoughnessTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_ANISOTROPY:
            return RadientStandardMaterialAnisotropyTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE:
            return RadientStandardMaterialIridescenceTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS:
            return RadientStandardMaterialIridescenceThicknessTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_TRANSMISSION:
            return RadientStandardMaterialTransmissionTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_THICKNESS:
            return RadientStandardMaterialThicknessTextureName;
        default:
            return GetSharedLitTextureParameterName(TextureAttribId);
    }
}

const char* GetSpecularGlossinessTextureParameterName(PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) noexcept
{
    switch (TextureAttribId)
    {
        case PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR:
            return RadientStandardMaterialDiffuseTextureName;
        case PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC:
            return RadientStandardMaterialSpecularGlossinessTextureName;
        default:
            return GetSharedLitTextureParameterName(TextureAttribId);
    }
}

const char* GetUnlitTextureParameterName(PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) noexcept
{
    return TextureAttribId == PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR ?
        RadientStandardMaterialBaseColorTextureName :
        nullptr;
}

const char* GetStandardTextureParameterName(
    RADIENT_SURFACE_SHADING_MODEL   ShadingModel,
    PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) noexcept
{
    switch (ShadingModel)
    {
        case RADIENT_SURFACE_SHADING_MODEL_METALLIC_ROUGHNESS:
            return GetMetallicRoughnessTextureParameterName(TextureAttribId);
        case RADIENT_SURFACE_SHADING_MODEL_SPECULAR_GLOSSINESS:
            return GetSpecularGlossinessTextureParameterName(TextureAttribId);
        case RADIENT_SURFACE_SHADING_MODEL_UNLIT:
            return GetUnlitTextureParameterName(TextureAttribId);
        default:
            return nullptr;
    }
}

RADIENT_STATUS BuildMaterialTextureSRBSlots(
    const IRadientMaterialDefinitionAsset&      Definition,
    const RadientSurfaceMaterialDefinitionDesc& SurfaceDesc,
    const RadientMaterialAssetView&             MaterialView,
    PBR_Renderer::PSO_FLAGS                     PSOFlags,
    RadientMaterialTextureSRBSlotArray&         TextureSlots)
{
    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) {
            if (Status != RADIENT_STATUS_OK)
                return;

            const char* const pParameterName =
                GetStandardTextureParameterName(SurfaceDesc.ShadingModel, TextureAttribId);
            if (pParameterName == nullptr)
            {
                UNEXPECTED("PBR texture attribute ", Uint32{TextureAttribId},
                           " has no standard material parameter mapping");
                Status = RADIENT_STATUS_UNSUPPORTED;
                return;
            }

            RadientMaterialParameterHandle Parameter;
            Status = Definition.FindParameter(pParameterName, &Parameter);
            if (Status != RADIENT_STATUS_OK)
            {
                LOG_ERROR_MESSAGE("Surface material definition does not contain required texture parameter '",
                                  pParameterName, "'");
                Status = Status == RADIENT_STATUS_NOT_FOUND ? RADIENT_STATUS_UNSUPPORTED : Status;
                return;
            }

            const RadientMaterialTextureEntry* const pTexture =
                MaterialView.GetTexture(Parameter.Index);
            if (pTexture == nullptr || pTexture->pTexture == nullptr)
            {
                LOG_ERROR_MESSAGE("Material texture parameter '", pParameterName, "' is not initialized");
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            const RadientTextureViewType ViewType =
                PBR_Renderer::IsSRGBTextureAttribute(TextureAttribId) ?
                RadientTextureViewType::SRGB :
                RadientTextureViewType::Linear;
            const RadientTextureBindingIdentity BindingIdentity =
                RadientTextureAssetManager::GetTextureBindingIdentity(pTexture->pTexture, ViewType);
            if (!BindingIdentity)
            {
                const RADIENT_STATUS TextureStatus =
                    RadientTextureAssetManager::GetGPUResourceStatus(pTexture->pTexture);
                Status = TextureStatus == RADIENT_STATUS_OK ? RADIENT_STATUS_FAILED : TextureStatus;
                return;
            }

            RadientMaterialTextureSRBSlot& TextureSlot = TextureSlots[TextureAttribId];
            TextureSlot.pTexture                       = pTexture->pTexture;
            TextureSlot.ViewType                       = ViewType;
            TextureSlot.BindingIdentity                = BindingIdentity;
        });

    return Status;
}

} // namespace

RadientTesseraMaterialData::RadientTesseraMaterialData(IRadientMaterialAsset*          pMaterial,
                                                       const RadientMaterialAssetView& MaterialView) :
    m_pMaterial{pMaterial},
    m_MaterialView{MaterialView},
    m_UniqueID{s_NextUniqueID.fetch_add(1, std::memory_order_relaxed) + 1}
{
    m_ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
}

RADIENT_STATUS RadientTesseraMaterialData::GetGPUResourceStatus() const noexcept
{
    RADIENT_STATUS Status = GetStatus();
    // The material aggregate covers the texture assets exposed through
    // m_MaterialView; renderer defaults that are not in this recipe are irrelevant.
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
    std::shared_ptr<RadientMaterialSRBTable> pMaterialSRBTable;
    Uint32                                   MaterialTextureSlotCount = 0;
    PBR_Renderer::PSO_FLAGS                  EnabledMaterialPSOFlags  = PBR_Renderer::PSO_FLAG_NONE;
    RadientMaterialDefaultTextureBindings    DefaultTextures;
    RadientTesseraMaterialBuffer             MaterialBuffer;

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
        // The drawable path resolves only materials whose selected textures are
        // GPU-ready. Preserve the actual status if another caller violates this
        // precondition in a non-development build.
        const RADIENT_STATUS MaterialStatus =
            RadientMaterialAssetManager::GetGPUResourceStatus(pMaterial);
        VERIFY(MaterialStatus == RADIENT_STATUS_OK, "Tessera material resolution requires GPU-ready material dependencies");
        if (MaterialStatus != RADIENT_STATUS_OK)
            return {{}, MaterialStatus};

        const RadientMaterialAssetView MaterialView =
            RadientMaterialAssetManager::GetMaterialView(pMaterial);
        VERIFY(MaterialView, "GPU-ready material asset did not provide a material view");
        if (!MaterialView)
            return {{}, RADIENT_STATUS_INVALID_OPERATION};

        Data = m_MaterialData.GetOrInsert(pMaterial, pMaterial, MaterialView);
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
    IRadientMaterialAsset* const pMaterial = Data.m_MaterialView.pMaterial;
    if (pMaterial == nullptr)
    {
        Data.PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
        return;
    }

    IRadientMaterialDefinitionAsset* const pDefinitionInterface = pMaterial->GetDefinition();
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

    RefCntAutoPtr<IRadientSurfaceMaterialAsset> pSurfaceMaterial{pMaterial, IID_RadientSurfaceMaterialAsset};
    if (!pSurfaceMaterial)
    {
        Data.PublishFailure(RADIENT_STATUS_INVALID_OPERATION);
        return;
    }

    RadientMaterialSRBLease                       MaterialSRB;
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;
    RadientMaterialTextureSRBSlotArray            TextureSlots;

    RADIENT_STATUS Status = BuildMaterialTextureSRBSlots(
        *pDefinitionInterface,
        SurfaceDesc,
        Data.m_MaterialView,
        MaterialPSOFlags,
        TextureSlots);
    if (Status != RADIENT_STATUS_OK)
    {
        Data.PublishFailure(Status);
        return;
    }

    Status =
        pContext->pMaterialSRBTable->Acquire(
            TextureSlots,
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

    RefCntAutoPtr<RadientMaterialDefinitionImpl> pDefinition{pDefinitionInterface, IID_MaterialDefinitionImpl};
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
    pDefinition->WriteShaderData(*pMaterial, MaterialAttribs.data());

    RadientTesseraMaterialBufferAllocation MaterialBufferAllocation =
        pContext->MaterialBuffer.Allocate(MaterialAttribs.data(), MaterialAttribsSize);
    if (!MaterialBufferAllocation)
    {
        Data.PublishFailure(RADIENT_STATUS_FAILED);
        return;
    }

    Data.PublishSuccess(MaterialPSOFlags,
                        pSurfaceMaterial->GetSurfaceMode(),
                        pSurfaceMaterial->IsDoubleSided(),
                        std::move(MaterialSRB),
                        std::move(MaterialBufferAllocation),
                        std::move(ShaderTextureIds));
}

} // namespace Diligent
