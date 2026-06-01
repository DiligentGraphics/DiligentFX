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

#include "RadientSceneWriter.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <memory>

namespace Diligent
{

class RadientSceneImpl;
class RadientSceneState;

class RadientSceneWriterImpl final : public ObjectBase<IRadientSceneWriter>
{
public:
    using TBase = ObjectBase<IRadientSceneWriter>;

    RadientSceneWriterImpl(IReferenceCounters* pRefCounters, std::shared_ptr<RadientSceneState> pState);
    ~RadientSceneWriterImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSceneWriter, TBase)

    static RefCntAutoPtr<IRadientSceneWriter> Create(RadientSceneImpl* pScene);

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateEntity(const RadientEntityDesc& Desc,
                                                           RadientEntityID&        Entity) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE DestroyEntity(RadientEntityID Entity) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetEntityFlags(RadientEntityID      Entity,
                                                             RADIENT_ENTITY_FLAGS Flags) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetEntityOwnVisibility(RadientEntityID Entity,
                                                                     Bool            Visible) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetParent(RadientEntityID Entity,
                                                        RadientEntityID Parent,
                                                        Bool            KeepWorldTransform) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetLocalTransform(RadientEntityID         Entity,
                                                                const RadientTransform& Transform) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetCamera(RadientEntityID               Entity,
                                                        const RadientCameraComponent& Camera) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetMesh(RadientEntityID             Entity,
                                                      const RadientMeshComponent& Mesh) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetMeshRenderer(RadientEntityID                     Entity,
                                                              const RadientMeshRendererComponent& Renderer) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetMaterialBindings(RadientEntityID                         Entity,
                                                                  const RadientMaterialBindingsComponent& Bindings) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetLight(RadientEntityID              Entity,
                                                       const RadientLightComponent& Light) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetEnvironment(const RadientEnvironmentDesc& Environment) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetCustomComponentData(RadientEntityID                   Entity,
                                                                     const RadientCustomComponentData& Component) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE RemoveComponent(RadientEntityID        Entity,
                                                              RadientComponentTypeID ComponentType) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CommitChanges() override final;

private:
    std::shared_ptr<RadientSceneState> m_pState;
};

} // namespace Diligent
