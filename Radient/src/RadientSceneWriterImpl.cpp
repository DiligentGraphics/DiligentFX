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

#include "RadientSceneWriterImpl.hpp"

#include "RadientSceneImpl.hpp"
#include "RadientSceneState.hpp"

#include <utility>

namespace Diligent
{

RadientSceneWriterImpl::RadientSceneWriterImpl(IReferenceCounters* pRefCounters, std::shared_ptr<RadientSceneState> pState) :
    TBase{pRefCounters},
    m_pState{std::move(pState)}
{}

RadientSceneWriterImpl::~RadientSceneWriterImpl()
{
}

RefCntAutoPtr<IRadientSceneWriter> RadientSceneWriterImpl::Create(RadientSceneImpl* pScene)
{
    return RefCntAutoPtr<RadientSceneWriterImpl>{MakeNewRCObj<RadientSceneWriterImpl>()(pScene != nullptr ? pScene->m_pState : nullptr)};
}

RADIENT_STATUS RadientSceneWriterImpl::CreateEntity(const RadientEntityDesc& Desc, RadientEntityID& Entity)
{
    Entity = InvalidRadientEntityID;
    return m_pState ? m_pState->CreateEntity(Desc, Entity) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::DestroyEntity(RadientEntityID Entity)
{
    return m_pState ? m_pState->DestroyEntity(Entity) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags)
{
    return m_pState ? m_pState->SetEntityFlags(Entity, Flags) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetEntityOwnVisibility(RadientEntityID Entity, Bool Visible)
{
    return m_pState ? m_pState->SetEntityOwnVisibility(Entity, Visible) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetParent(RadientEntityID Entity, RadientEntityID Parent, Bool KeepWorldTransform)
{
    return m_pState ? m_pState->SetParent(Entity, Parent, KeepWorldTransform) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform)
{
    return m_pState ? m_pState->SetLocalTransform(Entity, Transform) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetCamera(RadientEntityID Entity, const RadientCameraComponent& Camera)
{
    return m_pState ? m_pState->SetCamera(Entity, Camera) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetMesh(RadientEntityID Entity, const RadientMeshComponent& Mesh)
{
    return m_pState ? m_pState->SetMesh(Entity, Mesh) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetMeshRenderer(RadientEntityID Entity, const RadientMeshRendererComponent& Renderer)
{
    return m_pState ? m_pState->SetMeshRenderer(Entity, Renderer) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetMaterialBindings(RadientEntityID Entity, const RadientMaterialBindingsComponent& Bindings)
{
    return m_pState ? m_pState->SetMaterialBindings(Entity, Bindings) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetLight(RadientEntityID Entity, const RadientLightComponent& Light)
{
    return m_pState ? m_pState->SetLight(Entity, Light) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetEnvironment(const RadientEnvironmentDesc& Environment)
{
    return m_pState ? m_pState->SetEnvironment(Environment) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::SetCustomComponentData(RadientEntityID Entity, const RadientCustomComponentData& Component)
{
    return m_pState ? m_pState->SetCustomComponentData(Entity, Component) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::RemoveComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType)
{
    return m_pState ? m_pState->RemoveComponent(Entity, ComponentType) : RADIENT_STATUS_INVALID_ARGUMENT;
}

RADIENT_STATUS RadientSceneWriterImpl::CommitChanges()
{
    return m_pState ? m_pState->CommitChanges() : RADIENT_STATUS_INVALID_ARGUMENT;
}

} // namespace Diligent
