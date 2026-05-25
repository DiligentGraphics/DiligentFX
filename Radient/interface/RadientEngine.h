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
/// Defines the main Radient engine interface.

#include "RadientAssets.h"
#include "RadientRenderer.h"
#include "RadientSceneImporter.h"
#include "RadientSceneWriter.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

struct IRadientEngine;

/// Engine creation attributes.
struct RadientEngineCreateInfo
{
    /// Backend creation attributes.
    RadientBackendCreateInfo Backend DEFAULT_INITIALIZER({});

    /// Asset manager creation attributes.
    RadientAssetManagerCreateInfo Assets DEFAULT_INITIALIZER({});
};
typedef struct RadientEngineCreateInfo RadientEngineCreateInfo;


// {8013033E-71B8-4FF6-B2A0-519EE11BA4E0}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientEngine =
    {0x8013033e, 0x71b8, 0x4ff6, {0xb2, 0xa0, 0x51, 0x9e, 0xe1, 0x1b, 0xa4, 0xe0}};


#define DILIGENT_INTERFACE_NAME IRadientEngine
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientEngineInclusiveMethods \
    IObjectInclusiveMethods;           \
    IRadientEngineMethods RadientEngine

// clang-format off

/// Main Radient engine interface.
DILIGENT_BEGIN_INTERFACE(IRadientEngine, IObject)
{
    /// Returns the execution backend.
    VIRTUAL RADIENT_STATUS METHOD(GetBackend)(THIS_
                                              IRadientBackend** ppBackend) PURE;

    /// Returns the asset manager.
    VIRTUAL RADIENT_STATUS METHOD(GetAssetManager)(THIS_
                                                   IRadientAssetManager** ppAssetManager) PURE;

    /// Creates a scene.
    VIRTUAL RADIENT_STATUS METHOD(CreateScene)(THIS_
                                               const RadientSceneDesc REF Desc,
                                               IRadientScene**            ppScene) PURE;

    /// Creates a writer for a scene created by this engine.
    VIRTUAL RADIENT_STATUS METHOD(CreateSceneWriter)(THIS_
                                                     IRadientScene*        pScene,
                                                     IRadientSceneWriter** ppWriter) PURE;

    /// Creates a scene importer that writes into the scene through the specified writer.
    VIRTUAL RADIENT_STATUS METHOD(CreateSceneImporter)(THIS_
                                                       IRadientSceneWriter*   pWriter,
                                                       IRadientSceneImporter** ppImporter) PURE;

    /// Creates a renderer.
    VIRTUAL RADIENT_STATUS METHOD(CreateRenderer)(THIS_
                                                  const RadientRendererDesc REF Desc,
                                                  IRadientRenderer**            ppRenderer) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientEngine_GetBackend(This, ...)        CALL_IFACE_METHOD(RadientEngine, GetBackend,        This, __VA_ARGS__)
#    define IRadientEngine_GetAssetManager(This, ...)   CALL_IFACE_METHOD(RadientEngine, GetAssetManager,   This, __VA_ARGS__)
#    define IRadientEngine_CreateScene(This, ...)       CALL_IFACE_METHOD(RadientEngine, CreateScene,       This, __VA_ARGS__)
#    define IRadientEngine_CreateSceneWriter(This, ...) CALL_IFACE_METHOD(RadientEngine, CreateSceneWriter, This, __VA_ARGS__)
#    define IRadientEngine_CreateSceneImporter(This, ...) CALL_IFACE_METHOD(RadientEngine, CreateSceneImporter, This, __VA_ARGS__)
#    define IRadientEngine_CreateRenderer(This, ...)    CALL_IFACE_METHOD(RadientEngine, CreateRenderer,    This, __VA_ARGS__)

#endif

/// Creates a Radient engine.
#include "../../../DiligentCore/Primitives/interface/DefineRefMacro.h"
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientEngine)(const RadientEngineCreateInfo REF EngineCI,
                                                             IRadientEngine**                  ppEngine);
#include "../../../DiligentCore/Primitives/interface/UndefRefMacro.h"

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
