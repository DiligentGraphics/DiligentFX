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

#pragma once

#include <array>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"
#include "../../../DiligentFX/PBR/interface/USD_Renderer.hpp"

namespace Diligent
{

namespace USD
{

struct HnFrameRenderTargets
{
    enum GBUFFER_TARGET : Uint32
    {
        GBUFFER_TARGET_SCENE_COLOR,
        GBUFFER_TARGET_MESH_ID,
        GBUFFER_TARGET_MOTION_VECTOR,
        GBUFFER_TARGET_NORMAL,
        GBUFFER_TARGET_BASE_COLOR,
        GBUFFER_TARGET_MATERIAL,
        GBUFFER_TARGET_IBL,
        GBUFFER_TARGET_COUNT
    };

    ITextureView* FinalColorRTV = nullptr;

    std::array<ITextureView*, GBUFFER_TARGET_COUNT> GBufferRTVs = {};
    std::array<ITextureView*, GBUFFER_TARGET_COUNT> GBufferSRVs = {};

    ITextureView* SelectionDepthDSV = nullptr;
    ITextureView* DepthDSV          = nullptr;
    ITextureView* PrevDepthDSV      = nullptr;

    PBR_Renderer::OITResources OIT;

    std::array<ITextureView*, 2> ClosestSelectedLocationRTV = {};

    ITextureView* JitteredFinalColorRTV = nullptr;

    uint32_t Version = 0;

    static const char* GetGBufferTargetName(GBUFFER_TARGET Id);

    static constexpr GBUFFER_TARGET GetGBufferTargetFromRendererOutputFlag(USD_Renderer::USD_PSO_FLAGS OutputFlag)
    {
        static_assert(GBUFFER_TARGET_COUNT == 7, "Did you add a new GBuffer target? Please handle it here.");
        switch (OutputFlag)
        {
                // clang-format off
            case USD_Renderer::USD_PSO_FLAG_ENABLE_COLOR_OUTPUT:          return GBUFFER_TARGET_SCENE_COLOR;
            case USD_Renderer::USD_PSO_FLAG_ENABLE_MESH_ID_OUTPUT:        return GBUFFER_TARGET_MESH_ID;
            case USD_Renderer::USD_PSO_FLAG_ENABLE_MOTION_VECTORS_OUTPUT: return GBUFFER_TARGET_MOTION_VECTOR;
            case USD_Renderer::USD_PSO_FLAG_ENABLE_NORMAL_OUTPUT:         return GBUFFER_TARGET_NORMAL;
            case USD_Renderer::USD_PSO_FLAG_ENABLE_BASE_COLOR_OUTPUT:     return GBUFFER_TARGET_BASE_COLOR;
            case USD_Renderer::USD_PSO_FLAG_ENABLE_MATERIAL_DATA_OUTPUT:  return GBUFFER_TARGET_MATERIAL;
            case USD_Renderer::USD_PSO_FLAG_ENABLE_IBL_OUTPUT:            return GBUFFER_TARGET_IBL;
                // clang-format on

            default:
                return GBUFFER_TARGET_COUNT;
        }
    }
};

} // namespace USD

} // namespace Diligent
