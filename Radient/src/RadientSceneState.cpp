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

#include "RadientSceneState.hpp"

namespace Diligent
{

const RadientSceneDesc& RadientSceneState::GetDesc() const
{
    static const RadientSceneDesc DefaultDesc{};
    return DefaultDesc;
}

RADIENT_STATUS RadientSceneState::IsEntityAlive(RadientEntityID Entity) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS& Flags) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::IsEntityVisible(RadientEntityID Entity, Bool& Visible) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetParent(RadientEntityID Entity, RadientEntityID& Parent) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetChildCount(RadientEntityID Entity, Uint32& ChildCount) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetChildren(RadientEntityID Entity, Uint32 StartChild, Uint32 ChildCount, RadientEntityID* pChildren, Uint32& NumChildrenWritten) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetLocalTransform(RadientEntityID Entity, RadientTransform& Transform) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::GetWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix) const
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::HasComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType, Bool& HasComponent) const
{
    return RADIENT_STATUS_OK;
}

RadientRevision RadientSceneState::GetRevision() const
{
    return 0;
}

RADIENT_STATUS RadientSceneState::CreateEntity(const RadientEntityDesc& Desc, RadientEntityID& Entity)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::DestroyEntity(RadientEntityID Entity)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetEntityVisible(RadientEntityID Entity, Bool Visible)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetParent(RadientEntityID Entity, RadientEntityID Parent, Bool KeepWorldTransform)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetCamera(RadientEntityID Entity, const RadientCameraComponent& Camera)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetMesh(RadientEntityID Entity, const RadientMeshComponent& Mesh)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetMeshRenderer(RadientEntityID Entity, const RadientMeshRendererComponent& Renderer)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetLight(RadientEntityID Entity, const RadientLightComponent& Light)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::SetCustomComponentData(RadientEntityID Entity, const RadientCustomComponentData& Component)
{
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSceneState::RemoveComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType)
{
    return RADIENT_STATUS_NO_CHANGE;
}

RADIENT_STATUS RadientSceneState::CommitChanges()
{
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
