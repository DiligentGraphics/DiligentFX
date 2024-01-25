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

#include "HnTypeConversions.hpp"
#include "HnTokens.hpp"

#include <array>

#include "DebugUtilities.hpp"

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

CULL_MODE HdCullStyleToCullMode(pxr::HdCullStyle hdCullStyle)
{
    switch (hdCullStyle)
    {
        // clang-format off
        case pxr::HdCullStyleDontCare: return CULL_MODE_BACK;
        case pxr::HdCullStyleNothing:  return CULL_MODE_NONE;
        case pxr::HdCullStyleBack:     return CULL_MODE_BACK;
        case pxr::HdCullStyleFront:    return CULL_MODE_FRONT;
        case pxr::HdCullStyleBackUnlessDoubleSided:  return CULL_MODE_BACK;
        case pxr::HdCullStyleFrontUnlessDoubleSided: return CULL_MODE_FRONT;
        // clang-format on
        default:
            UNEXPECTED("Unexpected cull mode");
            return CULL_MODE_NONE;
    }
}

STENCIL_OP HdStencilOpToStencilOp(pxr::HdStencilOp hdStencilOp)
{
    switch (hdStencilOp)
    {
        // clang-format off
        case pxr::HdStencilOpKeep:          return STENCIL_OP_KEEP;
        case pxr::HdStencilOpZero:          return STENCIL_OP_ZERO;
        case pxr::HdStencilOpReplace:       return STENCIL_OP_REPLACE;
        case pxr::HdStencilOpIncrement:     return STENCIL_OP_INCR_SAT;
        case pxr::HdStencilOpIncrementWrap: return STENCIL_OP_INCR_WRAP;
        case pxr::HdStencilOpDecrement:     return STENCIL_OP_DECR_SAT;
        case pxr::HdStencilOpDecrementWrap: return STENCIL_OP_DECR_WRAP;
        case pxr::HdStencilOpInvert:        return STENCIL_OP_INVERT;
        // clang-format on
        default:
            UNEXPECTED("Unexpected stencil operation");
            return STENCIL_OP_KEEP;
    }
}

BLEND_OPERATION HdBlendOpToBlendOperation(pxr::HdBlendOp hdBlendOp)
{
    switch (hdBlendOp)
    {
        // clang-format off
        case pxr::HdBlendOpAdd:             return BLEND_OPERATION_ADD;
        case pxr::HdBlendOpSubtract:        return BLEND_OPERATION_SUBTRACT;
        case pxr::HdBlendOpReverseSubtract: return BLEND_OPERATION_REV_SUBTRACT;
        case pxr::HdBlendOpMin:             return BLEND_OPERATION_MIN;
        case pxr::HdBlendOpMax:             return BLEND_OPERATION_MAX;
        // clang-format on
        default:
            UNEXPECTED("Unexpected blend operation");
            return BLEND_OPERATION_ADD;
    }
}

BLEND_FACTOR HdBlendFactorToBlendFactor(pxr::HdBlendFactor hdBlendFactor)
{
    switch (hdBlendFactor)
    {
        // clang-format off
        case pxr::HdBlendFactorZero:                  return BLEND_FACTOR_ZERO;
        case pxr::HdBlendFactorOne:                   return BLEND_FACTOR_ONE;
        case pxr::HdBlendFactorSrcColor:              return BLEND_FACTOR_SRC_COLOR;
        case pxr::HdBlendFactorOneMinusSrcColor:      return BLEND_FACTOR_INV_SRC_COLOR;
        case pxr::HdBlendFactorDstColor:              return BLEND_FACTOR_DEST_COLOR;
        case pxr::HdBlendFactorOneMinusDstColor:      return BLEND_FACTOR_INV_DEST_COLOR;
        case pxr::HdBlendFactorSrcAlpha:              return BLEND_FACTOR_SRC_ALPHA;
        case pxr::HdBlendFactorOneMinusSrcAlpha:      return BLEND_FACTOR_INV_SRC_ALPHA;
        case pxr::HdBlendFactorConstantColor:         return BLEND_FACTOR_BLEND_FACTOR;
        case pxr::HdBlendFactorOneMinusConstantColor: return BLEND_FACTOR_INV_BLEND_FACTOR;
        case pxr::HdBlendFactorConstantAlpha:         return BLEND_FACTOR_BLEND_FACTOR;
        case pxr::HdBlendFactorOneMinusConstantAlpha: return BLEND_FACTOR_INV_BLEND_FACTOR;
        case pxr::HdBlendFactorSrcAlphaSaturate:      return BLEND_FACTOR_SRC_ALPHA_SAT;
        case pxr::HdBlendFactorSrc1Color:             return BLEND_FACTOR_SRC1_COLOR;
        case pxr::HdBlendFactorOneMinusSrc1Color:     return BLEND_FACTOR_INV_SRC1_COLOR;
        case pxr::HdBlendFactorSrc1Alpha:             return BLEND_FACTOR_SRC1_ALPHA;
        case pxr::HdBlendFactorOneMinusSrc1Alpha:     return BLEND_FACTOR_INV_SRC1_ALPHA;
        // clang-format on
        default:
            UNEXPECTED("Unexpected blend factor");
            return BLEND_FACTOR_ZERO;
    }
}

PBR_Renderer::ALPHA_MODE MaterialTagToPbrAlphaMode(const pxr::TfToken& MaterialTag)
{
    if (MaterialTag == HnMaterialTagTokens->translucent)
        return PBR_Renderer::ALPHA_MODE_BLEND;
    else if (MaterialTag == HnMaterialTagTokens->masked)
        return PBR_Renderer::ALPHA_MODE_MASK;
    else
        return PBR_Renderer::ALPHA_MODE_OPAQUE;
}

TEXTURE_FORMAT HdFormatToTextureFormat(pxr::HdFormat hdFormat)
{
    static_assert(pxr::HdFormatCount == 29, "Please handle the new format below");
    switch (hdFormat)
    {
        case pxr::HdFormatInvalid: return TEX_FORMAT_UNKNOWN;

        case pxr::HdFormatUNorm8: return TEX_FORMAT_R8_UNORM;
        case pxr::HdFormatUNorm8Vec2: return TEX_FORMAT_RG8_UNORM;
        case pxr::HdFormatUNorm8Vec3: UNSUPPORTED("Diligent does not support RGB formats"); return TEX_FORMAT_UNKNOWN;
        case pxr::HdFormatUNorm8Vec4: return TEX_FORMAT_RGBA8_UNORM;

        case pxr::HdFormatSNorm8: return TEX_FORMAT_R8_SNORM;
        case pxr::HdFormatSNorm8Vec2: return TEX_FORMAT_RG8_SNORM;
        case pxr::HdFormatSNorm8Vec3: UNSUPPORTED("Diligent does not support RGB formats"); return TEX_FORMAT_UNKNOWN;
        case pxr::HdFormatSNorm8Vec4: return TEX_FORMAT_RGBA8_SNORM;

        case pxr::HdFormatFloat16: return TEX_FORMAT_R16_FLOAT;
        case pxr::HdFormatFloat16Vec2: return TEX_FORMAT_RG16_FLOAT;
        case pxr::HdFormatFloat16Vec3: UNSUPPORTED("Diligent does not support RGB formats"); return TEX_FORMAT_UNKNOWN;
        case pxr::HdFormatFloat16Vec4: return TEX_FORMAT_RGBA16_FLOAT;

        case pxr::HdFormatFloat32: return TEX_FORMAT_R32_FLOAT;
        case pxr::HdFormatFloat32Vec2: return TEX_FORMAT_RG32_FLOAT;
        case pxr::HdFormatFloat32Vec3: UNSUPPORTED("Diligent does not support RGB formats"); return TEX_FORMAT_UNKNOWN;
        case pxr::HdFormatFloat32Vec4: return TEX_FORMAT_RGBA32_FLOAT;

        case pxr::HdFormatInt16: return TEX_FORMAT_R16_SINT;
        case pxr::HdFormatInt16Vec2: return TEX_FORMAT_RG16_SINT;
        case pxr::HdFormatInt16Vec3: UNSUPPORTED("Diligent does not support RGB formats"); return TEX_FORMAT_UNKNOWN;
        case pxr::HdFormatInt16Vec4: return TEX_FORMAT_RGBA16_SINT;

        case pxr::HdFormatUInt16: return TEX_FORMAT_R16_UINT;
        case pxr::HdFormatUInt16Vec2: return TEX_FORMAT_RG16_UINT;
        case pxr::HdFormatUInt16Vec3: UNSUPPORTED("Diligent does not support RGB formats"); return TEX_FORMAT_UNKNOWN;
        case pxr::HdFormatUInt16Vec4: return TEX_FORMAT_RGBA16_UINT;

        case pxr::HdFormatInt32: return TEX_FORMAT_R32_SINT;
        case pxr::HdFormatInt32Vec2: return TEX_FORMAT_RG32_SINT;
        case pxr::HdFormatInt32Vec3: UNSUPPORTED("Diligent does not support RGB formats"); return TEX_FORMAT_UNKNOWN;
        case pxr::HdFormatInt32Vec4: return TEX_FORMAT_RGBA32_SINT;

        case pxr::HdFormatFloat32UInt8: return TEX_FORMAT_D32_FLOAT_S8X24_UINT;

        default:
            UNEXPECTED("Unexpected format");
            return TEX_FORMAT_UNKNOWN;
    }
}

pxr::HdFormat TextureFormatToHdFormat(TEXTURE_FORMAT TexFmt)
{
    switch (TexFmt)
    {
        case TEX_FORMAT_UNKNOWN: return pxr::HdFormatInvalid;

        case TEX_FORMAT_R8_UNORM: return pxr::HdFormatUNorm8;
        case TEX_FORMAT_RG8_UNORM: return pxr::HdFormatUNorm8Vec2;
        case TEX_FORMAT_RGBA8_UNORM: return pxr::HdFormatUNorm8Vec4;

        case TEX_FORMAT_R8_SNORM: return pxr::HdFormatSNorm8;
        case TEX_FORMAT_RG8_SNORM: return pxr::HdFormatSNorm8Vec2;
        case TEX_FORMAT_RGBA8_SNORM: return pxr::HdFormatSNorm8Vec4;

        case TEX_FORMAT_R16_FLOAT: return pxr::HdFormatFloat16;
        case TEX_FORMAT_RG16_FLOAT: return pxr::HdFormatFloat16Vec2;
        case TEX_FORMAT_RGBA16_FLOAT: return pxr::HdFormatFloat16Vec4;

        case TEX_FORMAT_R32_FLOAT: return pxr::HdFormatFloat32;
        case TEX_FORMAT_RG32_FLOAT: return pxr::HdFormatFloat32Vec2;
        case TEX_FORMAT_RGBA32_FLOAT: return pxr::HdFormatFloat32Vec4;

        case TEX_FORMAT_R16_SINT: return pxr::HdFormatInt16;
        case TEX_FORMAT_RG16_SINT: return pxr::HdFormatInt16Vec2;
        case TEX_FORMAT_RGBA16_SINT: return pxr::HdFormatInt16Vec4;

        case TEX_FORMAT_R16_UINT: return pxr::HdFormatUInt16;
        case TEX_FORMAT_RG16_UINT: return pxr::HdFormatUInt16Vec2;
        case TEX_FORMAT_RGBA16_UINT: return pxr::HdFormatUInt16Vec4;

        case TEX_FORMAT_R32_SINT: return pxr::HdFormatInt32;
        case TEX_FORMAT_RG32_SINT: return pxr::HdFormatInt32Vec2;
        case TEX_FORMAT_RGBA32_SINT: return pxr::HdFormatInt32Vec4;

        case TEX_FORMAT_D32_FLOAT_S8X24_UINT: return pxr::HdFormatFloat32UInt8;

        default:
            UNSUPPORTED("This format is not supported in Hydra");
            return pxr::HdFormatInvalid;
    }
}

const pxr::TfToken& PBRTextureAttribIdToPxrName(PBR_Renderer::TEXTURE_ATTRIB_ID Id)
{
    static const std::array<pxr::TfToken, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> TexAttribIdToName = []() {
        std::array<pxr::TfToken, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> PxrNames;

        PxrNames[PBR_Renderer::TEXTURE_ATTRIB_ID_BASE_COLOR] = HnTokens->diffuseColor;
        PxrNames[PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL]     = HnTokens->normal;
        PxrNames[PBR_Renderer::TEXTURE_ATTRIB_ID_METALLIC]   = HnTokens->metallic;
        PxrNames[PBR_Renderer::TEXTURE_ATTRIB_ID_ROUGHNESS]  = HnTokens->roughness;
        PxrNames[PBR_Renderer::TEXTURE_ATTRIB_ID_OCCLUSION]  = HnTokens->occlusion;
        PxrNames[PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE]   = HnTokens->emissiveColor;

        return PxrNames;
    }();

    return TexAttribIdToName[Id];
}

} // namespace USD

} // namespace Diligent
