#ifndef _SHADOWS_FXH_
#define _SHADOWS_FXH_

// Must include BasicStructures.fxh

#include "PCF.fxh"

#ifndef FILTER_ACROSS_CASCADES
#   define FILTER_ACROSS_CASCADES 0
#endif

#ifndef BEST_CASCADE_SEARCH
#   define BEST_CASCADE_SEARCH 0
#endif

// Returns the minimum distance to cascade margin.
// If the point lies outside, the distance will be negative.
//  +1  ____________________ 
//     |   ______________ __|__ +1-Margin.y
//     |  |              |  | 
//     |  |              |  |
//     |  |              |  |
//     |  |---*          |  |
//     |  |              |  |
//     |  |______________|__|__ -1+Margin.y
//  -1 |__|______________|__|
//    -1  |              | +1
//     -1+Margin.x    +1-Marign.x
//
float GetDistanceToCascadeMargin(float3 f3PosInCascadeProjSpace, float4 f4MarginProjSpace)
{
    float4 f4DistToEdges;
    f4DistToEdges.xy = float2(1.0, 1.0) - f4MarginProjSpace.xy - abs(f3PosInCascadeProjSpace.xy);
    const float ZScale = 2.0 / (1.0 - NDC_MIN_Z);
    f4DistToEdges.z = (f3PosInCascadeProjSpace.z - (NDC_MIN_Z + f4MarginProjSpace.z)) * ZScale;
    f4DistToEdges.w = (1.0 - f4MarginProjSpace.w - f3PosInCascadeProjSpace.z) * ZScale;
    return min(min(f4DistToEdges.x, f4DistToEdges.y), min(f4DistToEdges.z, f4DistToEdges.w));
}

struct CascadeSamplingInfo
{
    int    iCascadeIdx;
    float2 f2UV;
    float  fDepth;
    float3 f3LightSpaceScale;
    float  fMinDistToMargin;
};

CascadeSamplingInfo GetCascadeSamplingInfo(ShadowMapAttribs ShadowAttribs,
                                           float3           f3PosInLightViewSpace,
                                           int              iCascadeIdx)
{
    CascadeAttribs Cascade = ShadowAttribs.Cascades[iCascadeIdx];
    float3 f3CascadeLightSpaceScale = Cascade.f4LightSpaceScale.xyz;
    float3 f3PosInCascadeProjSpace  = f3PosInLightViewSpace * f3CascadeLightSpaceScale + Cascade.f4LightSpaceScaledBias.xyz;
    CascadeSamplingInfo SamplingInfo;
    SamplingInfo.iCascadeIdx       = iCascadeIdx;
    SamplingInfo.f2UV              = NormalizedDeviceXYToTexUV(f3PosInCascadeProjSpace.xy);
    SamplingInfo.fDepth            = NormalizedDeviceZToDepth(f3PosInCascadeProjSpace.z);
    SamplingInfo.f3LightSpaceScale = f3CascadeLightSpaceScale;
    SamplingInfo.fMinDistToMargin  = GetDistanceToCascadeMargin(f3PosInCascadeProjSpace, Cascade.f4MarginProjSpace);
    return SamplingInfo;
}

CascadeSamplingInfo FindCascade(ShadowMapAttribs ShadowAttribs,
                                float3           f3PosInLightViewSpace,
                                float            fCameraViewSpaceZ)
{
    CascadeSamplingInfo SamplingInfo;
    float3 f3PosInCascadeProjSpace  = float3(0.0, 0.0, 0.0);
    float3 f3CascadeLightSpaceScale = float3(0.0, 0.0, 0.0);
    int    iCascadeIdx = 0;
#if BEST_CASCADE_SEARCH
    while (iCascadeIdx < ShadowAttribs.iNumCascades)
    {
        // Find the smallest cascade which covers current point
        CascadeAttribs Cascade         = ShadowAttribs.Cascades[iCascadeIdx];
        SamplingInfo.f3LightSpaceScale = Cascade.f4LightSpaceScale.xyz;
        f3PosInCascadeProjSpace        = f3PosInLightViewSpace * SamplingInfo.f3LightSpaceScale + Cascade.f4LightSpaceScaledBias.xyz;
        SamplingInfo.fMinDistToMargin  = GetDistanceToCascadeMargin(f3PosInCascadeProjSpace, Cascade.f4MarginProjSpace);

        if (SamplingInfo.fMinDistToMargin > 0.0)
        {
            SamplingInfo.f2UV   = NormalizedDeviceXYToTexUV(f3PosInCascadeProjSpace.xy);
            SamplingInfo.fDepth = NormalizedDeviceZToDepth(f3PosInCascadeProjSpace.z);
            break;
        }
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
	    iCascadeIdx += int(dot(float4(1.0, 1.0, 1.0, 1.0), v));
    }

    if (iCascadeIdx < ShadowAttribs.iNumCascades)
    {
        //Cascade = min(Cascade, ShadowAttribs.iNumCascades - 1);
        SamplingInfo = GetCascadeSamplingInfo(ShadowAttribs, f3PosInLightViewSpace, iCascadeIdx);
    }
#endif
    SamplingInfo.iCascadeIdx = iCascadeIdx;
    return SamplingInfo;
}

float GetNextCascadeBlendAmount(ShadowMapAttribs    ShadowAttribs,
                                float               fCameraViewSpaceZ,
                                CascadeSamplingInfo SamplingInfo,
                                CascadeSamplingInfo NextCscdSamplingInfo)
{
    float4 f4CascadeStartEndZ = ShadowAttribs.Cascades[SamplingInfo.iCascadeIdx].f4StartEndZ;
    float fDistToTransitionEdge = (f4CascadeStartEndZ.y - fCameraViewSpaceZ) / (f4CascadeStartEndZ.y - f4CascadeStartEndZ.x);

#if BEST_CASCADE_SEARCH
    // Use the maximum of the camera Z - based distance and the minimal distance
    // to the cascade margin.
    // Using the maximum of the two avoids unnecessary transitions shown below.
    fDistToTransitionEdge = max(fDistToTransitionEdge, SamplingInfo.fMinDistToMargin);
    //        /  
    //       /
    //      /______________
    //     /|              |
    //    / |              |
    //   /  | ___          |
    //  /   ||   |         |
    //  \   ||___|         |
    //   \  |<-.           |
    //    \ |   \          |
    //     \|____\_________|
    //      \     \       
    //       \    This area is very close to the margin of cascade 1.
    //        \   If we only use fMinDistToMargin, it will be morphed with cascade 2,
    //            which will look very bad, especially near the boundary of cascade 0.  
#endif

    return saturate(1.0 - fDistToTransitionEdge / ShadowAttribs.fCascadeTransitionRegion) * 
           saturate(NextCscdSamplingInfo.fMinDistToMargin / 0.01); // Make sure that we don't sample outside of the next cascade
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



float FilterShadowCascade(in ShadowMapAttribs       ShadowAttribs,
                          in Texture2DArray<float>  tex2DShadowMap,
                          in SamplerComparisonState tex2DShadowMap_sampler,
                          in float3                 f3ddXPosInLightViewSpace,
                          in float3                 f3ddYPosInLightViewSpace,
                          in CascadeSamplingInfo    SamplingInfo)
{
    float3 f3ddXShadowMapUVDepth  = f3ddXPosInLightViewSpace * SamplingInfo.f3LightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;
    float3 f3ddYShadowMapUVDepth  = f3ddYPosInLightViewSpace * SamplingInfo.f3LightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;
    float2 f2DepthSlopeScaledBias = ComputeReceiverPlaneDepthBias(f3ddXShadowMapUVDepth, f3ddYShadowMapUVDepth);
    // Rescale slope-scaled depth bias clamp to make it uniform across all cascades
    float2 f2SlopeScaledBiasClamp = abs( (SamplingInfo.f3LightSpaceScale.z  * F3NDC_XYZ_TO_UVD_SCALE.z) / 
                                         (SamplingInfo.f3LightSpaceScale.xy * F3NDC_XYZ_TO_UVD_SCALE.xy) ) *
                                    ShadowAttribs.fReceiverPlaneDepthBiasClamp;
    f2DepthSlopeScaledBias = clamp(f2DepthSlopeScaledBias, -f2SlopeScaledBiasClamp, f2SlopeScaledBiasClamp);
    f2DepthSlopeScaledBias *= ShadowAttribs.f4ShadowMapDim.zw;

    float FractionalSamplingError = dot( float2(1.0, 1.0), abs(f2DepthSlopeScaledBias.xy) ) + ShadowAttribs.fFixedDepthBias;
    SamplingInfo.fDepth -= FractionalSamplingError;

#if defined(PCF_FILTER_SIZE) && PCF_FILTER_SIZE > 0
    return FilterShadowMapFixedPCF(tex2DShadowMap, tex2DShadowMap_sampler, ShadowAttribs.f4ShadowMapDim,
                                   SamplingInfo.f2UV, SamplingInfo.iCascadeIdx, SamplingInfo.fDepth,
                                   f2DepthSlopeScaledBias);
#else
    float2 f2FilterSize = abs(ShadowAttribs.fFilterWorldSize * SamplingInfo.f3LightSpaceScale.xy * F3NDC_XYZ_TO_UVD_SCALE.xy);
    return FilterShadowMapVaryingPCF(tex2DShadowMap, tex2DShadowMap_sampler, ShadowAttribs.f4ShadowMapDim,
                                     SamplingInfo.f2UV, SamplingInfo.iCascadeIdx, SamplingInfo.fDepth,
                                     f2DepthSlopeScaledBias, f2FilterSize);
#endif
}


struct FilteredShadow
{
    float fLightAmount;
    int   iCascadeIdx;
    float fNextCascadeBlendAmount;
};

FilteredShadow FilterShadowMap(in ShadowMapAttribs       ShadowAttribs,
                               in Texture2DArray<float>  tex2DShadowMap,
                               in SamplerComparisonState tex2DShadowMap_sampler,
                               in float3                 f3PosInLightViewSpace,
                               in float3                 f3ddXPosInLightViewSpace,
                               in float3                 f3ddYPosInLightViewSpace,
                               in float                  fCameraSpaceZ)
{
    CascadeSamplingInfo SamplingInfo = FindCascade(ShadowAttribs, f3PosInLightViewSpace.xyz, fCameraSpaceZ);
    FilteredShadow Shadow;
    Shadow.iCascadeIdx             = SamplingInfo.iCascadeIdx;
    Shadow.fNextCascadeBlendAmount = 0.0;
    Shadow.fLightAmount            = 1.0;

    if (SamplingInfo.iCascadeIdx == ShadowAttribs.iNumCascades)
        return Shadow;

    Shadow.fLightAmount = FilterShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, f3ddXPosInLightViewSpace, f3ddYPosInLightViewSpace, SamplingInfo);
    
#if FILTER_ACROSS_CASCADES
    if (SamplingInfo.iCascadeIdx+1 < ShadowAttribs.iNumCascades)
    {
        CascadeSamplingInfo NextCscdSamplingInfo = GetCascadeSamplingInfo(ShadowAttribs, f3PosInLightViewSpace, SamplingInfo.iCascadeIdx + 1);
        Shadow.fNextCascadeBlendAmount = GetNextCascadeBlendAmount(ShadowAttribs, fCameraSpaceZ, SamplingInfo, NextCscdSamplingInfo);
        float NextCascadeShadow = 1.0;
        if (Shadow.fNextCascadeBlendAmount > 0.0)
        {
            NextCascadeShadow = FilterShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, f3ddXPosInLightViewSpace, f3ddYPosInLightViewSpace, NextCscdSamplingInfo);
        }
        Shadow.fLightAmount = lerp(Shadow.fLightAmount, NextCascadeShadow, Shadow.fNextCascadeBlendAmount);
    }
#endif

    return Shadow;
}




// Reduces VSM light bleedning
float ReduceLightBleeding(float pMax, float amount)
{
    // Remove the [0, amount] tail and linearly rescale (amount, 1].
     return saturate((pMax - amount) / (1.0 - amount));
}

float ChebyshevUpperBound(float2 f2Moments, float fMean, float fMinVariance, float fLightBleedingReduction)
{
    float Variance = f2Moments.y - (f2Moments.x * f2Moments.x);
    Variance = max(Variance, fMinVariance);

    // Probabilistic upper bound
    float d = fMean - f2Moments.x;
    float pMax = Variance / (Variance + (d * d));

    pMax = ReduceLightBleeding(pMax, fLightBleedingReduction);

    // One-tailed Chebyshev
    return (fMean <= f2Moments.x ? 1.0 : pMax);
}

float2 GetEVSMExponents(float positiveExponent, float negativeExponent, bool Is32BitFormat)
{
    float maxExponent = Is32BitFormat ? 42.0 : 5.54;
    // Clamp to maximum range of fp32/fp16 to prevent overflow/underflow
    return min(float2(positiveExponent, negativeExponent), float2(maxExponent, maxExponent));
}

// Applies exponential warp to shadow map depth, input depth should be in [0, 1]
float2 WarpDepthEVSM(float depth, float2 exponents)
{
    // Rescale depth into [-1, 1]
    depth = 2.0 * depth - 1.0;
    float pos =  exp( exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
    return float2(pos, neg);
}

float SampleVSM(in ShadowMapAttribs       ShadowAttribs,
                in Texture2DArray<float4> tex2DVSM,
                in SamplerState           tex2DVSM_sampler,
                in CascadeSamplingInfo    SamplingInfo,
                in float2                 f2ddXShadowMapUV,
                in float2                 f2ddYShadowMapUV)
{
    float2 f2Occluder = tex2DVSM.SampleGrad(tex2DVSM_sampler, float3(SamplingInfo.f2UV, SamplingInfo.iCascadeIdx), f2ddXShadowMapUV, f2ddYShadowMapUV).xy;
    return ChebyshevUpperBound(f2Occluder, SamplingInfo.fDepth, ShadowAttribs.fVSMBias, ShadowAttribs.fVSMLightBleedingReduction);
}

float SampleEVSM(in ShadowMapAttribs       ShadowAttribs,
                 in Texture2DArray<float4> tex2DEVSM,
                 in SamplerState           tex2DEVSM_sampler,
                 in CascadeSamplingInfo    SamplingInfo,
                 in float2                 f2ddXShadowMapUV,
                 in float2                 f2ddYShadowMapUV)
{
    float2 f2Exponents = GetEVSMExponents(ShadowAttribs.fEVSMPositiveExponent, ShadowAttribs.fEVSMNegativeExponent, ShadowAttribs.bIs32BitEVSM);
    float2 f2WarpedDepth = WarpDepthEVSM(SamplingInfo.fDepth, f2Exponents);

    float4 f4Occluder = tex2DEVSM.SampleGrad(tex2DEVSM_sampler, float3(SamplingInfo.f2UV, SamplingInfo.iCascadeIdx), f2ddXShadowMapUV, f2ddYShadowMapUV);

    float2 f2DepthScale  = ShadowAttribs.fVSMBias * f2Exponents * f2WarpedDepth;
    float2 f2MinVariance = f2DepthScale * f2DepthScale;

    float fContrib = ChebyshevUpperBound(f4Occluder.xy, f2WarpedDepth.x, f2MinVariance.x, ShadowAttribs.fVSMLightBleedingReduction);
    #if SHADOW_MODE == SHADOW_MODE_EVSM4
        float fNegContrib = ChebyshevUpperBound(f4Occluder.zw, f2WarpedDepth.y, f2MinVariance.y, ShadowAttribs.fVSMLightBleedingReduction);
        fContrib = min(fContrib, fNegContrib);
    #endif

    return fContrib;
}

float SampleFilterableShadowCascade(in ShadowMapAttribs       ShadowAttribs,
                                    in Texture2DArray<float4> tex2DShadowMap,
                                    in SamplerState           tex2DShadowMap_sampler,
                                    in float3                 f3ddXPosInLightViewSpace,
                                    in float3                 f3ddYPosInLightViewSpace,
                                    in CascadeSamplingInfo    SamplingInfo)
{
    float3 f3ddXShadowMapUVDepth = f3ddXPosInLightViewSpace * SamplingInfo.f3LightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;
    float3 f3ddYShadowMapUVDepth = f3ddYPosInLightViewSpace * SamplingInfo.f3LightSpaceScale * F3NDC_XYZ_TO_UVD_SCALE;
#if SHADOW_MODE == SHADOW_MODE_VSM
    return SampleVSM(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, SamplingInfo, f3ddXShadowMapUVDepth.xy, f3ddYShadowMapUVDepth.xy);
#elif SHADOW_MODE == SHADOW_MODE_EVSM2 || SHADOW_MODE == SHADOW_MODE_EVSM4
    return SampleEVSM(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, SamplingInfo, f3ddXShadowMapUVDepth.xy, f3ddYShadowMapUVDepth.xy);
#else
    return 1.0;
#endif
}

FilteredShadow SampleFilterableShadowMap(in ShadowMapAttribs       ShadowAttribs,
                                         in Texture2DArray<float4> tex2DShadowMap,
                                         in SamplerState           tex2DShadowMap_sampler,
                                         in float3                 f3PosInLightViewSpace,
                                         in float3                 f3ddXPosInLightViewSpace,
                                         in float3                 f3ddYPosInLightViewSpace,
                                         in float                  fCameraSpaceZ)
{
    CascadeSamplingInfo SamplingInfo = FindCascade(ShadowAttribs, f3PosInLightViewSpace.xyz, fCameraSpaceZ);
    FilteredShadow Shadow;
    Shadow.iCascadeIdx             = SamplingInfo.iCascadeIdx;
    Shadow.fNextCascadeBlendAmount = 0.0;
    Shadow.fLightAmount            = 1.0;

    if (SamplingInfo.iCascadeIdx == ShadowAttribs.iNumCascades)
        return Shadow;

    Shadow.fLightAmount = SampleFilterableShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, f3ddXPosInLightViewSpace, f3ddYPosInLightViewSpace, SamplingInfo);

#if FILTER_ACROSS_CASCADES
    if (SamplingInfo.iCascadeIdx+1 < ShadowAttribs.iNumCascades)
    {
        CascadeSamplingInfo NextCscdSamplingInfo = GetCascadeSamplingInfo(ShadowAttribs, f3PosInLightViewSpace, SamplingInfo.iCascadeIdx + 1);
        Shadow.fNextCascadeBlendAmount = GetNextCascadeBlendAmount(ShadowAttribs, fCameraSpaceZ, SamplingInfo, NextCscdSamplingInfo);
        float NextCascadeShadow = 1.0;
        if (Shadow.fNextCascadeBlendAmount > 0.0)
        {
            NextCascadeShadow = SampleFilterableShadowCascade(ShadowAttribs, tex2DShadowMap, tex2DShadowMap_sampler, f3ddXPosInLightViewSpace, f3ddYPosInLightViewSpace, NextCscdSamplingInfo);
        }
        Shadow.fLightAmount = lerp(Shadow.fLightAmount, NextCascadeShadow, Shadow.fNextCascadeBlendAmount);
    }
#endif

    return Shadow;
}




float3 GetCascadeColor(FilteredShadow Shadow)
{
    float3 f3CascadeColors[MAX_CASCADES];
    f3CascadeColors[0] = float3(0,1,0);
    f3CascadeColors[1] = float3(0,0,1);
    f3CascadeColors[2] = float3(1,1,0);
    f3CascadeColors[3] = float3(0,1,1);
    f3CascadeColors[4] = float3(1,0,1);
    f3CascadeColors[5] = float3(0.3, 1, 0.7);
    f3CascadeColors[6] = float3(0.7, 0.3,1);
    f3CascadeColors[7] = float3(1.0, 0.7, 0.3);
    float3 Color = f3CascadeColors[min(Shadow.iCascadeIdx, MAX_CASCADES-1)];
#if FILTER_ACROSS_CASCADES
    float3 NextCascadeColor = f3CascadeColors[min(Shadow.iCascadeIdx+1, MAX_CASCADES-1)];
    Color = lerp(Color, NextCascadeColor, Shadow.fNextCascadeBlendAmount);
#endif
    return Color;
}

#endif //_SHADOWS_FXH_
