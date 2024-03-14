/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include "HnRenderPassState.hpp"

#include "HnTypeConversions.hpp"
#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

pxr::HdRenderPassStateSharedPtr HnRenderPassState::Create()
{
    return pxr::HdRenderPassStateSharedPtr{new HnRenderPassState()};
}

HnRenderPassState::HnRenderPassState()
{
}

void HnRenderPassState::Commit(IDeviceContext* pContext)
{
    if (m_IsCommited)
        return;

    //_depthMaskEnabled
    //_framing
    //_overrideWindowPolicy
    //float   _pointSize;
    //bool    _lightingEnabled;
    //bool    _clippingEnabled;
    //float   _lineWidth;
    //float   _tessLevel;
    //float   _alphaThreshold;
    //float   _stepSize;
    //float   _stepSizeLighting;
    //GfVec4f _overrideColor;
    //GfVec4f _maskColor;
    //GfVec4f _indicatorColor;
    //float   _pointSelectedSize;
    //GfVec2f _drawRange;

    pContext->SetRenderTargets(m_NumRenderTargets, m_RTVs.data(), m_DSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    for (Uint32 rt = 0; rt < m_NumRenderTargets; ++rt)
    {
        if ((m_ClearMask & (1u << rt)) != 0)
        {
            pContext->ClearRenderTarget(m_RTVs[rt], m_ClearColors[rt].Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }
    if ((m_ClearMask & ClearDepthBit) != 0)
    {
        pContext->ClearDepthStencil(m_DSV, CLEAR_DEPTH_FLAG, m_ClearDepth, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    //pContext->SetBlendFactors(_blendConstantColor.data());
    //Viewport VP{_viewport[0], _viewport[1], _viewport[2], _viewport[3]};
    //pContext->SetViewports(1, &VP, 0, 0);
    pContext->SetStencilRef(_stencilRef);

    m_IsCommited = true;
}

RasterizerStateDesc HnRenderPassState::GetRasterizerState() const
{
    VERIFY(!_conservativeRasterizationEnabled, "Conservative rasterization is not supported");

    RasterizerStateDesc RSState;

    RSState.DepthClipEnable       = !_depthClampEnabled;
    RSState.FrontCounterClockwise = m_FrontFaceCCW;
    if (_depthBiasEnabled)
    {
        RSState.DepthBias            = _depthBiasConstantFactor;
        RSState.SlopeScaledDepthBias = _depthBiasSlopeFactor;
    }

    return RSState;
}

DepthStencilStateDesc HnRenderPassState::GetDepthStencilState() const
{
    DepthStencilStateDesc DSSState;
    DSSState.DepthEnable             = _depthTestEnabled;
    DSSState.DepthFunc               = HdCompareFunctionToComparisonFunction(_depthFunc);
    DSSState.StencilEnable           = _stencilEnabled;
    DSSState.StencilReadMask         = static_cast<Uint8>(_stencilMask);
    DSSState.StencilWriteMask        = static_cast<Uint8>(_stencilMask);
    DSSState.FrontFace.StencilFunc   = HdCompareFunctionToComparisonFunction(_stencilFunc);
    DSSState.FrontFace.StencilFailOp = HdStencilOpToStencilOp(_stencilFailOp);
    DSSState.FrontFace.StencilPassOp = HdStencilOpToStencilOp(_stencilZPassOp);
    DSSState.FrontFace.StencilFailOp = HdStencilOpToStencilOp(_stencilZFailOp);

    return DSSState;
}

BlendStateDesc HnRenderPassState::GetBlendState() const
{
    BlendStateDesc BSState;
    BSState.AlphaToCoverageEnable = _alphaToCoverageEnabled;

    auto& RT0          = BSState.RenderTargets[0];
    RT0.BlendEnable    = _blendEnabled;
    RT0.SrcBlend       = HdBlendFactorToBlendFactor(_blendColorSrcFactor);
    RT0.DestBlend      = HdBlendFactorToBlendFactor(_blendColorDstFactor);
    RT0.BlendOp        = HdBlendOpToBlendOperation(_blendColorOp);
    RT0.SrcBlendAlpha  = HdBlendFactorToBlendFactor(_blendAlphaSrcFactor);
    RT0.DestBlendAlpha = HdBlendFactorToBlendFactor(_blendAlphaDstFactor);
    RT0.BlendOpAlpha   = HdBlendOpToBlendOperation(_blendAlphaOp);

    if (!_colorMaskUseDefault)
    {
        for (size_t i = 0; i < std::min(_colorMasks.size(), _countof(BSState.RenderTargets)); ++i)
        {
            const auto SrcMask = _colorMasks[i];
            auto&      DstMask = BSState.RenderTargets[i].RenderTargetWriteMask;
            switch (SrcMask)
            {
                case ColorMaskNone: DstMask = COLOR_MASK_NONE; break;
                case ColorMaskRGB: DstMask = COLOR_MASK_RGB; break;
                case ColorMaskRGBA: DstMask = COLOR_MASK_ALL; break;
                default:
                    UNEXPECTED("Unexpected color mask");
            }
        }
    }

    return BSState;
}

GraphicsPipelineDesc HnRenderPassState::GetGraphicsPipelineDesc() const
{
    GraphicsPipelineDesc GraphicsPipeline;
    GraphicsPipeline.BlendDesc        = GetBlendState();
    GraphicsPipeline.RasterizerDesc   = GetRasterizerState();
    GraphicsPipeline.DepthStencilDesc = GetDepthStencilState();

    GraphicsPipeline.NumRenderTargets = m_NumRenderTargets;
    for (Uint32 rt = 0; rt < m_NumRenderTargets; ++rt)
        GraphicsPipeline.RTVFormats[rt] = m_RTVFormats[rt];
    GraphicsPipeline.DSVFormat = m_DepthFormat;

    return GraphicsPipeline;
}

void HnRenderPassState::Begin(Uint32        NumRenderTargets,
                              ITextureView* ppRTVs[],
                              ITextureView* pDSV,
                              float4*       ClearColors,
                              float         ClearDepth,
                              Uint32        ClearMask)
{
    VERIFY(NumRenderTargets == m_NumRenderTargets, "Number of render targets does not match the value set in the render pass state");

    m_ClearMask = ClearMask;
    for (Uint32 rt = 0; rt < NumRenderTargets; ++rt)
    {
        m_RTVs[rt] = ppRTVs[rt];
        VERIFY((m_RTVs[rt] != nullptr ? m_RTVs[rt]->GetDesc().Format : TEX_FORMAT_UNKNOWN) == m_RTVFormats[rt], "Invalid render target view format");
        if (ClearColors != nullptr)
        {
            m_ClearColors[rt] = ClearColors[rt];
        }
    }

    m_DSV = pDSV;
    VERIFY((m_DSV != nullptr ? m_DSV->GetDesc().Format : TEX_FORMAT_UNKNOWN) == m_DepthFormat, "Invalid depth-stencil view format");
    m_ClearDepth = ClearDepth;

    m_IsCommited = false;
}

} // namespace USD

} // namespace Diligent
