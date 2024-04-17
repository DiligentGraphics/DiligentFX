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

#include "ToneMapping.hpp"

namespace Diligent
{

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

} // namespace Diligent
