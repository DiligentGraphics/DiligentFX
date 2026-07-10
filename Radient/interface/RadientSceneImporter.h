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
/// Defines Radient scene import interfaces.

#include "RadientAssets.h"
#include "RadientScene.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

struct IRadientSceneImporter;

/// Uses the imported scene asset's default scene.
static DILIGENT_CONSTEXPR Uint32 InvalidRadientSceneIndex = ~0u;

/// Imported scene instantiation attributes.
struct RadientSceneInstantiateInfo
{
    /// Optional name for the root entity created by the import.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Optional parent entity for the imported scene graph root.
    RadientEntityID Parent DEFAULT_INITIALIZER(InvalidRadientEntityID);

    /// Root entity flags.
    RADIENT_ENTITY_FLAGS RootFlags DEFAULT_INITIALIZER(RADIENT_ENTITY_FLAG_VISIBLE);

    /// Root entity local transform.
    RadientTransform RootTransform DEFAULT_INITIALIZER({});

    /// Optional scene index to instantiate. InvalidRadientSceneIndex uses the asset default scene.
    Uint32 SceneIndex DEFAULT_INITIALIZER(InvalidRadientSceneIndex);
};
typedef struct RadientSceneInstantiateInfo RadientSceneInstantiateInfo;


// {8A6DE7D7-7588-48C6-8AE0-827DB3DA7C19}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientSceneImporter =
    {0x8a6de7d7, 0x7588, 0x48c6, {0x8a, 0xe0, 0x82, 0x7d, 0xb3, 0xda, 0x7c, 0x19}};


#define DILIGENT_INTERFACE_NAME IRadientSceneImporter
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientSceneImporterInclusiveMethods \
    IObjectInclusiveMethods;                  \
    IRadientSceneImporterMethods RadientSceneImporter

// clang-format off

/// Imports external scene descriptions into a Radient scene.
DILIGENT_BEGIN_INTERFACE(IRadientSceneImporter, IObject)
{
    /// Loads a scene asset from a URI and instantiates its scene graph.
    /// If loading continues asynchronously, this method creates the import root,
    /// schedules the scene graph instantiation, and returns RADIENT_STATUS_PENDING.
    VIRTUAL RADIENT_STATUS METHOD(ImportScene)(THIS_
                                               const RadientSceneLoadInfo REF        LoadInfo,
                                               const RadientSceneInstantiateInfo REF InstantiateInfo,
                                               IRadientSceneAsset**                  ppScene,
                                               RadientEntityID REF                   RootEntity) PURE;

    /// Instantiates a previously loaded scene asset into the scene graph.
    /// If the scene metadata is still loading, this method creates the import root,
    /// schedules the scene graph instantiation, and returns RADIENT_STATUS_PENDING.
    VIRTUAL RADIENT_STATUS METHOD(InstantiateScene)(THIS_
                                                    IRadientSceneAsset*                   pScene,
                                                    const RadientSceneInstantiateInfo REF InstantiateInfo,
                                                    RadientEntityID REF                   RootEntity) PURE;

    /// Completes pending asynchronous imports whose assets have finished loading.
    VIRTUAL RADIENT_STATUS METHOD(ProcessPendingImports)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientSceneImporter_ImportScene(This, ...)             CALL_IFACE_METHOD(RadientSceneImporter, ImportScene,            This, __VA_ARGS__)
#    define IRadientSceneImporter_InstantiateScene(This, ...)        CALL_IFACE_METHOD(RadientSceneImporter, InstantiateScene,       This, __VA_ARGS__)
#    define IRadientSceneImporter_ProcessPendingImports(This)        CALL_IFACE_METHOD(RadientSceneImporter, ProcessPendingImports,  This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
