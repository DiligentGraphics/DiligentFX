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

#include "Animation/RadientSceneAnimationDestinationImpl.hpp"
#include "Animation/RadientSceneAnimationDestinationBindingImpl.hpp"
#include "Core/RadientValidation.hpp"
#include "Scene/RadientSceneWriterImpl.hpp"

#include "EngineMemory.h"
#include "RefCntAutoPtr.hpp"

#include <exception>

namespace Diligent
{

void RadientSceneAnimationDestinationImpl::QueryInterface(const INTERFACE_ID& IID,
                                                          IObject**           ppInterface)
{
    m_Writer.QueryInterface(IID, ppInterface);
}

ReferenceCounterValueType RadientSceneAnimationDestinationImpl::AddRef()
{
    return m_Writer.AddRef();
}

ReferenceCounterValueType RadientSceneAnimationDestinationImpl::Release()
{
    return m_Writer.Release();
}

IReferenceCounters* RadientSceneAnimationDestinationImpl::GetReferenceCounters() const
{
    return m_Writer.GetReferenceCounters();
}

RADIENT_STATUS RadientSceneAnimationDestinationImpl::CreateBinding(const RadientAnimationPropertyBindingDesc* pProperties,
                                                                   Uint32                                     PropertyCount,
                                                                   RadientAnimationResolvedPropertyDesc*      pResolvedProperties,
                                                                   IRadientAnimationDestinationBinding**      ppBinding)
{
    if (ppBinding == nullptr || *ppBinding != nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (PropertyCount == 0 || pProperties == nullptr || pResolvedProperties == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (!RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationPropertyBindingDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationResolvedPropertyDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(void*)))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (m_Writer.m_pState == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        RefCntAutoPtr<RadientSceneAnimationDestinationBindingImpl> pBinding{
            MakeNewRCObj<RadientSceneAnimationDestinationBindingImpl>()(
                static_cast<IRadientAnimationDestination*>(this),
                *m_Writer.m_pState)};

        const RADIENT_STATUS Status = pBinding->Initialize(
            pProperties, PropertyCount, pResolvedProperties);
        if (Status != RADIENT_STATUS_OK)
            return Status;

        *ppBinding = pBinding.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a scene animation binding: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create a scene animation binding due to an unknown error");
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
