/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "HnTypeConversions.hpp"

namespace Diligent
{

namespace USD
{

TEXTURE_ADDRESS_MODE HdWrapToAddressMode(pxr::HdWrap hdWrap)
{
    switch (hdWrap)
    {
        // clang-format off
        case pxr::HdWrapClamp:     return TEXTURE_ADDRESS_CLAMP;
        case pxr::HdWrapRepeat:    return TEXTURE_ADDRESS_WRAP;
        case pxr::HdWrapBlack:     return TEXTURE_ADDRESS_BORDER;
        case pxr::HdWrapMirror:    return TEXTURE_ADDRESS_MIRROR;
        case pxr::HdWrapNoOpinion: return TEXTURE_ADDRESS_CLAMP;
        // clang-format on
        default: return TEXTURE_ADDRESS_WRAP;
    }
}

FILTER_TYPE HdMagFilterToFilterType(pxr::HdMagFilter hdMinFilter)
{
    switch (hdMinFilter)
    {
        // clang-format off
        case pxr::HdMagFilterNearest: return FILTER_TYPE_POINT;
        case pxr::HdMagFilterLinear:  return FILTER_TYPE_LINEAR;
        // clang-format on
        default: return FILTER_TYPE_LINEAR;
    }
}

void HdMinFilterToMinMipFilterType(pxr::HdMinFilter hdMinFilter, FILTER_TYPE& MinFilter, FILTER_TYPE& MipFilter)
{
    switch (hdMinFilter)
    {
        case pxr::HdMinFilterNearest:
            MinFilter = FILTER_TYPE_POINT;
            MipFilter = FILTER_TYPE_POINT;
            break;

        case pxr::HdMinFilterLinear:
            MinFilter = FILTER_TYPE_LINEAR;
            MipFilter = FILTER_TYPE_LINEAR;
            break;

        case pxr::HdMinFilterNearestMipmapNearest:
            MinFilter = FILTER_TYPE_POINT;
            MipFilter = FILTER_TYPE_POINT;
            break;

        case pxr::HdMinFilterLinearMipmapNearest:
            MinFilter = FILTER_TYPE_LINEAR;
            MipFilter = FILTER_TYPE_POINT;
            break;

        case pxr::HdMinFilterNearestMipmapLinear:
            MinFilter = FILTER_TYPE_POINT;
            MipFilter = FILTER_TYPE_LINEAR;
            break;

        case pxr::HdMinFilterLinearMipmapLinear:
            MinFilter = FILTER_TYPE_LINEAR;
            MipFilter = FILTER_TYPE_LINEAR;
            break;

        default:
            MinFilter = FILTER_TYPE_LINEAR;
            MipFilter = FILTER_TYPE_LINEAR;
    }
}

COMPARISON_FUNCTION HdCompareFunctionToComparisonFunction(pxr::HdCompareFunction hdComparFunc)
{
    switch (hdComparFunc)
    {
        // clang-format off
        case pxr::HdCmpFuncNever:    return COMPARISON_FUNC_NEVER;
        case pxr::HdCmpFuncLess:     return COMPARISON_FUNC_LESS;
        case pxr::HdCmpFuncEqual:    return COMPARISON_FUNC_EQUAL;
        case pxr::HdCmpFuncLEqual:   return COMPARISON_FUNC_LESS_EQUAL;
        case pxr::HdCmpFuncGreater:  return COMPARISON_FUNC_GREATER;
        case pxr::HdCmpFuncNotEqual: return COMPARISON_FUNC_NOT_EQUAL;
        case pxr::HdCmpFuncGEqual:   return COMPARISON_FUNC_GREATER_EQUAL;
        case pxr::HdCmpFuncAlways:   return COMPARISON_FUNC_ALWAYS;
        // clang-format on
        default: return COMPARISON_FUNC_NEVER;
    }
}

SamplerDesc HdSamplerParametersToSamplerDesc(const pxr::HdSamplerParameters& hdSamplerParams)
{
    SamplerDesc Desc;
    Desc.AddressU = HdWrapToAddressMode(hdSamplerParams.wrapS);
    Desc.AddressV = HdWrapToAddressMode(hdSamplerParams.wrapT);
    Desc.AddressW = HdWrapToAddressMode(hdSamplerParams.wrapR);

    HdMinFilterToMinMipFilterType(hdSamplerParams.minFilter, Desc.MinFilter, Desc.MipFilter);
    Desc.MagFilter = HdMagFilterToFilterType(hdSamplerParams.magFilter);

    if (hdSamplerParams.enableCompare)
    {
        Desc.ComparisonFunc = HdCompareFunctionToComparisonFunction(hdSamplerParams.compareFunction);
    }

    return Desc;
}

} // namespace USD

} // namespace Diligent
