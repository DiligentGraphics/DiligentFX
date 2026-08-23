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

#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "Assets/RadientMaterialStorage.hpp"
#include "DebugUtilities.hpp"

#include <exception>

namespace Diligent
{

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
    IRadientMaterialDefinitionAsset* pDefinition,
    IRadientMaterialAsset**          ppMaterial)
{
    if (pDefinition == nullptr || ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMaterial == nullptr, "Output material pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMaterial = nullptr;

    RefCntAutoPtr<RadientMaterialDefinitionImpl> pDefinitionImpl{
        pDefinition, IID_MaterialDefinitionImpl};
    if (pDefinitionImpl == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS DefinitionStatus = pDefinitionImpl->GetStatus();
    if (DefinitionStatus != RADIENT_STATUS_OK)
        return DefinitionStatus;

    try
    {
        RefCntAutoPtr<IRadientMaterialAsset> pMaterial = pDefinitionImpl->CreateAsset();
        if (pMaterial == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        *ppMaterial = pMaterial.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material asset: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material asset: unknown exception");
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientMaterialAssetManager::GetLoadStatus(IRadientAsset* pMaterial)
{
    const RadientMaterialDetail::MaterialStorage* const pStorage =
        RadientMaterialDetail::TryGetMaterialStorage(pMaterial);
    return pStorage != nullptr ? pStorage->GetLoadStatus() : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientMaterialAssetManager::GetGPUResourceStatus(IRadientAsset* pMaterial)
{
    const RadientMaterialDetail::MaterialStorage* const pStorage =
        RadientMaterialDetail::TryGetMaterialStorage(pMaterial);
    return pStorage != nullptr ? pStorage->GetGPUResourceStatus() : RADIENT_STATUS_INVALID_ARGUMENT;
}

RadientMaterialAssetView RadientMaterialAssetManager::GetMaterialView(IRadientMaterialAsset* pMaterial)
{
    RadientMaterialDetail::MaterialStorage* const pStorage =
        RadientMaterialDetail::TryGetMaterialStorage(pMaterial);
    return pStorage != nullptr ? pStorage->GetMaterialView(pMaterial) : RadientMaterialAssetView{};
}

} // namespace Diligent
