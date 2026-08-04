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

#include "PostProcess/Bloom/interface/Bloom.hpp"
#include "PostProcess/Common/interface/PostFXContext.hpp"
#include "PostProcess/DepthOfField/interface/DepthOfField.hpp"
#include "PostProcess/ScreenSpaceAmbientOcclusion/interface/ScreenSpaceAmbientOcclusion.hpp"
#include "PostProcess/ScreenSpaceReflection/interface/ScreenSpaceReflection.hpp"

#include "RefCntAutoPtr.hpp"

#include <memory>

namespace Diligent
{

/// Tessera post-processing stage chain. Screen-space effects are composed
/// before full-color effects, and each full-color effect consumes the output of
/// the previous stage. Tone mapping and output conversion are performed last.
class RadientTesseraPostProcessPipeline
{
public:
    RADIENT_STATUS Prepare(IRenderDevice*                   pDevice,
                           IDeviceContext*                  pContext,
                           const RadientFrameRenderTargets& Targets,
                           const RadientToneMappingDesc&    ToneMapping,
                           const RadientBloomDesc&          Bloom,
                           const RadientSSAODesc&           SSAO,
                           const RadientSSRDesc&            SSR,
                           const RadientDepthOfFieldDesc&   DepthOfField,
                           Uint32                           FrameIndex,
                           IBuffer*                         pFrameAttribsCB,
                           ITextureView*                    pPreintegratedGGXSRV);

    RADIENT_STATUS Execute(IRenderDevice*                   pDevice,
                           IDeviceContext*                  pContext,
                           const RadientFrameRenderTargets& Targets,
                           bool                             ResetTemporalHistory);

private:
    struct ColorPassPipelineDesc
    {
        TEXTURE_FORMAT OutputFormat         = TEX_FORMAT_UNKNOWN;
        Int32          ToneMappingMode      = -1;
        bool           ConvertOutputToSRGB  = false;
        bool           SSAOEnabled          = false;
        bool           SSREnabled           = false;
        IBuffer*       pFrameAttribsCB      = nullptr;
        ITextureView*  pPreintegratedGGXSRV = nullptr;

        bool operator==(const ColorPassPipelineDesc& Rhs) const noexcept
        {
            return OutputFormat == Rhs.OutputFormat &&
                ToneMappingMode == Rhs.ToneMappingMode &&
                ConvertOutputToSRGB == Rhs.ConvertOutputToSRGB &&
                SSAOEnabled == Rhs.SSAOEnabled &&
                SSREnabled == Rhs.SSREnabled &&
                pFrameAttribsCB == Rhs.pFrameAttribsCB &&
                (!SSREnabled || pPreintegratedGGXSRV == Rhs.pPreintegratedGGXSRV);
        }

        bool operator!=(const ColorPassPipelineDesc& Rhs) const noexcept
        {
            return !(*this == Rhs);
        }
    };

    struct ColorPassPrepareInfo
    {
        explicit ColorPassPrepareInfo(const RadientFrameRenderTargets& RenderTargets) :
            Targets{RenderTargets}
        {}

        const RadientFrameRenderTargets& Targets;
        ColorPassPipelineDesc            Pipeline;
        ITextureView*                    pColorSRV = nullptr;
        ITextureView*                    pSSAOSRV  = nullptr;
        ITextureView*                    pSSRSRV   = nullptr;
    };

    struct ColorPass
    {
        RefCntAutoPtr<IPipelineState>         pPSO;
        RefCntAutoPtr<IShaderResourceBinding> pSRB;

        ColorPassPipelineDesc Pipeline;
        Uint32                TargetVersion  = ~Uint32{0};
        ITextureView*         pBoundColorSRV = nullptr;
        ITextureView*         pBoundSSRSRV   = nullptr;
        ITextureView*         pBoundSSAOSRV  = nullptr;
    };

    RADIENT_STATUS PrepareColorPass(IRenderDevice*              pDevice,
                                    ColorPass&                  Pass,
                                    const ColorPassPrepareInfo& PrepareInfo);

    RADIENT_STATUS CreatePipelineState(IRenderDevice*               pDevice,
                                       ColorPass&                   Pass,
                                       const ColorPassPipelineDesc& Desc);

    static RADIENT_STATUS ExecuteColorPass(IDeviceContext*  pContext,
                                           const ColorPass& Pass,
                                           ITextureView*    pOutputRTV);

private:
    RefCntAutoPtr<IBuffer>      m_pToneMappingAttribsCB;
    RefCntAutoPtr<IBuffer>      m_pFrameAttribsCB;
    RefCntAutoPtr<ITextureView> m_pPreintegratedGGXSRV;
    RefCntAutoPtr<ITexture>     m_pComposedColor;

    ColorPass m_CompositionPass;
    ColorPass m_FinalPass;

    std::unique_ptr<PostFXContext>               m_pPostFXContext;
    std::unique_ptr<ScreenSpaceAmbientOcclusion> m_pSSAO;
    std::unique_ptr<ScreenSpaceReflection>       m_pSSR;
    std::unique_ptr<DepthOfField>                m_pDepthOfField;
    std::unique_ptr<Bloom>                       m_pBloom;
    RadientBloomDesc                             m_Bloom;
    RadientSSAODesc                              m_SSAO;
    RadientSSRDesc                               m_SSR;
    RadientDepthOfFieldDesc                      m_DepthOfField;

    bool   m_SSAOEnabled          = false;
    bool   m_SSREnabled           = false;
    bool   m_DepthOfFieldEnabled  = false;
    bool   m_BloomEnabled         = false;
    bool   m_CompositionRequired  = false;
    bool   m_ResetSSAO            = true;
    Uint32 m_ComposedColorVersion = ~Uint32{0};
};

} // namespace Diligent
