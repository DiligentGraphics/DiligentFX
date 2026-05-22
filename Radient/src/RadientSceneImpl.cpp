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

#include "RadientSceneState.hpp"

namespace Diligent
{

RadientSceneImpl::RadientSceneImpl(IReferenceCounters* pRefCounters) :
    TBase{pRefCounters},
    m_pState{std::make_shared<RadientSceneState>()}
{}

RadientSceneImpl::~RadientSceneImpl()
{
}

RefCntAutoPtr<RadientSceneImpl> RadientSceneImpl::Create()
{
    return RefCntAutoPtr<RadientSceneImpl>{MakeNewRCObj<RadientSceneImpl>()()};
}

const RadientSceneDesc& RadientSceneImpl::GetDesc() const
{
    return m_pState->GetDesc();
}

RADIENT_STATUS RadientSceneImpl::IsEntityAlive(RadientEntityID Entity) const
{
    return m_pState->IsEntityAlive(Entity);
}

RADIENT_STATUS RadientSceneImpl::GetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS& Flags) const
{
    return m_pState->GetEntityFlags(Entity, Flags);
}

RADIENT_STATUS RadientSceneImpl::GetEntityOwnVisibility(RadientEntityID Entity, Bool& Visible) const
{
    return m_pState->GetEntityOwnVisibility(Entity, Visible);
}

RADIENT_STATUS RadientSceneImpl::GetEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible)
{
    return m_pState->GetEntityEffectiveVisibility(Entity, Visible);
}

RADIENT_STATUS RadientSceneImpl::GetCachedEntityEffectiveVisibility(RadientEntityID Entity, Bool& Visible) const
{
    return m_pState->GetCachedEntityEffectiveVisibility(Entity, Visible);
}

RADIENT_STATUS RadientSceneImpl::GetParent(RadientEntityID Entity, RadientEntityID& Parent) const
{
    return m_pState->GetParent(Entity, Parent);
}

RADIENT_STATUS RadientSceneImpl::GetChildCount(RadientEntityID Entity, Uint32& ChildCount) const
{
    return m_pState->GetChildCount(Entity, ChildCount);
}

RADIENT_STATUS RadientSceneImpl::GetChildren(RadientEntityID Entity, Uint32 StartChild, Uint32 ChildCount, RadientEntityID* pChildren, Uint32& NumChildrenWritten) const
{
    return m_pState->GetChildren(Entity, StartChild, ChildCount, pChildren, NumChildrenWritten);
}

RADIENT_STATUS RadientSceneImpl::GetLocalTransform(RadientEntityID Entity, RadientTransform& Transform) const
{
    return m_pState->GetLocalTransform(Entity, Transform);
}

RADIENT_STATUS RadientSceneImpl::GetWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix)
{
    return m_pState->GetWorldMatrix(Entity, Matrix);
}

RADIENT_STATUS RadientSceneImpl::GetCachedWorldMatrix(RadientEntityID Entity, RadientMatrix4x4& Matrix) const
{
    return m_pState->GetCachedWorldMatrix(Entity, Matrix);
}

RADIENT_STATUS RadientSceneImpl::HasComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType, Bool& HasComponent) const
{
    return m_pState->HasComponent(Entity, ComponentType, HasComponent);
}

RadientRevision RadientSceneImpl::GetRevision() const
{
    return m_pState->GetRevision();
}

} // namespace Diligent
