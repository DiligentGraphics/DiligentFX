/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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
#include "../../Shaders/Common/public/ShaderDefinitions.fxh"
#include "../../Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
} // namespace HLSL


struct EnvMapRenderer::EnvMapShaderAttribs
{
    HLSL::ToneMappingAttribs ToneMapping;

    float AverageLogLum = 0.3f;
    float MipLevel      = 0.f;
    float Alpha         = 0.f;
    float Padding       = 0.f;

    float4 Scale{1, 1, 1, 1};
};

EnvMapRenderer::EnvMapRenderer(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pStateCache{CI.pStateCache},
    m_pCameraAttribsCB{CI.pCameraAttribsCB},
    m_RTVFormats{CI.RTVFormats, CI.RTVFormats + CI.NumRenderTargets},
    m_DSVFormat{CI.DSVFormat},
    m_RenderTargetMask{CI.RenderTargetMask},
    m_PSMainSource{CI.PSMainSource != nullptr ? CI.PSMainSource : ""},
    m_PackMatrixRowMajor{CI.PackMatrixRowMajor}
{
    DEV_CHECK_ERR(m_pDevice != nullptr, "Device must not be null");
    DEV_CHECK_ERR(m_pCameraAttribsCB != nullptr, "Camera Attribs CB must not be null");

    const RenderDeviceInfo& DeviceInfo = m_pDevice->GetDeviceInfo();

    USAGE Usage = (DeviceInfo.IsGLDevice() || DeviceInfo.Type == RENDER_DEVICE_TYPE_D3D11) ?
        USAGE_DEFAULT :
        USAGE_DYNAMIC;
    if (Usage == USAGE_DEFAULT)
    {
        m_ShaderAttribs = std::make_unique<EnvMapShaderAttribs>();
    }

    CreateUniformBuffer(m_pDevice, sizeof(EnvMapShaderAttribs), "EnvMap Render Attribs CB",
                        &m_RenderAttribsCB, Usage, BIND_UNIFORM_BUFFER,
                        Usage == USAGE_DEFAULT ? CPU_ACCESS_NONE : CPU_ACCESS_WRITE,
                        Usage == USAGE_DEFAULT ? m_ShaderAttribs.get() : nullptr);
    VERIFY_EXPR(m_RenderAttribsCB != nullptr);
}

EnvMapRenderer::~EnvMapRenderer()
{
}

static constexpr char DefaultPSMain[] = R"(
void main(in  float4 Pos     : SV_Position,
          in  float4 ClipPos : CLIP_POS,
          out float4 Color   : SV_Target)
{
    Color = SampleEnvMap(ClipPos).Color;
}
)";

IPipelineState* EnvMapRenderer::GetPSO(const PSOKey& Key)
{
    auto it = m_PSOs.find(Key);
    if (it != m_PSOs.end())
        return it->second;

    RenderDeviceWithCache_N Device{m_pDevice, m_pStateCache};

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pMemorySourceFactory =
        CreateMemoryShaderSourceFactory({MemoryShaderSourceFileInfo{"PSMainGenerated.generated", !m_PSMainSource.empty() ? m_PSMainSource.c_str() : DefaultPSMain}});
    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory =
        CreateCompoundShaderSourceFactory({&DiligentFXShaderSourceStreamFactory::GetInstance(), pMemorySourceFactory});

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
    ShaderCI.CompileFlags               = m_PackMatrixRowMajor ? SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR : SHADER_COMPILE_FLAG_NONE;

    ShaderMacroHelper Macros;
    Macros
        .Add("CONVERT_OUTPUT_TO_SRGB", (Key.Flags & OPTION_FLAG_CONVERT_OUTPUT_TO_SRGB) != 0)
        .Add("TONE_MAPPING_MODE", Key.ToneMappingMode)
        .Add("COMPUTE_MOTION_VECTORS", (Key.Flags & OPTION_FLAG_COMPUTE_MOTION_VECTORS) != 0)
        .Add("ENV_MAP_TYPE_CUBE", static_cast<int>(PSOKey::ENV_MAP_TYPE_CUBE))
        .Add("ENV_MAP_TYPE_SPHERE", static_cast<int>(PSOKey::ENV_MAP_TYPE_SPHERE))
        .Add("ENV_MAP_TYPE", static_cast<int>(Key.EnvMapType));
    ShaderCI.Macros = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"Environment Map VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "EnvMap.vsh";

        pVS = Device.CreateShader(ShaderCI);
        if (!pVS)
        {
            UNEXPECTED("Failed to create environment map vertex shader");
            return nullptr;
        }
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"Environment Map PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "EnvMap.psh";

        pPS = Device.CreateShader(ShaderCI);
        if (!pPS)
        {
            UNEXPECTED("Failed to create environment map pixel shader");
            return nullptr;
        }
    }

    PipelineResourceLayoutDescX ResourceLauout;
    ResourceLauout
        .SetDefaultVariableMergeStages(SHADER_TYPE_VS_PS)
        .AddVariable(SHADER_TYPE_PIXEL, "EnvMap", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        .AddImmutableSampler(SHADER_TYPE_PIXEL, "EnvMap", Sam_LinearClamp);

    GraphicsPipelineStateCreateInfoX PsoCI{"Environment Map PSO"};
    PsoCI
        .SetResourceLayout(ResourceLauout)
        .AddShader(pVS)
        .AddShader(pPS)
        .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .SetDepthFormat(m_DSVFormat);
    for (TEXTURE_FORMAT RTVFormat : m_RTVFormats)
        PsoCI.AddRenderTarget(RTVFormat);

    PsoCI.GraphicsPipeline.DepthStencilDesc.DepthFunc        = (Key.Flags & OPTION_FLAG_USE_REVERSE_DEPTH) != 0 ? COMPARISON_FUNC_GREATER_EQUAL : COMPARISON_FUNC_LESS_EQUAL;
    PsoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    for (Uint32 i = 0; i < PsoCI.GraphicsPipeline.NumRenderTargets; ++i)
    {
        PsoCI.GraphicsPipeline.BlendDesc.RenderTargets[i].RenderTargetWriteMask = (m_RenderTargetMask & (1u << i)) != 0 ?
            COLOR_MASK_ALL :
            COLOR_MASK_NONE;
    }

    RefCntAutoPtr<IPipelineState> PSO = Device.CreateGraphicsPipelineState(PsoCI);
    if (!PSO)
    {
        UNEXPECTED("Failed to create environment map PSO");
        return nullptr;
    }
    PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbCameraAttribs")->Set(m_pCameraAttribsCB);
    PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbEnvMapRenderAttribs")->Set(m_RenderAttribsCB);

    if (!m_SRB)
    {
        PSO->CreateShaderResourceBinding(&m_SRB, true);
        m_pEnvMapVar = m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "EnvMap");
        VERIFY_EXPR(m_pEnvMapVar != nullptr);
    }

    m_PSOs.emplace(Key, PSO);
    return PSO;
}

void EnvMapRenderer::Prepare(IDeviceContext*                 pContext,
                             const RenderAttribs&            Attribs,
                             const HLSL::ToneMappingAttribs& ToneMapping)
{
    if (Attribs.pEnvMap == nullptr)
    {
        UNEXPECTED("Environment map must not be null");
        return;
    }

    const PSOKey::ENV_MAP_TYPE EnvMapType = Attribs.pEnvMap->GetTexture()->GetDesc().IsCube() ?
        PSOKey::ENV_MAP_TYPE_CUBE :
        PSOKey::ENV_MAP_TYPE_SPHERE;

    m_pCurrentPSO = GetPSO({ToneMapping.iToneMappingMode, Attribs.Options, EnvMapType});
    if (m_pCurrentPSO == nullptr)
    {
        UNEXPECTED("Failed to get PSO");
        return;
    }

    m_pEnvMapVar->Set(Attribs.pEnvMap);

    if (m_ShaderAttribs)
    {
        if (std::memcmp(&m_ShaderAttribs->ToneMapping, &ToneMapping, sizeof(ToneMapping)) != 0 ||
            m_ShaderAttribs->AverageLogLum != Attribs.AverageLogLum ||
            m_ShaderAttribs->MipLevel != Attribs.MipLevel ||
            m_ShaderAttribs->Alpha != Attribs.Alpha ||
            m_ShaderAttribs->Scale != float4{Attribs.Scale, 1})
        {
            m_ShaderAttribs->ToneMapping   = ToneMapping;
            m_ShaderAttribs->AverageLogLum = Attribs.AverageLogLum;
            m_ShaderAttribs->MipLevel      = Attribs.MipLevel;
            m_ShaderAttribs->Alpha         = Attribs.Alpha;
            m_ShaderAttribs->Scale         = float4{Attribs.Scale, 1};

            pContext->UpdateBuffer(m_RenderAttribsCB, 0, sizeof(EnvMapShaderAttribs), m_ShaderAttribs.get(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            StateTransitionDesc Barrier{m_RenderAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            pContext->TransitionResourceStates(1, &Barrier);
        }
    }
    else
    {
        if (MapHelper<EnvMapShaderAttribs> EnvMapAttribs{pContext, m_RenderAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD})
        {
            EnvMapAttribs->ToneMapping   = ToneMapping;
            EnvMapAttribs->AverageLogLum = Attribs.AverageLogLum;
            EnvMapAttribs->MipLevel      = Attribs.MipLevel;
            EnvMapAttribs->Alpha         = Attribs.Alpha;
            EnvMapAttribs->Scale         = float4{Attribs.Scale, 1};
        }
    }
}

void EnvMapRenderer::Render(IDeviceContext* pContext)
{
    if (pContext == nullptr)
    {
        UNEXPECTED("Context must not be null");
        return;
    }

    if (m_pCurrentPSO == nullptr)
    {
        UNEXPECTED("Current PSO is null. Did you forget to call Prepare()?");
        return;
    }

    pContext->SetPipelineState(m_pCurrentPSO);
    pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    DrawAttribs drawAttribs{3, DRAW_FLAG_VERIFY_ALL};
    pContext->Draw(drawAttribs);
}

} // namespace Diligent
