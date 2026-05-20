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

#include "RadientScene.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

namespace Diligent
{

class RadientSceneImpl final : public ObjectBase<IRadientScene>
{
public:
    using TBase = ObjectBase<IRadientScene>;

    RadientSceneImpl(IReferenceCounters* pRefCounters);
    ~RadientSceneImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientScene, TBase)

    static RefCntAutoPtr<IRadientScene> Create();

    virtual const RadientSceneDesc& DILIGENT_CALL_TYPE GetDesc() const override final;

    virtual Bool DILIGENT_CALL_TYPE IsEntityAlive(RadientEntityID Entity) const override final;

    virtual Bool DILIGENT_CALL_TYPE GetEntityFlags(RadientEntityID       Entity,
                                                   RADIENT_ENTITY_FLAGS& Flags) const override final;

    virtual Bool DILIGENT_CALL_TYPE IsEntityVisible(RadientEntityID Entity) const override final;

    virtual RadientEntityID DILIGENT_CALL_TYPE GetParent(RadientEntityID Entity) const override final;

    virtual Uint32 DILIGENT_CALL_TYPE GetChildCount(RadientEntityID Entity) const override final;

    virtual Uint32 DILIGENT_CALL_TYPE GetChildren(RadientEntityID  Entity,
                                                  Uint32           StartChild,
                                                  Uint32           ChildCount,
                                                  RadientEntityID* pChildren) const override final;

    virtual Bool DILIGENT_CALL_TYPE GetLocalTransform(RadientEntityID   Entity,
                                                      RadientTransform& Transform) const override final;

    virtual Bool DILIGENT_CALL_TYPE GetWorldMatrix(RadientEntityID   Entity,
                                                   RadientMatrix4x4& Matrix) const override final;

    virtual Bool DILIGENT_CALL_TYPE HasComponent(RadientEntityID        Entity,
                                                 RADIENT_COMPONENT_TYPE Type,
                                                 RadientComponentTypeID CustomType) const override final;

    virtual RadientRevision DILIGENT_CALL_TYPE GetRevision() const override final;

private:
    RadientSceneDesc m_Desc;
};

} // namespace Diligent
