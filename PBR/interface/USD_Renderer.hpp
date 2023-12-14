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

#pragma once

#include "PBR_Renderer.hpp"

namespace Diligent
{

/// Implementation of a GLTF PBR renderer
class USD_Renderer : public PBR_Renderer
{
public:
    struct CreateInfo : PBR_Renderer::CreateInfo
    {
        Uint32 ColorTargetIndex  = 0;
        Uint32 MeshIdTargetIndex = 1;
    };
    /// Initializes the renderer
    USD_Renderer(IRenderDevice*     pDevice,
                 IRenderStateCache* pStateCache,
                 IDeviceContext*    pCtx,
                 const CreateInfo&  CI);
    enum USD_PSO_FLAGS
    {
        USD_PSO_FLAG_NONE                             = 0,
        USD_PSO_FLAG_ENABLE_COLOR_OUTPUT              = PSO_FLAG_FIRST_USER_DEFINED << 0u,
        USD_PSO_FLAG_ENABLE_MESH_ID_OUTPUT            = PSO_FLAG_FIRST_USER_DEFINED << 1u,
        USD_PSO_FLAG_ENABLE_COLOR_AND_MESH_ID_OUTPUTS = USD_PSO_FLAG_ENABLE_COLOR_OUTPUT | USD_PSO_FLAG_ENABLE_MESH_ID_OUTPUT,
        USD_PSO_FLAG_ENABLE_ALL_OUTPUTS               = USD_PSO_FLAG_ENABLE_COLOR_AND_MESH_ID_OUTPUTS
    };

private:
    std::string GetUsdPbrPSMainSource(USD_Renderer::PSO_FLAGS PSOFlags) const;
    struct USDRendererCreateInfoWrapper;

private:
    const Uint32 m_ColorTargetIndex;
    const Uint32 m_MeshIdTargetIndex;
};
DEFINE_FLAG_ENUM_OPERATORS(USD_Renderer::USD_PSO_FLAGS)

} // namespace Diligent
