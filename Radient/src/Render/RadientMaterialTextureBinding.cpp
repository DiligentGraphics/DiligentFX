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

#include <algorithm>
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

    Uint32 ActiveTextureCount   = 0;
    bool   CanUseDefaultMapping = true;
    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            ++ActiveTextureCount;
            if (AttribId >= MaxTextureSlots)
                CanUseDefaultMapping = false;
        });

    const bool UseDefaultMapping = ActiveTextureCount != 0 && CanUseDefaultMapping;

    RadientMaterialTextureBindingPlan NewPlan;
    RADIENT_STATUS                    Status = RADIENT_STATUS_OK;

    if (UseDefaultMapping)
    {
        NewPlan.Bindings.resize(std::min(MaxTextureSlots, Uint32{PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT}));
        for (Uint32 Slot = 0; Slot < NewPlan.Bindings.size(); ++Slot)
            NewPlan.ShaderTextureIds[Slot] = static_cast<Uint16>(Slot);
    }

    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            if (Status != RADIENT_STATUS_OK)
                return;

            const int           MaterialTextureAttribId = TextureAttribIndices[AttribId];
            ITextureView* const pTextureSRV             = TextureSRVs[AttribId];
            if (MaterialTextureAttribId < 0 ||
                (!UseDefaultMapping && pTextureSRV == nullptr))
            {
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            size_t SlotIndex = AttribId;
            if (!UseDefaultMapping)
            {
                SlotIndex = 0;
                while (SlotIndex < NewPlan.Bindings.size() &&
                       TextureSRVs[NewPlan.Bindings[SlotIndex].TextureAttribId] != pTextureSRV)
                    ++SlotIndex;
            }

            if (UseDefaultMapping)
            {
                NewPlan.Bindings[SlotIndex] = {
                    AttribId,
                    MaterialData.GetTexture(static_cast<Uint32>(MaterialTextureAttribId)),
                };
            }
            else if (SlotIndex == NewPlan.Bindings.size())
            {
                if (NewPlan.Bindings.size() >= MaxTextureSlots)
                {
                    Status = RADIENT_STATUS_INVALID_OPERATION;
                    return;
                }

                NewPlan.Bindings.push_back({
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
