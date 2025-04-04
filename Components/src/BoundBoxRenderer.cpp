/*
 *  Copyright 2024-2025 Diligent Graphics LLC
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

#include "BoundBoxRenderer.hpp"

#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderMacroHelper.hpp"
#include "RenderStateCache.hpp"
#include "CommonlyUsedStates.h"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "ShaderSourceFactoryUtils.hpp"
#include "GraphicsAccessories.hpp"

namespace Diligent
{

struct BoundBoxRenderer::BoundBoxShaderAttribs
{
    float4x4 Transform;
    float4   Color;

    float PatternLength = 32;
    uint  PatternMask   = 0xFFFFFFFFu;
    float Padding0      = 0;
    float Padding1      = 0;
};

BoundBoxRenderer::BoundBoxRenderer(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pStateCache{CI.pStateCache},
    m_pCameraAttribsCB{CI.pCameraAttribsCB},
    m_RTVFormats{CI.RTVFormats, CI.RTVFormats + CI.NumRenderTargets},
    m_DSVFormat{CI.DSVFormat},
    m_PSMainSource{CI.PSMainSource != nullptr ? CI.PSMainSource : ""},
    m_RenderTargetMask{CI.RenderTargetMask},
    m_PackMatrixRowMajor{CI.PackMatrixRowMajor},
    m_AsyncShaders{CI.AsyncShaders}
{
    DEV_CHECK_ERR(m_pDevice != nullptr, "Device must not be null");
    DEV_CHECK_ERR(m_pCameraAttribsCB != nullptr, "Camera Attribs CB must not be null");

    const RenderDeviceInfo& DeviceInfo = m_pDevice->GetDeviceInfo();

    USAGE Usage = (DeviceInfo.IsGLDevice() || DeviceInfo.Type == RENDER_DEVICE_TYPE_D3D11) ?
        USAGE_DEFAULT :
        USAGE_DYNAMIC;
    if (Usage == USAGE_DEFAULT)
    {
        m_ShaderAttribs = std::make_unique<BoundBoxShaderAttribs>();
    }

    CreateUniformBuffer(m_pDevice, sizeof(BoundBoxShaderAttribs), "Bound Box Attribs CB",
                        &m_RenderAttribsCB, Usage, BIND_UNIFORM_BUFFER,
                        Usage == USAGE_DEFAULT ? CPU_ACCESS_NONE : CPU_ACCESS_WRITE,
                        Usage == USAGE_DEFAULT ? m_ShaderAttribs.get() : nullptr);
    VERIFY_EXPR(m_RenderAttribsCB != nullptr);
}

BoundBoxRenderer::~BoundBoxRenderer()
{
}

static constexpr char DefaultPSMain[] = R"(
void main(in BoundBoxVSOutput VSOut,
          out float4 Color : SV_Target)
{
    Color = GetBoundBoxOutput(VSOut).Color;
}
)";

IPipelineState* BoundBoxRenderer::GetPSO(const PSOKey& Key)
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
    if (m_PackMatrixRowMajor)
        ShaderCI.CompileFlags |= SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;
    if (m_AsyncShaders)
        ShaderCI.CompileFlags |= SHADER_COMPILE_FLAG_ASYNCHRONOUS;

    ShaderMacroHelper Macros;
    Macros
        .Add("CONVERT_OUTPUT_TO_SRGB", (Key.Flags & OPTION_FLAG_CONVERT_OUTPUT_TO_SRGB) != 0)
        .Add("COMPUTE_MOTION_VECTORS", (Key.Flags & OPTION_FLAG_COMPUTE_MOTION_VECTORS) != 0);
    ShaderCI.Macros = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"Bound Box VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = "BoundBoxVS";
        ShaderCI.FilePath   = "BoundBox.vsh";

        pVS = Device.CreateShader(ShaderCI);
        if (!pVS)
        {
            UNEXPECTED("Failed to create bound box vertex shader");
            return nullptr;
        }
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"Bound Box PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "BoundBox.psh";

        pPS = Device.CreateShader(ShaderCI);
        if (!pPS)
        {
            UNEXPECTED("Failed to create bound box pixel shader");
            return nullptr;
        }
    }

    GraphicsPipelineStateCreateInfoX PsoCI{"Bound Box PSO"};
    PsoCI
        .AddShader(pVS)
        .AddShader(pPS)
        .SetPrimitiveTopology(PRIMITIVE_TOPOLOGY_LINE_LIST)
        .SetDepthFormat(m_DSVFormat);
    for (TEXTURE_FORMAT RTVFormat : m_RTVFormats)
        PsoCI.AddRenderTarget(RTVFormat);

    PsoCI.PSODesc.ResourceLayout.DefaultVariableMergeStages  = SHADER_TYPE_VS_PS;
    PsoCI.PSODesc.ResourceLayout.DefaultVariableType         = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    PsoCI.GraphicsPipeline.DepthStencilDesc.DepthFunc        = (Key.Flags & OPTION_FLAG_USE_REVERSE_DEPTH) != 0 ? COMPARISON_FUNC_GREATER_EQUAL : COMPARISON_FUNC_LESS_EQUAL;
    PsoCI.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    for (Uint32 i = 0; i < PsoCI.GraphicsPipeline.NumRenderTargets; ++i)
    {
        PsoCI.GraphicsPipeline.BlendDesc.RenderTargets[i].RenderTargetWriteMask = (m_RenderTargetMask & (1u << i)) != 0 ?
            COLOR_MASK_ALL :
            COLOR_MASK_NONE;
    }

    if (m_AsyncShaders)
    {
        PsoCI.Flags |= PSO_CREATE_FLAG_ASYNCHRONOUS;
    }

    RefCntAutoPtr<IPipelineState> PSO = Device.CreateGraphicsPipelineState(PsoCI);
    if (!PSO)
    {
        UNEXPECTED("Failed to create bound box PSO");
        return nullptr;
    }

    m_PSOs.emplace(Key, PSO);
    return PSO;
}

void BoundBoxRenderer::Prepare(IDeviceContext* pContext, const RenderAttribs& Attribs)
{
    m_pCurrentPSO = GetPSO(PSOKey{Attribs.Options});
    if (m_pCurrentPSO == nullptr)
    {
        UNEXPECTED("Failed to get PSO");
        return;
    }

    if (m_pCurrentPSO->GetStatus() != PIPELINE_STATE_STATUS_READY)
        return;

    if (!m_SRB)
    {
        m_pCurrentPSO->CreateShaderResourceBinding(&m_SRB, true);
        m_SRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbCameraAttribs")->Set(m_pCameraAttribsCB);
        m_SRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbBoundBoxAttribs")->Set(m_RenderAttribsCB);
    }

    VERIFY_EXPR(Attribs.BoundBoxTransform != nullptr);

    if (m_ShaderAttribs)
    {
        const BoundBoxShaderAttribs ShaderAttribs{
            m_PackMatrixRowMajor ? *Attribs.BoundBoxTransform : Attribs.BoundBoxTransform->Transpose(),
            Attribs.Color != nullptr ? *Attribs.Color : float4{1.0, 1.0, 1.0, 1.0},
            Attribs.PatternLength,
            Attribs.PatternMask,
            0,
            0,
        };
        if (std::memcmp(m_ShaderAttribs.get(), &ShaderAttribs, sizeof(ShaderAttribs)) != 0)
        {
            *m_ShaderAttribs = ShaderAttribs;
            pContext->UpdateBuffer(m_RenderAttribsCB, 0, sizeof(BoundBoxShaderAttribs), m_ShaderAttribs.get(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            StateTransitionDesc Barrier{m_RenderAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE};
            pContext->TransitionResourceStates(1, &Barrier);
        }
    }
    else
    {
        if (MapHelper<BoundBoxShaderAttribs> BBAttribs{pContext, m_RenderAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD})
        {
            WriteShaderMatrix(&BBAttribs->Transform, *Attribs.BoundBoxTransform, !m_PackMatrixRowMajor);
            BBAttribs->Color         = Attribs.Color != nullptr ? *Attribs.Color : float4{1.0, 1.0, 1.0, 1.0};
            BBAttribs->PatternLength = Attribs.PatternLength;
            BBAttribs->PatternMask   = Attribs.PatternMask;
        }
    }
}

void BoundBoxRenderer::Render(IDeviceContext* pContext)
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

    if (m_pCurrentPSO->GetStatus() != PIPELINE_STATE_STATUS_READY || !m_SRB)
        return;

    pContext->SetPipelineState(m_pCurrentPSO);
    pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    DrawAttribs DrawAttrs{24, DRAW_FLAG_VERIFY_ALL};
    pContext->Draw(DrawAttrs);
}

} // namespace Diligent
