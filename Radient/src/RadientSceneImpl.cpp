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

#include "RadientSceneImpl.hpp"

namespace Diligent
{

RadientSceneImpl::RadientSceneImpl(IReferenceCounters* pRefCounters) :
    TBase{pRefCounters}
{}

RadientSceneImpl::~RadientSceneImpl()
{
}

RefCntAutoPtr<IRadientScene> RadientSceneImpl::Create()
{
    return RefCntAutoPtr<RadientSceneImpl>{MakeNewRCObj<RadientSceneImpl>()()};
}

const RadientSceneDesc& RadientSceneImpl::GetDesc() const
{
    return m_Desc;
}

RadientEntityID RadientSceneImpl::CreateEntity(const RadientEntityDesc& Desc)
{
    (void)Desc;
    return InvalidRadientEntityID;
}

void RadientSceneImpl::DestroyEntity(RadientEntityID Entity)
{
    (void)Entity;
}

Bool RadientSceneImpl::IsEntityAlive(RadientEntityID Entity) const
{
    (void)Entity;
    return False;
}

void RadientSceneImpl::SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags)
{
    (void)Entity;
    (void)Flags;
}

Bool RadientSceneImpl::GetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS& Flags) const
{
    (void)Entity;
    Flags = RADIENT_ENTITY_FLAG_NONE;
    return False;
}

void RadientSceneImpl::SetEntityVisible(RadientEntityID Entity, Bool Visible)
{
    (void)Entity;
    (void)Visible;
}

Bool RadientSceneImpl::IsEntityVisible(RadientEntityID Entity) const
{
    (void)Entity;
    return False;
}

void RadientSceneImpl::SetParent(RadientEntityID Entity, RadientEntityID Parent, Bool KeepWorldTransform)
{
    (void)Entity;
    (void)Parent;
    (void)KeepWorldTransform;
}

RadientEntityID RadientSceneImpl::GetParent(RadientEntityID Entity) const
{
    (void)Entity;
    return InvalidRadientEntityID;
}

void RadientSceneImpl::SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform)
{
    (void)Entity;
    (void)Transform;
}

Bool RadientSceneImpl::GetLocalTransform(RadientEntityID Entity, RadientTransform& Transform) const
{
    (void)Entity;
    Transform = {};
    return False;
}

Bool RadientSceneImpl::GetWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix) const
{
    (void)Entity;
    Matrix = {};
    return False;
}

void RadientSceneImpl::SetCamera(RadientEntityID Entity, const RadientCameraComponent& Camera)
{
    (void)Entity;
    (void)Camera;
}

void RadientSceneImpl::SetMesh(RadientEntityID Entity, const RadientMeshComponent& Mesh)
{
    (void)Entity;
    (void)Mesh;
}

void RadientSceneImpl::SetMeshRenderer(RadientEntityID Entity, const RadientMeshRendererComponent& Renderer)
{
    (void)Entity;
    (void)Renderer;
}

void RadientSceneImpl::SetLight(RadientEntityID Entity, const RadientLightComponent& Light)
{
    (void)Entity;
    (void)Light;
}

void RadientSceneImpl::SetCustomComponentData(RadientEntityID Entity, const RadientCustomComponentData& Component)
{
    (void)Entity;
    (void)Component;
}

void RadientSceneImpl::RemoveComponent(RadientEntityID Entity, RADIENT_COMPONENT_TYPE Type, RadientComponentTypeID CustomType)
{
    (void)Entity;
    (void)Type;
    (void)CustomType;
}

Bool RadientSceneImpl::HasComponent(RadientEntityID Entity, RADIENT_COMPONENT_TYPE Type, RadientComponentTypeID CustomType) const
{
    (void)Entity;
    (void)Type;
    (void)CustomType;
    return False;
}

RadientRevision RadientSceneImpl::GetRevision() const
{
    return 0;
}

void RadientSceneImpl::CommitChanges()
{
}

} // namespace Diligent
