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

#include "Render/Tessera/Passes/RadientTesseraPostProcessPipeline.hpp"

#include "Render/RadientPostFXParameters.hpp"

#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"

#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "ShaderMacroHelper.hpp"

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/Radient/public/RadientPostProcessStructures.fxh"
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

} // namespace

RADIENT_STATUS RadientTesseraPostProcessPipeline::Prepare(IRenderDevice*                   pDevice,
                                                          IDeviceContext*                  pContext,
                                                          const RadientFrameRenderTargets& Targets,
                                                          const RadientToneMappingDesc&    ToneMapping,
                                                          IBuffer*                         pFrameAttribsCB)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_OK;

    ITextureView* const pSceneColorSRV  = Targets.GetSceneColorSRV();
    ITextureView* const pOutputColorRTV = Targets.GetOutputColorRTV();
    if (pSceneColorSRV == nullptr || pOutputColorRTV == nullptr || pFrameAttribsCB == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const TEXTURE_FORMAT OutputFormat = GetTextureViewFormat(pOutputColorRTV);
    if (OutputFormat == TEX_FORMAT_UNKNOWN)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const HLSL::ToneMappingAttribs ToneMappingAttribs  = RadientPostFX::MakeToneMappingAttribs(ToneMapping);
    const bool                     ConvertOutputToSRGB = RequiresOutputSRGBConversion(OutputFormat);
    if (m_pPSO == nullptr ||
        m_OutputFormat != OutputFormat ||
        m_ToneMappingMode != ToneMappingAttribs.iToneMappingMode ||
        m_ConvertOutputToSRGB != ConvertOutputToSRGB ||
        m_pFrameAttribsCB != pFrameAttribsCB)
    {
        const RADIENT_STATUS Status = CreatePipelineState(pDevice,
                                                          OutputFormat,
                                                          ToneMappingAttribs.iToneMappingMode,
                                                          ConvertOutputToSRGB,
                                                          pFrameAttribsCB);
        if (RADIENT_FAILED(Status))
            return Status;
    }

    const bool ToneMappingEnabled = ToneMappingAttribs.iToneMappingMode > TONE_MAPPING_MODE_NONE;
    if (ToneMappingEnabled)
    {
        if (m_pToneMappingAttribsCB == nullptr)
        {
            CreateUniformBuffer(pDevice,
                                sizeof(HLSL::RadientPostProcessAttribs),
                                "Radient post-process attribs buffer",
                                m_pToneMappingAttribsCB.GetAddressOfEmpty(),
                                USAGE_DYNAMIC);
            if (m_pToneMappingAttribsCB == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;

            IShaderResourceVariable* const pAttribsVar =
                m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbRadientPostProcessAttribs");
            if (pAttribsVar == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
            pAttribsVar->Set(m_pToneMappingAttribsCB);
        }

        MapHelper<HLSL::RadientPostProcessAttribs> Attribs{pContext,
                                                           m_pToneMappingAttribsCB,
                                                           MAP_WRITE,
                                                           MAP_FLAG_DISCARD};
        if (!Attribs)
            return RADIENT_STATUS_INVALID_OPERATION;

        Attribs->ToneMapping = ToneMappingAttribs;
        // Hydrogent currently uses a fixed luminance value. Automatic
        // luminance reduction will replace this value in a later stage.
        Attribs->AverageLogLum = 0.3f;
    }

    if (m_pSRB == nullptr || m_TargetVersion != Targets.GetVersion())
    {
        m_pSRB.Release();
        m_pPSO->CreateShaderResourceBinding(m_pSRB.GetAddressOfEmpty(), true);
        if (m_pSRB == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        IShaderResourceVariable* const pColorVar =
            m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer");
        if (pColorVar == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        pColorVar->Set(pSceneColorSRV);
        m_TargetVersion = Targets.GetVersion();
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::Execute(IDeviceContext* pContext, const RadientFrameRenderTargets& Targets)
{
    if (pContext == nullptr)
        return RADIENT_STATUS_OK;

    ITextureView* pOutputColorRTV = Targets.GetOutputColorRTV();
    if (pOutputColorRTV == nullptr || m_pPSO == nullptr || m_pSRB == nullptr)
        return RADIENT_STATUS_OUT_OF_DATE;

    pContext->SetRenderTargets(1, &pOutputColorRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->SetPipelineState(m_pPSO);
    pContext->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    pContext->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraPostProcessPipeline::CreatePipelineState(IRenderDevice* pDevice,
                                                                      TEXTURE_FORMAT OutputFormat,
                                                                      Int32          ToneMappingMode,
                                                                      bool           ConvertOutputToSRGB,
                                                                      IBuffer*       pFrameAttribsCB)
{
    m_pSRB.Release();
    m_pPSO.Release();
    m_pToneMappingAttribsCB.Release();
    m_pFrameAttribsCB.Release();
    m_TargetVersion = ~Uint32{0};

    ShaderMacroHelper Macros;
    Macros.Add("TONE_MAPPING_MODE", ToneMappingMode);
    Macros.Add("CONVERT_OUTPUT_TO_SRGB", ConvertOutputToSRGB);

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.CompileFlags               = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    RefCntAutoPtr<IShader> pVS;
    ShaderCI.Desc       = {"Radient full-screen triangle VS", SHADER_TYPE_VERTEX, true};
    ShaderCI.EntryPoint = "FullScreenTriangleVS";
    ShaderCI.FilePath   = "FullScreenTriangleVS.fx";
    ShaderCI.Macros     = {};
    pDevice->CreateShader(ShaderCI, pVS.GetAddressOfEmpty());

    RefCntAutoPtr<IShader> pPS;
    ShaderCI.Desc       = {"Radient tone mapping PS", SHADER_TYPE_PIXEL, true};
    ShaderCI.EntryPoint = "main";
    ShaderCI.FilePath   = "RadientToneMapping.psh";
    ShaderCI.Macros     = Macros;
    pDevice->CreateShader(ShaderCI, pPS.GetAddressOfEmpty());
    if (pVS == nullptr || pPS == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    ShaderResourceVariableDesc Variables[] = {
        {SHADER_TYPE_PIXEL, "g_ColorBuffer", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
        {SHADER_TYPE_PIXEL, "cbRadientPostProcessAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_PIXEL, "cbFrameAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
    };
    const bool ToneMappingEnabled = ToneMappingMode > TONE_MAPPING_MODE_NONE;

    GraphicsPipelineStateCreateInfo PSOCI;
    PSOCI.PSODesc.Name                                       = "Radient tone mapping PSO";
    PSOCI.PSODesc.PipelineType                               = PIPELINE_TYPE_GRAPHICS;
    PSOCI.PSODesc.ResourceLayout.Variables                   = Variables;
    PSOCI.PSODesc.ResourceLayout.NumVariables                = ToneMappingEnabled ? _countof(Variables) : 1;
    PSOCI.GraphicsPipeline.NumRenderTargets                  = 1;
    PSOCI.GraphicsPipeline.RTVFormats[0]                     = OutputFormat;
    PSOCI.GraphicsPipeline.PrimitiveTopology                 = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCI.GraphicsPipeline.RasterizerDesc.CullMode           = CULL_MODE_NONE;
    PSOCI.GraphicsPipeline.DepthStencilDesc.DepthEnable      = False;
    PSOCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = False;
    PSOCI.pVS                                                = pVS;
    PSOCI.pPS                                                = pPS;
    pDevice->CreateGraphicsPipelineState(PSOCI, m_pPSO.GetAddressOfEmpty());
    if (m_pPSO == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (ToneMappingEnabled)
    {
        IShaderResourceVariable* const pFrameAttribsVar =
            m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbFrameAttribs");
        if (pFrameAttribsVar == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        pFrameAttribsVar->Set(pFrameAttribsCB);
    }

    m_pFrameAttribsCB     = pFrameAttribsCB;
    m_OutputFormat        = OutputFormat;
    m_ToneMappingMode     = ToneMappingMode;
    m_ConvertOutputToSRGB = ConvertOutputToSRGB;
    return RADIENT_STATUS_OK;
}

} // namespace Diligent
