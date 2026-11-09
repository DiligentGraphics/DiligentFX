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

// Private bridge used by RadientBrowser.mjs. Native graphics interfaces do not
// cross the JavaScript boundary; ordinary Radient interfaces use generated glue.
#include "../../interface/RadientEngine.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)
#include "../../../../DiligentCore/Primitives/interface/DefineRefMacro.h"

/// Attaches to the module's preinitialized WebGPU device and canvas.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientBrowserEngine)(const RadientEngineCreateInfo REF CreateInfo,
                                                                    IRadientEngine**                  ppEngine);

/// Creates an immutable target that follows the engine's canvas swap chain.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientCanvasRenderTarget)(IRadientEngine*        pEngine,
                                                                         IRadientRenderer*      pRenderer,
                                                                         IRadientRenderTarget** ppTarget);

/// Resizes the canvas swap chain between frames, in physical pixels.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(ResizeRadientCanvas)(IRadientEngine* pEngine, Uint32 Width, Uint32 Height);

/// Submits work and releases the canvas back buffer before yielding to the browser.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(PresentRadientCanvas)(IRadientEngine* pEngine);

/// Cancels pending uploads before releasing the engine and its worker pool.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(StopRadientBrowserEngine)(IRadientEngine* pEngine);

#include "../../../../DiligentCore/Primitives/interface/UndefRefMacro.h"
DILIGENT_END_NAMESPACE
