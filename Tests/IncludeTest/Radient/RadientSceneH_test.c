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

#include "Radient/interface/RadientScene.h"

void RadientScene_C_UseCameraComponent(void)
{
    RadientCameraComponent Camera;

    Camera.Projection               = RADIENT_CAMERA_PROJECTION_PERSPECTIVE;
    Camera.HorizontalAperture       = 2.0955f;
    Camera.VerticalAperture         = 1.52908f;
    Camera.HorizontalApertureOffset = 0.f;
    Camera.VerticalApertureOffset   = 0.f;
    Camera.FocalLength              = 5.f;
    Camera.ClippingRange.x          = 0.1f;
    Camera.ClippingRange.y          = 1000.f;
    Camera.FStop                    = 0.f;
    Camera.FocusDistance            = 0.f;

    (void)Camera;
}

void RadientScene_C_UseLightComponent(void)
{
    RadientLightComponent Light;

    Light.Type                   = RADIENT_LIGHT_TYPE_DIRECTIONAL;
    Light.Color.x                = 1.f;
    Light.Color.y                = 1.f;
    Light.Color.z                = 1.f;
    Light.Intensity              = 1.f;
    Light.Exposure               = 0.f;
    Light.Diffuse                = 1.f;
    Light.Specular               = 1.f;
    Light.Normalize              = False;
    Light.EnableColorTemperature = False;
    Light.ColorTemperature       = 6500.f;
    Light.Radius                 = 0.5f;
    Light.Angle                  = 0.53f;
    Light.InnerConeAngle         = 0.f;
    Light.OuterConeAngle         = 0.7853981633974483f;
    Light.ShapingFocus           = 0.f;

    (void)Light;
}

void RadientScene_C_UseCustomComponentData(void)
{
    RadientCustomComponentData Component;

    Component.ComponentType = InvalidRadientComponentTypeID;
    Component.Name          = 0;
    Component.Schema        = 0;
    Component.Version       = 0;
    Component.pData         = 0;
    Component.DataSize      = 0;

    (void)Component;
}

void RadientScene_C_UseMaterialBindingsComponent(void)
{
    RadientMaterialBinding           Binding;
    RadientMaterialBindingsComponent Bindings;

    Binding.PrimitiveIndex   = 0;
    Binding.Material.URI     = 0;
    Binding.Material.Version = 0;

    Bindings.pBindings    = &Binding;
    Bindings.BindingCount = 1;

    (void)Bindings;
}

void RadientScene_C_TestMacros(IRadientScene* pScene)
{
    RadientEntityID              Entity          = 0;
    RadientEntityID              Parent          = 0;
    RadientEntityID              Children[1]     = {0};
    RADIENT_ENTITY_FLAGS         EntityFlags     = 0;
    Uint32                       ChildCount      = 1;
    Uint32                       NumChildren     = 0;
    Bool                         Visible         = False;
    Bool                         HasComponent    = False;
    RadientTransform             Transform       = {0};
    RadientMatrix4x4             WorldMatrix     = {0};
    RadientCameraComponent       Camera          = {0};
    RadientCustomComponentData   CustomComponent = {0};
    const RadientSceneRevisions* pRevisions      = 0;
    RADIENT_STATUS               Status          = RADIENT_STATUS_OK;

    EntityFlags = RADIENT_ENTITY_FLAGS_ALL;

    IRadientScene_GetDesc(pScene);
    Status     = IRadientScene_IsEntityAlive(pScene, Entity);
    Status     = IRadientScene_GetEntityFlags(pScene, Entity, &EntityFlags);
    Status     = IRadientScene_GetEntityOwnVisibility(pScene, Entity, &Visible);
    Status     = IRadientScene_GetEntityEffectiveVisibility(pScene, Entity, &Visible);
    Status     = IRadientScene_GetCachedEntityEffectiveVisibility(pScene, Entity, &Visible);
    Status     = IRadientScene_GetParent(pScene, Entity, &Parent);
    Status     = IRadientScene_GetChildCount(pScene, Entity, &ChildCount);
    Status     = IRadientScene_GetChildren(pScene, Entity, 0, ChildCount, Children, &NumChildren);
    Status     = IRadientScene_GetLocalTransform(pScene, Entity, &Transform);
    Status     = IRadientScene_GetWorldMatrix(pScene, Entity, &WorldMatrix);
    Status     = IRadientScene_GetCachedWorldMatrix(pScene, Entity, &WorldMatrix);
    Status     = IRadientScene_GetCamera(pScene, Entity, &Camera);
    Status     = IRadientScene_HasComponent(pScene, Entity, CustomComponent.ComponentType, &HasComponent);
    pRevisions = IRadientScene_GetSceneRevisions(pScene);

    (void)Parent;
    (void)NumChildren;
    (void)Visible;
    (void)HasComponent;
    (void)Camera;
    (void)pRevisions;
    (void)Status;
}
