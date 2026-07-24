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

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127) // conditional expression is constant
#endif
#include "absl/container/inlined_vector.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <array>
#include <cstddef>

namespace Diligent
{

struct RadientMaterialTextureSlot
{
    // Texture attribute ID (e.g. BASE_COLOR, NORMAL, etc.).
    PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId = PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT;

    // Pointer to the texture asset. Can be nullptr if the slot is empty.
    IRadientTextureAsset* pTexture = nullptr;
};

/// Static shader texture indices and the corresponding SRB slot contents.
/// ShaderTextureIds is part of the PSO identity; Slots will form the SRB identity.
struct RadientMaterialTextureBindingPlan
{
    PBR_Renderer::StaticShaderTextureIdsArrayType ShaderTextureIds;

    absl::InlinedVector<RadientMaterialTextureSlot, 8> Slots;

    RadientMaterialTextureBindingPlan() noexcept
    {
        ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
    }
};

using RadientMaterialTextureSRVArray =
    std::array<ITextureView*, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>;

/// Builds the preferred material texture binding plan. Materials that fit in
/// the slot budget retain one slot per active semantic. Larger materials group
/// semantics that resolve to the same SRV and fail only when the number of
/// distinct SRVs exceeds the slot budget.
RADIENT_STATUS BuildMaterialTextureBindingPlan(
    const RadientMaterialRenderData&                              MaterialData,
    const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
    PBR_Renderer::PSO_FLAGS                                       PSOFlags,
    Uint32                                                        MaxTextureSlots,
    const RadientMaterialTextureSRVArray&                         TextureSRVs,
    RadientMaterialTextureBindingPlan&                            Plan);

} // namespace Diligent
