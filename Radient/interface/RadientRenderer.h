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
/// Defines Radient renderer interfaces.

#include "RadientBackend.h"
#include "RadientScene.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientRenderTarget IRadientRenderTarget;
typedef struct IRadientRenderer     IRadientRenderer;

/// Renderer description.
struct RadientRendererDesc
{
    /// Renderer name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Enables asynchronous geometry pipeline compilation.
    ///
    /// When enabled, geometry drawables are skipped until their pipeline state
    /// is ready instead of blocking the render call.
    Bool EnableAsyncPipelineCompilation DEFAULT_INITIALIZER(True);
};
typedef struct RadientRendererDesc RadientRendererDesc;


/// Render target description.
struct RadientRenderTargetDesc
{
    /// Target name.
    const Char* Name DEFAULT_INITIALIZER(nullptr);

    /// Target size.
    RadientExtent2D Size DEFAULT_INITIALIZER({});

    /// Native swap chain for local presentation targets.
    ISwapChain* pSwapChain DEFAULT_INITIALIZER(nullptr);

    /// Native color render target view for local texture targets.
    ITextureView* pColorRTV DEFAULT_INITIALIZER(nullptr);

    /// Native depth-stencil view for local texture targets.
    ITextureView* pDepthDSV DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientRenderTargetDesc RadientRenderTargetDesc;


/// Render call attributes.
struct RadientRenderAttribs
{
    /// Scene to render.
    IRadientScene* pScene DEFAULT_INITIALIZER(nullptr);

    /// Camera entity to render from.
    RadientEntityID Camera DEFAULT_INITIALIZER(InvalidRadientEntityID);

    /// Render target.
    IRadientRenderTarget* pRenderTarget DEFAULT_INITIALIZER(nullptr);

    /// Optional device context override for local rendering.
    IDeviceContext* pDeviceContext DEFAULT_INITIALIZER(nullptr);

    /// Time since previous frame.
    double DeltaTime DEFAULT_INITIALIZER(0.0);

    /// Absolute application time.
    double Time DEFAULT_INITIALIZER(0.0);
};
typedef struct RadientRenderAttribs RadientRenderAttribs;


// {E15BDBFE-2B5E-4A5A-AF6C-0B7DD326D182}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientRenderTarget =
    {0xe15bdbfe, 0x2b5e, 0x4a5a, {0xaf, 0x6c, 0xb, 0x7d, 0xd3, 0x26, 0xd1, 0x82}};

#define DILIGENT_INTERFACE_NAME IRadientRenderTarget
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientRenderTargetInclusiveMethods \
    IObjectInclusiveMethods;                 \
    IRadientRenderTargetMethods RadientRenderTarget

// clang-format off

/// Render target interface.
DILIGENT_BEGIN_INTERFACE(IRadientRenderTarget, IObject)
{
    /// Returns the render target description.
    VIRTUAL const RadientRenderTargetDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Updates the logical render target size.
    VIRTUAL RADIENT_STATUS METHOD(Resize)(THIS_
                                          Uint32 Width,
                                          Uint32 Height) PURE;

    /// Returns local color RTV when one exists.
    VIRTUAL ITextureView* METHOD(GetColorRTV)(THIS) PURE;

    /// Returns local depth DSV when one exists.
    VIRTUAL ITextureView* METHOD(GetDepthDSV)(THIS) PURE;

    /// Returns local swap chain when one exists.
    VIRTUAL ISwapChain* METHOD(GetSwapChain)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientRenderTarget_GetDesc(This)    CALL_IFACE_METHOD(RadientRenderTarget, GetDesc,    This)
#    define IRadientRenderTarget_Resize(This, ...) CALL_IFACE_METHOD(RadientRenderTarget, Resize,     This, __VA_ARGS__)
#    define IRadientRenderTarget_GetColorRTV(This) CALL_IFACE_METHOD(RadientRenderTarget, GetColorRTV, This)
#    define IRadientRenderTarget_GetDepthDSV(This) CALL_IFACE_METHOD(RadientRenderTarget, GetDepthDSV, This)
#    define IRadientRenderTarget_GetSwapChain(This) CALL_IFACE_METHOD(RadientRenderTarget, GetSwapChain, This)

#endif


// {B4FB1E81-1497-49A3-AD09-468EA85E3DC9}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientRenderer =
    { 0xb4fb1e81, 0x1497, 0x49a3, { 0xad, 0x9, 0x46, 0x8e, 0xa8, 0x5e, 0x3d, 0xc9 } };

#define DILIGENT_INTERFACE_NAME IRadientRenderer
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientRendererInclusiveMethods \
    IObjectInclusiveMethods;             \
    IRadientRendererMethods RadientRenderer

/// Scene renderer interface.
DILIGENT_BEGIN_INTERFACE(IRadientRenderer, IObject)
{
    /// Returns the renderer description.
    VIRTUAL const RadientRendererDesc REF METHOD(GetDesc)(THIS) CONST PURE;

    /// Creates a render target.
    VIRTUAL RADIENT_STATUS METHOD(CreateRenderTarget)(THIS_
                                                      const RadientRenderTargetDesc REF Desc,
                                                      IRadientRenderTarget**            ppTarget) PURE;

    /// Renders one frame.
    VIRTUAL RADIENT_STATUS METHOD(Render)(THIS_
                                          const RadientRenderAttribs REF Attribs) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientRenderer_GetDesc(This)                  CALL_IFACE_METHOD(RadientRenderer, GetDesc,            This)
#    define IRadientRenderer_CreateRenderTarget(This, ...)  CALL_IFACE_METHOD(RadientRenderer, CreateRenderTarget, This, __VA_ARGS__)
#    define IRadientRenderer_Render(This, ...)              CALL_IFACE_METHOD(RadientRenderer, Render,             This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
