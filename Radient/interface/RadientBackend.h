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
/// Defines Radient execution backend interface.

#include "RadientTypes.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/SwapChain.h"
#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

/// Backend description.
struct RadientBackendDesc
{
    /// Backend name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Backend execution type.
    RADIENT_BACKEND_TYPE Type DEFAULT_INITIALIZER(RADIENT_BACKEND_TYPE_LOCAL);

    /// Remote endpoint URI for remote backends.
    const Char* RemoteEndpoint DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientBackendDesc RadientBackendDesc;


/// Backend creation attributes.
struct RadientBackendCreateInfo
{
    /// Backend description.
    RadientBackendDesc Desc DEFAULT_INITIALIZER({});

    /// Native render device for local backends.
    IRenderDevice* pDevice DEFAULT_INITIALIZER(nullptr);

    /// Native immediate context for local backends.
    IDeviceContext* pImmediateContext DEFAULT_INITIALIZER(nullptr);

    /// Optional native swap chain for local backends.
    ISwapChain* pSwapChain DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientBackendCreateInfo RadientBackendCreateInfo;


// {0581F6E8-FBCA-4943-BD49-2138714931BA}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientBackend =
    {0x581f6e8, 0xfbca, 0x4943, {0xbd, 0x49, 0x21, 0x38, 0x71, 0x49, 0x31, 0xba}};


#define DILIGENT_INTERFACE_NAME IRadientBackend
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientBackendInclusiveMethods \
    IObjectInclusiveMethods;            \
    IRadientBackendMethods RadientBackend

// clang-format off

/// Radient execution backend interface.
DILIGENT_BEGIN_INTERFACE(IRadientBackend, IObject)
{
    /// Returns the backend description.
    VIRTUAL const RadientBackendDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Returns a native render device when one exists.
    VIRTUAL IRenderDevice* METHOD(GetNativeDevice)(THIS) PURE;

    /// Returns a native immediate context when one exists.
    VIRTUAL IDeviceContext* METHOD(GetNativeImmediateContext)(THIS) PURE;

    /// Returns a native swap chain when one exists.
    VIRTUAL ISwapChain* METHOD(GetNativeSwapChain)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientBackend_GetDesc(This)                   CALL_IFACE_METHOD(RadientBackend, GetDesc,                   This)
#    define IRadientBackend_GetNativeDevice(This)           CALL_IFACE_METHOD(RadientBackend, GetNativeDevice,           This)
#    define IRadientBackend_GetNativeImmediateContext(This) CALL_IFACE_METHOD(RadientBackend, GetNativeImmediateContext, This)
#    define IRadientBackend_GetNativeSwapChain(This)        CALL_IFACE_METHOD(RadientBackend, GetNativeSwapChain,        This)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
