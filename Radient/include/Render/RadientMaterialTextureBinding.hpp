/*
 *  Copyright 2026 Diligent Graphics LLC
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

#include "Assets/RadientMaterialAssetManager.hpp"
#include "PBR_Renderer.hpp"

#include <array>

namespace Diligent
{

struct RadientMaterialTextureSlot
{
    PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId = PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT;
    IRadientTextureAsset*           pTexture        = nullptr;
};

/// Static shader texture indices and the corresponding SRB slot contents.
/// ShaderTextureIds is part of the PSO identity; Slots will form the SRB identity.
struct RadientMaterialTextureBindingPlan
{
    PBR_Renderer::StaticShaderTextureIdsArrayType                                 ShaderTextureIds;
    std::array<RadientMaterialTextureSlot, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> Slots{};
    Uint32                                                                        SlotCount = 0;

    RadientMaterialTextureBindingPlan() noexcept
    {
        ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
    }
};

/// Assigns every active material texture semantic to a distinct SRB slot in
/// stable PBR texture-attribute order. Null texture assets are preserved so the
/// SRB builder can bind the semantic's fallback atlas.
RADIENT_STATUS BuildStandardMaterialTextureBindingPlan(
    const RadientMaterialRenderData&                              MaterialData,
    const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
    PBR_Renderer::PSO_FLAGS                                       PSOFlags,
    Uint32                                                        MaxTextureSlots,
    RadientMaterialTextureBindingPlan&                            Plan);

} // namespace Diligent
