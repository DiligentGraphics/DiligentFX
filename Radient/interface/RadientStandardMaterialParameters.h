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

#pragma once

/// \file
/// Defines the canonical parameter names used by Radient standard materials.
///
/// The documented ranges describe values accepted by the standard shading
/// model. Material instance writers store values verbatim and do not clamp or
/// otherwise validate these semantic constraints.

#include "../../../DiligentCore/Primitives/interface/BasicTypes.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

/// Revision of the generated standard-material schema.
///
/// The revision changes whenever parameter names, ordering, types, defaults,
/// presence conditions, or semantic interpretation change. Runtime parameter
/// handles remain specific to one material definition and must not be persisted.
static DILIGENT_CONSTEXPR Uint32 RadientStandardMaterialSchemaVersion = 1;

/// Name of the FLOAT4 linear RGBA base-color multiplier. Components are expected
/// in [0, 1]. The parameter is present in every standard material and defaults to
/// (1, 1, 1, 1).
static DILIGENT_CONSTEXPR Char RadientStandardMaterialBaseColorFactorName[] = "BaseColorFactor";

/// Name of the FLOAT metallic multiplier. The value is expected in [0, 1]. The
/// parameter is present in metallic-roughness materials and defaults to 1.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialMetallicFactorName[] = "MetallicFactor";

/// Name of the FLOAT perceptual-roughness multiplier. The value is expected in
/// [0, 1]. The parameter is present in metallic-roughness materials and defaults
/// to 1.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialRoughnessFactorName[] = "RoughnessFactor";

/// Name of the FLOAT3 linear RGB emissive multiplier. Values are expected to be
/// non-negative and may exceed 1 for HDR emission. The parameter is present in
/// metallic-roughness materials and defaults to (0, 0, 0).
static DILIGENT_CONSTEXPR Char RadientStandardMaterialEmissiveFactorName[] = "EmissiveFactor";

/// Name of the FLOAT multiplier applied to the X and Y components decoded from
/// the normal texture. The parameter is present when a normal texture is declared
/// and defaults to 1.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialNormalScaleName[] = "NormalScale";

/// Name of the FLOAT occlusion multiplier. The value is expected in [0, 1]. The
/// parameter is present when an occlusion texture is declared and defaults to 1.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialOcclusionStrengthName[] = "OcclusionStrength";

/// Name of the FLOAT clear-coat coverage. The value is expected in [0, 1]. The
/// parameter requires the clear-coat feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatFactorName[] = "ClearCoatFactor";

/// Name of the FLOAT clear-coat perceptual roughness. The value is expected in
/// [0, 1]. The parameter requires the clear-coat feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatRoughnessFactorName[] = "ClearCoatRoughnessFactor";

/// Name of the FLOAT multiplier applied to the X and Y components decoded from
/// the clear-coat normal texture. The parameter is present when that texture is
/// declared and defaults to 1.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatNormalScaleName[] = "ClearCoatNormalScale";

/// Name of the FLOAT3 linear RGB sheen-color multiplier. Components are expected
/// in [0, 1]. The parameter requires the sheen feature and defaults to (0, 0, 0).
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenColorFactorName[] = "SheenColorFactor";

/// Name of the FLOAT sheen perceptual roughness. The value is expected in [0, 1].
/// The parameter requires the sheen feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenRoughnessFactorName[] = "SheenRoughnessFactor";

/// Name of the FLOAT anisotropy strength. The value is expected in [0, 1]. The
/// parameter requires the anisotropy feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyStrengthName[] = "AnisotropyStrength";

/// Name of the FLOAT anisotropy rotation in radians around the shading normal,
/// relative to the material tangent. The parameter requires the anisotropy
/// feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyRotationName[] = "AnisotropyRotation";

/// Name of the FLOAT iridescence coverage. The value is expected in [0, 1]. The
/// parameter requires the iridescence feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceFactorName[] = "IridescenceFactor";

/// Name of the FLOAT iridescent thin-film index of refraction. The value is
/// expected to be at least 1. The parameter requires the iridescence feature and
/// defaults to 1.3.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceIORName[] = "IridescenceIOR";

/// Name of the FLOAT minimum iridescent thin-film thickness in nanometers. The
/// value is expected to be non-negative and no greater than the maximum
/// thickness. The parameter requires the iridescence feature and defaults to 100.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessMinimumName[] = "IridescenceThicknessMinimum";

/// Name of the FLOAT maximum iridescent thin-film thickness in nanometers. The
/// value is expected to be no less than the minimum thickness. The parameter
/// requires the iridescence feature and defaults to 400.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessMaximumName[] = "IridescenceThicknessMaximum";

/// Name of the FLOAT transmission coverage. The value is expected in [0, 1]. The
/// parameter requires the transmission feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialTransmissionFactorName[] = "TransmissionFactor";

/// Name of the FLOAT material index of refraction. The value is expected to be at
/// least 1. The parameter requires the transmission feature and defaults to 1.5.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIORName[] = "IOR";

/// Name of the FLOAT non-negative volume-thickness multiplier in scene units.
/// The parameter requires the volume feature and defaults to 0.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialThicknessFactorName[] = "ThicknessFactor";

/// Name of the FLOAT3 linear RGB attenuation color. Components are expected in
/// [0, 1]. The parameter requires the volume feature and defaults to (1, 1, 1).
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAttenuationColorName[] = "AttenuationColor";

/// Name of the FLOAT distance in scene units over which transmitted light is
/// attenuated to AttenuationColor. The value is expected to be positive. The
/// parameter requires the volume feature and defaults to the maximum Float32
/// value, which represents no attenuation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAttenuationDistanceName[] = "AttenuationDistance";

/// Name of the UINT alpha mode. The value must be a
/// RADIENT_STANDARD_MATERIAL_ALPHA_MODE enumerator. The parameter is present in
/// every standard material and defaults to RADIENT_STANDARD_MATERIAL_ALPHA_MODE_OPAQUE.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAlphaModeName[] = "AlphaMode";

/// Name of the FLOAT alpha cutoff used by masked materials. The value is expected
/// in [0, 1]. The parameter is present in every standard material and defaults to
/// 0.5.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAlphaCutoffName[] = "AlphaCutoff";

/// Name of the BOOL controlling whether both sides of a surface are rendered.
/// The parameter is present in every standard material and defaults to False.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialDoubleSidedName[] = "DoubleSided";

/// Every declared texture semantic has the following six parameters:
///
/// - `Texture`: TEXTURE, default null.
/// - `TextureUVSelector`: INT texture-coordinate set index, default 0.
/// - `TextureUVScaleAndRotation`: FLOAT2X2 transform, default identity.
/// - `TextureUVBias`: FLOAT2 translation applied after the transform, default zero.
/// - `TextureWrapU` and `TextureWrapV`: UINT
///   RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE values, default
///   RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP.

/// Name of the base-color TEXTURE. RGB is interpreted as sRGB, alpha is linear,
/// and all channels are expected in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialBaseColorTextureName[] = "BaseColorTexture";
/// Name of the base-color texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialBaseColorTextureUVSelectorName[] = "BaseColorTextureUVSelector";
/// Name of the base-color texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialBaseColorTextureUVScaleAndRotationName[] = "BaseColorTextureUVScaleAndRotation";
/// Name of the base-color texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialBaseColorTextureUVBiasName[] = "BaseColorTextureUVBias";
/// Name of the base-color texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialBaseColorTextureWrapUName[] = "BaseColorTextureWrapU";
/// Name of the base-color texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialBaseColorTextureWrapVName[] = "BaseColorTextureWrapV";

/// Name of the metallic-roughness TEXTURE. It is sampled linearly; G stores
/// perceptual roughness and B stores metallic, both in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialMetallicRoughnessTextureName[] = "MetallicRoughnessTexture";
/// Name of the metallic-roughness texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialMetallicRoughnessTextureUVSelectorName[] = "MetallicRoughnessTextureUVSelector";
/// Name of the metallic-roughness texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialMetallicRoughnessTextureUVScaleAndRotationName[] = "MetallicRoughnessTextureUVScaleAndRotation";
/// Name of the metallic-roughness texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialMetallicRoughnessTextureUVBiasName[] = "MetallicRoughnessTextureUVBias";
/// Name of the metallic-roughness texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialMetallicRoughnessTextureWrapUName[] = "MetallicRoughnessTextureWrapU";
/// Name of the metallic-roughness texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialMetallicRoughnessTextureWrapVName[] = "MetallicRoughnessTextureWrapV";

/// Name of the normal TEXTURE. Linear RGB values in [0, 1] encode a tangent-space
/// normal that is remapped to [-1, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialNormalTextureName[] = "NormalTexture";
/// Name of the normal texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialNormalTextureUVSelectorName[] = "NormalTextureUVSelector";
/// Name of the normal texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialNormalTextureUVScaleAndRotationName[] = "NormalTextureUVScaleAndRotation";
/// Name of the normal texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialNormalTextureUVBiasName[] = "NormalTextureUVBias";
/// Name of the normal texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialNormalTextureWrapUName[] = "NormalTextureWrapU";
/// Name of the normal texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialNormalTextureWrapVName[] = "NormalTextureWrapV";

/// Name of the occlusion TEXTURE. It is sampled linearly and R stores the
/// occlusion value in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialOcclusionTextureName[] = "OcclusionTexture";
/// Name of the occlusion texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialOcclusionTextureUVSelectorName[] = "OcclusionTextureUVSelector";
/// Name of the occlusion texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialOcclusionTextureUVScaleAndRotationName[] = "OcclusionTextureUVScaleAndRotation";
/// Name of the occlusion texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialOcclusionTextureUVBiasName[] = "OcclusionTextureUVBias";
/// Name of the occlusion texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialOcclusionTextureWrapUName[] = "OcclusionTextureWrapU";
/// Name of the occlusion texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialOcclusionTextureWrapVName[] = "OcclusionTextureWrapV";

/// Name of the emissive TEXTURE. RGB is interpreted as sRGB and expected in
/// [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialEmissiveTextureName[] = "EmissiveTexture";
/// Name of the emissive texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialEmissiveTextureUVSelectorName[] = "EmissiveTextureUVSelector";
/// Name of the emissive texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialEmissiveTextureUVScaleAndRotationName[] = "EmissiveTextureUVScaleAndRotation";
/// Name of the emissive texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialEmissiveTextureUVBiasName[] = "EmissiveTextureUVBias";
/// Name of the emissive texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialEmissiveTextureWrapUName[] = "EmissiveTextureWrapU";
/// Name of the emissive texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialEmissiveTextureWrapVName[] = "EmissiveTextureWrapV";

/// Name of the clear-coat TEXTURE. It is sampled linearly and R stores coverage
/// in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatTextureName[] = "ClearCoatTexture";
/// Name of the clear-coat texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatTextureUVSelectorName[] = "ClearCoatTextureUVSelector";
/// Name of the clear-coat texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatTextureUVScaleAndRotationName[] = "ClearCoatTextureUVScaleAndRotation";
/// Name of the clear-coat texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatTextureUVBiasName[] = "ClearCoatTextureUVBias";
/// Name of the clear-coat texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatTextureWrapUName[] = "ClearCoatTextureWrapU";
/// Name of the clear-coat texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatTextureWrapVName[] = "ClearCoatTextureWrapV";

/// Name of the clear-coat roughness TEXTURE. It is sampled linearly and G stores
/// perceptual roughness in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatRoughnessTextureName[] = "ClearCoatRoughnessTexture";
/// Name of the clear-coat roughness texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatRoughnessTextureUVSelectorName[] = "ClearCoatRoughnessTextureUVSelector";
/// Name of the clear-coat roughness texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatRoughnessTextureUVScaleAndRotationName[] = "ClearCoatRoughnessTextureUVScaleAndRotation";
/// Name of the clear-coat roughness texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatRoughnessTextureUVBiasName[] = "ClearCoatRoughnessTextureUVBias";
/// Name of the clear-coat roughness texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatRoughnessTextureWrapUName[] = "ClearCoatRoughnessTextureWrapU";
/// Name of the clear-coat roughness texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatRoughnessTextureWrapVName[] = "ClearCoatRoughnessTextureWrapV";

/// Name of the clear-coat normal TEXTURE. Linear RGB values in [0, 1] encode a
/// tangent-space normal that is remapped to [-1, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatNormalTextureName[] = "ClearCoatNormalTexture";
/// Name of the clear-coat normal texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatNormalTextureUVSelectorName[] = "ClearCoatNormalTextureUVSelector";
/// Name of the clear-coat normal texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatNormalTextureUVScaleAndRotationName[] = "ClearCoatNormalTextureUVScaleAndRotation";
/// Name of the clear-coat normal texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatNormalTextureUVBiasName[] = "ClearCoatNormalTextureUVBias";
/// Name of the clear-coat normal texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatNormalTextureWrapUName[] = "ClearCoatNormalTextureWrapU";
/// Name of the clear-coat normal texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialClearCoatNormalTextureWrapVName[] = "ClearCoatNormalTextureWrapV";

/// Name of the sheen-color TEXTURE. RGB is interpreted as sRGB and expected in
/// [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenColorTextureName[] = "SheenColorTexture";
/// Name of the sheen-color texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenColorTextureUVSelectorName[] = "SheenColorTextureUVSelector";
/// Name of the sheen-color texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenColorTextureUVScaleAndRotationName[] = "SheenColorTextureUVScaleAndRotation";
/// Name of the sheen-color texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenColorTextureUVBiasName[] = "SheenColorTextureUVBias";
/// Name of the sheen-color texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenColorTextureWrapUName[] = "SheenColorTextureWrapU";
/// Name of the sheen-color texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenColorTextureWrapVName[] = "SheenColorTextureWrapV";

/// Name of the sheen-roughness TEXTURE. It is sampled linearly and A stores
/// perceptual roughness in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenRoughnessTextureName[] = "SheenRoughnessTexture";
/// Name of the sheen-roughness texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenRoughnessTextureUVSelectorName[] = "SheenRoughnessTextureUVSelector";
/// Name of the sheen-roughness texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenRoughnessTextureUVScaleAndRotationName[] = "SheenRoughnessTextureUVScaleAndRotation";
/// Name of the sheen-roughness texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenRoughnessTextureUVBiasName[] = "SheenRoughnessTextureUVBias";
/// Name of the sheen-roughness texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenRoughnessTextureWrapUName[] = "SheenRoughnessTextureWrapU";
/// Name of the sheen-roughness texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialSheenRoughnessTextureWrapVName[] = "SheenRoughnessTextureWrapV";

/// Name of the anisotropy TEXTURE. It is sampled linearly; RG encodes the
/// tangent-space direction remapped from [0, 1] to [-1, 1], and B stores strength
/// in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyTextureName[] = "AnisotropyTexture";
/// Name of the anisotropy texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyTextureUVSelectorName[] = "AnisotropyTextureUVSelector";
/// Name of the anisotropy texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyTextureUVScaleAndRotationName[] = "AnisotropyTextureUVScaleAndRotation";
/// Name of the anisotropy texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyTextureUVBiasName[] = "AnisotropyTextureUVBias";
/// Name of the anisotropy texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyTextureWrapUName[] = "AnisotropyTextureWrapU";
/// Name of the anisotropy texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialAnisotropyTextureWrapVName[] = "AnisotropyTextureWrapV";

/// Name of the iridescence TEXTURE. It is sampled linearly and R stores coverage
/// in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceTextureName[] = "IridescenceTexture";
/// Name of the iridescence texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceTextureUVSelectorName[] = "IridescenceTextureUVSelector";
/// Name of the iridescence texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceTextureUVScaleAndRotationName[] = "IridescenceTextureUVScaleAndRotation";
/// Name of the iridescence texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceTextureUVBiasName[] = "IridescenceTextureUVBias";
/// Name of the iridescence texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceTextureWrapUName[] = "IridescenceTextureWrapU";
/// Name of the iridescence texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceTextureWrapVName[] = "IridescenceTextureWrapV";

/// Name of the iridescence-thickness TEXTURE. It is sampled linearly and G stores
/// a value in [0, 1] that interpolates between the minimum and maximum thickness.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessTextureName[] = "IridescenceThicknessTexture";
/// Name of the iridescence-thickness texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessTextureUVSelectorName[] = "IridescenceThicknessTextureUVSelector";
/// Name of the iridescence-thickness texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessTextureUVScaleAndRotationName[] = "IridescenceThicknessTextureUVScaleAndRotation";
/// Name of the iridescence-thickness texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessTextureUVBiasName[] = "IridescenceThicknessTextureUVBias";
/// Name of the iridescence-thickness texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessTextureWrapUName[] = "IridescenceThicknessTextureWrapU";
/// Name of the iridescence-thickness texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialIridescenceThicknessTextureWrapVName[] = "IridescenceThicknessTextureWrapV";

/// Name of the transmission TEXTURE. It is sampled linearly and R stores
/// transmission coverage in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialTransmissionTextureName[] = "TransmissionTexture";
/// Name of the transmission texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialTransmissionTextureUVSelectorName[] = "TransmissionTextureUVSelector";
/// Name of the transmission texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialTransmissionTextureUVScaleAndRotationName[] = "TransmissionTextureUVScaleAndRotation";
/// Name of the transmission texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialTransmissionTextureUVBiasName[] = "TransmissionTextureUVBias";
/// Name of the transmission texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialTransmissionTextureWrapUName[] = "TransmissionTextureWrapU";
/// Name of the transmission texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialTransmissionTextureWrapVName[] = "TransmissionTextureWrapV";

/// Name of the thickness TEXTURE. It is sampled linearly and G stores a
/// non-negative thickness multiplier, conventionally in [0, 1].
static DILIGENT_CONSTEXPR Char RadientStandardMaterialThicknessTextureName[] = "ThicknessTexture";
/// Name of the thickness texture INT UV-set selector.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialThicknessTextureUVSelectorName[] = "ThicknessTextureUVSelector";
/// Name of the thickness texture FLOAT2X2 UV scale-and-rotation transform.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialThicknessTextureUVScaleAndRotationName[] = "ThicknessTextureUVScaleAndRotation";
/// Name of the thickness texture FLOAT2 UV translation.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialThicknessTextureUVBiasName[] = "ThicknessTextureUVBias";
/// Name of the thickness texture UINT U address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialThicknessTextureWrapUName[] = "ThicknessTextureWrapU";
/// Name of the thickness texture UINT V address mode.
static DILIGENT_CONSTEXPR Char RadientStandardMaterialThicknessTextureWrapVName[] = "ThicknessTextureWrapV";

DILIGENT_END_NAMESPACE // Diligent
