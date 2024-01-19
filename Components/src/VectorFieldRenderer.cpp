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

#include "VectorFieldRenderer.hpp"

#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderMacroHelper.hpp"
#include "RenderStateCache.hpp"
#include "CommonlyUsedStates.h"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "ShaderSourceFactoryUtils.hpp"

namespace Diligent
{

namespace HLSL
{

namespace
{

struct VectorFieldRenderAttribs
{
    float4 ScaleAndBias;

    float2 f2GridSize;
    uint2  u2GridSize;

    float4 StartColor;
    float4 EndColor;
};

} // namespace

} // namespace HLSL

VectorFieldRenderer::VectorFieldRenderer(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pStateCache{CI.pStateCache},
    m_RTVFormats{CI.RTVFormats, CI.RTVFormats + CI.NumRenderTargets},
    m_DSVFormat{CI.DSVFormat},
    m_PSMainSource{CI.PSMainSource != nullptr ? CI.PSMainSource : ""}
{
    DEV_CHECK_ERR(m_pDevice != nullptr, "Device must not be null");

    CreateUniformBuffer(m_pDevice, sizeof(HLSL::VectorFieldRenderAttribs), "Vector Field Render Attribs CB", &m_RenderAttribsCB);
    VERIFY_EXPR(m_RenderAttribsCB != nullptr);
}

static constexpr char DefaultPSMain[] = R"(
void main(in  float4 Pos      : SV_Position,
          in  float4 Color    : COLOR,
          out float4 OutColor : SV_Target)
{
    OutColor = Color;
#if CONVERT_OUTPUT_TO_SRGB
    OutColor.rgb = pow(OutColor.rgb, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
#endif
}
)";

IPipelineState* VectorFieldRenderer::GetPSO(const PSOKey& Key)
{
    auto it = m_PSOs.find(Key);
    if (it != m_PSOs.end())
        return it->second;

    RenderDeviceWithCache_N Device{m_pDevice, m_pStateCache};

    std::string PSMainSource = m_PSMainSource;
    if (PSMainSource.empty())
        PSMainSource = DefaultPSMain;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pMemorySourceFactory =
        CreateMemoryShaderSourceFactory({MemoryShaderSourceFileInfo{"PSMainGenerated.generated", PSMainSource}});
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory =
        CreateCompoundShaderSourceFactory({&DiligentFXShaderSourceStreamFactory::GetInstance(), pMemorySourceFactory});

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    ShaderMacroHelper Macros;
    Macros.Add("CONVERT_OUTPUT_TO_SRGB", Key.ConvertOutputToSRGB);
    ShaderCI.Macros = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"Vector Field VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "VectorField.vsh";

        pVS = Device.CreateShader(ShaderCI);
        if (!pVS)
        {
            UNEXPECTED("Failed to create vector field vertex shader");
            return nullptr;
        }
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"Vector Field PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "PSMainGenerated.generated";

        pPS = Device.CreateShader(ShaderCI);
        if (!pPS)
        {
            UNEXPECTED("Failed to create vector field pixel shader");
            return nullptr;
        }
    }

    PipelineResourceLayoutDescX ResourceLauout;
    ResourceLauout
        .SetDefaultVariableType(SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
        .AddVariable(SHADER_TYPE_VERTEX, "g_tex2DVectorField", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        .AddImmutableSampler(SHADER_TYPE_VERTEX, "g_tex2DVectorField", Sam_LinearClamp);

    GraphicsPipelineStateCreateInfoX PsoCI{"Vector Field PSO"};
    PsoCI
        .SetResourceLayout(ResourceLauout)
        .AddShader(pVS)
        .AddShader(pPS)
        .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_LINE_LIST)
        .SetDepthFormat(m_DSVFormat)
        .SetDepthStencilDesc(DSS_DisableDepth);
    for (auto RTVFormat : m_RTVFormats)
        PsoCI.AddRenderTarget(RTVFormat);

    auto PSO = Device.CreateGraphicsPipelineState(PsoCI);
    if (!PSO)
    {
        UNEXPECTED("Failed to create vector field PSO");
        return nullptr;
    }
    PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbAttribs")->Set(m_RenderAttribsCB);

    if (!m_SRB)
    {
        PSO->CreateShaderResourceBinding(&m_SRB, true);
        m_pVectorFieldVar = m_SRB->GetVariableByName(SHADER_TYPE_VERTEX, "g_tex2DVectorField");
        VERIFY_EXPR(m_pVectorFieldVar != nullptr);
    }

    m_PSOs.emplace(Key, PSO);
    return PSO;
}

void VectorFieldRenderer::Render(const RenderAttribs& Attribs)
{
    if (Attribs.pContext == nullptr)
    {
        UNEXPECTED("Context must not be null");
        return;
    }

    if (Attribs.GridSize.x == 0 || Attribs.GridSize.y == 0)
        return;

    if (Attribs.pVectorField == nullptr)
    {
        UNEXPECTED("Vector field must not be null");
        return;
    }

    auto* pPSO = GetPSO({Attribs.ConvertOutputToSRGB});
    if (pPSO == nullptr)
    {
        UNEXPECTED("Failed to get PSO");
        return;
    }

    m_pVectorFieldVar->Set(Attribs.pVectorField);

    {
        MapHelper<HLSL::VectorFieldRenderAttribs> AttribsData{Attribs.pContext, m_RenderAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        AttribsData->ScaleAndBias = {Attribs.Scale, Attribs.Bias};
        AttribsData->f2GridSize   = {static_cast<float>(Attribs.GridSize.x), static_cast<float>(Attribs.GridSize.y)};
        AttribsData->u2GridSize   = {Attribs.GridSize.x, Attribs.GridSize.y};
        AttribsData->StartColor   = Attribs.StartColor;
        AttribsData->EndColor     = Attribs.EndColor;
    }

    Attribs.pContext->SetPipelineState(pPSO);
    Attribs.pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    DrawAttribs drawAttribs{Attribs.GridSize.x * Attribs.GridSize.y * 2, DRAW_FLAG_VERIFY_ALL};
    Attribs.pContext->Draw(drawAttribs);
}

} // namespace Diligent
