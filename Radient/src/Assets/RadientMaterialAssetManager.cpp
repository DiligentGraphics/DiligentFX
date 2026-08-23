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
#include "Assets/RadientAssetURI.hpp"
#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "Assets/RadientMaterialInstanceImpl.hpp"
#include "Assets/RadientSurfaceMaterialInstanceImpl.hpp"
#include "DebugUtilities.hpp"

#include <exception>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_MaterialAssetImpl = {0x1a11a468, 0xbf30, 0x4c4d, {0xb8, 0xcd, 0x48, 0x89, 0xa4, 0x65, 0xa, 0x50}};

class MaterialStorage
{
public:
    MaterialStorage(IRadientMaterialInstance*                     pInstance,
                    RadientMaterialDetail::MaterialInstanceState& InstanceState) :
        m_pInstance{pInstance},
        m_InstanceState{InstanceState}
    {
        VERIFY_EXPR(m_pInstance != nullptr);
    }

    RADIENT_STATUS GetLoadStatus() const noexcept
    {
        return m_InstanceState.GetLoadStatus();
    }

    RADIENT_STATUS GetGPUResourceStatus() const noexcept
    {
        return m_InstanceState.GetGPUResourceStatus();
    }

    RefCntAutoPtr<IRadientMaterialInstance> GetInstance() const
    {
        return m_pInstance;
    }

    RadientMaterialAssetView GetMaterialView()
    {
        return m_InstanceState.GetMaterialView(m_pInstance);
    }

private:
    // Both members refer to the same material instance. m_pInstance keeps the
    // instance, and therefore m_InstanceState, alive for the asset's lifetime.
    RefCntAutoPtr<IRadientMaterialInstance>       m_pInstance;
    RadientMaterialDetail::MaterialInstanceState& m_InstanceState;
};

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

RADIENT_STATUS RadientMaterialAssetManager::CreateDefinition(const RadientMaterialDefinitionDesc& DefinitionDesc,
                                                             IRadientMaterialDefinitionAsset**    ppDefinition)
{
    return CreateDefinition(DefinitionDesc, {}, ppDefinition);
}

RADIENT_STATUS RadientMaterialAssetManager::CreateDefinition(
    const RadientMaterialDefinitionDesc&       DefinitionDesc,
    const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout,
    IRadientMaterialDefinitionAsset**          ppDefinition)
{
    if (ppDefinition == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppDefinition = nullptr;

    try
    {
        const RADIENT_STATUS ValidationStatus =
            RadientMaterialDetail::ValidateMaterialDefinitionDesc(DefinitionDesc);
        if (ValidationStatus != RADIENT_STATUS_OK)
            return ValidationStatus;

        const RADIENT_STATUS LayoutValidationStatus =
            RadientMaterialDetail::ValidateMaterialShaderDataLayout(DefinitionDesc, ShaderDataLayout);
        if (LayoutValidationStatus != RADIENT_STATUS_OK)
            return LayoutValidationStatus;

        RefCntAutoPtr<RadientMaterialDefinitionImpl> pDefinition{
            MakeNewRCObj<RadientMaterialDefinitionImpl>()(DefinitionDesc, ShaderDataLayout)};
        *ppDefinition = pDefinition.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material definition: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material definition: unknown exception");
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientMaterialAssetManager::CreateMaterial(
    IRadientMaterialInstance* pInstance,
    IRadientMaterialAsset**   ppMaterial)
{
    if (pInstance == nullptr || ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMaterial == nullptr, "Output material pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMaterial = nullptr;

    RadientMaterialDetail::MaterialInstanceState* pInstanceState =
        RadientMaterialDetail::TryGetMaterialInstanceState(pInstance);
    if (pInstanceState == nullptr)
        pInstanceState = RadientMaterialDetail::TryGetSurfaceMaterialInstanceState(pInstance);
    if (pInstanceState == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RefCntAutoPtr<MaterialPayloadImpl> pPayload = MaterialPayloadImpl::Create(pInstance, *pInstanceState);
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
