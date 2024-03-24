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

#pragma once

#include <vector>

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

struct PostFXRenderTechnique
{
    static RefCntAutoPtr<IShader> CreateShader(IRenderDevice*          pDevice,
                                               IRenderStateCache*      pStateCache,
                                               const Char*             FileName,
                                               const Char*             EntryPoint,
                                               SHADER_TYPE             Type,
                                               const ShaderMacroArray& Macros = {});

    void InitializePSO(IRenderDevice*                     pDevice,
                       IRenderStateCache*                 pStateCache,
                       const char*                        PSOName,
                       IShader*                           VertexShader,
                       IShader*                           PixelShader,
                       const PipelineResourceLayoutDesc&  ResourceLayout,
                       const std::vector<TEXTURE_FORMAT>& RTVFmts,
                       TEXTURE_FORMAT                     DSVFmt,
                       const DepthStencilStateDesc&       DSSDesc,
                       const BlendStateDesc&              BSDesc,
                       bool                               IsDSVReadOnly);

    void InitializeSRB(bool InitStaticResources);

    bool IsInitializedPSO() const
    {
        return PSO != nullptr;
    }

    bool IsInitializedSRB() const
    {
        return SRB != nullptr;
    }

    RefCntAutoPtr<IPipelineState>         PSO{};
    RefCntAutoPtr<IShaderResourceBinding> SRB{};
};

} // namespace Diligent
