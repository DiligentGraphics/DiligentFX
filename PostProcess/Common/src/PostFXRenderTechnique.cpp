/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "PostFXRenderTechnique.hpp"
#include "RenderStateCache.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"

namespace Diligent
{

RefCntAutoPtr<IShader> PostFXRenderTechnique::CreateShader(IRenderDevice*          pDevice,
                                                           IRenderStateCache*      pStateCache,
                                                           const Char*             FileName,
                                                           const Char*             EntryPoint,
                                                           SHADER_TYPE             Type,
                                                           const ShaderMacroArray& Macros)
{
    ShaderCreateInfo ShaderCI;
    ShaderCI.EntryPoint                      = EntryPoint;
    ShaderCI.FilePath                        = FileName;
    ShaderCI.Macros                          = Macros;
    ShaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.ShaderType                 = Type;
    ShaderCI.Desc.Name                       = EntryPoint;
    ShaderCI.pShaderSourceStreamFactory      = &DiligentFXShaderSourceStreamFactory::GetInstance();
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    return RenderDeviceWithCache<false>{pDevice, pStateCache}.CreateShader(ShaderCI);
}

void PostFXRenderTechnique::InitializePSO(IRenderDevice*                     pDevice,
                                          IRenderStateCache*                 pStateCache,
                                          const char*                        PSOName,
                                          IShader*                           VertexShader,
                                          IShader*                           PixelShader,
                                          const PipelineResourceLayoutDesc&  ResourceLayout,
                                          const std::vector<TEXTURE_FORMAT>& RTVFmts,
                                          TEXTURE_FORMAT                     DSVFmt,
                                          const DepthStencilStateDesc&       DSSDesc,
                                          const BlendStateDesc&              BSDesc,
                                          bool                               IsDSVReadOnly)
{

    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc = PSOCreateInfo.PSODesc;

    PSODesc.Name           = PSOName;
    PSODesc.ResourceLayout = ResourceLayout;

    auto& GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    GraphicsPipeline.RasterizerDesc.FillMode              = FILL_MODE_SOLID;
    GraphicsPipeline.RasterizerDesc.CullMode              = CULL_MODE_BACK;
    GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = false;
    GraphicsPipeline.DepthStencilDesc                     = DSSDesc;
    GraphicsPipeline.BlendDesc                            = BSDesc;
    PSOCreateInfo.pVS                                     = VertexShader;
    PSOCreateInfo.pPS                                     = PixelShader;
    GraphicsPipeline.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    GraphicsPipeline.NumRenderTargets                     = static_cast<Uint8>(RTVFmts.size());
    GraphicsPipeline.DSVFormat                            = DSVFmt;
    GraphicsPipeline.ReadOnlyDSV                          = IsDSVReadOnly;

    for (Uint32 RTIndex = 0; RTIndex < RTVFmts.size(); ++RTIndex)
        GraphicsPipeline.RTVFormats[RTIndex] = RTVFmts[RTIndex];

    PSO.Release();
    PSO = RenderDeviceWithCache<false>{pDevice, pStateCache}.CreateGraphicsPipelineState(PSOCreateInfo);
}

void PostFXRenderTechnique::InitializeSRB(bool InitStaticResources)
{
    SRB.Release();
    PSO->CreateShaderResourceBinding(&SRB, InitStaticResources);
}

} // namespace Diligent
