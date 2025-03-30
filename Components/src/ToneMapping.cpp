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

#include "imgui.h"
#include "ImGuiUtils.hpp"

#include "ToneMapping.hpp"

#include <array>

namespace Diligent
{

namespace HLSL
{
#include "Shaders/Common/public/ShaderDefinitions.fxh"
#include "Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
} // namespace HLSL

float3 ReverseExpToneMap(const float3& Color, float MiddleGray, float AverageLogLum)
{
    // Exponential tone mapping is computed as follows:
    //
    //     float LumScale         = MiddleGray / AverageLogLum;
    //     float InitialLum       = dot(RGB_TO_LUMINANCE, Color);
    //     float ScaledLum        = InitialLum * LumScale;
    //     float3 ScaledColor     = Color * LumScale;
    //     float  ToneMappedLum   = 1.0 - exp(-ScaledLum);
    //     float3 ToneMappedColor = ToneMappedLum * pow(Color / InitialLum, LuminanceSaturation);
    //
    // To reverse this operator, we will make the following assumptions:
    //  - The color is grayscale:
    //      - Color = (L, L, L)
    //      - InitialLum = L
    //  - LuminanceSaturation is 1.0 (this is the default value)
    //
    // With the assumption the math simplifies to:
    //
    //     float3 ToneMappedColor = ToneMappedLum
    //                            = 1.0 - exp(-ScaledLum)
    //                            = 1.0 - exp(-InitialLum * LumScale)
    // Thus
    //
    //      ToneMappedLum = 1.0 - exp(-InitialLum * LumScale)
    //
    // And
    //
    //     InitialLum = -log(1.0 - ToneMappedLum) / LumScale
    //

    static constexpr float3 RGB_TO_LUMINANCE{0.212671f, 0.715160f, 0.072169f};

    const float Luminance = dot(RGB_TO_LUMINANCE, Color);
    if (Luminance == 0)
        return float3{0, 0, 0};

    float fLumScale     = MiddleGray / AverageLogLum;
    float ToneMappedLum = -std::log(std::max(1.0f - Luminance, 0.01f)) / fLumScale;
    return Color * ToneMappedLum / Luminance;
}

bool ToneMappingUpdateUI(HLSL::ToneMappingAttribs& Attribs, float* AverageLogLum)
{
    bool AttribsChanged = false;
    {
        std::array<const char*, 12> ToneMappingMode{};
        ToneMappingMode[TONE_MAPPING_MODE_NONE]         = "None";
        ToneMappingMode[TONE_MAPPING_MODE_EXP]          = "Exp";
        ToneMappingMode[TONE_MAPPING_MODE_REINHARD]     = "Reinhard";
        ToneMappingMode[TONE_MAPPING_MODE_REINHARD_MOD] = "Reinhard Mod";
        ToneMappingMode[TONE_MAPPING_MODE_UNCHARTED2]   = "Uncharted 2";
        ToneMappingMode[TONE_MAPPING_MODE_FILMIC_ALU]   = "Filmic ALU";
        ToneMappingMode[TONE_MAPPING_MODE_LOGARITHMIC]  = "Logarithmic";
        ToneMappingMode[TONE_MAPPING_MODE_ADAPTIVE_LOG] = "Adaptive log";
        ToneMappingMode[TONE_MAPPING_MODE_AGX]          = "AgX";
        ToneMappingMode[TONE_MAPPING_MODE_AGX_CUSTOM]   = "AgX Custom";
        ToneMappingMode[TONE_MAPPING_MODE_PBR_NEUTRAL]  = "PBR Neutral";
        ToneMappingMode[TONE_MAPPING_MODE_COMMERCE]     = "Commerce";
        if (ImGui::Combo("Tone Mapping Mode", &Attribs.iToneMappingMode, ToneMappingMode.data(), static_cast<int>(ToneMappingMode.size())))
            AttribsChanged = true;
    }

    if (AverageLogLum != nullptr)
    {
        if (ImGui::SliderFloat("Average log lum", AverageLogLum, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic))
            AttribsChanged = true;
    }

    if (ImGui::SliderFloat("Middle gray", &Attribs.fMiddleGray, 0.01f, 1.0f))
        AttribsChanged = true;

    if (Attribs.iToneMappingMode == TONE_MAPPING_MODE_REINHARD_MOD ||
        Attribs.iToneMappingMode == TONE_MAPPING_MODE_UNCHARTED2 ||
        Attribs.iToneMappingMode == TONE_MAPPING_MODE_LOGARITHMIC ||
        Attribs.iToneMappingMode == TONE_MAPPING_MODE_ADAPTIVE_LOG)
    {
        if (ImGui::SliderFloat("White point", &Attribs.fWhitePoint, 0.1f, 20.0f))
            AttribsChanged = true;
    }

    if (Attribs.iToneMappingMode == TONE_MAPPING_MODE_AGX_CUSTOM)
    {
        if (ImGui::SliderFloat("AgX Saturation", &Attribs.AgX.Saturation, 0.0f, 2.0f))
            AttribsChanged = true;
        if (ImGui::SliderFloat("AgX Offset", &Attribs.AgX.Offset, 0.0f, 1.0f))
            AttribsChanged = true;
        if (ImGui::SliderFloat("AgX Slope", &Attribs.AgX.Slope, 0.0f, 10.0f))
            AttribsChanged = true;
        if (ImGui::SliderFloat("AgX Power", &Attribs.AgX.Power, 0.0f, 10.0f))
            AttribsChanged = true;
    }

    return AttribsChanged;
}

} // namespace Diligent
