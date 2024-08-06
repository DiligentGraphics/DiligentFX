/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include "pxr/pxr.h"
#include "pxr/base/tf/staticTokens.h"

namespace Diligent
{

namespace USD
{

// clang-format off

#define HN_TOKENS              \
    (constantLighting)         \
    (packedSmoothNormals)      \
    (smoothNormals)            \
    (packedFlatNormals)        \
    (flatNormals)              \
    (scale)                    \
    (bias)                     \
    (rotation)                 \
    (translation)              \
    (sRGB)                     \
    (raw)                      \
    ((_double, "double"))      \
    ((_float, "float"))        \
    ((_int, "int"))            \
    ((colorSpaceAuto, "auto")) \
    (fvarIndices)              \
    (fvarPatchParam)           \
    (coarseFaceIndex)          \
    (processedFaceCounts)      \
    (processedFaceIndices)     \
    (geomSubsetFaceIndices)    \
    (pointSizeScale)           \
    (screenSpaceWidths)        \
    (minScreenSpaceWidths)     \
    (shadowCompareTextures)    \
    (diffuseColor)             \
    (metallic)                 \
    (roughness)                \
    (normal)                   \
    (occlusion)                \
    (opacity)                  \
    (emissiveColor)            \
    (clearcoat)                \
    (clearcoatRoughness)       \
    (renderPassParams) 	       \
	(renderPassName)

#define HN_MATERIAL_TAG_TOKENS \
    (defaultTag)               \
    (masked)                   \
    (additive)                 \
    (translucent)              \
    (volume)

#define HN_SDR_METADATA_TOKENS \
    (swizzle)

#define HN_TEXTURE_TOKENS  \
    (wrapS)                \
    (wrapT)                \
    (wrapR)                \
    (black)                \
    (clamp)                \
    (mirror)               \
    (repeat)               \
    (useMetadata)          \
    (minFilter)            \
    (magFilter)            \
    (linear)               \
    (nearest)              \
    (linearMipmapLinear)   \
    (linearMipmapNearest)  \
    (nearestMipmapLinear)  \
    (nearestMipmapNearest)

#define HN_RENDER_RESOURCE_TOKENS        \
    (cameraAttribsBuffer)                \
    (lightAttribsBuffer)                 \
    (offscreenColorTarget)               \
    (jitteredFinalColorTarget)           \
    (finalColorTarget)                   \
    (normalTarget)                       \
    (baseColorTarget)                    \
    (materialDataTarget)                 \
    (motionVectorsTarget)                \
    (iblTarget)                          \
    (meshIdTarget)                       \
    (selectionDepthBuffer)               \
    (closestSelectedLocation0Target)     \
    (closestSelectedLocation1Target)     \
    (closestSelectedLocationFinalTarget) \
    (frameRenderTargets)                 \
    (depthBuffer)                        \
    (depthBuffer0)                       \
    (depthBuffer1)                       \
    (frameShaderAttribs)				 \
    (useTaa)							 \
	(taaJitterOffsets)					 \
	(taaReset)							 \
	(suspendSuperSampling)				 \
    (cameraTransformDirty)               \
    (renderPass_OpaqueSelected)		     \
    (renderPass_TransparentSelected)	 \
    (renderPass_OpaqueUnselected_TransparentAll) \
    (renderPass_Shadow) \
    (backgroundDepth)

// clang-format on

using TfToken = pxr::TfToken;
template <typename T>
using TfStaticData = pxr::TfStaticData<T>;

TF_DECLARE_PUBLIC_TOKENS(HnMaterialTagTokens, HN_MATERIAL_TAG_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(HnSdrMetadataTokens, HN_SDR_METADATA_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(HnTextureTokens, HN_TEXTURE_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(HnTokens, HN_TOKENS);
TF_DECLARE_PUBLIC_TOKENS(HnRenderResourceTokens, HN_RENDER_RESOURCE_TOKENS);

} // namespace USD

} // namespace Diligent
