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

#include "Radient/interface/RadientSceneWriter.h"

void RadientSceneWriter_C_TestMacros(IRadientSceneWriter* pWriter)
{
    RadientEntityID                  Entity           = 0;
    RadientEntityDesc                EntityDesc       = {0};
    RADIENT_ENTITY_FLAGS             EntityFlags      = 0;
    RadientTransform                 Transform        = {0};
    RadientCameraComponent           Camera           = {0};
    RadientMeshComponent             Mesh             = {0};
    RadientMeshRendererComponent     MeshRenderer     = {0};
    RadientMaterialBindingsComponent MaterialBindings = {0};
    RadientLightComponent            Light            = {0};
    RadientCustomComponentData       CustomComponent  = {0};
    RADIENT_STATUS                   Status           = RADIENT_STATUS_OK;

    Status = IRadientSceneWriter_CreateEntity(pWriter, &EntityDesc, &Entity);
    Status = IRadientSceneWriter_DestroyEntity(pWriter, Entity);
    Status = IRadientSceneWriter_SetEntityFlags(pWriter, Entity, EntityFlags);
    Status = IRadientSceneWriter_SetEntityOwnVisibility(pWriter, Entity, True);
    Status = IRadientSceneWriter_SetParent(pWriter, Entity, InvalidRadientEntityID, True);
    Status = IRadientSceneWriter_SetLocalTransform(pWriter, Entity, &Transform);
    Status = IRadientSceneWriter_SetCamera(pWriter, Entity, &Camera);
    Status = IRadientSceneWriter_SetMesh(pWriter, Entity, &Mesh);
    Status = IRadientSceneWriter_SetMeshRenderer(pWriter, Entity, &MeshRenderer);
    Status = IRadientSceneWriter_SetMaterialBindings(pWriter, Entity, &MaterialBindings);
    Status = IRadientSceneWriter_SetLight(pWriter, Entity, &Light);
    Status = IRadientSceneWriter_SetCustomComponentData(pWriter, Entity, &CustomComponent);
    Status = IRadientSceneWriter_RemoveComponent(pWriter, Entity, CustomComponent.ComponentType);
    Status = IRadientSceneWriter_CommitChanges(pWriter);

    (void)Status;
}
