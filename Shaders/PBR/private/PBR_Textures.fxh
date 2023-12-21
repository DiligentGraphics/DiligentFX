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

#if USE_IBL
    TextureCube  g_IrradianceMap;
    SamplerState g_IrradianceMap_sampler;

    TextureCube  g_PrefilteredEnvMap;
    // Use g_IrradianceMap_sampler as we are running out of sampers on D3D11
#   define       g_PrefilteredEnvMap_sampler g_IrradianceMap_sampler

    Texture2D    g_PreintegratedGGX;
    SamplerState g_PreintegratedGGX_sampler;

#   if ENABLE_SHEEN
        Texture2D g_PreintegratedCharlie;
#       define    g_PreintegratedCharlie_sampler g_PreintegratedGGX_sampler
#   endif
#endif

#if ENABLE_SHEEN
    Texture2D     g_SheenAlbedoScalingLUT;
    SamplerState  g_SheenAlbedoScalingLUT_sampler;    
#endif

#if USE_COLOR_MAP
    Texture2DArray g_ColorMap;
    SamplerState   g_ColorMap_sampler;
#endif

#if USE_METALLIC_MAP
    Texture2DArray g_MetallicMap;
    SamplerState   g_MetallicMap_sampler;
#endif

#if USE_ROUGHNESS_MAP
    Texture2DArray g_RoughnessMap;
    SamplerState   g_RoughnessMap_sampler;
#endif

#if USE_PHYS_DESC_MAP
    Texture2DArray g_PhysicalDescriptorMap;
    SamplerState   g_PhysicalDescriptorMap_sampler;
#endif

#if USE_NORMAL_MAP
    Texture2DArray g_NormalMap;
    SamplerState   g_NormalMap_sampler;
#endif

#if USE_AO_MAP
    Texture2DArray g_AOMap;
    SamplerState   g_AOMap_sampler;
#endif

#if USE_EMISSIVE_MAP
    Texture2DArray g_EmissiveMap;
    SamplerState   g_EmissiveMap_sampler;
#endif

#if USE_CLEAR_COAT_MAP
    Texture2DArray g_ClearCoatMap;
    SamplerState   g_ClearCoatMap_sampler;
#endif

#if USE_CLEAR_COAT_ROUGHNESS_MAP
    Texture2DArray g_ClearCoatRoughnessMap;
    SamplerState   g_ClearCoatRoughnessMap_sampler;
#endif

#if USE_CLEAR_COAT_NORMAL_MAP
    Texture2DArray g_ClearCoatNormalMap;
    SamplerState   g_ClearCoatNormalMap_sampler;
#endif

#if USE_SHEEN_COLOR_MAP
    Texture2DArray g_SheenColorMap;
    SamplerState   g_SheenColorMap_sampler;
#endif

#if USE_SHEEN_ROUGHNESS_MAP
    Texture2DArray g_SheenRoughnessMap;
    SamplerState   g_SheenRoughnessMap_sampler;
#endif

#if USE_ANISOTROPY_MAP
    Texture2DArray g_AnisotropyMap;
    SamplerState   g_AnisotropyMap_sampler;
#endif

#if USE_IRIDESCENCE_MAP
    Texture2DArray g_IridescenceMap;
    SamplerState   g_IridescenceMap_sampler;
#endif

#if USE_IRIDESCENCE_THICKNESS_MAP
    Texture2DArray g_IridescenceThicknessMap;
    SamplerState   g_IridescenceThicknessMap_sampler;
#endif

#if USE_TRANSMISSION_MAP
    Texture2DArray g_TransmissionMap;
    SamplerState   g_TransmissionMap_sampler;
#endif

#if USE_THICKNESS_MAP
    Texture2DArray g_ThicknessMap;
    SamplerState   g_ThicknessMap_sampler;
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
                     float4                    DefaultValue)
{
    if (TexAttribs.UVSelector < 0.0)
    {
        return DefaultValue;
    }
    
#   if USE_TEXCOORD0 || USE_TEXCOORD1
    {
        float2 UV = SelectUV(VSOut, TexAttribs.UVSelector);
#       if ENABLE_TEXCOORD_TRANSFORM
        {
            UV = TransformUV(UV, TexAttribs);
        }
#       endif

#       if USE_TEXTURE_ATLAS
        {
            SampleTextureAtlasAttribs SampleAttribs;
            SampleAttribs.f2UV                   = frac(UV) * TexAttribs.AtlasUVScaleAndBias.xy + TexAttribs.AtlasUVScaleAndBias.zw;
            SampleAttribs.f2SmoothUV             = UV      * TexAttribs.AtlasUVScaleAndBias.xy;
            SampleAttribs.f2dSmoothUV_dx         = ddx(UV) * TexAttribs.AtlasUVScaleAndBias.xy;
            SampleAttribs.f2dSmoothUV_dy         = ddy(UV) * TexAttribs.AtlasUVScaleAndBias.xy;
            SampleAttribs.fSlice                 = TexAttribs.TextureSlice;
            SampleAttribs.f4UVRegion             = TexAttribs.AtlasUVScaleAndBias;
            SampleAttribs.fSmallestValidLevelDim = 4.0;
            SampleAttribs.IsNonFilterable        = false;
            SampleAttribs.fMaxAnisotropy         = 1.0; // Only used on GLES
            return SampleTextureAtlas(Tex, Tex_sampler, SampleAttribs);
        }
#       else
        {
            return Tex.Sample(Tex_sampler, float3(UV, TexAttribs.TextureSlice));
        }
#       endif
    }
#   else
    {
        return DefaultValue;
    }
#   endif
}

float4 GetBaseColor(VSOutput              VSOut,
                    PBRMaterialShaderInfo Material)
{
    float4 BaseColor = float4(1.0, 1.0, 1.0, 1.0);

#   if USE_COLOR_MAP
    {
        BaseColor = SampleTexture(g_ColorMap,
                                  g_ColorMap_sampler,
                                  VSOut,
                                  Material.Textures[BaseColorTextureAttribId],
                                  float4(1.0, 1.0, 1.0, 1.0));
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
                           float2                    dNormalMapUV_dy)
{
    if (TexAttribs.UVSelector < 0.0)
    {
        return float3(0.5, 0.5, 1.0);
    }

#   if USE_TEXTURE_ATLAS
    {
        SampleTextureAtlasAttribs SampleAttribs;
        SampleAttribs.f2UV                   = NormalMapUV;
        SampleAttribs.f2SmoothUV             = SmoothNormalMapUV;
        SampleAttribs.f2dSmoothUV_dx         = dNormalMapUV_dx;
        SampleAttribs.f2dSmoothUV_dy         = dNormalMapUV_dy;
        SampleAttribs.fSlice                 = TexAttribs.TextureSlice;
        SampleAttribs.f4UVRegion             = TexAttribs.AtlasUVScaleAndBias;
        SampleAttribs.fSmallestValidLevelDim = 4.0;
        SampleAttribs.IsNonFilterable        = false;
        SampleAttribs.fMaxAnisotropy         = 1.0; // Only used on GLES
        return SampleTextureAtlas(NormalMap, NormalMap_sampler, SampleAttribs).xyz;
    }
#   else
    {
        return NormalMap.Sample(NormalMap_sampler, float3(NormalMapUV, TexAttribs.TextureSlice)).xyz;
    }
#   endif
}

float3 GetMicroNormal(PBRMaterialShaderInfo Material,
                      float2                NormalMapUV,
                      float2                SmoothNormalMapUV,
                      float2                dNormalMapUV_dx,
                      float2                dNormalMapUV_dy)
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
                                          dNormalMapUV_dy);
    }
#endif

    return MicroNormal * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);
}

float GetOcclusion(VSOutput              VSOut,
                   PBRMaterialShaderInfo Material)
{
    float Occlusion = 1.0;
#   if USE_AO_MAP
    {
        Occlusion = SampleTexture(g_AOMap,
                                  g_AOMap_sampler,
                                  VSOut,
                                  Material.Textures[OcclusionTextureAttribId],
                                  float4(1.0, 1.0, 1.0, 1.0)).r;
    }
#   endif
    return Occlusion * Material.Basic.OcclusionFactor;
}

float3 GetEmissive(VSOutput              VSOut,
                   PBRMaterialShaderInfo Material)
{
    float3 Emissive = float3(1.0, 1.0, 1.0);
#   if USE_EMISSIVE_MAP
    {
        Emissive = SampleTexture(g_EmissiveMap,
                                 g_EmissiveMap_sampler,
                                 VSOut,
                                 Material.Textures[EmissiveTextureAttribId],
                                 float4(1.0, 1.0, 1.0, 1.0)).rgb;
        Emissive = TO_LINEAR(Emissive);
    }
#   endif
    return Emissive * Material.Basic.EmissiveFactor.rgb;
}

float4 GetPhysicalDesc(VSOutput              VSOut,
                       PBRMaterialShaderInfo Material)
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
                                           float4(1.0, 1.0, 1.0, 1.0)).r;
        }
#       endif

#       if USE_ROUGHNESS_MAP
        {
            PhysicalDesc.g = SampleTexture(g_RoughnessMap,
                                           g_RoughnessMap_sampler,
                                           VSOut,
                                           Material.Textures[RoughnessTextureAttribId],
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
                         PBRMaterialShaderInfo Material)
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
                            PBRMaterialShaderInfo Material)
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
                          float2                dNormalMapUV_dy)
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
                                    dNormalMapUV_dy);
        }
#       endif
    }
#endif
    return ClearcoatNormal * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);
}


// Sheen
// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_sheen

float3 GetSheenColor(VSOutput              VSOut,
                     PBRMaterialShaderInfo Material)
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
                        PBRMaterialShaderInfo Material)
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
                     PBRMaterialShaderInfo Material)
{
#   if ENABLE_ANISOTROPY
    {
        float3 Anisotropy = float3(0.5, 0.5, 1.0);
#       if USE_ANISOTROPY_MAP
        {
            Anisotropy = SampleTexture(g_AnisotropyMap,
                                       g_AnisotropyMap_sampler,
                                       VSOut,
                                       Material.Textures[AnisotropyTextureAttribId],
                                       float4(1.0, 1.0, 1.0, 1.0)).rgb;
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
                     PBRMaterialShaderInfo Material)
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
                              PBRMaterialShaderInfo Material)
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
                      PBRMaterialShaderInfo Material)
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
                         PBRMaterialShaderInfo Material)
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
