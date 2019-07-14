"#ifndef _SHADOWS_FXH_\n"
"#define _SHADOWS_FXH_\n"
"\n"
"// Must include BasicStructures.fxh\n"
"\n"
"void FindCascade(ShadowMapAttribs ShadowAttribs,\n"
"                 float3           f3PosInLightViewSpace,\n"
"                 float            fCameraViewSpaceZ,\n"
"                 out float3       f3PosInCascadeProjSpace,\n"
"                 out float3       f3CascadeLightSpaceScale,\n"
"                 out int          Cascade)\n"
"{\n"
"    f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);\n"
"    f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);\n"
"    Cascade = 0;\n"
"#if BEST_CASCADE_SEARCH\n"
"    while (Cascade < ShadowAttribs.iNumCascades)\n"
"    {\n"
"        // Find the smallest cascade which covers current point\n"
"        CascadeAttribs CascadeAttribs = ShadowAttribs.Cascades[Cascade];\n"
"        f3CascadeLightSpaceScale = CascadeAttribs.f4LightSpaceScale.xyz;\n"
"        f3PosInCascadeProjSpace = f3PosInLightViewSpace * f3CascadeLightSpaceScale + ShadowAttribs.Cascades[Cascade].f4LightSpaceScaledBias.xyz;\n"
"        \n"
"        // In order to perform PCF filtering without getting out of the cascade shadow map,\n"
"        // we need to be far enough from its boundaries.\n"
"        if( //Cascade == (ShadowAttribs.iNumCascades - 1) || \n"
"            abs(f3PosInCascadeProjSpace.x) < 1.0/*- CascadeAttribs.f4LightProjSpaceFilterRadius.x*/ &&\n"
"            abs(f3PosInCascadeProjSpace.y) < 1.0/*- CascadeAttribs.f4LightProjSpaceFilterRadius.y*/ &&\n"
"            // It is necessary to check f3PosInCascadeProjSpace.z as well since it could be behind\n"
"            // the far clipping plane of the current cascade\n"
"            // Besides, if VSM or EVSM filtering is performed, there is also z boundary\n"
"            NDC_MIN_Z /*+ CascadeAttribs.f4LightProjSpaceFilterRadius.z*/ < f3PosInCascadeProjSpace.z && f3PosInCascadeProjSpace.z < 1.0  /*- CascadeAttribs.f4LightProjSpaceFilterRadius.w*/ )\n"
"            break;\n"
"        else\n"
"            Cascade++;\n"
"    }\n"
"#else\n"
"    [unroll]\n"
"    for(int i=0; i< (ShadowAttribs.iNumCascades+3)/4; ++i)\n"
"    {\n"
"        float4 f4CascadeZEnd = ShadowAttribs.f4CascadeCamSpaceZEnd[i];\n"
"        float4 v = float4( f4CascadeZEnd.x < fCameraViewSpaceZ ? 1.0 : 0.0, \n"
"                           f4CascadeZEnd.y < fCameraViewSpaceZ ? 1.0 : 0.0,\n"
"                           f4CascadeZEnd.z < fCameraViewSpaceZ ? 1.0 : 0.0,\n"
"                           f4CascadeZEnd.w < fCameraViewSpaceZ ? 1.0 : 0.0);\n"
"	    //float4 v = float4(ShadowAttribs.f4CascadeCamSpaceZEnd[i] < fCameraViewSpaceZ);\n"
"	    Cascade += int(dot(float4(1.0,1.0,1.0,1.0), v));\n"
"    }\n"
"    if( Cascade < ShadowAttribs.iNumCascades )\n"
"    {\n"
"    //Cascade = min(Cascade, ShadowAttribs.iNumCascades - 1);\n"
"        f3CascadeLightSpaceScale = ShadowAttribs.Cascades[Cascade].f4LightSpaceScale.xyz;\n"
"        f3PosInCascadeProjSpace = f3PosInLightViewSpace * f3CascadeLightSpaceScale + ShadowAttribs.Cascades[Cascade].f4LightSpaceScaledBias.xyz;\n"
"    }\n"
"#endif\n"
"}\n"
"\n"
"float2 ComputeReceiverPlaneDepthBias(float3 ShadowUVDepthDX,\n"
"                                     float3 ShadowUVDepthDY)\n"
"{    \n"
"    // Compute (dDepth/dU, dDepth/dV):\n"
"    //  \n"
"    //  | dDepth/dU |    | dX/dU    dX/dV |T  | dDepth/dX |     | dU/dX    dU/dY |-1T | dDepth/dX |\n"
"    //                 =                                     =                                      =\n"
"    //  | dDepth/dV |    | dY/dU    dY/dV |   | dDepth/dY |     | dV/dX    dV/dY |    | dDepth/dY |\n"
"    //\n"
"    //  | A B |-1   | D  -B |                      | A B |-1T   | D  -C |                                   \n"
"    //            =           / det                           =           / det                    \n"
"    //  | C D |     |-C   A |                      | C D |      |-B   A |\n"
"    //\n"
"    //  | dDepth/dU |           | dV/dY   -dV/dX |  | dDepth/dX |\n"
"    //                 = 1/det                                       \n"
"    //  | dDepth/dV |           |-dU/dY    dU/dX |  | dDepth/dY |\n"
"\n"
"    float2 biasUV;\n"
"    //               dV/dY       V      dDepth/dX    D       dV/dX       V     dDepth/dY     D\n"
"    biasUV.x =   ShadowUVDepthDY.y * ShadowUVDepthDX.z - ShadowUVDepthDX.y * ShadowUVDepthDY.z;\n"
"    //               dU/dY       U      dDepth/dX    D       dU/dX       U     dDepth/dY     D\n"
"    biasUV.y = - ShadowUVDepthDY.x * ShadowUVDepthDX.z + ShadowUVDepthDX.x * ShadowUVDepthDY.z;\n"
"\n"
"    float Det = (ShadowUVDepthDX.x * ShadowUVDepthDY.y) - (ShadowUVDepthDX.y * ShadowUVDepthDY.x);\n"
"	biasUV /= sign(Det) * max( abs(Det), 1e-10 );\n"
"    //biasUV = abs(Det) > 1e-7 ? biasUV / abs(Det) : 0;// sign(Det) * max( abs(Det), 1e-10 );\n"
"    return biasUV;\n"
"}\n"
"\n"
"\n"
"float ComputeShadowAmount(ShadowMapAttribs          ShadowAttribs,\n"
"                          in Texture2DArray<float>  tex2DShadowMap,\n"
"                          in SamplerComparisonState tex2DShadowMap_sampler,\n"
"                          in float3                 f3PosInLightViewSpace,\n"
"                          in float                  fCameraSpaceZ,\n"
"                          out int                   Cascade)\n"
"{\n"
"    float3 f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);\n"
"    float3 f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);\n"
"    FindCascade(ShadowAttribs, f3PosInLightViewSpace.xyz, fCameraSpaceZ, f3PosInCascadeProjSpace, f3CascadeLightSpaceScale, Cascade);\n"
"    if( Cascade == ShadowAttribs.iNumCascades )\n"
"        return 1.0;\n"
"\n"
"    float3 f3ShadowMapUVDepth;\n"
"    f3ShadowMapUVDepth.xy = NormalizedDeviceXYToTexUV( f3PosInCascadeProjSpace.xy );\n"
"    f3ShadowMapUVDepth.z = NormalizedDeviceZToDepth( f3PosInCascadeProjSpace.z );\n"
"        \n"
"    float3 f3ddXShadowMapUVDepth = ddx(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;\n"
"    float3 f3ddYShadowMapUVDepth = ddy(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;\n"
"\n"
"    float2 f2DepthSlopeScaledBias = ComputeReceiverPlaneDepthBias(f3ddXShadowMapUVDepth, f3ddYShadowMapUVDepth);\n"
"    const float MaxSlope = 10.0;\n"
"    f2DepthSlopeScaledBias = clamp(f2DepthSlopeScaledBias, -float2(MaxSlope, MaxSlope), float2(MaxSlope, MaxSlope));\n"
"    uint SMWidth, SMHeight, Elems; \n"
"    tex2DShadowMap.GetDimensions(SMWidth, SMHeight, Elems);\n"
"    float2 ShadowMapDim = float2(SMWidth, SMHeight);\n"
"    f2DepthSlopeScaledBias /= ShadowMapDim.xy;\n"
"\n"
"    float fractionalSamplingError = dot( float2(1.0, 1.0), abs(f2DepthSlopeScaledBias.xy) );\n"
"    fractionalSamplingError = max(fractionalSamplingError, 1e-5);\n"
"    f3ShadowMapUVDepth.z -= fractionalSamplingError;\n"
"    \n"
"    float fLightAmount = tex2DShadowMap.SampleCmp(tex2DShadowMap_sampler, float3(f3ShadowMapUVDepth.xy, Cascade), f3ShadowMapUVDepth.z);\n"
"\n"
"#if SMOOTH_SHADOWS\n"
"        float2 Offsets[4];\n"
"        Offsets[0] = float2(-1.0, -1.0);\n"
"        Offsets[1] = float2(+1.0, -1.0);\n"
"        Offsets[2] = float2(-1.0, +1.0);\n"
"        Offsets[3] = float2(+1.0, +1.0);\n"
"        \n"
"        [unroll]\n"
"        for(int i=0; i<4; ++i)\n"
"        {\n"
"            float fDepthBias = dot(Offsets[i].xy, f2DepthSlopeScaledBias.xy);\n"
"            float2 f2Offset = Offsets[i].xy /  ShadowMapDim.xy;\n"
"            fLightAmount += tex2DShadowMap.SampleCmp( tex2DShadowMap_sampler, float3(f3ShadowMapUVDepth.xy + f2Offset.xy, Cascade), f3ShadowMapUVDepth.z + fDepthBias );\n"
"        }\n"
"        fLightAmount /= 5.0;\n"
"#endif\n"
"    return fLightAmount;\n"
"}\n"
"\n"
"\n"
"#endif //_SHADOWS_FXH_\n"
