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
#include "../../Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"
#include "ShaderSourceFactoryUtils.h"

namespace Diligent
{

std::string USD_Renderer::GetUsdPbrPSMainSource(USD_Renderer::PSO_FLAGS PSOFlags)
{
    std::stringstream ss;
    ss << "struct PSOutput" << std::endl
       << '{' << std::endl
       << "    float4 Color      : SV_Target" << m_ColorTargetIndex << ';' << std::endl;

    if (PSOFlags & USD_PSO_FLAG_ENABLE_MESH_ID_OUTPUT)
        ss << "    float4 MeshID     : SV_Target" << m_MeshIdTargetIndex << ';' << std::endl;

    ss << "};" << std::endl;

    ss << R"(
void main(in VSOutput VSOut,
          in bool IsFrontFace : SV_IsFrontFace,
          out PSOutput PSOut)
{
    PSOut.Color = ComputePbrSurfaceColor(VSOut, IsFrontFace);
)";

    // It is important to set alpha to 1.0 as all targets are rendered with the same blend mode
    if (PSOFlags & USD_PSO_FLAG_ENABLE_MESH_ID_OUTPUT)
        ss << "    PSOut.MeshID     = float4(g_PBRAttribs.Renderer.CustomData.x, 0.0, 0.0, 1.0);" << std::endl;

    ss << "}" << std::endl;

    return ss.str();
}

USD_Renderer::USD_Renderer(IRenderDevice*     pDevice,
                           IRenderStateCache* pStateCache,
                           IDeviceContext*    pCtx,
                           const CreateInfo&  CI) :
    PBR_Renderer{
        pDevice,
        pStateCache,
        pCtx,
        [this](CreateInfo CI) {
            if (CI.GetPSMainSource == nullptr)
                CI.GetPSMainSource = [this](USD_Renderer::PSO_FLAGS PSOFlags) {
                    return GetUsdPbrPSMainSource(PSOFlags);
                };
            return CI;
        }(CI),
    },
    m_ColorTargetIndex{CI.ColorTargetIndex},
    m_MeshIdTargetIndex{CI.MeshIdTargetIndex}
{
}

} // namespace Diligent
