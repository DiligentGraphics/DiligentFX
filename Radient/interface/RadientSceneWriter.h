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

/// \file
/// Defines Radient scene graph mutation interface.

#include "RadientScene.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// {0E75833D-1529-4C50-8C87-F5DA2F491E5B}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSceneWriter =
    {0xe75833d, 0x1529, 0x4c50, {0x8c, 0x87, 0xf5, 0xda, 0x2f, 0x49, 0x1e, 0x5b}};


#define DILIGENT_INTERFACE_NAME IRadientSceneWriter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSceneWriterInclusiveMethods \
    IObjectInclusiveMethods;                \
    IRadientSceneWriterMethods RadientSceneWriter

// clang-format off

/// Scene graph mutation interface.
DILIGENT_BEGIN_INTERFACE(IRadientSceneWriter, IObject)
{
    /// Creates an entity.
    VIRTUAL RADIENT_STATUS METHOD(CreateEntity)(THIS_
                                                const RadientEntityDesc REF Desc,
                                                RadientEntityID REF         Entity) PURE;

    /// Destroys an entity and its owned components.
    VIRTUAL RADIENT_STATUS METHOD(DestroyEntity)(THIS_
                                                 RadientEntityID Entity) PURE;

    /// Sets entity flags.
    VIRTUAL RADIENT_STATUS METHOD(SetEntityFlags)(THIS_
                                                  RadientEntityID      Entity,
                                                  RADIENT_ENTITY_FLAGS Flags) PURE;

    /// Sets entity's own visibility flag.
    VIRTUAL RADIENT_STATUS METHOD(SetEntityOwnVisibility)(THIS_
                                                          RadientEntityID Entity,
                                                          Bool            Visible) PURE;

    /// Sets the entity parent.
    VIRTUAL RADIENT_STATUS METHOD(SetParent)(THIS_
                                             RadientEntityID Entity,
                                             RadientEntityID Parent,
                                             Bool            KeepWorldTransform DEFAULT_VALUE(True)) PURE;

    /// Sets local transform.
    VIRTUAL RADIENT_STATUS METHOD(SetLocalTransform)(THIS_
                                                     RadientEntityID            Entity,
                                                     const RadientTransform REF Transform) PURE;

    /// Adds or updates a camera component.
    VIRTUAL RADIENT_STATUS METHOD(SetCamera)(THIS_
                                             RadientEntityID                  Entity,
                                             const RadientCameraComponent REF Camera) PURE;

    /// Adds or updates a mesh component.
    VIRTUAL RADIENT_STATUS METHOD(SetMesh)(THIS_
                                           RadientEntityID                Entity,
                                           const RadientMeshComponent REF Mesh) PURE;

    /// Adds or updates a mesh renderer component.
    VIRTUAL RADIENT_STATUS METHOD(SetMeshRenderer)(THIS_
                                                   RadientEntityID                        Entity,
                                                   const RadientMeshRendererComponent REF Renderer) PURE;

    /// Adds or updates a light component.
    VIRTUAL RADIENT_STATUS METHOD(SetLight)(THIS_
                                            RadientEntityID                 Entity,
                                            const RadientLightComponent REF Light) PURE;

    /// Adds or updates a custom serialized component.
    VIRTUAL RADIENT_STATUS METHOD(SetCustomComponentData)(THIS_
                                                          RadientEntityID                       Entity,
                                                          const RadientCustomComponentData REF Component) PURE;

    /// Removes a component from the entity.
    VIRTUAL RADIENT_STATUS METHOD(RemoveComponent)(THIS_
                                                   RadientEntityID        Entity,
                                                   RadientComponentTypeID ComponentType) PURE;

    /// Commits pending scene changes to the active backend.
    VIRTUAL RADIENT_STATUS METHOD(CommitChanges)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSceneWriter_CreateEntity(This, ...)           CALL_IFACE_METHOD(RadientSceneWriter, CreateEntity,      This, __VA_ARGS__)
#    define IRadientSceneWriter_DestroyEntity(This, ...)          CALL_IFACE_METHOD(RadientSceneWriter, DestroyEntity,     This, __VA_ARGS__)
#    define IRadientSceneWriter_SetEntityFlags(This, ...)         CALL_IFACE_METHOD(RadientSceneWriter, SetEntityFlags,    This, __VA_ARGS__)
#    define IRadientSceneWriter_SetEntityOwnVisibility(This, ...) CALL_IFACE_METHOD(RadientSceneWriter, SetEntityOwnVisibility, This, __VA_ARGS__)
#    define IRadientSceneWriter_SetParent(This, ...)              CALL_IFACE_METHOD(RadientSceneWriter, SetParent,         This, __VA_ARGS__)
#    define IRadientSceneWriter_SetLocalTransform(This, ...)      CALL_IFACE_METHOD(RadientSceneWriter, SetLocalTransform, This, __VA_ARGS__)
#    define IRadientSceneWriter_SetCamera(This, ...)              CALL_IFACE_METHOD(RadientSceneWriter, SetCamera,         This, __VA_ARGS__)
#    define IRadientSceneWriter_SetMesh(This, ...)                CALL_IFACE_METHOD(RadientSceneWriter, SetMesh,           This, __VA_ARGS__)
#    define IRadientSceneWriter_SetMeshRenderer(This, ...)        CALL_IFACE_METHOD(RadientSceneWriter, SetMeshRenderer,   This, __VA_ARGS__)
#    define IRadientSceneWriter_SetLight(This, ...)               CALL_IFACE_METHOD(RadientSceneWriter, SetLight,          This, __VA_ARGS__)
#    define IRadientSceneWriter_SetCustomComponentData(This, ...) CALL_IFACE_METHOD(RadientSceneWriter, SetCustomComponentData,  This, __VA_ARGS__)
#    define IRadientSceneWriter_RemoveComponent(This, ...)        CALL_IFACE_METHOD(RadientSceneWriter, RemoveComponent,   This, __VA_ARGS__)
#    define IRadientSceneWriter_CommitChanges(This)               CALL_IFACE_METHOD(RadientSceneWriter, CommitChanges,     This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
