"// PBR shader based on the Khronos WebGL PBR implementation\n"
"// See https://github.com/KhronosGroup/glTF-WebGL-PBR\n"
"// Supports both metallic roughness and specular glossiness inputs\n"
"\n"
"#include \"BasicStructures.fxh\"\n"
"#include \"PBR_Shading.fxh\"\n"
"#include \"ToneMapping.fxh\"\n"
"\n"
"#include \"VSOutputStruct.generated\"\n"
"// struct VSOutput\n"
"// {\n"
"//     float4 ClipPos  : SV_Position;\n"
"//     float3 WorldPos : WORLD_POS;\n"
"//     float4 Color    : COLOR;\n"
"//     float3 Normal   : NORMAL;\n"
"//     float2 UV0      : UV0;\n"
"//     float2 UV1      : UV1;\n"
"// };\n"
"\n"
"\n"
"#ifndef USE_TEXTURE_ATLAS\n"
"#   define USE_TEXTURE_ATLAS 0\n"
"#endif\n"
"\n"
"#ifndef ALLOW_DEBUG_VIEW\n"
"#   define ALLOW_DEBUG_VIEW 0\n"
"#endif\n"
"\n"
"#if USE_TEXTURE_ATLAS\n"
"#   include \"AtlasSampling.fxh\"\n"
"#endif\n"
"\n"
"cbuffer cbCameraAttribs\n"
"{\n"
"    CameraAttribs g_CameraAttribs;\n"
"}\n"
"\n"
"cbuffer cbLightAttribs\n"
"{\n"
"    LightAttribs g_LightAttribs;\n"
"}\n"
"\n"
"cbuffer cbPBRAttribs\n"
"{\n"
"    PBRShaderAttribs g_PBRAttribs;\n"
"}\n"
"\n"
"#if USE_IBL\n"
"    TextureCube  g_IrradianceMap;\n"
"    SamplerState g_IrradianceMap_sampler;\n"
"\n"
"    TextureCube  g_PrefilteredEnvMap;\n"
"    SamplerState g_PrefilteredEnvMap_sampler;\n"
"\n"
"    Texture2D     g_BRDF_LUT;\n"
"    SamplerState  g_BRDF_LUT_sampler;\n"
"#endif\n"
"\n"
"#if USE_COLOR_MAP\n"
"    Texture2DArray g_ColorMap;\n"
"    SamplerState   g_ColorMap_sampler;\n"
"#endif\n"
"\n"
"#if USE_METALLIC_MAP\n"
"    Texture2DArray g_MetallicMap;\n"
"    SamplerState   g_MetallicMap_sampler;\n"
"#endif\n"
"\n"
"#if USE_ROUGHNESS_MAP\n"
"    Texture2DArray g_RoughnessMap;\n"
"    SamplerState   g_RoughnessMap_sampler;\n"
"#endif\n"
"\n"
"#if USE_PHYS_DESC_MAP\n"
"    Texture2DArray g_PhysicalDescriptorMap;\n"
"    SamplerState   g_PhysicalDescriptorMap_sampler;\n"
"#endif\n"
"\n"
"#if USE_NORMAL_MAP\n"
"    Texture2DArray g_NormalMap;\n"
"    SamplerState   g_NormalMap_sampler;\n"
"#endif\n"
"\n"
"#if USE_AO_MAP\n"
"    Texture2DArray g_AOMap;\n"
"    SamplerState   g_AOMap_sampler;\n"
"#endif\n"
"\n"
"#if USE_EMISSIVE_MAP\n"
"    Texture2DArray g_EmissiveMap;\n"
"    SamplerState   g_EmissiveMap_sampler;\n"
"#endif\n"
"\n"
"\n"
"float2 SelectUV(VSOutput VSOut, float Selector)\n"
"{\n"
"#if USE_TEXCOORD0 && USE_TEXCOORD1\n"
"    return lerp(VSOut.UV0, VSOut.UV1, Selector);\n"
"#elif USE_TEXCOORD0\n"
"    return VSOut.UV0;\n"
"#elif USE_TEXCOORD1\n"
"    return VSOut.UV1;\n"
"#else\n"
"    return float2(0.0, 0.0);\n"
"#endif\n"
"}\n"
"\n"
"float4 SampleTexture(Texture2DArray Tex,\n"
"                     SamplerState   Tex_sampler,\n"
"                     VSOutput       VSOut,\n"
"                     float          Selector,\n"
"                     float4         ScaleBias,\n"
"                     float          Slice,\n"
"                     float4         DefaultValue)\n"
"{\n"
"#   if USE_TEXCOORD0 || USE_TEXCOORD1\n"
"    {\n"
"        float2 UV = SelectUV(VSOut, Selector);\n"
"#       if USE_TEXTURE_ATLAS\n"
"        {\n"
"            if (Selector < 0.0)\n"
"            {\n"
"                return DefaultValue;\n"
"            }\n"
"            else\n"
"            {\n"
"                SampleTextureAtlasAttribs SampleAttribs;\n"
"                SampleAttribs.f2UV                   = frac(UV) * ScaleBias.xy + ScaleBias.zw;\n"
"                SampleAttribs.f2SmoothUV             = UV * ScaleBias.xy;\n"
"                SampleAttribs.f2dSmoothUV_dx         = ddx(UV) * ScaleBias.xy;\n"
"                SampleAttribs.f2dSmoothUV_dy         = ddy(UV) * ScaleBias.xy;\n"
"                SampleAttribs.fSlice                 = Slice;\n"
"                SampleAttribs.f4UVRegion             = ScaleBias;\n"
"                SampleAttribs.fSmallestValidLevelDim = 4.0;\n"
"                SampleAttribs.IsNonFilterable        = false;\n"
"                return SampleTextureAtlas(Tex, Tex_sampler, SampleAttribs);\n"
"            }\n"
"        }\n"
"#       else\n"
"        {\n"
"            return Tex.Sample(Tex_sampler, float3(UV, Slice));\n"
"        }\n"
"#       endif\n"
"    }\n"
"#   else\n"
"    {\n"
"        return DefaultValue;\n"
"    }\n"
"#   endif\n"
"}\n"
"\n"
"float4 GetBaseColor(VSOutput VSOut)\n"
"{\n"
"    float4 BaseColor = float4(1.0, 1.0, 1.0, 1.0);\n"
"\n"
"#   if USE_COLOR_MAP\n"
"    {\n"
"        BaseColor = SampleTexture(g_ColorMap,\n"
"                                  g_ColorMap_sampler,\n"
"                                  VSOut,\n"
"                                  g_PBRAttribs.Material.BaseColorTextureUVSelector,\n"
"                                  g_PBRAttribs.Material.BaseColorUVScaleBias,\n"
"                                  g_PBRAttribs.Material.BaseColorSlice,\n"
"                                  float4(1.0, 1.0, 1.0, 1.0));\n"
"        BaseColor = float4(TO_LINEAR(BaseColor.rgb), BaseColor.a);\n"
"    }\n"
"#   endif\n"
"\n"
"#   if USE_VERTEX_COLORS\n"
"    {\n"
"        BaseColor *= VSOut.Color;\n"
"    }\n"
"#   endif\n"
"    return BaseColor * g_PBRAttribs.Material.BaseColorFactor;\n"
"}\n"
"\n"
"float3 GetMicroNormal(VSOutput VSOut,\n"
"                      float2 NormalMapUV,\n"
"                      float2 dNormalMapUV_dx,\n"
"                      float2 dNormalMapUV_dy)\n"
"{\n"
"    float3 MicroNormal = float3(0.5, 0.5, 1.0);\n"
"\n"
"#   if USE_NORMAL_MAP && (USE_TEXCOORD0 || USE_TEXCOORD1)\n"
"    {\n"
"#       if USE_TEXTURE_ATLAS\n"
"        {\n"
"            if (g_PBRAttribs.Material.NormalTextureUVSelector >= 0.0)\n"
"            {\n"
"                SampleTextureAtlasAttribs SampleAttribs;\n"
"                SampleAttribs.f2UV                   = NormalMapUV;\n"
"                SampleAttribs.f2SmoothUV             = SelectUV(VSOut, g_PBRAttribs.Material.NormalTextureUVSelector) * g_PBRAttribs.Material.NormalMapUVScaleBias.xy;\n"
"                SampleAttribs.f2dSmoothUV_dx         = dNormalMapUV_dx;\n"
"                SampleAttribs.f2dSmoothUV_dy         = dNormalMapUV_dy;\n"
"                SampleAttribs.fSlice                 = g_PBRAttribs.Material.NormalSlice;\n"
"                SampleAttribs.f4UVRegion             = g_PBRAttribs.Material.NormalMapUVScaleBias;\n"
"                SampleAttribs.fSmallestValidLevelDim = 4.0;\n"
"                SampleAttribs.IsNonFilterable        = false;\n"
"                MicroNormal = SampleTextureAtlas(g_NormalMap, g_NormalMap_sampler, SampleAttribs).xyz;\n"
"            }\n"
"        }\n"
"#       else\n"
"        {\n"
"            MicroNormal = g_NormalMap.Sample(g_NormalMap_sampler, float3(NormalMapUV, g_PBRAttribs.Material.NormalSlice)).xyz;\n"
"        }\n"
"#       endif\n"
"    }\n"
"#endif\n"
"\n"
"    return MicroNormal * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);\n"
"}\n"
"\n"
"float GetOcclusion(VSOutput VSOut)\n"
"{\n"
"    float Occlusion = 1.0;\n"
"#   if USE_AO_MAP\n"
"    {\n"
"        Occlusion = SampleTexture(g_AOMap,\n"
"                                  g_AOMap_sampler,\n"
"                                  VSOut,\n"
"                                  g_PBRAttribs.Material.OcclusionTextureUVSelector,\n"
"                                  g_PBRAttribs.Material.OcclusionUVScaleBias,\n"
"                                  g_PBRAttribs.Material.OcclusionSlice,\n"
"                                  float4(1.0, 1.0, 1.0, 1.0)).r;\n"
"    }\n"
"#   endif\n"
"    return Occlusion * g_PBRAttribs.Material.OcclusionFactor;\n"
"}\n"
"\n"
"float3 GetEmissive(VSOutput VSOut)\n"
"{\n"
"    float3 Emissive = float3(0.0, 0.0, 0.0);\n"
"\n"
"#   if USE_EMISSIVE_MAP\n"
"    {\n"
"        Emissive = SampleTexture(g_EmissiveMap,\n"
"                                 g_EmissiveMap_sampler,\n"
"                                 VSOut,\n"
"                                 g_PBRAttribs.Material.EmissiveTextureUVSelector,\n"
"                                 g_PBRAttribs.Material.EmissiveUVScaleBias,\n"
"                                 g_PBRAttribs.Material.EmissiveSlice,\n"
"                                 float4(0.0, 0.0, 0.0, 0.0)).rgb;\n"
"        Emissive = TO_LINEAR(Emissive);\n"
"    }\n"
"#   endif\n"
"    return Emissive * g_PBRAttribs.Material.EmissiveFactor.rgb;\n"
"}\n"
"\n"
"float4 GetPhysicalDesc(VSOutput VSOut)\n"
"{\n"
"    // Set defaults to 1 so that if the textures are not available, the values\n"
"    // are controlled by the metallic/roughness scale factors.\n"
"    float4 PhysicalDesc = float4(1.0, 1.0, 1.0, 1.0);\n"
"#   if USE_PHYS_DESC_MAP\n"
"    {\n"
"        PhysicalDesc = SampleTexture(g_PhysicalDescriptorMap,\n"
"                                     g_PhysicalDescriptorMap_sampler,\n"
"                                     VSOut,\n"
"                                     g_PBRAttribs.Material.PhysicalDescriptorTextureUVSelector,\n"
"                                     g_PBRAttribs.Material.PhysicalDescriptorUVScaleBias,\n"
"                                     g_PBRAttribs.Material.PhysicalDescriptorSlice,\n"
"                                     float4(1.0, 1.0, 1.0, 1.0));\n"
"    }\n"
"#   else\n"
"    {\n"
"#       if USE_METALLIC_MAP\n"
"        {\n"
"            PhysicalDesc.b = SampleTexture(g_MetallicMap,\n"
"                                           g_MetallicMap_sampler,\n"
"                                           VSOut,\n"
"                                           g_PBRAttribs.Material.PhysicalDescriptorTextureUVSelector,\n"
"                                           g_PBRAttribs.Material.PhysicalDescriptorUVScaleBias,\n"
"                                           g_PBRAttribs.Material.PhysicalDescriptorSlice,\n"
"                                           float4(1.0, 1.0, 1.0, 1.0)).r;\n"
"        }\n"
"#       endif\n"
"\n"
"#       if USE_ROUGHNESS_MAP\n"
"        {\n"
"            PhysicalDesc.g = SampleTexture(g_RoughnessMap,\n"
"                                           g_RoughnessMap_sampler,\n"
"                                           VSOut,\n"
"                                           g_PBRAttribs.Material.PhysicalDescriptorTextureUVSelector,\n"
"                                           g_PBRAttribs.Material.PhysicalDescriptorUVScaleBias,\n"
"                                           g_PBRAttribs.Material.PhysicalDescriptorSlice,\n"
"                                           float4(1.0, 1.0, 1.0, 1.0)).r;\n"
"\n"
"        }\n"
"#       endif\n"
"    }\n"
"#endif\n"
"\n"
"    return PhysicalDesc;\n"
"}\n"
"\n"
"void main(in  VSOutput VSOut,\n"
"          in  bool     IsFrontFace : SV_IsFrontFace,\n"
"          out float4   OutColor    : SV_Target)\n"
"{\n"
"    float4 BaseColor = GetBaseColor(VSOut);\n"
"\n"
"    float2 NormalMapUV  = SelectUV(VSOut, g_PBRAttribs.Material.NormalTextureUVSelector);\n"
"\n"
"    // We have to compute gradients in uniform flow control to avoid issues with perturbed normal\n"
"    float3 dWorldPos_dx = ddx(VSOut.WorldPos);\n"
"    float3 dWorldPos_dy = ddy(VSOut.WorldPos);\n"
"    float2 dNormalMapUV_dx = ddx(NormalMapUV);\n"
"    float2 dNormalMapUV_dy = ddy(NormalMapUV);\n"
"#if USE_TEXTURE_ATLAS\n"
"    {\n"
"        NormalMapUV = frac(NormalMapUV);\n"
"        NormalMapUV = NormalMapUV * g_PBRAttribs.Material.NormalMapUVScaleBias.xy + g_PBRAttribs.Material.NormalMapUVScaleBias.zw;\n"
"        dNormalMapUV_dx *= g_PBRAttribs.Material.NormalMapUVScaleBias.xy;\n"
"        dNormalMapUV_dy *= g_PBRAttribs.Material.NormalMapUVScaleBias.xy;\n"
"    }\n"
"#endif\n"
"\n"
"    if (g_PBRAttribs.Material.AlphaMode == PBR_ALPHA_MODE_MASK && BaseColor.a < g_PBRAttribs.Material.AlphaMaskCutoff)\n"
"    {\n"
"        discard;\n"
"    }\n"
"\n"
"    float3 TSNormal     = GetMicroNormal(VSOut, NormalMapUV, dNormalMapUV_dx, dNormalMapUV_dy);\n"
"    float  Occlusion    = GetOcclusion(VSOut);\n"
"    float3 Emissive     = GetEmissive(VSOut);\n"
"    float4 PhysicalDesc = GetPhysicalDesc(VSOut);\n"
"\n"
"     if (g_PBRAttribs.Material.Workflow == PBR_WORKFLOW_SPECULAR_GLOSINESS)\n"
"    {\n"
"        PhysicalDesc.rgb = TO_LINEAR(PhysicalDesc.rgb) * g_PBRAttribs.Material.SpecularFactor.rgb;\n"
"        const float u_GlossinessFactor = 1.0;\n"
"        PhysicalDesc.a *= u_GlossinessFactor;\n"
"    }\n"
"    else if (g_PBRAttribs.Material.Workflow == PBR_WORKFLOW_METALLIC_ROUGHNESS)\n"
"    {\n"
"        // PhysicalDesc should already be in linear space\n"
"        PhysicalDesc.g = saturate(PhysicalDesc.g * g_PBRAttribs.Material.RoughnessFactor);\n"
"        PhysicalDesc.b = saturate(PhysicalDesc.b * g_PBRAttribs.Material.MetallicFactor);\n"
"    }\n"
"    float metallic = 0.0;\n"
"    SurfaceReflectanceInfo SrfInfo = GetSurfaceReflectance(g_PBRAttribs.Material.Workflow, BaseColor, PhysicalDesc, metallic);\n"
"\n"
"    float3 view = normalize(g_CameraAttribs.f4Position.xyz - VSOut.WorldPos.xyz); // Direction from surface point to camera\n"
"\n"
"    float3 color           = BaseColor.rgb;\n"
"    float3 perturbedNormal = float3(0.0, 0.0, 0.0);\n"
"    float3 DirectLighting  = float3(0.0, 0.0, 0.0);\n"
"\n"
"    IBL_Contribution IBLContrib;\n"
"    IBLContrib.f3Diffuse  = float3(0.0, 0.0, 0.0);\n"
"    IBLContrib.f3Specular = float3(0.0, 0.0, 0.0);\n"
"\n"
"#if USE_VERTEX_NORMALS\n"
"    {\n"
"        // LIGHTING\n"
"        perturbedNormal = PerturbNormal(dWorldPos_dx,\n"
"                                        dWorldPos_dy,\n"
"                                        dNormalMapUV_dx,\n"
"                                        dNormalMapUV_dy,\n"
"                                        VSOut.Normal,\n"
"                                        TSNormal,\n"
"                                        g_PBRAttribs.Material.NormalTextureUVSelector >= 0.0,\n"
"                                        IsFrontFace);\n"
"\n"
"        DirectLighting = ApplyDirectionalLight(g_LightAttribs.f4Direction.xyz, g_LightAttribs.f4Intensity.rgb, SrfInfo, perturbedNormal, view);\n"
"        color = DirectLighting;\n"
"\n"
"        //#ifdef USE_PUNCTUAL\n"
"        //    for (int i = 0; i < LIGHT_COUNT; ++i)\n"
"        //    {\n"
"        //        Light light = u_Lights[i];\n"
"        //        if (light.type == LightType_Directional)\n"
"        //        {\n"
"        //            color += applyDirectionalLight(light, materialInfo, normal, view);\n"
"        //        }\n"
"        //        else if (light.type == LightType_Point)\n"
"        //        {\n"
"        //            color += applyPointLight(light, materialInfo, normal, view);\n"
"        //        }\n"
"        //        else if (light.type == LightType_Spot)\n"
"        //        {\n"
"        //            color += applySpotLight(light, materialInfo, normal, view);\n"
"        //        }\n"
"        //    }\n"
"        //#endif\n"
"        //\n"
"\n"
"        // Calculate lighting contribution from image based lighting source (IBL)\n"
"#       if USE_IBL\n"
"        {\n"
"            IBLContrib =\n"
"                GetIBLContribution(SrfInfo, perturbedNormal, view, float(g_PBRAttribs.Renderer.PrefilteredCubeMipLevels),\n"
"                                   g_BRDF_LUT,          g_BRDF_LUT_sampler,\n"
"                                   g_IrradianceMap,     g_IrradianceMap_sampler,\n"
"                                   g_PrefilteredEnvMap, g_PrefilteredEnvMap_sampler);\n"
"            color += (IBLContrib.f3Diffuse + IBLContrib.f3Specular) * g_PBRAttribs.Renderer.IBLScale;\n"
"        }\n"
"#       endif\n"
"    }\n"
"#   endif\n"
"\n"
"#   if USE_AO_MAP\n"
"    {\n"
"        color = lerp(color, color * Occlusion, g_PBRAttribs.Renderer.OcclusionStrength);\n"
"    }\n"
"#endif\n"
"\n"
"#   if USE_EMISSIVE_MAP\n"
"    {\n"
"        color += Emissive.rgb * g_PBRAttribs.Renderer.EmissionScale;\n"
"    }\n"
"#   endif\n"
"\n"
"    // Perform tone mapping\n"
"    ToneMappingAttribs TMAttribs;\n"
"    TMAttribs.iToneMappingMode     = TONE_MAPPING_MODE_UNCHARTED2;\n"
"    TMAttribs.bAutoExposure        = false;\n"
"    TMAttribs.fMiddleGray          = g_PBRAttribs.Renderer.MiddleGray;\n"
"    TMAttribs.bLightAdaptation     = false;\n"
"    TMAttribs.fWhitePoint          = g_PBRAttribs.Renderer.WhitePoint;\n"
"    TMAttribs.fLuminanceSaturation = 1.0;\n"
"    color = ToneMap(color, TMAttribs, g_PBRAttribs.Renderer.AverageLogLum);\n"
"\n"
"    // Add highlight color\n"
"    color = lerp(color, g_PBRAttribs.Renderer.HighlightColor.rgb, g_PBRAttribs.Renderer.HighlightColor.a);\n"
"\n"
"    OutColor = float4(color, BaseColor.a);\n"
"\n"
"#   if ALLOW_DEBUG_VIEW\n"
"    // Shader inputs debug visualization\n"
"    if (g_PBRAttribs.Renderer.DebugViewType != 0)\n"
"    {\n"
"        switch (g_PBRAttribs.Renderer.DebugViewType)\n"
"        {\n"
"            case DEBUG_VIEW_BASE_COLOR:       OutColor.rgba = BaseColor;                                            break;\n"
"            case DEBUG_VIEW_TEXCOORD0:        OutColor.rgb  = float3(VSOut.UV0, 0.0);                               break;\n"
"            case DEBUG_VIEW_TRANSPARENCY:     OutColor.rgba = float4(BaseColor.a, BaseColor.a, BaseColor.a, 1.0);   break;\n"
"            case DEBUG_VIEW_NORMAL_MAP:       OutColor.rgb  = TSNormal.xyz;                                         break;\n"
"            case DEBUG_VIEW_OCCLUSION:        OutColor.rgb  = Occlusion * float3(1.0, 1.0, 1.0);                    break;\n"
"            case DEBUG_VIEW_EMISSIVE:         OutColor.rgb  = Emissive.rgb;                                         break;\n"
"            case DEBUG_VIEW_METALLIC:         OutColor.rgb  = metallic * float3(1.0, 1.0, 1.0);                     break;\n"
"            case DEBUG_VIEW_ROUGHNESS:        OutColor.rgb  = SrfInfo.PerceptualRoughness * float3(1.0, 1.0, 1.0);  break;\n"
"            case DEBUG_VIEW_DIFFUSE_COLOR:    OutColor.rgb  = SrfInfo.DiffuseColor;                                 break;\n"
"            case DEBUG_VIEW_SPECULAR_COLOR:   OutColor.rgb  = SrfInfo.Reflectance0;                                 break;\n"
"            case DEBUG_VIEW_REFLECTANCE90:    OutColor.rgb  = SrfInfo.Reflectance90;                                break;\n"
"            case DEBUG_VIEW_MESH_NORMAL:      OutColor.rgb  = abs(VSOut.Normal / max(length(VSOut.Normal), 1e-3));  break;\n"
"            case DEBUG_VIEW_PERTURBED_NORMAL: OutColor.rgb  = abs(perturbedNormal);                                 break;\n"
"            case DEBUG_VIEW_NDOTV:            OutColor.rgb  = dot(perturbedNormal, view) * float3(1.0, 1.0, 1.0);   break;\n"
"            case DEBUG_VIEW_DIRECT_LIGHTING:  OutColor.rgb  = DirectLighting;                                       break;\n"
"#   if USE_IBL\n"
"            case DEBUG_VIEW_DIFFUSE_IBL:      OutColor.rgb  = IBLContrib.f3Diffuse;                                 break;\n"
"            case DEBUG_VIEW_SPECULAR_IBL:     OutColor.rgb  = IBLContrib.f3Specular;                                break;\n"
"#   endif\n"
"        }\n"
"    }\n"
"#   endif\n"
"\n"
"#   if CONVERT_OUTPUT_TO_SRGB\n"
"    {\n"
"        OutColor.rgb = FastLinearToSRGB(OutColor.rgb);\n"
"    }\n"
"#   endif\n"
"}\n"
