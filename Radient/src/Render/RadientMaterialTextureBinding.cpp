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
 */

#include "Render/RadientMaterialTextureBinding.hpp"

#include <utility>

namespace Diligent
{

RADIENT_STATUS BuildMaterialTextureBindingPlan(
    const RadientMaterialRenderData&                              MaterialData,
    const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
    PBR_Renderer::PSO_FLAGS                                       PSOFlags,
    Uint32                                                        MaxTextureSlots,
    const RadientMaterialTextureSRVArray&                         TextureSRVs,
    RadientMaterialTextureBindingPlan&                            Plan)
{
    if (!MaterialData)
    {
        Plan = {};
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    Uint32 ActiveTextureCount = 0;
    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID) {
            ++ActiveTextureCount;
        });

    const bool UseCompactMapping = ActiveTextureCount > MaxTextureSlots;

    RadientMaterialTextureBindingPlan NewPlan;
    RADIENT_STATUS                    Status = RADIENT_STATUS_OK;

    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            if (Status != RADIENT_STATUS_OK)
                return;

            const int           MaterialTextureAttribId = TextureAttribIndices[AttribId];
            ITextureView* const pTextureSRV             = TextureSRVs[AttribId];
            if (MaterialTextureAttribId < 0 ||
                (UseCompactMapping && pTextureSRV == nullptr))
            {
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            size_t SlotIndex = NewPlan.Slots.size();
            if (UseCompactMapping)
            {
                SlotIndex = 0;
                while (SlotIndex < NewPlan.Slots.size() &&
                       TextureSRVs[NewPlan.Slots[SlotIndex].TextureAttribId] != pTextureSRV)
                    ++SlotIndex;
            }

            if (SlotIndex == NewPlan.Slots.size())
            {
                if (NewPlan.Slots.size() >= MaxTextureSlots)
                {
                    Status = RADIENT_STATUS_INVALID_OPERATION;
                    return;
                }

                NewPlan.Slots.push_back({
                    AttribId,
                    MaterialData.GetTexture(static_cast<Uint32>(MaterialTextureAttribId)),
                });
            }

            NewPlan.ShaderTextureIds[AttribId] = static_cast<Uint16>(SlotIndex);
        });

    if (Status == RADIENT_STATUS_OK)
        Plan = std::move(NewPlan);
    else
        Plan = {};

    return Status;
}

} // namespace Diligent
