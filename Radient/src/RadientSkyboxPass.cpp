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

#include "RadientSkyboxPass.hpp"

#include "RadientAssetManagerImpl.hpp"
#include "RadientGeometryPass.hpp"

#include "EnvMapRenderer.hpp"

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

RadientSkyboxPass::RadientSkyboxPass() = default;

RadientSkyboxPass::~RadientSkyboxPass() = default;

RADIENT_STATUS RadientSkyboxPass::Prepare(RadientGeometryRenderer&         Renderer,
                                          IRenderDevice*                   pDevice,
                                          const RadientFrameRenderTargets& Targets)
{
    if (pDevice == nullptr)
        return RADIENT_STATUS_OK;

    ITextureView* pColorRTV = Targets.GetColorRTV();
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

    EnvMapRenderer::CreateInfo RendererCI;
    RendererCI.pDevice            = pDevice;
    RendererCI.pCameraAttribsCB   = Renderer.GetFrameAttribsCB();
    RendererCI.PackMatrixRowMajor = true;
    RendererCI.NumRenderTargets   = 1;
    RendererCI.RTVFormats[0]      = RTVFormat;
    RendererCI.DSVFormat          = DSVFormat;

    m_pRenderer = std::make_unique<EnvMapRenderer>(RendererCI);
    m_RTVFormat = RTVFormat;
    m_DSVFormat = DSVFormat;

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientSkyboxPass::Execute(RadientGeometryRenderer&         Renderer,
                                          IDeviceContext*                  pContext,
                                          const RadientViewDesc&           ViewDesc,
                                          const RadientEnvironmentDesc&    Environment,
                                          RadientAssetManagerImpl*         pAssetManager,
                                          const RadientFrameRenderTargets& Targets)
{
    if (pContext == nullptr || ViewDesc.Skybox.Source == RADIENT_SKYBOX_SOURCE_NONE)
        return RADIENT_STATUS_OK;

    if (m_pRenderer == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    ITextureView* pEnvMap = nullptr;
    switch (ViewDesc.Skybox.Source)
    {
        case RADIENT_SKYBOX_SOURCE_SCENE_ENVIRONMENT:
            if (pAssetManager != nullptr && Environment.EnvironmentMap.URI != nullptr)
                pEnvMap = pAssetManager->GetTextureSRV(Environment.EnvironmentMap);
            break;

        case RADIENT_SKYBOX_SOURCE_TEXTURE:
            if (pAssetManager != nullptr && ViewDesc.Skybox.Texture.URI != nullptr)
                pEnvMap = pAssetManager->GetTextureSRV(ViewDesc.Skybox.Texture);
            break;

        default:
            UNEXPECTED("Unexpected Radient skybox source");
            break;
    }

    if (pEnvMap == nullptr)
        pEnvMap = Renderer.GetDefaultIBLCubemapSRV();
    if (pEnvMap == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    ITextureView* pColorRTV = Targets.GetColorRTV();
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
    Attribs.pEnvMap  = pEnvMap;
    Attribs.MipLevel = ViewDesc.Skybox.MipLevel;
    Attribs.Alpha    = 0.f;
    Attribs.Scale    = GetLightingScale(ViewDesc.Skybox.Color, ViewDesc.Skybox.Intensity, ViewDesc.Skybox.Exposure);

    if (RequiresOutputSRGBConversion(m_RTVFormat))
        Attribs.Options |= EnvMapRenderer::OPTION_FLAG_CONVERT_OUTPUT_TO_SRGB;

    m_pRenderer->Prepare(pContext, Attribs, ToneMapping);
    m_pRenderer->Render(pContext);

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
