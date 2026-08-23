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

#include "Assets/RadientMaterialAssetManager.hpp"

#include "Assets/RadientAssetImpl.hpp"
#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientAssetURI.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "DebugUtilities.hpp"

#include <atomic>
#include <vector>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_MaterialAssetImpl = {0x1a11a468, 0xbf30, 0x4c4d, {0xb8, 0xcd, 0x48, 0x89, 0xa4, 0x65, 0xa, 0x50}};

struct MaterialTextureSource
{
    RadientMaterialParameterHandle      Parameter;
    Uint32                              ArrayIndex = 0;
    RefCntAutoPtr<IRadientTextureAsset> pRequestedTexture;
    RefCntAutoPtr<IRadientTextureAsset> pFallbackTexture;

    IRadientTextureAsset* GetRenderTexture() const noexcept
    {
        if (pRequestedTexture != nullptr &&
            RADIENT_SUCCEEDED(RadientTextureAssetManager::GetLoadStatus(pRequestedTexture)))
        {
            return pRequestedTexture;
        }

        return pFallbackTexture != nullptr ? pFallbackTexture.RawPtr() : pRequestedTexture.RawPtr();
    }
};

class MaterialStorage
{
public:
    RADIENT_STATUS Initialize(IRadientMaterialInstance* pInstance);
    RADIENT_STATUS GetLoadStatus() const noexcept;
    RADIENT_STATUS GetGPUResourceStatus() const noexcept;

    RefCntAutoPtr<IRadientMaterialInstance> GetInstance() const;
    RadientMaterialAssetView                GetMaterialView();

private:
    using TextureSourceArray = std::vector<MaterialTextureSource>;
    using TextureEntryArray  = std::vector<RadientMaterialTextureEntry>;
    using TextureIndexArray  = std::vector<Uint32>;

    RADIENT_STATUS GetTextureDependenciesStatus() const noexcept;
    RADIENT_STATUS GetTextureDependenciesGPUResourceStatus() const noexcept;
    RADIENT_STATUS FinalizeTextureSelection();

    RefCntAutoPtr<IRadientMaterialInstance> m_pInstance;
    bool                                    m_TextureSelectionReady = false;
    mutable std::atomic<RADIENT_STATUS>     m_LoadStatus{RADIENT_STATUS_OK};
    mutable std::atomic<RADIENT_STATUS>     m_GPUResourceStatus{RADIENT_STATUS_OK};

    TextureSourceArray m_TextureSources;
    TextureIndexArray  m_TextureIndexByParameter;

    // GetMaterialView() exposes this immutable array after the requested/fallback
    // texture selection has been finalized.
    TextureEntryArray m_TextureEntries;
};

RADIENT_STATUS MaterialStorage::Initialize(IRadientMaterialInstance* pInstance)
{
    if (pInstance == nullptr)
    {
        UNEXPECTED("Material instance must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (m_pInstance != nullptr)
    {
        UNEXPECTED("Material storage has already been initialized");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    m_pInstance = pInstance;

    IRadientMaterialDefinitionAsset* const pDefinition = m_pInstance->GetDefinition();
    if (pDefinition == nullptr)
    {
        UNEXPECTED("Material instance has no definition");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    const Uint32 ParameterCount = pDefinition->GetParameterCount();
    m_TextureSources.reserve(ParameterCount);
    m_TextureIndexByParameter.resize(ParameterCount, RadientMaterialAssetView::InvalidTextureIndex);
    bool HasTextureDependency = false;
    for (Uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
    {
        const RadientMaterialParameterDesc& ParameterDesc = pDefinition->GetParameterDesc(ParameterIndex);
        if (ParameterDesc.Type != RADIENT_MATERIAL_PARAMETER_TYPE_TEXTURE)
            continue;

        m_TextureIndexByParameter[ParameterIndex] = static_cast<Uint32>(m_TextureSources.size());

        RadientMaterialParameterHandle Handle;
        RADIENT_STATUS                 Status = pDefinition->GetParameterHandle(ParameterIndex, &Handle);
        if (Status != RADIENT_STATUS_OK)
            return Status;

        for (Uint32 ArrayIndex = 0; ArrayIndex < ParameterDesc.ArraySize; ++ArrayIndex)
        {
            MaterialTextureSource Source;
            Source.Parameter        = Handle;
            Source.ArrayIndex       = ArrayIndex;
            Source.pFallbackTexture = ParameterDesc.pDefaultTexture;

            Status = m_pInstance->GetTexture(Handle, ArrayIndex, Source.pRequestedTexture.GetAddressOfEmpty());
            if (Status != RADIENT_STATUS_OK)
                return Status;

            if (Source.pRequestedTexture != nullptr || Source.pFallbackTexture != nullptr)
                HasTextureDependency = true;

            m_TextureSources.push_back(std::move(Source));
        }
    }

    const RADIENT_STATUS InitStatus = HasTextureDependency ? RADIENT_STATUS_PENDING : RADIENT_STATUS_OK;
    m_LoadStatus.store(InitStatus, std::memory_order_release);
    m_GPUResourceStatus.store(InitStatus, std::memory_order_release);

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS MaterialStorage::GetLoadStatus() const noexcept
{
    RADIENT_STATUS Status = m_LoadStatus.load(std::memory_order_acquire);
    if (Status != RADIENT_STATUS_PENDING)
        return Status;

    Status = GetTextureDependenciesStatus();
    if (Status != RADIENT_STATUS_PENDING)
        m_LoadStatus.store(Status, std::memory_order_release);

    return Status;
}

RADIENT_STATUS MaterialStorage::GetTextureDependenciesStatus() const noexcept
{
    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    for (const MaterialTextureSource& Source : m_TextureSources)
    {
        IRadientTextureAsset* pTexture = Source.GetRenderTexture();
        if (pTexture == nullptr)
            continue;

        const RADIENT_STATUS TextureStatus = RadientTextureAssetManager::GetLoadStatus(pTexture);
        Status                             = CombineDependencyStatus(Status, TextureStatus);
    }

    return Status;
}

RADIENT_STATUS MaterialStorage::GetGPUResourceStatus() const noexcept
{
    const RADIENT_STATUS Status = GetLoadStatus();
    if (Status != RADIENT_STATUS_OK)
        return Status;

    RADIENT_STATUS GPUStatus = m_GPUResourceStatus.load(std::memory_order_acquire);
    if (GPUStatus != RADIENT_STATUS_PENDING)
        return GPUStatus;

    GPUStatus = GetTextureDependenciesGPUResourceStatus();
    if (GPUStatus != RADIENT_STATUS_PENDING)
        m_GPUResourceStatus.store(GPUStatus, std::memory_order_release);

    return GPUStatus;
}

RADIENT_STATUS MaterialStorage::GetTextureDependenciesGPUResourceStatus() const noexcept
{
    RADIENT_STATUS Status = RADIENT_STATUS_OK;

    for (const MaterialTextureSource& Source : m_TextureSources)
    {
        IRadientTextureAsset* pTexture = Source.GetRenderTexture();
        if (pTexture == nullptr)
            continue;

        const RADIENT_STATUS TextureStatus = RadientTextureAssetManager::GetGPUResourceStatus(pTexture);
        Status                             = CombineDependencyStatus(Status, TextureStatus);
    }

    return Status;
}

RefCntAutoPtr<IRadientMaterialInstance> MaterialStorage::GetInstance() const
{
    return m_pInstance;
}

// Requested and fallback textures are kept separately while asynchronous source
// loads are pending because the final choice is not known yet. Once every selected
// dependency has reached a terminal load state, freeze the requested-or-fallback
// choice before exposing render data or scheduling renderer-specific processing.
RADIENT_STATUS MaterialStorage::FinalizeTextureSelection()
{
    if (m_TextureSelectionReady)
        return RADIENT_STATUS_OK;

    const RADIENT_STATUS LoadStatus = GetLoadStatus();
    if (LoadStatus != RADIENT_STATUS_OK)
        return LoadStatus;

    TextureEntryArray TextureEntries;
    TextureEntries.reserve(m_TextureSources.size());

    // Keep definition-owned texture parameters consistent with TextureEntries.
    // Commit all fallback substitutions together as one instance version update.
    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    for (const MaterialTextureSource& Source : m_TextureSources)
    {
        IRadientTextureAsset* const  pSelectedTexture = Source.GetRenderTexture();
        RadientMaterialTextureEntry& Texture          = TextureEntries.emplace_back();
        Texture.ParameterIndex                        = Source.Parameter.Index;
        Texture.ArrayIndex                            = Source.ArrayIndex;
        Texture.pTexture                              = pSelectedTexture;

        if (pSelectedTexture == Source.pRequestedTexture)
            continue;

        if (!pWriter)
        {
            const RADIENT_STATUS Status = m_pInstance->CreateWriter(pWriter.GetAddressOfEmpty());
            if (Status != RADIENT_STATUS_OK)
                return Status;
        }

        const RADIENT_STATUS Status = pWriter->SetTexture(Source.Parameter, Source.ArrayIndex, pSelectedTexture);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    if (pWriter)
    {
        const RADIENT_STATUS Status = pWriter->Commit();
        if (RADIENT_FAILED(Status))
            return Status;
    }

    m_TextureEntries        = std::move(TextureEntries);
    m_TextureSelectionReady = true;
    return RADIENT_STATUS_OK;
}

RadientMaterialAssetView MaterialStorage::GetMaterialView()
{
    if (FinalizeTextureSelection() != RADIENT_STATUS_OK)
        return {};

    return {
        m_pInstance,
        m_TextureEntries.data(),
        static_cast<Uint32>(m_TextureEntries.size()),
        m_TextureIndexByParameter.data(),
        static_cast<Uint32>(m_TextureIndexByParameter.size()),
    };
}

class MaterialPayloadImpl final : public RadientAssetPayloadImpl<MaterialStorage, MaterialPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MaterialStorage, MaterialPayloadImpl>;
    using TBase::TBase;
};

using MaterialAssetImpl =
    RadientAssetImpl<IRadientMaterialAsset, IID_RadientMaterialAsset, IID_MaterialAssetImpl, RADIENT_ASSET_TYPE_MATERIAL, MaterialPayloadImpl>;

RADIENT_STATUS CreateMaterialAssetFromPayload(RefCntAutoPtr<MaterialPayloadImpl> pPayload,
                                              IRadientMaterialAsset**            ppMaterial)
{
    VERIFY_EXPR(pPayload != nullptr);
    VERIFY_EXPR(ppMaterial != nullptr);
    VERIFY_EXPR(*ppMaterial == nullptr);

    RefCntAutoPtr<MaterialAssetImpl> pMaterial =
        MaterialAssetImpl::Create(MakeRadientAssetURI("material"), std::move(pPayload));
    *ppMaterial = pMaterial.Detach();
    return RADIENT_STATUS_OK;
}

} // namespace

RadientMaterialAssetManager::~RadientMaterialAssetManager() = default;

RadientMaterialAssetManager::RadientMaterialAssetManager(const CreateInfo& CI) :
    m_DefaultTextures{CI.DefaultTextures}
{
}

RadientMaterialAssetManagerSharedPtr RadientMaterialAssetManager::Create(const CreateInfo& CI)
{
    return RadientMaterialAssetManagerSharedPtr{new RadientMaterialAssetManager{CI}};
}

RADIENT_STATUS RadientMaterialAssetManager::CreateMaterial(
    IRadientMaterialInstance* pInstance,
    IRadientMaterialAsset**   ppMaterial)
{
    if (pInstance == nullptr || ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMaterial == nullptr, "Output material pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMaterial = nullptr;

    RefCntAutoPtr<MaterialPayloadImpl> pPayload = MaterialPayloadImpl::Create();

    const RADIENT_STATUS InitStatus = pPayload->GetStorage().Initialize(pInstance);
    if (InitStatus != RADIENT_STATUS_OK)
        return InitStatus;

    return CreateMaterialAssetFromPayload(std::move(pPayload), ppMaterial);
}

RADIENT_STATUS RadientMaterialAssetManager::GetLoadStatus(IRadientAsset* pMaterial)
{
    return MaterialAssetImpl::GetLoadStatus(pMaterial);
}

RADIENT_STATUS RadientMaterialAssetManager::GetGPUResourceStatus(IRadientAsset* pMaterial)
{
    RefCntAutoPtr<MaterialAssetImpl> pImpl{pMaterial, IID_MaterialAssetImpl};
    if (!pImpl)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS PayloadStatus = pImpl->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return PayloadStatus;

    return pImpl->GetStorage().GetGPUResourceStatus();
}

RefCntAutoPtr<IRadientMaterialInstance> RadientMaterialAssetManager::GetInstance(IRadientMaterialAsset* pMaterial)
{
    RefCntAutoPtr<MaterialAssetImpl> pImpl = MaterialAssetImpl::ResolveAsset(pMaterial);
    return pImpl ? pImpl->GetStorage().GetInstance() : RefCntAutoPtr<IRadientMaterialInstance>{};
}

RadientMaterialAssetView RadientMaterialAssetManager::GetMaterialView(IRadientMaterialAsset* pMaterial)
{
    RefCntAutoPtr<MaterialAssetImpl> pImpl = MaterialAssetImpl::ResolveAsset(pMaterial);
    if (!pImpl)
        return {};

    return pImpl->GetStorage().GetMaterialView();
}

} // namespace Diligent
