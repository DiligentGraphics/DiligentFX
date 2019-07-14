#ifndef _SHADOWS_FXH_
#define _SHADOWS_FXH_

// Must include BasicStructures.fxh

void FindCascade(ShadowMapAttribs ShadowAttribs,
                 float3           f3PosInLightViewSpace,
                 float            fCameraViewSpaceZ,
                 out float3       f3PosInCascadeProjSpace,
                 out float3       f3CascadeLightSpaceScale,
                 out int          Cascade)
{
    f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);
    f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);
    Cascade = 0;
#if BEST_CASCADE_SEARCH
    while (Cascade < ShadowAttribs.iNumCascades)
    {
        // Find the smallest cascade which covers current point
        CascadeAttribs CascadeAttribs = ShadowAttribs.Cascades[Cascade];
        f3CascadeLightSpaceScale = CascadeAttribs.f4LightSpaceScale.xyz;
        f3PosInCascadeProjSpace = f3PosInLightViewSpace * f3CascadeLightSpaceScale + ShadowAttribs.Cascades[Cascade].f4LightSpaceScaledBias.xyz;
        
        // In order to perform PCF filtering without getting out of the cascade shadow map,
        // we need to be far enough from its boundaries.
        if( //Cascade == (ShadowAttribs.iNumCascades - 1) || 
            abs(f3PosInCascadeProjSpace.x) < 1.0/*- CascadeAttribs.f4LightProjSpaceFilterRadius.x*/ &&
            abs(f3PosInCascadeProjSpace.y) < 1.0/*- CascadeAttribs.f4LightProjSpaceFilterRadius.y*/ &&
            // It is necessary to check f3PosInCascadeProjSpace.z as well since it could be behind
            // the far clipping plane of the current cascade
            // Besides, if VSM or EVSM filtering is performed, there is also z boundary
            NDC_MIN_Z /*+ CascadeAttribs.f4LightProjSpaceFilterRadius.z*/ < f3PosInCascadeProjSpace.z && f3PosInCascadeProjSpace.z < 1.0  /*- CascadeAttribs.f4LightProjSpaceFilterRadius.w*/ )
            break;
        else
            Cascade++;
    }
#else
    [unroll]
    for(int i=0; i< (ShadowAttribs.iNumCascades+3)/4; ++i)
    {
        float4 f4CascadeZEnd = ShadowAttribs.f4CascadeCamSpaceZEnd[i];
        float4 v = float4( f4CascadeZEnd.x < fCameraViewSpaceZ ? 1.0 : 0.0, 
                           f4CascadeZEnd.y < fCameraViewSpaceZ ? 1.0 : 0.0,
                           f4CascadeZEnd.z < fCameraViewSpaceZ ? 1.0 : 0.0,
                           f4CascadeZEnd.w < fCameraViewSpaceZ ? 1.0 : 0.0);
	    //float4 v = float4(ShadowAttribs.f4CascadeCamSpaceZEnd[i] < fCameraViewSpaceZ);
	    Cascade += int(dot(float4(1.0,1.0,1.0,1.0), v));
    }
    if( Cascade < ShadowAttribs.iNumCascades )
    {
    //Cascade = min(Cascade, ShadowAttribs.iNumCascades - 1);
        f3CascadeLightSpaceScale = ShadowAttribs.Cascades[Cascade].f4LightSpaceScale.xyz;
        f3PosInCascadeProjSpace = f3PosInLightViewSpace * f3CascadeLightSpaceScale + ShadowAttribs.Cascades[Cascade].f4LightSpaceScaledBias.xyz;
    }
#endif
}

float2 ComputeReceiverPlaneDepthBias(float3 ShadowUVDepthDX,
                                     float3 ShadowUVDepthDY)
{    
    // Compute (dDepth/dU, dDepth/dV):
    //  
    //  | dDepth/dU |    | dX/dU    dX/dV |T  | dDepth/dX |     | dU/dX    dU/dY |-1T | dDepth/dX |
    //                 =                                     =                                      =
    //  | dDepth/dV |    | dY/dU    dY/dV |   | dDepth/dY |     | dV/dX    dV/dY |    | dDepth/dY |
    //
    //  | A B |-1   | D  -B |                      | A B |-1T   | D  -C |                                   
    //            =           / det                           =           / det                    
    //  | C D |     |-C   A |                      | C D |      |-B   A |
    //
    //  | dDepth/dU |           | dV/dY   -dV/dX |  | dDepth/dX |
    //                 = 1/det                                       
    //  | dDepth/dV |           |-dU/dY    dU/dX |  | dDepth/dY |

    float2 biasUV;
    //               dV/dY       V      dDepth/dX    D       dV/dX       V     dDepth/dY     D
    biasUV.x =   ShadowUVDepthDY.y * ShadowUVDepthDX.z - ShadowUVDepthDX.y * ShadowUVDepthDY.z;
    //               dU/dY       U      dDepth/dX    D       dU/dX       U     dDepth/dY     D
    biasUV.y = - ShadowUVDepthDY.x * ShadowUVDepthDX.z + ShadowUVDepthDX.x * ShadowUVDepthDY.z;

    float Det = (ShadowUVDepthDX.x * ShadowUVDepthDY.y) - (ShadowUVDepthDX.y * ShadowUVDepthDY.x);
	biasUV /= sign(Det) * max( abs(Det), 1e-10 );
    //biasUV = abs(Det) > 1e-7 ? biasUV / abs(Det) : 0;// sign(Det) * max( abs(Det), 1e-10 );
    return biasUV;
}


float ComputeShadowAmount(ShadowMapAttribs          ShadowAttribs,
                          in Texture2DArray<float>  tex2DShadowMap,
                          in SamplerComparisonState tex2DShadowMap_sampler,
                          in float3                 f3PosInLightViewSpace,
                          in float                  fCameraSpaceZ,
                          out int                   Cascade)
{
    float3 f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);
    float3 f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);
    FindCascade(ShadowAttribs, f3PosInLightViewSpace.xyz, fCameraSpaceZ, f3PosInCascadeProjSpace, f3CascadeLightSpaceScale, Cascade);
    if( Cascade == ShadowAttribs.iNumCascades )
        return 1.0;

    float3 f3ShadowMapUVDepth;
    f3ShadowMapUVDepth.xy = NormalizedDeviceXYToTexUV( f3PosInCascadeProjSpace.xy );
    f3ShadowMapUVDepth.z = NormalizedDeviceZToDepth( f3PosInCascadeProjSpace.z );
        
    float3 f3ddXShadowMapUVDepth = ddx(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;
    float3 f3ddYShadowMapUVDepth = ddy(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;

    float2 f2DepthSlopeScaledBias = ComputeReceiverPlaneDepthBias(f3ddXShadowMapUVDepth, f3ddYShadowMapUVDepth);
    const float MaxSlope = 10.0;
    f2DepthSlopeScaledBias = clamp(f2DepthSlopeScaledBias, -float2(MaxSlope, MaxSlope), float2(MaxSlope, MaxSlope));
    uint SMWidth, SMHeight, Elems; 
    tex2DShadowMap.GetDimensions(SMWidth, SMHeight, Elems);
    float2 ShadowMapDim = float2(SMWidth, SMHeight);
    f2DepthSlopeScaledBias /= ShadowMapDim.xy;

    float fractionalSamplingError = dot( float2(1.0, 1.0), abs(f2DepthSlopeScaledBias.xy) );
    fractionalSamplingError = max(fractionalSamplingError, 1e-5);
    f3ShadowMapUVDepth.z -= fractionalSamplingError;
    
    float fLightAmount = tex2DShadowMap.SampleCmp(tex2DShadowMap_sampler, float3(f3ShadowMapUVDepth.xy, Cascade), f3ShadowMapUVDepth.z);

#if SMOOTH_SHADOWS
        float2 Offsets[4];
        Offsets[0] = float2(-1.0, -1.0);
        Offsets[1] = float2(+1.0, -1.0);
        Offsets[2] = float2(-1.0, +1.0);
        Offsets[3] = float2(+1.0, +1.0);
        
        [unroll]
        for(int i=0; i<4; ++i)
        {
            float fDepthBias = dot(Offsets[i].xy, f2DepthSlopeScaledBias.xy);
            float2 f2Offset = Offsets[i].xy /  ShadowMapDim.xy;
            fLightAmount += tex2DShadowMap.SampleCmp( tex2DShadowMap_sampler, float3(f3ShadowMapUVDepth.xy + f2Offset.xy, Cascade), f3ShadowMapUVDepth.z + fDepthBias );
        }
        fLightAmount /= 5.0;
#endif
    return fLightAmount;
}


#endif //_SHADOWS_FXH_
