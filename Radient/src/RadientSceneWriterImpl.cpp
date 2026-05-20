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

namespace Diligent
{

RadientSceneWriterImpl::RadientSceneWriterImpl(IReferenceCounters* pRefCounters, IRadientScene* pScene) :
    TBase{pRefCounters},
    m_pScene{pScene}
{}

RadientSceneWriterImpl::~RadientSceneWriterImpl()
{
}

RefCntAutoPtr<IRadientSceneWriter> RadientSceneWriterImpl::Create(IRadientScene* pScene)
{
    return RefCntAutoPtr<RadientSceneWriterImpl>{MakeNewRCObj<RadientSceneWriterImpl>()(pScene)};
}

RadientEntityID RadientSceneWriterImpl::CreateEntity(const RadientEntityDesc& Desc)
{
    (void)Desc;
    return InvalidRadientEntityID;
}

void RadientSceneWriterImpl::DestroyEntity(RadientEntityID Entity)
{
    (void)Entity;
}

void RadientSceneWriterImpl::SetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS Flags)
{
    (void)Entity;
    (void)Flags;
}

void RadientSceneWriterImpl::SetEntityVisible(RadientEntityID Entity, Bool Visible)
{
    (void)Entity;
    (void)Visible;
}

void RadientSceneWriterImpl::SetParent(RadientEntityID Entity, RadientEntityID Parent, Bool KeepWorldTransform)
{
    (void)Entity;
    (void)Parent;
    (void)KeepWorldTransform;
}

void RadientSceneWriterImpl::SetLocalTransform(RadientEntityID Entity, const RadientTransform& Transform)
{
    (void)Entity;
    (void)Transform;
}

void RadientSceneWriterImpl::SetCamera(RadientEntityID Entity, const RadientCameraComponent& Camera)
{
    (void)Entity;
    (void)Camera;
}

void RadientSceneWriterImpl::SetMesh(RadientEntityID Entity, const RadientMeshComponent& Mesh)
{
    (void)Entity;
    (void)Mesh;
}

void RadientSceneWriterImpl::SetMeshRenderer(RadientEntityID Entity, const RadientMeshRendererComponent& Renderer)
{
    (void)Entity;
    (void)Renderer;
}

void RadientSceneWriterImpl::SetLight(RadientEntityID Entity, const RadientLightComponent& Light)
{
    (void)Entity;
    (void)Light;
}

void RadientSceneWriterImpl::SetCustomComponentData(RadientEntityID Entity, const RadientCustomComponentData& Component)
{
    (void)Entity;
    (void)Component;
}

void RadientSceneWriterImpl::RemoveComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType)
{
    (void)Entity;
    (void)ComponentType;
}

void RadientSceneWriterImpl::CommitChanges()
{
}

} // namespace Diligent
