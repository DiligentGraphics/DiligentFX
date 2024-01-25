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

#pragma once

#include "GraphicsTypes.h"
#include "Sampler.h"
#include "RasterizerState.h"
#include "DepthStencilState.h"
#include "BlendState.h"
#include "PBR_Renderer.hpp"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/types.h"

namespace Diligent
{

namespace USD
{

TEXTURE_ADDRESS_MODE HdWrapToAddressMode(pxr::HdWrap hdWrap);
FILTER_TYPE          HdMagFilterToFilterType(pxr::HdMagFilter hdMinFilter);
void                 HdMinFilterToMinMipFilterType(pxr::HdMinFilter hdMinFilter, FILTER_TYPE& MinFilter, FILTER_TYPE& MipFilter);
COMPARISON_FUNCTION  HdCompareFunctionToComparisonFunction(pxr::HdCompareFunction hdComparFunc);

SamplerDesc HdSamplerParametersToSamplerDesc(const pxr::HdSamplerParameters& hdSamplerParams);

CULL_MODE       HdCullStyleToCullMode(pxr::HdCullStyle hdCullStyle);
STENCIL_OP      HdStencilOpToStencilOp(pxr::HdStencilOp hdStencilOp);
BLEND_OPERATION HdBlendOpToBlendOperation(pxr::HdBlendOp hdBlendOp);
BLEND_FACTOR    HdBlendFactorToBlendFactor(pxr::HdBlendFactor hdBlendFactor);

PBR_Renderer::ALPHA_MODE MaterialTagToPbrAlphaMode(const pxr::TfToken& MaterialTag);

TEXTURE_FORMAT HdFormatToTextureFormat(pxr::HdFormat hdFormat);
pxr::HdFormat  TextureFormatToHdFormat(TEXTURE_FORMAT TexFmt);

/// Converts PBR texture attrib ID to Pxr name, for example:
///		TEXTURE_ATTRIB_ID_BASE_COLOR -> "diffuseColor"
///		TEXTURE_ATTRIB_ID_NORMAL     -> "normal"
///		TEXTURE_ATTRIB_ID_METALLIC	 -> "metallic"
const pxr::TfToken& PBRTextureAttribIdToPxrName(PBR_Renderer::TEXTURE_ATTRIB_ID Id);

} // namespace USD

} // namespace Diligent
