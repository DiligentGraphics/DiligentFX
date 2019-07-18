#ifndef _SHADOWS_FXH_
#define _SHADOWS_FXH_

// Must include BasicStructures.fxh

#ifndef SHADOW_FILTER_SIZE
#   define SHADOW_FILTER_SIZE 2
#endif

#ifndef FILTER_ACROSS_CASCADES
#   define FILTER_ACROSS_CASCADES 0
#endif

float GetDistanceToCascadeMargin(float3 f3PosInCascadeProjSpace, float4 f4MarginProjSpace)
{
    float4 f4DistToEdges;
    f4DistToEdges.xy = float2(1.0, 1.0) - f4MarginProjSpace.xy - abs(f3PosInCascadeProjSpace.xy);
    const float ZScale = 2.0 / (1.0 - NDC_MIN_Z);
    f4DistToEdges.z = (f3PosInCascadeProjSpace.z - (NDC_MIN_Z + f4MarginProjSpace.z)) * ZScale;
    f4DistToEdges.w = (1.0 - f4MarginProjSpace.w - f3PosInCascadeProjSpace.z) * ZScale;
    return min(min(f4DistToEdges.x, f4DistToEdges.y), min(f4DistToEdges.z, f4DistToEdges.w));
}

void FindCascade(ShadowMapAttribs ShadowAttribs,
                 float3           f3PosInLightViewSpace,
                 float            fCameraViewSpaceZ,
                 out float3       f3PosInCascadeProjSpace,
                 out float3       f3CascadeLightSpaceScale,
                 out int          iCascadeIdx,
                 out float        fMinDistToEdge)
{
    f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);
    f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);
    iCascadeIdx = 0;
    fMinDistToEdge = 0.0;
#if BEST_CASCADE_SEARCH
    while (iCascadeIdx < ShadowAttribs.iNumCascades)
    {
        // Find the smallest cascade which covers current point
        CascadeAttribs Cascade = ShadowAttribs.Cascades[iCascadeIdx];
        f3CascadeLightSpaceScale = Cascade.f4LightSpaceScale.xyz;
        f3PosInCascadeProjSpace = f3PosInLightViewSpace * f3CascadeLightSpaceScale + ShadowAttribs.Cascades[iCascadeIdx].f4LightSpaceScaledBias.xyz;
        fMinDistToEdge = GetDistanceToCascadeMargin(f3PosInCascadeProjSpace, Cascade.f4MarginProjSpace);

        if(fMinDistToEdge > 0.0)
            break;
        else
            iCascadeIdx++;
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
	    iCascadeIdx += int(dot(float4(1.0,1.0,1.0,1.0), v));
    }
    if( iCascadeIdx < ShadowAttribs.iNumCascades )
    {
        //Cascade = min(Cascade, ShadowAttribs.iNumCascades - 1);
        CascadeAttribs Cascade = ShadowAttribs.Cascades[iCascadeIdx];
        f3CascadeLightSpaceScale = Cascade.f4LightSpaceScale.xyz;
        f3PosInCascadeProjSpace = f3PosInLightViewSpace * f3CascadeLightSpaceScale + Cascade.f4LightSpaceScaledBias.xyz;
        fMinDistToEdge = (Cascade.f4StartEndZ.y - fCameraViewSpaceZ) / (Cascade.f4StartEndZ.y - Cascade.f4StartEndZ.x);
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

//-------------------------------------------------------------------------------------------------
// The method used in The Witness
//-------------------------------------------------------------------------------------------------
float FilterShadowMapOptimizedPCF(in Texture2DArray<float>  tex2DShadowMap,
                                  in SamplerComparisonState tex2DShadowMap_sampler,
                                  in float4                 shadowMapSize,
                                  in float3                 shadowPos,
                                  in int                    cascadeIdx,
                                  in float2                 receiverPlaneDepthBias)
{
    float lightDepth = shadowPos.z;

    float2 uv = shadowPos.xy * shadowMapSize.xy;
    float2 base_uv = floor(uv + float2(0.5, 0.5));
    float s = (uv.x + 0.5 - base_uv.x);
    float t = (uv.y + 0.5 - base_uv.y);
    base_uv -= float2(0.5, 0.5);
    base_uv *= shadowMapSize.zw;

    float sum = 0;

    // It is essential to clamp biased depth to 0 to avoid shadow leaks at near cascade depth boundary.
    //        
    //            No clamping                 With clamping
    //                                      
    //              \ |                             ||    
    //       ==>     \|                             ||
    //                |                             ||         
    // Light ==>      |\                            |\         
    //                | \Receiver plane             | \ Receiver plane
    //       ==>      |  \                          |  \   
    //                0   ...   1                   0   ...   1
    //
    // Note that clamping at far depth boundary makes no difference as 1 < 1 produces 0 and so does 1+x < 1
    const float DepthClamp = 1e-8;
#define SAMPLE_SHADOW_MAP(u, v) tex2DShadowMap.SampleCmp(tex2DShadowMap_sampler, float3(base_uv.xy + float2(u,v) * shadowMapSize.zw, cascadeIdx), max(lightDepth + dot(float2(u, v), receiverPlaneDepthBias), DepthClamp))

    #if SHADOW_FILTER_SIZE == 2

        return tex2DShadowMap.SampleCmp(tex2DShadowMap_sampler, float3(shadowPos.xy, cascadeIdx), max(lightDepth, DepthClamp));

    #elif SHADOW_FILTER_SIZE == 3

        float uw0 = (3.0 - 2.0 * s);
        float uw1 = (1.0 + 2.0 * s);

        float u0 = (2.0 - s) / uw0 - 1.0;
        float u1 = s / uw1 + 1.0;

        float vw0 = (3.0 - 2.0 * t);
        float vw1 = (1.0 + 2.0 * t);

        float v0 = (2.0 - t) / vw0 - 1;
        float v1 = t / vw1 + 1;

        sum += uw0 * vw0 * SAMPLE_SHADOW_MAP(u0, v0);
        sum += uw1 * vw0 * SAMPLE_SHADOW_MAP(u1, v0);
        sum += uw0 * vw1 * SAMPLE_SHADOW_MAP(u0, v1);
        sum += uw1 * vw1 * SAMPLE_SHADOW_MAP(u1, v1);

        return sum * 1.0 / 16.0;

    #elif SHADOW_FILTER_SIZE == 5

        float uw0 = (4.0 - 3.0 * s);
        float uw1 = 7.0;
        float uw2 = (1.0 + 3.0 * s);

        float u0 = (3.0 - 2.0 * s) / uw0 - 2.0;
        float u1 = (3.0 + s) / uw1;
        float u2 = s / uw2 + 2.0;

        float vw0 = (4.0 - 3.0 * t);
        float vw1 = 7.0;
        float vw2 = (1.0 + 3.0 * t);

        float v0 = (3.0 - 2.0 * t) / vw0 - 2.0;
        float v1 = (3.0 + t) / vw1;
        float v2 = t / vw2 + 2;

        sum += uw0 * vw0 * SAMPLE_SHADOW_MAP(u0, v0);
        sum += uw1 * vw0 * SAMPLE_SHADOW_MAP(u1, v0);
        sum += uw2 * vw0 * SAMPLE_SHADOW_MAP(u2, v0);

        sum += uw0 * vw1 * SAMPLE_SHADOW_MAP(u0, v1);
        sum += uw1 * vw1 * SAMPLE_SHADOW_MAP(u1, v1);
        sum += uw2 * vw1 * SAMPLE_SHADOW_MAP(u2, v1);

        sum += uw0 * vw2 * SAMPLE_SHADOW_MAP(u0, v2);
        sum += uw1 * vw2 * SAMPLE_SHADOW_MAP(u1, v2);
        sum += uw2 * vw2 * SAMPLE_SHADOW_MAP(u2, v2);

        return sum * 1.0 / 144.0;

    #elif SHADOW_FILTER_SIZE == 7

        float uw0 = (5.0 * s - 6.0);
        float uw1 = (11.0 * s - 28.0);
        float uw2 = -(11.0 * s + 17.0);
        float uw3 = -(5.0 * s + 1.0);

        float u0 = (4.0 * s - 5.0) / uw0 - 3.0;
        float u1 = (4.0 * s - 16.0) / uw1 - 1.0;
        float u2 = -(7.0 * s + 5.0) / uw2 + 1.0;
        float u3 = -s / uw3 + 3.0;

        float vw0 = (5.0 * t - 6.0);
        float vw1 = (11.0 * t - 28.0);
        float vw2 = -(11.0 * t + 17.0);
        float vw3 = -(5.0 * t + 1.0);

        float v0 = (4.0 * t - 5.0) / vw0 - 3.0;
        float v1 = (4.0 * t - 16.0) / vw1 - 1.0;
        float v2 = -(7.0 * t + 5.0) / vw2 + 1.0;
        float v3 = -t / vw3 + 3.0;

        sum += uw0 * vw0 * SAMPLE_SHADOW_MAP(u0, v0);
        sum += uw1 * vw0 * SAMPLE_SHADOW_MAP(u1, v0);
        sum += uw2 * vw0 * SAMPLE_SHADOW_MAP(u2, v0);
        sum += uw3 * vw0 * SAMPLE_SHADOW_MAP(u3, v0);

        sum += uw0 * vw1 * SAMPLE_SHADOW_MAP(u0, v1);
        sum += uw1 * vw1 * SAMPLE_SHADOW_MAP(u1, v1);
        sum += uw2 * vw1 * SAMPLE_SHADOW_MAP(u2, v1);
        sum += uw3 * vw1 * SAMPLE_SHADOW_MAP(u3, v1);

        sum += uw0 * vw2 * SAMPLE_SHADOW_MAP(u0, v2);
        sum += uw1 * vw2 * SAMPLE_SHADOW_MAP(u1, v2);
        sum += uw2 * vw2 * SAMPLE_SHADOW_MAP(u2, v2);
        sum += uw3 * vw2 * SAMPLE_SHADOW_MAP(u3, v2);

        sum += uw0 * vw3 * SAMPLE_SHADOW_MAP(u0, v3);
        sum += uw1 * vw3 * SAMPLE_SHADOW_MAP(u1, v3);
        sum += uw2 * vw3 * SAMPLE_SHADOW_MAP(u2, v3);
        sum += uw3 * vw3 * SAMPLE_SHADOW_MAP(u3, v3);

        return sum * 1.0 / 2704.0;
    #else
        return 0.0;
    #endif
#undef SAMPLE_SHADOW_MAP
}


float FilterShadowCascade(in ShadowMapAttribs       ShadowAttribs,
                           in Texture2DArray<float>  tex2DShadowMap,
                           in SamplerComparisonState tex2DShadowMap_sampler,
                           in int                    iCascadeIdx,
                           in float3                 f3PosInLightViewSpace,
                           in float3                 f3CascadeLightSpaceScale,
                           in float3                 f3PosInCascadeProjSpace)
{
    float3 f3ShadowMapUVDepth;
    f3ShadowMapUVDepth.xy = NormalizedDeviceXYToTexUV( f3PosInCascadeProjSpace.xy );
    f3ShadowMapUVDepth.z = NormalizedDeviceZToDepth( f3PosInCascadeProjSpace.z );
        
    float3 f3ddXShadowMapUVDepth = ddx(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;
    float3 f3ddYShadowMapUVDepth = ddy(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;

    float2 f2DepthSlopeScaledBias = ComputeReceiverPlaneDepthBias(f3ddXShadowMapUVDepth, f3ddYShadowMapUVDepth);
    float2 f2SlopeScaledBiasClamp = float2(ShadowAttribs.fReceiverPlaneDepthBiasClamp, ShadowAttribs.fReceiverPlaneDepthBiasClamp);
    f2DepthSlopeScaledBias = clamp(f2DepthSlopeScaledBias, -f2SlopeScaledBiasClamp, f2SlopeScaledBiasClamp);
    f2DepthSlopeScaledBias *= ShadowAttribs.f4ShadowMapDim.zw;

    float FractionalSamplingError = dot( float2(1.0, 1.0), abs(f2DepthSlopeScaledBias.xy) );
    FractionalSamplingError = FractionalSamplingError + ShadowAttribs.fFixedDepthBias;
    f3ShadowMapUVDepth.z -= FractionalSamplingError;

    return FilterShadowMapOptimizedPCF(tex2DShadowMap, tex2DShadowMap_sampler, ShadowAttribs.f4ShadowMapDim, f3ShadowMapUVDepth, iCascadeIdx, f2DepthSlopeScaledBias);
}

float GetNextCascadeBlendAmount(ShadowMapAttribs ShadowAttribs,
                                float            fMinDistToMargin,
                                float3           f3PosInNextCascadeProjSpace,
                                float4           f4NextCascadeMarginProjSpace)
{
    float fMinDistToNextCascadeMargin = GetDistanceToCascadeMargin(f3PosInNextCascadeProjSpace, f4NextCascadeMarginProjSpace);
    return saturate(1.0 - fMinDistToMargin / ShadowAttribs.fCascadeTransitionRegion) * 
           saturate(fMinDistToNextCascadeMargin / ShadowAttribs.fCascadeTransitionRegion); // Make sure that we don't sample outside of the next cascade
}

float FilterShadowMap(in ShadowMapAttribs       ShadowAttribs,
                      in Texture2DArray<float>  tex2DShadowMap,
                      in SamplerComparisonState tex2DShadowMap_sampler,
                      in float3                 f3PosInLightViewSpace,
                      in float                  fCameraSpaceZ,
                      out int                   iCascadeIdx,
                      out float                 fNextCascadeBlendAmount)
{
    float3 f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);
    float3 f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);
    float  fMinDistToMargin = 0.0;
    FindCascade(ShadowAttribs, f3PosInLightViewSpace.xyz, fCameraSpaceZ, f3PosInCascadeProjSpace, f3CascadeLightSpaceScale, iCascadeIdx, fMinDistToMargin);
    if( iCascadeIdx == ShadowAttribs.iNumCascades )
        return 1.0;

    float ShadowAmount = FilterShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, iCascadeIdx, f3PosInLightViewSpace, f3CascadeLightSpaceScale, f3PosInCascadeProjSpace);
    fNextCascadeBlendAmount = 0.0;

#if FILTER_ACROSS_CASCADES
    if (iCascadeIdx+1 < ShadowAttribs.iNumCascades)
    {
        CascadeAttribs NextCascade = ShadowAttribs.Cascades[iCascadeIdx + 1];
        float3 f3PosInNextCascadeProjSpace = f3PosInLightViewSpace * NextCascade.f4LightSpaceScale.xyz + NextCascade.f4LightSpaceScaledBias.xyz;
        float NextCascadeShadow = FilterShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, iCascadeIdx+1, f3PosInLightViewSpace, NextCascade.f4LightSpaceScale.xyz, f3PosInNextCascadeProjSpace);
        fNextCascadeBlendAmount = GetNextCascadeBlendAmount(ShadowAttribs, fMinDistToMargin, f3PosInNextCascadeProjSpace, NextCascade.f4MarginProjSpace);
        ShadowAmount = lerp(ShadowAmount, NextCascadeShadow, fNextCascadeBlendAmount);
    }
#endif

    return ShadowAmount;
}




// Reduces VSM light bleedning
float ReduceLightBleeding(float pMax, float amount)
{
  // Remove the [0, amount] tail and linearly rescale (amount, 1].
   return saturate((pMax - amount) / (1.0 - amount));
}

float ChebyshevUpperBound(float2 f2Moments, float fMean, float fMinVariance, float fLightBleedingReduction)
{
    // Compute variance
    float Variance = f2Moments.y - (f2Moments.x * f2Moments.x);
    Variance = max(Variance, fMinVariance);

    // Compute probabilistic upper bound
    float d = fMean - f2Moments.x;
    float pMax = Variance / (Variance + (d * d));

    pMax = ReduceLightBleeding(pMax, fLightBleedingReduction);

    // One-tailed Chebyshev
    return (fMean <= f2Moments.x ? 1.0 : pMax);
}

//-------------------------------------------------------------------------------------------------
// Samples the VSM shadow map
//-------------------------------------------------------------------------------------------------
float SampleVSM(in ShadowMapAttribs       ShadowAttribs,
                in Texture2DArray<float4> tex2DVSM,
                in SamplerState           tex2DVSM_sampler,
                in int                    iCascadeIdx,
                in float3                 f3ShadowMapUVDepth,
                in float3                 f3ddXShadowMapUVDepth,
                in float3                 f3ddYShadowMapUVDepth)
{
    float2 f2Occluder = tex2DVSM.SampleGrad(tex2DVSM_sampler, float3(f3ShadowMapUVDepth.xy, iCascadeIdx), f3ddXShadowMapUVDepth.xy, f3ddYShadowMapUVDepth.xy).xy;
    return ChebyshevUpperBound(f2Occluder, f3ShadowMapUVDepth.z, ShadowAttribs.fVSMBias, ShadowAttribs.fVSMLightBleedingReduction);
}

float SampleFilterableShadowCascade(in ShadowMapAttribs       ShadowAttribs,
                                    in Texture2DArray<float4> tex2DShadowMap,
                                    in SamplerState           tex2DShadowMap_sampler,
                                    in int                    iCascadeIdx,
                                    in float3                 f3PosInLightViewSpace,
                                    in float3                 f3CascadeLightSpaceScale,
                                    in float3                 f3PosInCascadeProjSpace)
{
    float3 f3ShadowMapUVDepth;
    f3ShadowMapUVDepth.xy = NormalizedDeviceXYToTexUV( f3PosInCascadeProjSpace.xy );
    f3ShadowMapUVDepth.z = NormalizedDeviceZToDepth( f3PosInCascadeProjSpace.z );
        
    float3 f3ddXShadowMapUVDepth = ddx(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;
    float3 f3ddYShadowMapUVDepth = ddy(f3PosInLightViewSpace) * f3CascadeLightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;

    return SampleVSM(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, iCascadeIdx, f3ShadowMapUVDepth, 
                     f3ddXShadowMapUVDepth, f3ddYShadowMapUVDepth);
}


float SampleFilterableShadowMap(in ShadowMapAttribs       ShadowAttribs,
                                in Texture2DArray<float4> tex2DShadowMap,
                                in SamplerState           tex2DShadowMap_sampler,
                                in float3                 f3PosInLightViewSpace,
                                in float                  fCameraSpaceZ,
                                out int                   iCascadeIdx,
                                out float                 fNextCascadeBlendAmount)
{
    float3 f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);
    float3 f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);
    float  fMinDistToMargin = 0.0;
    FindCascade(ShadowAttribs, f3PosInLightViewSpace.xyz, fCameraSpaceZ, f3PosInCascadeProjSpace, f3CascadeLightSpaceScale, iCascadeIdx, fMinDistToMargin);
    if( iCascadeIdx == ShadowAttribs.iNumCascades )
        return 1.0;

    float ShadowAmount = SampleFilterableShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, iCascadeIdx, f3PosInLightViewSpace, f3CascadeLightSpaceScale, f3PosInCascadeProjSpace);
    fNextCascadeBlendAmount = 0.0;

#if FILTER_ACROSS_CASCADES
    if (iCascadeIdx+1 < ShadowAttribs.iNumCascades)
    {
        CascadeAttribs NextCascade = ShadowAttribs.Cascades[iCascadeIdx + 1];
        float3 f3PosInNextCascadeProjSpace = f3PosInLightViewSpace * NextCascade.f4LightSpaceScale.xyz + NextCascade.f4LightSpaceScaledBias.xyz;
        float NextCascadeShadow = SampleFilterableShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, iCascadeIdx+1, f3PosInLightViewSpace, NextCascade.f4LightSpaceScale.xyz, f3PosInNextCascadeProjSpace);
        float fMinDistToNextCascadeMargin = GetDistanceToCascadeMargin(f3PosInNextCascadeProjSpace, NextCascade.f4MarginProjSpace);
        fNextCascadeBlendAmount = GetNextCascadeBlendAmount(ShadowAttribs, fMinDistToMargin, f3PosInNextCascadeProjSpace, NextCascade.f4MarginProjSpace);
        ShadowAmount = lerp(ShadowAmount, NextCascadeShadow, fNextCascadeBlendAmount);
    }
#endif

    return ShadowAmount;
}




float3 GetCascadeColor(int Cascade, float fNextCascadeBlendAmount)
{
    float3 f3CascadeColors[MAX_CASCADES];
    f3CascadeColors[0] = float3(0,1,0);
    f3CascadeColors[1] = float3(0,0,1);
    f3CascadeColors[2] = float3(1,1,0);
    f3CascadeColors[3] = float3(0,1,1);
    f3CascadeColors[4] = float3(1,0,1);
    f3CascadeColors[5] = float3(0.3, 1, 0.7);
    f3CascadeColors[6] = float3(0.7, 0.3,1);
    f3CascadeColors[7] = float3(1, 0.7, 0.3);
    float3 Color = f3CascadeColors[min(Cascade, MAX_CASCADES-1)];
#if FILTER_ACROSS_CASCADES
    float3 NextCascadeColor = f3CascadeColors[min(Cascade+1, MAX_CASCADES-1)];
    Color = lerp(Color, NextCascadeColor, fNextCascadeBlendAmount);
#endif
    return Color;
}

#endif //_SHADOWS_FXH_
