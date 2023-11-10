#ifndef _PBR_TEXTURES_FXH_
#define _PBR_TEXTURES_FXH_

#if USE_TEXTURE_ATLAS
#   include "AtlasSampling.fxh"
#endif

#if USE_IBL
    TextureCube  g_IrradianceMap;
    SamplerState g_IrradianceMap_sampler;

    TextureCube  g_PrefilteredEnvMap;
    SamplerState g_PrefilteredEnvMap_sampler;

    Texture2D     g_BRDF_LUT;
    SamplerState  g_BRDF_LUT_sampler;
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

float4 SampleTexture(Texture2DArray Tex,
                     SamplerState   Tex_sampler,
                     VSOutput       VSOut,
                     float          Selector, 
                     float4         ScaleBias,
                     float          Slice,
                     float4         DefaultValue)
{
#   if USE_TEXCOORD0 || USE_TEXCOORD1
    {
        float2 UV = SelectUV(VSOut, Selector);
#       if USE_TEXTURE_ATLAS
        {
            if (Selector < 0.0)
            {
                return DefaultValue;
            }
            else
            {
                SampleTextureAtlasAttribs SampleAttribs;
                SampleAttribs.f2UV                   = frac(UV) * ScaleBias.xy + ScaleBias.zw;
                SampleAttribs.f2SmoothUV             = UV * ScaleBias.xy;
                SampleAttribs.f2dSmoothUV_dx         = ddx(UV) * ScaleBias.xy;
                SampleAttribs.f2dSmoothUV_dy         = ddy(UV) * ScaleBias.xy;
                SampleAttribs.fSlice                 = Slice;
                SampleAttribs.f4UVRegion             = ScaleBias;
                SampleAttribs.fSmallestValidLevelDim = 4.0;
                SampleAttribs.IsNonFilterable        = false;
                return SampleTextureAtlas(Tex, Tex_sampler, SampleAttribs);
            }
        }
#       else
        {
            return Tex.Sample(Tex_sampler, float3(UV, Slice));
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
                                  Material.BaseColorTextureUVSelector,
                                  Material.BaseColorUVScaleBias,
                                  Material.BaseColorSlice,
                                  float4(1.0, 1.0, 1.0, 1.0));
        BaseColor = float4(TO_LINEAR(BaseColor.rgb), BaseColor.a);
    }
#   endif

#   if USE_VERTEX_COLORS
    {
        BaseColor *= VSOut.Color;
    }
#   endif
    return BaseColor * Material.BaseColorFactor;
}

float3 GetMicroNormal(VSOutput              VSOut,
                      PBRMaterialShaderInfo Material,
                      float2                NormalMapUV,
                      float2                dNormalMapUV_dx,
                      float2                dNormalMapUV_dy)
{
    float3 MicroNormal = float3(0.5, 0.5, 1.0);

#   if USE_NORMAL_MAP && (USE_TEXCOORD0 || USE_TEXCOORD1)
    {
#       if USE_TEXTURE_ATLAS
        {
            if (Material.NormalTextureUVSelector >= 0.0)
            {
                SampleTextureAtlasAttribs SampleAttribs;
                SampleAttribs.f2UV                   = NormalMapUV;
                SampleAttribs.f2SmoothUV             = SelectUV(VSOut, Material.NormalTextureUVSelector) * Material.NormalMapUVScaleBias.xy;
                SampleAttribs.f2dSmoothUV_dx         = dNormalMapUV_dx;
                SampleAttribs.f2dSmoothUV_dy         = dNormalMapUV_dy;
                SampleAttribs.fSlice                 = Material.NormalSlice;
                SampleAttribs.f4UVRegion             = Material.NormalMapUVScaleBias;
                SampleAttribs.fSmallestValidLevelDim = 4.0;
                SampleAttribs.IsNonFilterable        = false;
                MicroNormal = SampleTextureAtlas(g_NormalMap, g_NormalMap_sampler, SampleAttribs).xyz;
            }
        }
#       else
        {
            MicroNormal = g_NormalMap.Sample(g_NormalMap_sampler, float3(NormalMapUV, Material.NormalSlice)).xyz;
        }
#       endif
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
                                  Material.OcclusionTextureUVSelector,
                                  Material.OcclusionUVScaleBias,
                                  Material.OcclusionSlice,
                                  float4(1.0, 1.0, 1.0, 1.0)).r;
    }
#   endif
    return Occlusion * Material.OcclusionFactor;
}

float3 GetEmissive(VSOutput              VSOut,
                   PBRMaterialShaderInfo Material)
{
    float3 Emissive = float3(0.0, 0.0, 0.0);

#   if USE_EMISSIVE_MAP
    {
        Emissive = SampleTexture(g_EmissiveMap,
                                 g_EmissiveMap_sampler,
                                 VSOut,
                                 Material.EmissiveTextureUVSelector,
                                 Material.EmissiveUVScaleBias,
                                 Material.EmissiveSlice,
                                 float4(0.0, 0.0, 0.0, 0.0)).rgb;
        Emissive = TO_LINEAR(Emissive);
    }
#   endif
    return Emissive * Material.EmissiveFactor.rgb;
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
                                     Material.PhysicalDescriptorTextureUVSelector,
                                     Material.PhysicalDescriptorUVScaleBias,
                                     Material.PhysicalDescriptorSlice,
                                     float4(1.0, 1.0, 1.0, 1.0));
    }
#   else
    {
#       if USE_METALLIC_MAP
        {
            PhysicalDesc.b = SampleTexture(g_MetallicMap,
                                           g_MetallicMap_sampler,
                                           VSOut,
                                           Material.PhysicalDescriptorTextureUVSelector,
                                           Material.PhysicalDescriptorUVScaleBias,
                                           Material.PhysicalDescriptorSlice,
                                           float4(1.0, 1.0, 1.0, 1.0)).r;
        }
#       endif

#       if USE_ROUGHNESS_MAP
        {
            PhysicalDesc.g = SampleTexture(g_RoughnessMap,
                                           g_RoughnessMap_sampler,
                                           VSOut,
                                           Material.PhysicalDescriptorTextureUVSelector,
                                           Material.PhysicalDescriptorUVScaleBias,
                                           Material.PhysicalDescriptorSlice,
                                           float4(1.0, 1.0, 1.0, 1.0)).r;

        }
#       endif
    }
#endif 

    return PhysicalDesc;
}

#endif // _PBR_TEXTURES_FXH_
