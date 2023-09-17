/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "EnvMapRenderer.hpp"

#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderMacroHelper.hpp"
#include "RenderStateCache.hpp"
#include "CommonlyUsedStates.h"
#include "MapHelper.hpp"

namespace Diligent
{

namespace HLSL
{

namespace
{

struct EnvMapRenderAttribs
{
    ToneMappingAttribs ToneMapping;

    float AverageLogLum;
    float MipLevel;
    float Unusued1;
    float Unusued2;
};

} // namespace

} // namespace HLSL

EnvMapRenderer::EnvMapRenderer(const CreateInfo& CI)
{
    DEV_CHECK_ERR(CI.pDevice != nullptr, "Device must not be null");
    DEV_CHECK_ERR(CI.pCameraAttribsCB != nullptr, "Camera Attribs CB must not be null");

    RenderDeviceWithCache<true> Device{CI.pDevice, CI.pStateCache};

    m_RenderAttribsCB = Device.CreateBuffer("EnvMap Render Attribs CB", sizeof(HLSL::EnvMapRenderAttribs));

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    ShaderMacroHelper Macros;
    Macros
        .Add("CONVERT_OUTPUT_TO_SRGB", CI.ConvertOutputToSRGB)
        .Add("TONE_MAPPING_MODE", CI.ToneMappingMode);
    ShaderCI.Macros = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"Environment Map VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "EnvMap.vsh";

        pVS = Device.CreateShader(ShaderCI);
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"Environment Map PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "EnvMap.psh";

        pPS = Device.CreateShader(ShaderCI);
    }

    PipelineResourceLayoutDescX ResourceLauout;
    ResourceLauout
        .AddVariable(SHADER_TYPE_PIXEL, "EnvMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        .AddImmutableSampler(SHADER_TYPE_PIXEL, "EnvMap", Sam_LinearClamp);

    GraphicsPipelineStateCreateInfoX PsoCI{"Environment Map PSO"};
    PsoCI
        .SetResourceLayout(ResourceLauout)
        .AddShader(pVS)
        .AddShader(pPS)
        .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .AddRenderTarget(CI.RTVFormat)
        .SetDepthFormat(CI.DSVFormat);

    PsoCI.GraphicsPipeline.DepthStencilDesc.DepthFunc = COMPARISON_FUNC_LESS_EQUAL;

    m_PSO = Device.CreateGraphicsPipelineState(PsoCI);
    m_PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbCameraAttribs")->Set(CI.pCameraAttribsCB);
    m_PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbEnvMapRenderAttribs")->Set(m_RenderAttribsCB);
    m_PSO->CreateShaderResourceBinding(&m_SRB, true);
    m_pEnvMapVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "EnvMap");
    VERIFY_EXPR(m_pEnvMapVar != nullptr);
}

void EnvMapRenderer::Render(const RenderAttribs& Attribs, const HLSL::ToneMappingAttribs& ToneMapping)
{
    if (Attribs.pContext == nullptr)
    {
        UNEXPECTED("Context must not be null");
        return;
    }

    if (Attribs.pEnvMap == nullptr)
    {
        UNEXPECTED("Environment map SRB must not be null");
        return;
    }

    m_pEnvMapVar->Set(Attribs.pEnvMap);

    {
        MapHelper<HLSL::EnvMapRenderAttribs> EnvMapAttribs{Attribs.pContext, m_RenderAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        EnvMapAttribs->ToneMapping   = ToneMapping;
        EnvMapAttribs->AverageLogLum = Attribs.AverageLogLum;
        EnvMapAttribs->MipLevel      = Attribs.MipLevel;
    }

    Attribs.pContext->SetPipelineState(m_PSO);
    Attribs.pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    DrawAttribs drawAttribs{3, DRAW_FLAG_VERIFY_ALL};
    Attribs.pContext->Draw(drawAttribs);
}

} // namespace Diligent
