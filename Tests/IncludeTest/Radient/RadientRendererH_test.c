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

#include "Radient/interface/RadientRenderer.h"

void RadientRenderTarget_C_TestMacros(IRadientRenderTarget* pTarget)
{
    const RadientRenderTargetDesc* pDesc      = IRadientRenderTarget_GetDesc(pTarget);
    RADIENT_STATUS                 Status     = RADIENT_STATUS_OK;
    ITextureView*                  pRTV       = IRadientRenderTarget_GetColorRTV(pTarget);
    ITextureView*                  pDSV       = IRadientRenderTarget_GetDepthDSV(pTarget);
    ISwapChain*                    pSwapChain = IRadientRenderTarget_GetSwapChain(pTarget);

    Status = IRadientRenderTarget_Resize(pTarget, 1, 1);

    (void)pDesc;
    (void)Status;
    (void)pRTV;
    (void)pDSV;
    (void)pSwapChain;
}

void RadientRenderer_C_TestMacros(IRadientRenderer* pRenderer)
{
    const RadientRendererDesc* pDesc         = IRadientRenderer_GetDesc(pRenderer);
    RadientRenderTargetDesc    TargetDesc    = {0};
    RadientViewDesc            ViewDesc      = {0};
    RadientRenderAttribs       RenderAttribs = {0};
    IRadientRenderTarget*      pTarget       = 0;
    IRadientView*              pView         = 0;
    RADIENT_STATUS             Status        = RADIENT_STATUS_OK;

    Status = IRadientRenderer_CreateRenderTarget(pRenderer, &TargetDesc, &pTarget);
    Status = IRadientRenderer_CreateView(pRenderer, &ViewDesc, &pView);
    Status = IRadientRenderer_Render(pRenderer, &RenderAttribs);

    (void)pDesc;
    (void)pTarget;
    (void)pView;
    (void)Status;
}
