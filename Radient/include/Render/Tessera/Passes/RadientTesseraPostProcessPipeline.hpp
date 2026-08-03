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

#include "Render/RadientFrameRenderTargets.hpp"
#include "RadientView.h"

#include "RefCntAutoPtr.hpp"

namespace Diligent
{

/// Tessera post-processing stage chain. Tone mapping and output conversion are
/// performed by the final pass after all image-space effects.
class RadientTesseraPostProcessPipeline
{
public:
    RADIENT_STATUS Prepare(IRenderDevice*                   pDevice,
                           IDeviceContext*                  pContext,
                           const RadientFrameRenderTargets& Targets,
                           const RadientToneMappingDesc&    ToneMapping,
                           IBuffer*                         pFrameAttribsCB);
    RADIENT_STATUS Execute(IDeviceContext* pContext, const RadientFrameRenderTargets& Targets);

private:
    RADIENT_STATUS CreatePipelineState(IRenderDevice* pDevice,
                                       TEXTURE_FORMAT OutputFormat,
                                       Int32          ToneMappingMode,
                                       bool           ConvertOutputToSRGB,
                                       IBuffer*       pFrameAttribsCB);

private:
    RefCntAutoPtr<IBuffer>                m_pToneMappingAttribsCB;
    RefCntAutoPtr<IBuffer>                m_pFrameAttribsCB;
    RefCntAutoPtr<IPipelineState>         m_pPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pSRB;

    TEXTURE_FORMAT m_OutputFormat        = TEX_FORMAT_UNKNOWN;
    Int32          m_ToneMappingMode     = -1;
    bool           m_ConvertOutputToSRGB = false;
    Uint32         m_TargetVersion       = ~Uint32{0};
};

} // namespace Diligent
