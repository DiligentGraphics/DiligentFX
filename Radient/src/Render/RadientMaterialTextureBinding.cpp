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

#include "Render/RadientMaterialTextureBinding.hpp"

#include <utility>

namespace Diligent
{

const RadientMaterialTextureRenderData* RadientMaterialDefaultTextureBindings::Get(
    PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) const noexcept
{
    if (TextureAttribId >= PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT)
    {
        UNEXPECTED("Invalid PBR texture attribute ID ", Uint32{TextureAttribId});
        return nullptr;
    }

    static_assert(PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT == 17, "Update the switch statement below to handle new PBR texture attributes");
    switch (TextureAttribId)
    {
        case PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL:
        case PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL:
            return &Normal;

        case PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC:
            return &PhysicalDesc;

        case PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE:
            return &BlackSRGB;

        default:
            return PBR_Renderer::IsSRGBTextureAttribute(TextureAttribId) ?
                &WhiteSRGB :
                &WhiteLinear;
    }
}

RADIENT_STATUS BuildMaterialTextureBindingPlan(
    const RadientMaterialRenderData&                              MaterialData,
    const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
    PBR_Renderer::PSO_FLAGS                                       PSOFlags,
    Uint32                                                        MaxTextureSlots,
    const RadientMaterialDefaultTextureBindings&                  DefaultTextures,
    RadientMaterialTextureBindingPlan&                            Plan)
{
    if (MaxTextureSlots == 0)
    {
        UNEXPECTED("Maximum material texture slot count must not be zero");
        Plan = {};
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (MaxTextureSlots > PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT)
    {
        UNEXPECTED("Maximum material texture slot count ", MaxTextureSlots,
                   " exceeds the number of PBR texture attributes ",
                   Uint32{PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT});
        Plan = {};
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (!DefaultTextures)
    {
        UNEXPECTED("Default material texture bindings are not initialized");
        Plan = {};
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    bool CanUseDefaultMapping = true;
    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            if (AttribId >= MaxTextureSlots)
                CanUseDefaultMapping = false;
        });

    RadientMaterialTextureBindingPlan NewPlan;
    NewPlan.Slots.reserve(MaxTextureSlots);
    for (Uint32 Slot = 0; Slot < MaxTextureSlots; ++Slot)
    {
        const auto                              TextureAttribId = static_cast<PBR_Renderer::TEXTURE_ATTRIB_ID>(Slot);
        const RadientMaterialTextureRenderData* pDefaultTexture = DefaultTextures.Get(TextureAttribId);
        if (pDefaultTexture == nullptr || !*pDefaultTexture)
        {
            UNEXPECTED("Default material texture for PBR texture attribute ", Slot, " is not initialized");
            Plan = {};
            return RADIENT_STATUS_INVALID_OPERATION;
        }
        NewPlan.Slots.push_back(*pDefaultTexture);
    }

    if (CanUseDefaultMapping)
    {
        for (Uint32 Slot = 0; Slot < MaxTextureSlots; ++Slot)
            NewPlan.ShaderTextureIds[Slot] = static_cast<Uint16>(Slot);
    }

    Uint32         NextTextureSlot = 0;
    RADIENT_STATUS Status          = RADIENT_STATUS_OK;
    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            if (Status != RADIENT_STATUS_OK)
                return;

            const int TextureId = TextureAttribIndices[AttribId];
            if (TextureId < 0)
            {
                UNEXPECTED("PBR texture attribute ", Uint32{AttribId},
                           " does not have a material texture mapping");
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            const RadientMaterialTextureRenderData* pTexture =
                MaterialData.GetTextureData(static_cast<Uint32>(TextureId));
            if (pTexture == nullptr || !*pTexture)
            {
                LOG_ERROR_MESSAGE("Material texture ", TextureId,
                                  " used by PBR texture attribute ", Uint32{AttribId},
                                  " is not initialized");
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            size_t SlotIndex = AttribId;
            if (!CanUseDefaultMapping)
            {
                SlotIndex = 0;
                while (SlotIndex < NextTextureSlot &&
                       NewPlan.Slots[SlotIndex].BindingIdentity != pTexture->BindingIdentity)
                    ++SlotIndex;

                if (SlotIndex == NextTextureSlot)
                {
                    if (NextTextureSlot >= MaxTextureSlots)
                    {
                        LOG_ERROR_MESSAGE("Material requires more than ", MaxTextureSlots,
                                          " distinct texture bindings");
                        Status = RADIENT_STATUS_INVALID_OPERATION;
                        return;
                    }
                    ++NextTextureSlot;
                }
            }

            NewPlan.Slots[SlotIndex]           = *pTexture;
            NewPlan.ShaderTextureIds[AttribId] = static_cast<Uint16>(SlotIndex);
        });

    if (Status == RADIENT_STATUS_OK)
        Plan = std::move(NewPlan);
    else
        Plan = {};

    return Status;
}

} // namespace Diligent
