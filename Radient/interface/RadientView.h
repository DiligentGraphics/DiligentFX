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
/// Defines Radient render view interfaces.

#include "RadientScene.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientRenderTarget IRadientRenderTarget;
typedef struct IRadientView         IRadientView;

/// View description.
///
/// A view describes one persistent way to render a scene: which scene is
/// rendered, which camera is used, and where the image is written.
struct RadientViewDesc
{
    /// View name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Scene to render.
    IRadientScene* pScene DEFAULT_INITIALIZER(nullptr);

    /// Camera entity to render from.
    RadientEntityID Camera DEFAULT_INITIALIZER(InvalidRadientEntityID);

    /// Render target.
    IRadientRenderTarget* pRenderTarget DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientViewDesc RadientViewDesc;

// {6BFFEECB-D937-44E2-80E6-696236759008}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientView =
    {0x6bffeecb, 0xd937, 0x44e2, {0x80, 0xe6, 0x69, 0x62, 0x36, 0x75, 0x90, 0x8}};

#define DILIGENT_INTERFACE_NAME IRadientView
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientViewInclusiveMethods \
    IObjectInclusiveMethods;         \
    IRadientViewMethods RadientView

// clang-format off

/// Persistent render view interface.
DILIGENT_BEGIN_INTERFACE(IRadientView, IObject)
{
    /// Returns the view description.
    VIRTUAL const RadientViewDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Sets the scene rendered by the view.
    VIRTUAL RADIENT_STATUS METHOD(SetScene)(THIS_
                                            IRadientScene* pScene) PURE;

    /// Sets the camera rendered by the view.
    VIRTUAL RADIENT_STATUS METHOD(SetCamera)(THIS_
                                             RadientEntityID Camera) PURE;

    /// Sets the render target used by the view.
    VIRTUAL RADIENT_STATUS METHOD(SetRenderTarget)(THIS_
                                                   IRadientRenderTarget* pRenderTarget) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientView_GetDesc(This)                CALL_IFACE_METHOD(RadientView, GetDesc,         This)
#    define IRadientView_SetScene(This, ...)          CALL_IFACE_METHOD(RadientView, SetScene,        This, __VA_ARGS__)
#    define IRadientView_SetCamera(This, ...)         CALL_IFACE_METHOD(RadientView, SetCamera,       This, __VA_ARGS__)
#    define IRadientView_SetRenderTarget(This, ...)   CALL_IFACE_METHOD(RadientView, SetRenderTarget, This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
