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

namespace Diligent
{

RADIENT_STATUS BuildStandardMaterialTextureBindingPlan(
    const RadientMaterialRenderData&                              MaterialData,
    const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
    PBR_Renderer::PSO_FLAGS                                       PSOFlags,
    Uint32                                                        MaxTextureSlots,
    RadientMaterialTextureBindingPlan&                            Plan)
{
    RadientMaterialTextureBindingPlan NewPlan;
    RADIENT_STATUS                    Status = MaterialData ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_ARGUMENT;

    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int SlotIndex, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            if (Status != RADIENT_STATUS_OK)
                return;

            if (static_cast<Uint32>(SlotIndex) >= MaxTextureSlots)
            {
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            const int MaterialTextureAttribId = TextureAttribIndices[AttribId];
            if (MaterialTextureAttribId < 0)
            {
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            NewPlan.ShaderTextureIds[AttribId] = static_cast<Uint16>(SlotIndex);

            NewPlan.Slots[SlotIndex] = {
                AttribId,
                MaterialData.GetTexture(static_cast<Uint32>(MaterialTextureAttribId)),
            };
            ++NewPlan.SlotCount;
        });

    Plan = Status == RADIENT_STATUS_OK ? NewPlan : RadientMaterialTextureBindingPlan{};
    return Status;
}

} // namespace Diligent
