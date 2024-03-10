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

#include "HnFrameRenderTargets.hpp"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

const char* HnFrameRenderTargets::GetGBufferTargetName(GBUFFER_TARGET Id)
{
    static_assert(GBUFFER_TARGET_COUNT == 7, "Please handle all GBuffer target names.");
    switch (Id)
    {
        // clang-format off
        case GBUFFER_TARGET_SCENE_COLOR:   return "Scene color";
        case GBUFFER_TARGET_MESH_ID:       return "Mesh ID";
        case GBUFFER_TARGET_MOTION_VECTOR: return "Motion vectors";
        case GBUFFER_TARGET_NORMAL:        return "Normal";
        case GBUFFER_TARGET_BASE_COLOR:    return "Base color";
        case GBUFFER_TARGET_MATERIAL:      return "Material";
        case GBUFFER_TARGET_IBL:           return "IBL";
        // clang-format on
        default:
            UNEXPECTED("Unexpected GBuffer target");
            return "Unknown";
    };
}

} // namespace USD

} // namespace Diligent
