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

Bool RadientSceneImpl::IsEntityAlive(RadientEntityID Entity) const
{
    (void)Entity;
    return False;
}

Bool RadientSceneImpl::GetEntityFlags(RadientEntityID Entity, RADIENT_ENTITY_FLAGS& Flags) const
{
    (void)Entity;
    Flags = RADIENT_ENTITY_FLAG_NONE;
    return False;
}

Bool RadientSceneImpl::IsEntityVisible(RadientEntityID Entity) const
{
    (void)Entity;
    return False;
}

RadientEntityID RadientSceneImpl::GetParent(RadientEntityID Entity) const
{
    (void)Entity;
    return InvalidRadientEntityID;
}

Uint32 RadientSceneImpl::GetChildCount(RadientEntityID Entity) const
{
    (void)Entity;
    return 0;
}

Uint32 RadientSceneImpl::GetChildren(RadientEntityID Entity, Uint32 StartChild, Uint32 ChildCount, RadientEntityID* pChildren) const
{
    (void)Entity;
    (void)StartChild;
    (void)ChildCount;
    (void)pChildren;
    return 0;
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

Bool RadientSceneImpl::HasComponent(RadientEntityID Entity, RadientComponentTypeID ComponentType) const
{
    (void)Entity;
    (void)ComponentType;
    return False;
}

RadientRevision RadientSceneImpl::GetRevision() const
{
    return 0;
}

} // namespace Diligent
