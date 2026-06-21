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
 *  for loss of goodwill, work stoppage, computer failure or malfunction, and all other
 *  commercial damages or losses), even if such Contributor has been advised of the
 *  possibility of such damages.
 */

#pragma once

#include "RadientAssets.h"

#include "Texture.h"

namespace Diligent
{

inline TEXTURE_FORMAT RadientToTextureFormat(RADIENT_TEXTURE_FORMAT Format)
{
#define RADIENT_TEXTURE_FORMAT_CASE(Fmt) \
    case RADIENT_TEXTURE_FORMAT_##Fmt: return TEX_FORMAT_##Fmt

    switch (Format)
    {
        RADIENT_TEXTURE_FORMAT_CASE(R8_UNORM);
        RADIENT_TEXTURE_FORMAT_CASE(RG8_UNORM);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA8_UNORM);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA8_UNORM_SRGB);
        RADIENT_TEXTURE_FORMAT_CASE(R8_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(RG8_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA8_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(R8_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(RG8_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA8_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(R16_UNORM);
        RADIENT_TEXTURE_FORMAT_CASE(RG16_UNORM);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA16_UNORM);
        RADIENT_TEXTURE_FORMAT_CASE(R16_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(RG16_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA16_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(R16_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(RG16_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA16_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(R32_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(RG32_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA32_UINT);
        RADIENT_TEXTURE_FORMAT_CASE(R32_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(RG32_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA32_SINT);
        RADIENT_TEXTURE_FORMAT_CASE(R32_FLOAT);
        RADIENT_TEXTURE_FORMAT_CASE(RG32_FLOAT);
        RADIENT_TEXTURE_FORMAT_CASE(RGBA32_FLOAT);
        case RADIENT_TEXTURE_FORMAT_UNKNOWN:
        default:
            return TEX_FORMAT_UNKNOWN;
    }

#undef RADIENT_TEXTURE_FORMAT_CASE
}

} // namespace Diligent
