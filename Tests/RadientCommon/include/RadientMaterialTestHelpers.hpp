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

#include "RadientAssets.h"
#include "RadientMaterials.h"
#include "RefCntAutoPtr.hpp"

#include <utility>

namespace Diligent
{

namespace Testing
{

namespace Detail
{

template <typename ManagerType>
RADIENT_STATUS CreateStandardMaterialDefinitionAndInstance(
    ManagerType&                                       Manager,
    const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
    RefCntAutoPtr<IRadientMaterialDefinitionAsset>&    pDefinition,
    RefCntAutoPtr<IRadientMaterialInstance>&           pInstance)
{
    RADIENT_STATUS Status =
        Manager.CreateStandardMaterialDefinition(DefinitionCI, pDefinition.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    return pDefinition->CreateInstance(pInstance.GetAddressOfEmpty());
}

} // namespace Detail

template <typename ManagerType>
RADIENT_STATUS CreateStandardMaterialInstance(
    ManagerType&                                       Manager,
    const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
    IRadientMaterialInstance**                         ppInstance)
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    RefCntAutoPtr<IRadientMaterialInstance>        pInstance;
    const RADIENT_STATUS                           Status = Detail::CreateStandardMaterialDefinitionAndInstance(
        Manager, DefinitionCI, pDefinition, pInstance);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    *ppInstance = pInstance.Detach();
    return RADIENT_STATUS_OK;
}

// Creates a standard instance, invokes Initialize with its definition and a
// writer, and commits the writer before returning the instance. Initialize may
// return RADIENT_STATUS_NO_CHANGE; every other non-OK status aborts the
// operation.
template <typename ManagerType, typename InitializeType>
RADIENT_STATUS CreateStandardMaterialInstance(
    ManagerType&                                       Manager,
    const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
    InitializeType&&                                   Initialize,
    IRadientMaterialInstance**                         ppInstance)
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> pDefinition;
    RefCntAutoPtr<IRadientMaterialInstance>        pInstance;
    RADIENT_STATUS                                 Status = Detail::CreateStandardMaterialDefinitionAndInstance(
        Manager, DefinitionCI, pDefinition, pInstance);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    RefCntAutoPtr<IRadientMaterialInstanceWriter> pWriter;
    Status = pInstance->CreateWriter(pWriter.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    Status = std::forward<InitializeType>(Initialize)(*pDefinition, *pWriter);
    if (Status != RADIENT_STATUS_OK && Status != RADIENT_STATUS_NO_CHANGE)
        return Status;

    Status = pWriter->Commit();
    if (Status != RADIENT_STATUS_OK && Status != RADIENT_STATUS_NO_CHANGE)
        return Status;

    *ppInstance = pInstance.Detach();
    return RADIENT_STATUS_OK;
}

// Creates an asset that retains a default-initialized standard instance.
template <typename ManagerType>
RADIENT_STATUS CreateStandardMaterialAsset(
    ManagerType&                                       Manager,
    const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
    IRadientMaterialAsset**                            ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMaterial = nullptr;

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    const RADIENT_STATUS                    Status = CreateStandardMaterialInstance(
        Manager, DefinitionCI, pInstance.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    return Manager.CreateMaterial(pInstance, ppMaterial);
}

// Creates and initializes a standard instance, then registers it as an asset.
template <typename ManagerType, typename InitializeType>
RADIENT_STATUS CreateStandardMaterialAsset(
    ManagerType&                                       Manager,
    const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
    InitializeType&&                                   Initialize,
    IRadientMaterialAsset**                            ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppMaterial = nullptr;

    RefCntAutoPtr<IRadientMaterialInstance> pInstance;
    const RADIENT_STATUS                    Status = CreateStandardMaterialInstance(
        Manager,
        DefinitionCI,
        std::forward<InitializeType>(Initialize),
        pInstance.GetAddressOfEmpty());
    if (Status != RADIENT_STATUS_OK)
        return Status;

    return Manager.CreateMaterial(pInstance, ppMaterial);
}

} // namespace Testing

} // namespace Diligent
