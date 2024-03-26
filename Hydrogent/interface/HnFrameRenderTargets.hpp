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

#include <array>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"

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

    std::array<ITextureView*, 2> ClosestSelectedLocationRTV = {};

    ITextureView* JitteredFinalColorRTV = nullptr;

    static const char* GetGBufferTargetName(GBUFFER_TARGET Id);
};

} // namespace USD

} // namespace Diligent
