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

#include "USD_Renderer.hpp"

#include "RenderStateCache.hpp"
#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"

namespace Diligent
{

USD_Renderer::USD_Renderer(IRenderDevice*     pDevice,
                           IRenderStateCache* pStateCache,
                           IDeviceContext*    pCtx,
                           const CreateInfo&  CI) :
    PBR_Renderer{pDevice, pStateCache, pCtx, CI}
{
    CreatMeshEdgesPSO(pDevice, pStateCache);
}

void USD_Renderer::CreatMeshEdgesPSO(IRenderDevice* pDevice, IRenderStateCache* pStateCache)
{
    RenderDeviceWithCache<false> Device{pDevice, pStateCache};

    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc          = PSOCreateInfo.PSODesc;
    GraphicsPipelineDesc&           GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    PSODesc.Name         = "USD MeshEdges PSO";
    PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    GraphicsPipeline.NumRenderTargets  = 1;
    GraphicsPipeline.RTVFormats[0]     = m_Settings.RTVFmt;
    GraphicsPipeline.DSVFormat         = m_Settings.DSVFmt;
    GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_LINE_LIST;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    const auto Macros = DefineMacros();
    ShaderCI.Macros   = Macros;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc       = {"PBR VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.EntryPoint = m_Settings.MaxJointCount > 0 ? "VSMainSkinned" : "VSMain";
        ShaderCI.FilePath   = "RenderPBR.vsh";

        pVS = Device.CreateShader(ShaderCI);
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc       = {"USD Mesh Edges PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint = "main";
        ShaderCI.FilePath   = "RenderWireframe.psh";

        pPS = Device.CreateShader(ShaderCI);
    }

    const auto InputLayout                     = GetInputLayout();
    PSOCreateInfo.GraphicsPipeline.InputLayout = InputLayout;

    IPipelineResourceSignature* ppSignatures[] = {m_ResourceSignature};
    PSOCreateInfo.ppResourceSignatures         = ppSignatures;
    PSOCreateInfo.ResourceSignaturesCount      = _countof(ppSignatures);

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    m_MeshEdgesPSO = Device.CreateGraphicsPipelineState(PSOCreateInfo);
}

} // namespace Diligent
