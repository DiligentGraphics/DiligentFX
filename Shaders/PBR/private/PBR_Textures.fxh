#ifndef _PBR_TEXTURES_FXH_
#define _PBR_TEXTURES_FXH_

#if USE_TEXTURE_ATLAS
#   include "AtlasSampling.fxh"
#endif

#ifndef BaseColorTextureAttribId
#   define BaseColorTextureAttribId 0
#endif

#ifndef PhysicalDescriptorTextureAttribId
#   define PhysicalDescriptorTextureAttribId 1
#endif

#ifndef NormalTextureAttribId
#   define NormalTextureAttribId 2
#endif

#ifndef OcclusionTextureAttribId
#   define OcclusionTextureAttribId 3
#endif

#ifndef EmissiveTextureAttribId
#   define EmissiveTextureAttribId 4
#endif

#if !defined(USE_TEXCOORD0) && !defined(USE_TEXCOORD1)
#   undef USE_COLOR_MAP
#   define USE_COLOR_MAP 0

#   undef USE_METALLIC_MAP
#   define USE_METALLIC_MAP 0

#   undef USE_ROUGHNESS_MAP
#   define USE_ROUGHNESS_MAP 0

#   undef USE_PHYS_DESC_MAP
#   define USE_PHYS_DESC_MAP 0

#   undef USE_NORMAL_MAP
#   define USE_NORMAL_MAP 0

#   undef USE_AO_MAP
#   define USE_AO_MAP 0

#   undef USE_EMISSIVE_MAP
#   define USE_EMISSIVE_MAP 0

#   undef USE_CLEAR_COAT_MAP
#   define USE_CLEAR_COAT_MAP 0

#   undef USE_CLEAR_COAT_ROUGHNESS_MAP
#   define USE_CLEAR_COAT_ROUGHNESS_MAP 0

#   undef USE_CLEAR_COAT_NORMAL_MAP
#   define USE_CLEAR_COAT_NORMAL_MAP 0

#   undef USE_SHEEN_COLOR_MAP
#   define USE_SHEEN_COLOR_MAP 0

#   undef USE_SHEEN_ROUGHNESS_MAP
#   define USE_SHEEN_ROUGHNESS_MAP 0

#   undef USE_ANISOTROPY_MAP
#   define USE_ANISOTROPY_MAP 0

#   undef USE_IRIDESCENCE_MAP
#   define USE_IRIDESCENCE_MAP 0

#   undef USE_IRIDESCENCE_THICKNESS_MAP
#   define USE_IRIDESCENCE_THICKNESS_MAP 0

#   undef USE_TRANSMISSION_MAP
#   define USE_TRANSMISSION_MAP 0

#   undef USE_THICKNESS_MAP
#   define USE_THICKNESS_MAP 0
#endif

SamplerState g_LinearClampSampler;

#if USE_IBL
    TextureCube  g_IrradianceMap;
#   define       g_IrradianceMap_sampler g_LinearClampSampler

    TextureCube  g_PrefilteredEnvMap;
#   define       g_PrefilteredEnvMap_sampler g_LinearClampSampler

    Texture2D    g_PreintegratedGGX;
#   define       g_PreintegratedGGX_sampler g_LinearClampSampler

#   if ENABLE_SHEEN
        Texture2D g_PreintegratedCharlie;
#       define    g_PreintegratedCharlie_sampler g_LinearClampSampler
#   endif
#endif

#if ENABLE_SHEEN
    Texture2D     g_SheenAlbedoScalingLUT;
#       define    g_SheenAlbedoScalingLUT_sampler g_LinearClampSampler
#endif

// Access textures by name:
//  - g_BaseColorMap
//  - g_PhysicalDescriptorMap
//  - g_NormalMap
//  - ...
#ifndef PBR_TEXTURE_ARRAY_INDEXING_MODE_NONE
#   define PBR_TEXTURE_ARRAY_INDEXING_MODE_NONE 0
#endif

// Access textures using the compile-time static indices:
//  - g_MaterialTextures[BaseColorTextureId]
//  - g_MaterialTextures[PhysicalDescriptorTextureId]
//  - g_MaterialTextures[NormalTextureId]
//  - ...
#ifndef PBR_TEXTURE_ARRAY_INDEXING_MODE_STATIC
#   define PBR_TEXTURE_ARRAY_INDEXING_MODE_STATIC 1
#endif

// Access textures using the run-time dynamic indices
#ifndef PBR_TEXTURE_ARRAY_INDEXING_MODE_DYNAMIC
#   define PBR_TEXTURE_ARRAY_INDEXING_MODE_DYNAMIC 2
#endif

#ifndef PBR_TEXTURE_ARRAY_INDEXING_MODE
#   define PBR_TEXTURE_ARRAY_INDEXING_MODE PBR_TEXTURE_ARRAY_INDEXING_MODE_NONE
#endif

#if PBR_TEXTURE_ARRAY_INDEXING_MODE == PBR_TEXTURE_ARRAY_INDEXING_MODE_NONE
#   define USE_MATERIAL_TEXTURES_ARRAY 0
#else
#   define USE_MATERIAL_TEXTURES_ARRAY 1
#endif


#if USE_MATERIAL_TEXTURES_ARRAY && defined(PBR_NUM_MATERIAL_TEXTURES) && PBR_NUM_MATERIAL_TEXTURES > 0
#   ifndef WEBGPU // WebGPU does not support resource arrays
        Texture2DArray g_MaterialTextures[PBR_NUM_MATERIAL_TEXTURES];
#   else
        // Defined on the host
        UNROLLED_MATERIAL_TEXTURES_ARRAY
        // Texture2DArray g_MaterialTextures_0;
        // Texture2DArray g_MaterialTextures_1;
        // ...
#   endif
#endif

#if PBR_TEXTURE_ARRAY_INDEXING_MODE == PBR_TEXTURE_ARRAY_INDEXING_MODE_DYNAMIC
// Use TextureSlice as the index into the texture array.
#   define BaseColorTextureId            Material.Textures[BaseColorTextureAttribId].TextureSlice
#   define MetallicTextureId             Material.Textures[MetallicTextureAttribId].TextureSlice
#   define RoughnessTextureId            Material.Textures[RoughnessTextureAttribId].TextureSlice
#   define PhysicalDescriptorTextureId   Material.Textures[PhysicalDescriptorTextureAttribId].TextureSlice
#   define NormalTextureId               Material.Textures[NormalTextureAttribId].TextureSlice
#   define OcclusionTextureId            Material.Textures[OcclusionTextureAttribId].TextureSlice
#   define EmissiveTextureId             Material.Textures[EmissiveTextureAttribId].TextureSlice
#   define ClearCoatTextureId            Material.Textures[ClearCoatTextureAttribId].TextureSlice
#   define ClearCoatRoughnessTextureId   Material.Textures[ClearCoatRoughnessTextureAttribId].TextureSlice
#   define ClearCoatNormalTextureId      Material.Textures[ClearCoatNormalTextureAttribId].TextureSlice
#   define SheenColorTextureId           Material.Textures[SheenColorTextureAttribId].TextureSlice
#   define SheenRoughnessTextureId       Material.Textures[SheenRoughnessTextureAttribId].TextureSlice
#   define AnisotropyTextureId           Material.Textures[AnisotropyTextureAttribId].TextureSlice
#   define IridescenceTextureId          Material.Textures[IridescenceTextureAttribId].TextureSlice
#   define IridescenceThicknessTextureId Material.Textures[IridescenceThicknessTextureAttribId].TextureSlice
#   define TransmissionTextureId         Material.Textures[TransmissionTextureAttribId].TextureSlice
#   define ThicknessTextureId            Material.Textures[ThicknessTextureAttribId].TextureSlice
#endif

#ifndef WEBGPU
#   define MATERIAL_TEXTURE(Idx) g_MaterialTextures[Idx]
#else
    // glsang supports token pasting, but it is not part of official GLSL or HLSL specs,
    // so we define this macros on the host.
    // #define MATERIAL_TEXTURE(Idx) g_MaterialTextures_##Idx
#endif

#if USE_COLOR_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_BaseColorMap MATERIAL_TEXTURE(BaseColorTextureId)
#   else
        Texture2DArray g_BaseColorMap;
#   endif
    SamplerState g_BaseColorMap_sampler;
#endif

#if USE_METALLIC_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_MetallicMap MATERIAL_TEXTURE(MetallicTextureId)
#   else
        Texture2DArray g_MetallicMap;
#   endif
    SamplerState g_MetallicMap_sampler;
#endif

#if USE_ROUGHNESS_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_RoughnessMap MATERIAL_TEXTURE(RoughnessTextureId)
#   else
        Texture2DArray g_RoughnessMap;
#   endif
    SamplerState g_RoughnessMap_sampler;
#endif

#if USE_PHYS_DESC_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_PhysicalDescriptorMap MATERIAL_TEXTURE(PhysicalDescriptorTextureId)
#   else
        Texture2DArray g_PhysicalDescriptorMap;
#   endif
    SamplerState g_PhysicalDescriptorMap_sampler;
#endif

#if USE_NORMAL_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_NormalMap MATERIAL_TEXTURE(NormalTextureId)
#   else
        Texture2DArray g_NormalMap;
#   endif
    SamplerState g_NormalMap_sampler;
#endif

#if USE_AO_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_OcclusionMap MATERIAL_TEXTURE(OcclusionTextureId)
#   else
        Texture2DArray g_OcclusionMap;
#   endif
    SamplerState g_OcclusionMap_sampler;
#endif

#if USE_EMISSIVE_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_EmissiveMap MATERIAL_TEXTURE(EmissiveTextureId)
#   else
        Texture2DArray g_EmissiveMap;
#   endif
    SamplerState g_EmissiveMap_sampler;
#endif


#if USE_CLEAR_COAT_MAP || USE_CLEAR_COAT_ROUGHNESS_MAP || USE_CLEAR_COAT_NORMAL_MAP
    SamplerState g_ClearCoat_sampler;
#endif

#if USE_CLEAR_COAT_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_ClearCoatMap MATERIAL_TEXTURE(ClearCoatTextureId)
#   else
        Texture2DArray g_ClearCoatMap;
#   endif
#   define g_ClearCoatMap_sampler g_ClearCoat_sampler
#endif

#if USE_CLEAR_COAT_ROUGHNESS_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_ClearCoatRoughnessMap MATERIAL_TEXTURE(ClearCoatRoughnessTextureId)
#   else
        Texture2DArray g_ClearCoatRoughnessMap;
#   endif
#   define g_ClearCoatRoughnessMap_sampler g_ClearCoat_sampler
#endif

#if USE_CLEAR_COAT_NORMAL_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_ClearCoatNormalMap MATERIAL_TEXTURE(ClearCoatNormalTextureId)
#   else
        Texture2DArray g_ClearCoatNormalMap;
#   endif
#   define g_ClearCoatNormalMap_sampler g_ClearCoat_sampler
#endif


#if USE_SHEEN_COLOR_MAP || USE_SHEEN_ROUGHNESS_MAP
    SamplerState g_Sheen_sampler;
#endif

#if USE_SHEEN_COLOR_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_SheenColorMap MATERIAL_TEXTURE(SheenColorTextureId)
#   else
        Texture2DArray g_SheenColorMap;
#   endif
#   define g_SheenColorMap_sampler g_Sheen_sampler
#endif

#if USE_SHEEN_ROUGHNESS_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_SheenRoughnessMap MATERIAL_TEXTURE(SheenRoughnessTextureId)
#   else
        Texture2DArray g_SheenRoughnessMap;
#   endif
#   define g_SheenRoughnessMap_sampler g_Sheen_sampler
#endif

#if USE_ANISOTROPY_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_AnisotropyMap MATERIAL_TEXTURE(AnisotropyTextureId)
#   else
        Texture2DArray g_AnisotropyMap;
#   endif
    SamplerState g_AnisotropyMap_sampler;
#endif

#if USE_IRIDESCENCE_MAP || USE_IRIDESCENCE_THICKNESS_MAP
    SamplerState g_Iridescence_sampler;
#endif

#if USE_IRIDESCENCE_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_IridescenceMap MATERIAL_TEXTURE(IridescenceTextureId)
#   else
        Texture2DArray g_IridescenceMap;
#   endif
#   define g_IridescenceMap_sampler g_Iridescence_sampler
#endif

#if USE_IRIDESCENCE_THICKNESS_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_IridescenceThicknessMap MATERIAL_TEXTURE(IridescenceThicknessTextureId)
#   else
        Texture2DArray g_IridescenceThicknessMap;
#   endif
#   define g_IridescenceThicknessMap_sampler g_Iridescence_sampler
#endif

#if USE_TRANSMISSION_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_TransmissionMap MATERIAL_TEXTURE(TransmissionTextureId)
#   else
        Texture2DArray g_TransmissionMap;
#   endif
    SamplerState g_TransmissionMap_sampler;
#endif

#if USE_THICKNESS_MAP
#   if USE_MATERIAL_TEXTURES_ARRAY
#       define g_ThicknessMap MATERIAL_TEXTURE(ThicknessTextureId)
#   else
        Texture2DArray g_ThicknessMap;
#   endif
    SamplerState g_ThicknessMap_sampler;
#endif

float2 SelectUV(VSOutput VSOut, float Selector)
{
#if USE_TEXCOORD0 && USE_TEXCOORD1
    return lerp(VSOut.UV0, VSOut.UV1, Selector);
#elif USE_TEXCOORD0
    return VSOut.UV0;
#elif USE_TEXCOORD1
    return VSOut.UV1;
#else
    return float2(0.0, 0.0);
#endif
}

float2 TransformUV(float2 UV, PBRMaterialTextureAttribs TexAttribs)
{
    return mul(UV, MatrixFromRows(TexAttribs.UVScaleAndRotation.xy, TexAttribs.UVScaleAndRotation.zw)) + float2(TexAttribs.UBias, TexAttribs.VBias);
}

float4 SampleTexture(Texture2DArray            Tex,
                     SamplerState              Tex_sampler,
                     VSOutput                  VSOut,
                     PBRMaterialTextureAttribs TexAttribs,
                     float                     MipBias,
                     float4                    DefaultValue)
{
    float4 SampledValue = DefaultValue;
#   if USE_TEXCOORD0 || USE_TEXCOORD1
    {
        float UVSelector = UnpackPBRMaterialTextureUVSelector(TexAttribs.PackedProps);
        uint  WrapUMode  = UnpackPBRMaterialTextureWrapUMode(TexAttribs.PackedProps);
        uint  WrapVMode  = UnpackPBRMaterialTextureWrapVMode(TexAttribs.PackedProps);
        uint  WrapModeClamp = 3u; // TEXTURE_ADDRESS_CLAMP
        if (UVSelector >= 0.0)
        {
            float2 UV = SelectUV(VSOut, UVSelector);
#           if ENABLE_TEXCOORD_TRANSFORM
            {
                UV = TransformUV(UV, TexAttribs);
            }
#           endif

#           if USE_TEXTURE_ATLAS
            {
                // Note: Mirror mode is not supported
                float2 WrappedUV = float2(WrapUMode == WrapModeClamp ? saturate(UV.x) : frac(UV.x),
                                          WrapVMode == WrapModeClamp ? saturate(UV.y) : frac(UV.y));
                    
                float GradientScale = exp2(MipBias);

                SampleTextureAtlasAttribs SampleAttribs;
                SampleAttribs.f2UV                   = WrappedUV * TexAttribs.AtlasUVScaleAndBias.xy + TexAttribs.AtlasUVScaleAndBias.zw;
                SampleAttribs.f2SmoothUV             = UV      * TexAttribs.AtlasUVScaleAndBias.xy * GradientScale;
                SampleAttribs.f2dSmoothUV_dx         = ddx(UV) * TexAttribs.AtlasUVScaleAndBias.xy * GradientScale;
                SampleAttribs.f2dSmoothUV_dy         = ddy(UV) * TexAttribs.AtlasUVScaleAndBias.xy * GradientScale;
                SampleAttribs.fSlice                 = TexAttribs.TextureSlice;
                SampleAttribs.f4UVRegion             = TexAttribs.AtlasUVScaleAndBias;
                SampleAttribs.fSmallestValidLevelDim = 4.0;
                SampleAttribs.IsNonFilterable        = false;
                SampleAttribs.fMaxAnisotropy         = 1.0; // Only used on GLES
    
                SampledValue = SampleTextureAtlas(Tex, Tex_sampler, SampleAttribs);
            }
#           else
            {
                // Tex_sampler uses wrap, so we only need to clamp the UVs.
                // Note: Mirror mode is not supported
                if (WrapUMode == WrapModeClamp || WrapVMode == WrapModeClamp)
                {
                    float2 TexDim;
                    float  Elements;
                    Tex.GetDimensions(TexDim.x, TexDim.y, Elements);
                    // Note: to be accurate, the bias should depend on LOD
                    UV.x = (WrapUMode == WrapModeClamp) ? clamp(UV.x, 0.5 / TexDim.x, 1.0 - 0.5 / TexDim.x) : UV.x;
                    UV.y = (WrapVMode == WrapModeClamp) ? clamp(UV.y, 0.5 / TexDim.y, 1.0 - 0.5 / TexDim.y) : UV.y;
                }    
                SampledValue = Tex.SampleBias(Tex_sampler, float3(UV, TexAttribs.TextureSlice), MipBias);
            }
#           endif
        }
    }
#   endif
    
    return SampledValue;
}

float4 GetBaseColor(VSOutput              VSOut,
                    PBRMaterialShaderInfo Material,
                    float                 MipBias,
                    float4                DefaultValue)
{
    float4 BaseColor = DefaultValue;

#   if USE_COLOR_MAP
    {
        BaseColor = SampleTexture(g_BaseColorMap,
                                  g_BaseColorMap_sampler,
                                  VSOut,
                                  Material.Textures[BaseColorTextureAttribId],
                                  MipBias,
                                  DefaultValue);
        BaseColor = float4(TO_LINEAR(BaseColor.rgb), BaseColor.a);
    }
#   endif

#   if USE_VERTEX_COLORS
    {
        BaseColor *= VSOut.Color;
    }
#   endif

    return BaseColor * Material.Basic.BaseColorFactor;
}

float3 SampleNormalTexture(PBRMaterialTextureAttribs TexAttribs,
                           Texture2DArray            NormalMap,
                           SamplerState              NormalMap_sampler,
                           float2                    NormalMapUV,
                           float2                    SmoothNormalMapUV,
                           float2                    dNormalMapUV_dx,
                           float2                    dNormalMapUV_dy,
                           float                     MipBias)
{
    float3 SampledNormal = float3(0.5, 0.5, 1.0);
    
    float UVSelector = UnpackPBRMaterialTextureUVSelector(TexAttribs.PackedProps);
    if (UVSelector >= 0.0)
    {
#       if USE_TEXTURE_ATLAS
        {
            float GradientScale = exp2(MipBias);
    
            SampleTextureAtlasAttribs SampleAttribs;
            SampleAttribs.f2UV                   = NormalMapUV;
            SampleAttribs.f2SmoothUV             = SmoothNormalMapUV * GradientScale;
            SampleAttribs.f2dSmoothUV_dx         = dNormalMapUV_dx   * GradientScale;
            SampleAttribs.f2dSmoothUV_dy         = dNormalMapUV_dy   * GradientScale;
            SampleAttribs.fSlice                 = TexAttribs.TextureSlice;
            SampleAttribs.f4UVRegion             = TexAttribs.AtlasUVScaleAndBias;
            SampleAttribs.fSmallestValidLevelDim = 4.0;
            SampleAttribs.IsNonFilterable        = false;
            SampleAttribs.fMaxAnisotropy         = 1.0; // Only used on GLES

            SampledNormal = SampleTextureAtlas(NormalMap, NormalMap_sampler, SampleAttribs).xyz;
        }
#       else
        {
            uint WrapUMode     = UnpackPBRMaterialTextureWrapUMode(TexAttribs.PackedProps);
            uint WrapVMode     = UnpackPBRMaterialTextureWrapVMode(TexAttribs.PackedProps);
            uint WrapModeClamp = 3u; // TEXTURE_ADDRESS_CLAMP
 
            // Tex_sampler uses wrap, so we only need to clamp the UVs.
            // Note: Mirror mode is not supported
            if (WrapUMode == WrapModeClamp || WrapVMode == WrapModeClamp)
            {
                float2 TexDim;
                float  Elements;
                NormalMap.GetDimensions(TexDim.x, TexDim.y, Elements);
                // Note: to be accurate, the bias should depend on LOD
                NormalMapUV.x = (WrapUMode == WrapModeClamp) ? clamp(NormalMapUV.x, 0.5 / TexDim.x, 1.0 - 0.5 / TexDim.x) : NormalMapUV.x;
                NormalMapUV.y = (WrapVMode == WrapModeClamp) ? clamp(NormalMapUV.y, 0.5 / TexDim.y, 1.0 - 0.5 / TexDim.y) : NormalMapUV.y;
            }       
            SampledNormal = NormalMap.SampleBias(NormalMap_sampler, float3(NormalMapUV, TexAttribs.TextureSlice), MipBias).xyz;
        }
#endif
    }

    return SampledNormal;
}

float3 GetMicroNormal(PBRMaterialShaderInfo Material,
                      float2                NormalMapUV,
                      float2                SmoothNormalMapUV,
                      float2                dNormalMapUV_dx,
                      float2                dNormalMapUV_dy,
                      float                 MipBias)
{
    float3 MicroNormal = float3(0.5, 0.5, 1.0);

#   if USE_NORMAL_MAP && (USE_TEXCOORD0 || USE_TEXCOORD1)
    {
        MicroNormal = SampleNormalTexture(Material.Textures[NormalTextureAttribId],
                                          g_NormalMap,
                                          g_NormalMap_sampler,
                                          NormalMapUV,
                                          SmoothNormalMapUV,
                                          dNormalMapUV_dx,
                                          dNormalMapUV_dy,
                                          MipBias);
    }
#endif

    MicroNormal = MicroNormal * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);
    MicroNormal.xy *= Material.Basic.NormalScale;
    if (MicroNormal.z < 0.0)
    {
        // The texture does not contain the Z component of the normal.
        MicroNormal.z = sqrt(max(1.0 - dot(MicroNormal.xy, MicroNormal.xy), 0.0));
    }
    return MicroNormal;
}

float GetOcclusion(VSOutput              VSOut,
                   PBRMaterialShaderInfo Material,
                   float                 MipBias)
{
    float Occlusion = 1.0;
#   if USE_AO_MAP
    {
        Occlusion = SampleTexture(g_OcclusionMap,
                                  g_OcclusionMap_sampler,
                                  VSOut,
                                  Material.Textures[OcclusionTextureAttribId],
                                  MipBias,
                                  float4(1.0, 1.0, 1.0, 1.0)).r;
    }
#   endif
    return Occlusion * Material.Basic.OcclusionFactor;
}

float3 GetEmissive(VSOutput              VSOut,
                   PBRMaterialShaderInfo Material,
                   float                 MipBias)
{
    float3 Emissive = float3(1.0, 1.0, 1.0);
#   if USE_EMISSIVE_MAP
    {
        Emissive = SampleTexture(g_EmissiveMap,
                                 g_EmissiveMap_sampler,
                                 VSOut,
                                 Material.Textures[EmissiveTextureAttribId],
                                 MipBias,
                                 float4(1.0, 1.0, 1.0, 1.0)).rgb;
        Emissive = TO_LINEAR(Emissive);
    }
#   endif
    Emissive.r *= Material.Basic.EmissiveFactorR;
    Emissive.g *= Material.Basic.EmissiveFactorG;
    Emissive.b *= Material.Basic.EmissiveFactorB;
    return Emissive;
}

float4 GetPhysicalDesc(VSOutput              VSOut,
                       PBRMaterialShaderInfo Material,
                       float                 MipBias)
{
    // Set defaults to 1 so that if the textures are not available, the values
    // are controlled by the metallic/roughness scale factors.
    float4 PhysicalDesc = float4(1.0, 1.0, 1.0, 1.0);
#   if USE_PHYS_DESC_MAP
    {
        PhysicalDesc = SampleTexture(g_PhysicalDescriptorMap,
                                     g_PhysicalDescriptorMap_sampler,
                                     VSOut,
                                     Material.Textures[PhysicalDescriptorTextureAttribId],
                                     MipBias,
                                     float4(1.0, 1.0, 1.0, 1.0));
    }
#   else
    {
#       if USE_METALLIC_MAP
        {
            PhysicalDesc.b = SampleTexture(g_MetallicMap,
                                           g_MetallicMap_sampler,
                                           VSOut,
                                           Material.Textures[MetallicTextureAttribId],
                                           MipBias,
                                           float4(1.0, 1.0, 1.0, 1.0)).r;
        }
#       endif

#       if USE_ROUGHNESS_MAP
        {
            PhysicalDesc.g = SampleTexture(g_RoughnessMap,
                                           g_RoughnessMap_sampler,
                                           VSOut,
                                           Material.Textures[RoughnessTextureAttribId],
                                           MipBias,
                                           float4(1.0, 1.0, 1.0, 1.0)).r;

        }
#       endif
    }
#endif 

    return PhysicalDesc;
}


// Extensions

// Clear coat
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_clearcoat

float GetClearcoatFactor(VSOutput              VSOut,
                         PBRMaterialShaderInfo Material,
                         float                 MipBias)
{
#   if ENABLE_CLEAR_COAT
    {
        float Cearcoat = 1.0;
#       if USE_CLEAR_COAT_MAP
        {
            Cearcoat = SampleTexture(g_ClearCoatMap,
                                     g_ClearCoatMap_sampler,
                                     VSOut,
                                     Material.Textures[ClearCoatTextureAttribId],
                                     MipBias,
                                     float4(1.0, 1.0, 1.0, 1.0)).r;
        }
#       endif
        return Cearcoat * Material.Basic.ClearcoatFactor;
    }
#   else
    {
        return 0.0;
    }
#   endif
}

float GetClearcoatRoughness(VSOutput              VSOut,
                            PBRMaterialShaderInfo Material,
                            float                 MipBias)
{
#   if ENABLE_CLEAR_COAT
    {
        float CearcoatRoughness = 1.0;
#       if USE_CLEAR_COAT_ROUGHNESS_MAP
        {
            CearcoatRoughness = SampleTexture(g_ClearCoatRoughnessMap,
                                              g_ClearCoatRoughnessMap_sampler,
                                              VSOut,
                                              Material.Textures[ClearCoatRoughnessTextureAttribId],
                                              MipBias,
                                              float4(1.0, 1.0, 1.0, 1.0)).g;
        }
#       endif
        return CearcoatRoughness * Material.Basic.ClearcoatRoughnessFactor;
    }
#   else
    {
        return 0.0;
    }
#   endif
}

float3 GetClearcoatNormal(PBRMaterialShaderInfo Material,
                          float2                NormalMapUV,
                          float2                SmoothNormalMapUV,
                          float2                dNormalMapUV_dx,
                          float2                dNormalMapUV_dy,
                          float                 MipBias)
{
    float3 ClearcoatNormal = float3(0.5, 0.5, 1.0);
#   if ENABLE_CLEAR_COAT
    {
#       if USE_CLEAR_COAT_NORMAL_MAP
        {
            ClearcoatNormal =
                SampleNormalTexture(Material.Textures[ClearCoatNormalTextureAttribId],
                                    g_ClearCoatNormalMap,
                                    g_ClearCoatNormalMap_sampler,
                                    NormalMapUV,
                                    SmoothNormalMapUV,
                                    dNormalMapUV_dx,
                                    dNormalMapUV_dy,
                                    MipBias);
        }
#       endif
    }
#endif

    ClearcoatNormal = ClearcoatNormal * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);
    ClearcoatNormal.xy *= Material.Basic.ClearcoatNormalScale;
    if (ClearcoatNormal.z < 0.0)
    {
        // The texture does not contain the Z component of the normal.
        ClearcoatNormal.z = sqrt(max(1.0 - dot(ClearcoatNormal.xy, ClearcoatNormal.xy), 0.0));
    }
    return ClearcoatNormal;
}


// Sheen
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_sheen

float3 GetSheenColor(VSOutput              VSOut,
                     PBRMaterialShaderInfo Material,
                     float                 MipBias)
{
#   if ENABLE_SHEEN
    {
        float3 SheenColor = float3(1.0, 1.0, 1.0);
#       if USE_SHEEN_COLOR_MAP
        {
            SheenColor = SampleTexture(g_SheenColorMap,
                                       g_SheenColorMap_sampler,
                                       VSOut,
                                       Material.Textures[SheenColorTextureAttribId],
                                       MipBias,
                                       float4(1.0, 1.0, 1.0, 1.0)).rgb;
            SheenColor = TO_LINEAR(SheenColor);
        }
#       endif
        return SheenColor * float3(Material.Sheen.ColorFactorR, Material.Sheen.ColorFactorG, Material.Sheen.ColorFactorB);
    }
#   else
    {
        return float3(0.0, 0.0, 0.0);
    }
#endif
}

float GetSheenRoughness(VSOutput              VSOut,
                        PBRMaterialShaderInfo Material,
                        float                 MipBias)
{
#   if ENABLE_SHEEN
    {
        float SheenRoughness = 1.0;
#       if USE_SHEEN_ROUGHNESS_MAP
        {
            SheenRoughness = SampleTexture(g_SheenRoughnessMap,
                                           g_SheenRoughnessMap_sampler,
                                           VSOut,
                                           Material.Textures[SheenRoughnessTextureAttribId],
                                           MipBias,
                                           float4(1.0, 1.0, 1.0, 1.0)).a;
        }
#       endif
        return SheenRoughness * Material.Sheen.RoughnessFactor;
    }
#   else
    {
        return 0.0;
    }
#   endif
}


// Anisotropy
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_anisotropy

float3 GetAnisotropy(VSOutput              VSOut,
                     PBRMaterialShaderInfo Material,
                     float                 MipBias)
{
#   if ENABLE_ANISOTROPY
    {
        // Default anisotropy direction is (1, 0) and default strength is 1.0.
        float3 Anisotropy = float3(1.0, 0.5, 1.0);
#       if USE_ANISOTROPY_MAP
        {
            Anisotropy = SampleTexture(g_AnisotropyMap,
                                       g_AnisotropyMap_sampler,
                                       VSOut,
                                       Material.Textures[AnisotropyTextureAttribId],
                                       MipBias,
                                       float4(Anisotropy, 1.0)).rgb;
        }
#       endif
        Anisotropy.xy = Anisotropy.xy * 2.0 - 1.0;
        Anisotropy.z *= Material.Anisotropy.Strength;
        return Anisotropy;
    }
#   else
    {
        return float3(0.0, 0.0, 0.0);
    }
#endif
}


// Iridescence
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_iridescence

float GetIridescence(VSOutput              VSOut,
                     PBRMaterialShaderInfo Material,
                     float                 MipBias)
{
#   if ENABLE_IRIDESCENCE
    {
        float Iridescence = 1.0;
#       if USE_IRIDESCENCE_MAP
        {
            Iridescence = SampleTexture(g_IridescenceMap,
                                        g_IridescenceMap_sampler,
                                        VSOut,
                                        Material.Textures[IridescenceTextureAttribId],
                                        MipBias,
                                        float4(1.0, 1.0, 1.0, 1.0)).r;
        }
#       endif
        return Iridescence * Material.Iridescence.Factor;
    }
#   else
    {
        return 0.0;
    }
#   endif
}


float GetIridescenceThickness(VSOutput              VSOut,
                              PBRMaterialShaderInfo Material,
                              float                 MipBias)
{
#   if ENABLE_IRIDESCENCE
    {
        float Thickness = 1.0;
#       if USE_IRIDESCENCE_THICKNESS_MAP
        {
            Thickness = SampleTexture(g_IridescenceThicknessMap,
                                      g_IridescenceThicknessMap_sampler,
                                      VSOut,
                                      Material.Textures[IridescenceThicknessTextureAttribId],
                                      MipBias,
                                      float4(1.0, 1.0, 1.0, 1.0)).g;
        }
#       endif
        return lerp(Material.Iridescence.ThicknessMinimum, Material.Iridescence.ThicknessMaximum, Thickness);
    }
#   else
    {
        return 0.0;
    }
#   endif
}


// Transmission
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission

float GetTransmission(VSOutput              VSOut,
                      PBRMaterialShaderInfo Material,
                      float                 MipBias)
{
#   if ENABLE_TRANSMISSION
    {
        float Transmission = 1.0;
#       if USE_TRANSMISSION_MAP
        {
            Transmission = SampleTexture(g_TransmissionMap,
                                         g_TransmissionMap_sampler,
                                         VSOut,
                                         Material.Textures[TransmissionTextureAttribId],
                                         MipBias,
                                         float4(1.0, 1.0, 1.0, 1.0)).r;
        }
#       endif
        return Transmission * Material.Transmission.Factor;
    }
#   else
    {
        return 0.0;
    }
#   endif
}


// Volume
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_volume

float GetVolumeThickness(VSOutput              VSOut,
                         PBRMaterialShaderInfo Material,
                         float                 MipBias)
{
#   if ENABLE_VOLUME
    {
        float Thickness = 1.0;
#       if USE_THICKNESS_MAP
        {
            Thickness = SampleTexture(g_ThicknessMap,
                                      g_ThicknessMap_sampler,
                                      VSOut,
                                      Material.Textures[ThicknessTextureAttribId],
                                      MipBias,
                                      float4(1.0, 1.0, 1.0, 1.0)).g;
        }
#       endif
        return Thickness * Material.Volume.ThicknessFactor;
    }
#   else
    {
        return 0.0;
    }
#   endif
}

#endif // _PBR_TEXTURES_FXH_
