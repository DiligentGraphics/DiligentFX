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

#include "Render/Tessera/Passes/RadientTesseraSkyboxPass.hpp"

#include "Assets/RadientTextureAssetManager.hpp"
#include "Render/Tessera/RadientTesseraGeometryRenderer.hpp"

#include "EnvMapRenderer.hpp"

#include <algorithm>
#include <cmath>

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
} // namespace HLSL

namespace
{

TEXTURE_FORMAT GetTextureViewFormat(ITextureView* pView)
{
    if (pView == nullptr)
        return TEX_FORMAT_UNKNOWN;

    const TextureViewDesc& ViewDesc = pView->GetDesc();
    if (ViewDesc.Format != TEX_FORMAT_UNKNOWN)
        return ViewDesc.Format;

    ITexture* pTexture = pView->GetTexture();
    return pTexture != nullptr ? pTexture->GetDesc().Format : TEX_FORMAT_UNKNOWN;
}

bool RequiresOutputSRGBConversion(TEXTURE_FORMAT Format)
{
    return Format == TEX_FORMAT_RGBA8_UNORM ||
        Format == TEX_FORMAT_BGRA8_UNORM;
}

float3 GetLightingScale(const RadientFloat3& Color, Float32 Intensity, Float32 Exposure)
{
    const float Scale = Intensity * std::exp2(Exposure);
    return float3{Color.x * Scale, Color.y * Scale, Color.z * Scale};
}

} // namespace

RadientTesseraSkyboxPass::RadientTesseraSkyboxPass() = default;

RadientTesseraSkyboxPass::~RadientTesseraSkyboxPass() = default;

RADIENT_STATUS RadientTesseraSkyboxPass::Prepare(RadientTesseraGeometryRenderer&  Renderer,
                                                 IRenderDevice*                   pDevice,
                                                 const RadientFrameRenderTargets& Targets)
{
    if (pDevice == nullptr)
        return RADIENT_STATUS_OK;

    ITextureView* pColorRTV = Targets.GetSceneColorRTV();
    if (pColorRTV == nullptr)
        return RADIENT_STATUS_OK;

    const TEXTURE_FORMAT RTVFormat = GetTextureViewFormat(pColorRTV);
    const TEXTURE_FORMAT DSVFormat = GetTextureViewFormat(Targets.GetDepthDSV());
    if (RTVFormat == TEX_FORMAT_UNKNOWN)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_pRenderer != nullptr &&
        m_RTVFormat == RTVFormat &&
        m_DSVFormat == DSVFormat)
    {
        return RADIENT_STATUS_OK;
    }

    RadientPBRRenderer* const pPBRRenderer = Renderer.GetRenderer();
    if (pPBRRenderer == nullptr || pPBRRenderer->GetFrameAttribsCB() == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    EnvMapRenderer::CreateInfo RendererCI;
    RendererCI.pDevice            = pDevice;
    RendererCI.pCameraAttribsCB   = pPBRRenderer->GetFrameAttribsCB();
    RendererCI.PackMatrixRowMajor = true;
    RendererCI.NumRenderTargets   = 1;
    RendererCI.RTVFormats[0]      = RTVFormat;
    RendererCI.DSVFormat          = DSVFormat;

    m_pRenderer = std::make_unique<EnvMapRenderer>(RendererCI);
    m_RTVFormat = RTVFormat;
    m_DSVFormat = DSVFormat;

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraSkyboxPass::Execute(IDeviceContext*                   pContext,
                                                 const RadientSkyboxDesc&          Skybox,
                                                 ITextureView*                     pSkyboxSRV,
                                                 const RadientTextureSamplingInfo& SamplingInfo,
                                                 bool                              SphereMapRow0IsNegativeY,
                                                 const RadientFrameRenderTargets&  Targets)
{
    if (pContext == nullptr || Skybox.Source == RADIENT_SKYBOX_SOURCE_NONE)
        return RADIENT_STATUS_OK;

    if (m_pRenderer == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    if (pSkyboxSRV == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    if (SamplingInfo.MipLevels == 0)
    {
        UNEXPECTED("Skybox texture must expose at least one sampling-safe mip level");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    ITextureView* pColorRTV = Targets.GetSceneColorRTV();
    ITextureView* pDepthDSV = Targets.GetDepthDSV();
    if (pColorRTV == nullptr)
        return RADIENT_STATUS_OK;

    pContext->SetRenderTargets(1, &pColorRTV, pDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    HLSL::ToneMappingAttribs ToneMapping{};
    ToneMapping.iToneMappingMode     = TONE_MAPPING_MODE_NONE;
    ToneMapping.bAutoExposure        = 0;
    ToneMapping.fMiddleGray          = 0.18f;
    ToneMapping.bLightAdaptation     = 0;
    ToneMapping.fWhitePoint          = 3.f;
    ToneMapping.fLuminanceSaturation = 1.f;

    EnvMapRenderer::RenderAttribs Attribs;
    Attribs.pEnvMap                  = pSkyboxSRV;
    Attribs.MipLevel                 = std::min(Skybox.MipLevel, static_cast<float>(SamplingInfo.MipLevels - 1));
    Attribs.Alpha                    = 0.f;
    Attribs.Scale                    = GetLightingScale(Skybox.Color, Skybox.Intensity, Skybox.Exposure);
    Attribs.SphereMapUVScaleBias     = SamplingInfo.UVScaleBias;
    Attribs.SphereMapSlice           = SamplingInfo.TextureSlice;
    Attribs.SphereMapRow0IsNegativeY = SphereMapRow0IsNegativeY;

    if (RequiresOutputSRGBConversion(m_RTVFormat))
        Attribs.Options |= EnvMapRenderer::OPTION_FLAG_CONVERT_OUTPUT_TO_SRGB;
    if (Targets.GetUseReverseDepth())
        Attribs.Options |= EnvMapRenderer::OPTION_FLAG_USE_REVERSE_DEPTH;

    m_pRenderer->Prepare(pContext, Attribs, ToneMapping);
    m_pRenderer->Render(pContext);

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
