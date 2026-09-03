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

#include "RadientRenderer.h"
#include "RadientScene.h"

namespace Diligent
{

/// Resolved per-frame inputs passed to a render technique.
struct RadientRenderContext
{
    const RadientFrameAttribs&  FrameAttribs;
    const RadientRenderAttribs& Attribs;
    RadientFrameID              RenderFrameID = InvalidRadientFrameID;
    IRenderDevice*              pDevice       = nullptr;
    IDeviceContext*             pContext      = nullptr;
};


/// Defines renderer-specific scene synchronization and rendering behavior.
class IRadientRenderTechnique
{
public:
    virtual ~IRadientRenderTechnique() = default;

    /// Incrementally synchronizes renderer-specific state with the scene.
    virtual RADIENT_STATUS SyncScene(const IRadientScene& Scene) = 0;

    /// Prepares renderer-specific resources for the frame.
    virtual RADIENT_STATUS PrepareFrame(const RadientRenderContext& Context) = 0;

    /// Begins, renders, and ends one frame. EndFrame() is called whenever
    /// BeginFrame() succeeds, including when Render() reports a failure.
    virtual RADIENT_STATUS BeginFrame(const RadientRenderContext& Context) = 0;
    virtual RADIENT_STATUS Render(const RadientRenderContext& Context)     = 0;
    virtual RADIENT_STATUS EndFrame(const RadientRenderContext& Context)   = 0;
};

} // namespace Diligent
