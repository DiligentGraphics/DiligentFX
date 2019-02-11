//RayMarch.fx
//Performs ray marching and computes shadowed scattering

#include "AtmosphereShadersCommon.fxh"

cbuffer cbParticipatingMediaScatteringParams
{
    AirScatteringAttribs g_MediaParams;
}

cbuffer cbCameraAttribs
{
    CameraAttribs g_CameraAttribs;
}

cbuffer cbLightParams
{
    LightAttribs g_LightAttribs;
}

cbuffer cbPostProcessingAttribs
{
    PostProcessingAttribs g_PPAttribs;
};

cbuffer cbMiscDynamicParams
{
    MiscDynamicParams g_MiscParams;
}

Texture2D<float2> g_tex2DOccludedNetDensityToAtmTop;
SamplerState g_tex2DOccludedNetDensityToAtmTop_sampler;

Texture2D<float>  g_tex2DEpipolarCamSpaceZ;

Texture2D<float4> g_tex2DSliceUVDirAndOrigin;

Texture2D<float2> g_tex2DMinMaxLightSpaceDepth;

Texture2DArray<float> g_tex2DLightSpaceDepthMap;
SamplerComparisonState g_tex2DLightSpaceDepthMap_sampler;

Texture2D<float2> g_tex2DCoordinates;

Texture2D<float>  g_tex2DCamSpaceZ;
SamplerState g_tex2DCamSpaceZ_sampler;

Texture2D<float4> g_tex2DColorBuffer;

Texture2D<float>  g_tex2DAverageLuminance;

Texture3D<float3> g_tex3DSingleSctrLUT;
SamplerState g_tex3DSingleSctrLUT_sampler;

Texture3D<float3> g_tex3DHighOrderSctrLUT;
SamplerState g_tex3DHighOrderSctrLUT_sampler;

Texture3D<float3> g_tex3DMultipleSctrLUT;
SamplerState g_tex3DMultipleSctrLUT_sampler;


#include "LookUpTables.fxh"
#include "ScatteringIntegrals.fxh"
#include "UnshadowedScattering.fxh"
#include "ToneMapping.fxh"

// This function calculates inscattered light integral over the ray from the camera to 
// the specified world space position using ray marching
float3 ComputeShadowedInscattering( in float2 f2RayMarchingSampleLocation,
                                    in float fRayEndCamSpaceZ,
                                    in float fCascadeInd,
                                    uint uiEpipolarSliceInd )
{   
    float3 f3CameraPos = g_CameraAttribs.f4CameraPos.xyz;
    uint uiCascadeInd = uint(fCascadeInd);
    
    // Compute the ray termination point, full ray length and view direction
    float3 f3RayTermination = ProjSpaceXYZToWorldSpace( float3(f2RayMarchingSampleLocation, fRayEndCamSpaceZ), g_CameraAttribs.mProj, g_CameraAttribs.mViewProjInv );
    float3 f3FullRay = f3RayTermination - f3CameraPos;
    float fFullRayLength = length(f3FullRay);
    float3 f3ViewDir = f3FullRay / fFullRayLength;

    const float3 f3EarthCentre = float3(0.0, -EARTH_RADIUS, 0.0);

    // Intersect the ray with the top of the atmosphere and the Earth:
    float4 f4Isecs;
    GetRaySphereIntersection2(f3CameraPos, f3ViewDir, f3EarthCentre, 
                              float2(ATM_TOP_RADIUS, EARTH_RADIUS), f4Isecs);
    float2 f2RayAtmTopIsecs = f4Isecs.xy; 
    float2 f2RayEarthIsecs  = f4Isecs.zw;
    
    if( f2RayAtmTopIsecs.y <= 0.0 )
    {
        //                                                          view dir
        //                                                        /
        //             d<0                                       /
        //               *--------->                            *
        //            .      .                             .   /  . 
        //  .  '                    '  .         .  '         /\         '  .
        //                                                   /  f2rayatmtopisecs.y < 0
        //
        // the camera is outside the atmosphere and the ray either does not intersect the
        // top of it or the intersection point is behind the camera. In either
        // case there is no inscattering
        return float3(0.0, 0.0, 0.0);
    }

    // Restrict the camera position to the top of the atmosphere
    float fDistToAtmosphere = max(f2RayAtmTopIsecs.x, 0.0);
    float3 f3RestrainedCameraPos = f3CameraPos + fDistToAtmosphere * f3ViewDir;

    // Limit the ray length by the distance to the top of the atmosphere if the ray does not hit terrain
    float fOrigRayLength = fFullRayLength;
    if( fRayEndCamSpaceZ > g_CameraAttribs.fFarPlaneZ ) // fFarPlaneZ is pre-multiplied with 0.999999f
        fFullRayLength = +FLT_MAX;
    // Limit the ray length by the distance to the point where the ray exits the atmosphere
    fFullRayLength = min(fFullRayLength, f2RayAtmTopIsecs.y);

    // If there is an intersection with the Earth surface, limit the tracing distance to the intersection
    if( f2RayEarthIsecs.x > 0.0 )
    {
        fFullRayLength = min(fFullRayLength, f2RayEarthIsecs.x);
    }

    fRayEndCamSpaceZ *= fFullRayLength / fOrigRayLength; 
    
    float3 f3RayleighInscattering = float3(0.0, 0.0, 0.0);
    float3 f3MieInscattering = float3(0.0, 0.0, 0.0);
    float2 f2ParticleNetDensityFromCam = float2(0.0, 0.0);
    float3 f3RayEnd = float3(0.0, 0.0, 0.0), f3RayStart = float3(0.0, 0.0, 0.0);
    
    // Note that cosTheta = dot(DirOnCamera, LightDir) = dot(ViewDir, DirOnLight) because
    // DirOnCamera = -ViewDir and LightDir = -DirOnLight
    float cosTheta = dot(f3ViewDir, g_LightAttribs.f4DirOnLight.xyz);
    
    float fCascadeEndCamSpaceZ = 0.0;
    float fTotalLitLength = 0.0, fTotalMarchedLength = 0.0; // Required for multiple scattering
    float fDistToFirstLitSection = -1.0; // Used only in when SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT

#if CASCADE_PROCESSING_MODE == CASCADE_PROCESSING_MODE_SINGLE_PASS
    for(; uiCascadeInd < uint(g_PPAttribs.m_iNumCascades); ++uiCascadeInd, ++fCascadeInd)
#else
    for(int i=0; i<1; ++i)
#endif
    {
        float2 f2CascadeStartEndCamSpaceZ = g_LightAttribs.ShadowAttribs.Cascades[uiCascadeInd].f4StartEndZ.xy;
        float fCascadeStartCamSpaceZ = f2CascadeStartEndCamSpaceZ.x;//(uiCascadeInd > (uint)g_PPAttribs.m_iFirstCascade) ? f2CascadeStartEndCamSpaceZ.x : 0;
        fCascadeEndCamSpaceZ = f2CascadeStartEndCamSpaceZ.y;
        
        // Check if the ray terminates before it enters current cascade 
        if( fRayEndCamSpaceZ < fCascadeStartCamSpaceZ )
        {
            #if CASCADE_PROCESSING_MODE == CASCADE_PROCESSING_MODE_SINGLE_PASS
                break;
            #else
                return float3(0.0, 0.0, 0.0);
            #endif
        }

        // Truncate the ray against the far and near planes of the current cascade:
        float fRayEndRatio = min( fRayEndCamSpaceZ, fCascadeEndCamSpaceZ ) / fRayEndCamSpaceZ;
        float fRayStartRatio = fCascadeStartCamSpaceZ / fRayEndCamSpaceZ;
        float fDistToRayStart = fFullRayLength * fRayStartRatio;
        float fDistToRayEnd   = fFullRayLength * fRayEndRatio;

        // If the camera is outside the atmosphere and the ray intersects the top of it,
        // we must start integration from the first intersection point.
        // If the camera is in the atmosphere, first intersection point is always behind the camera 
        // and thus is negative
        //                               
        //                      
        //                     
        //                   *                                              /
        //              .   /  .                                       .   /  . 
        //    .  '         /\         '  .                   .  '         /\         '  .
        //                /  f2RayAtmTopIsecs.x > 0                      /  f2RayAtmTopIsecs.y > 0
        //                                                              *
        //                 f2RayAtmTopIsecs.y > 0                         f2RayAtmTopIsecs.x < 0
        //                /                                              /
        //
        fDistToRayStart = max(fDistToRayStart, f2RayAtmTopIsecs.x);
        fDistToRayEnd   = max(fDistToRayEnd,   f2RayAtmTopIsecs.x);
        
        // To properly compute scattering from the space, we must 
        // set up ray end position before extiting the loop
        f3RayEnd   = f3CameraPos + f3ViewDir * fDistToRayEnd;
        f3RayStart = f3CameraPos + f3ViewDir * fDistToRayStart;

        #if CASCADE_PROCESSING_MODE != CASCADE_PROCESSING_MODE_SINGLE_PASS
            float r = length(f3RestrainedCameraPos - f3EarthCentre);
            float fCosZenithAngle = dot(f3RestrainedCameraPos-f3EarthCentre, f3ViewDir) / r;
            float fDist = max(fDistToRayStart - fDistToAtmosphere, 0.0);
            f2ParticleNetDensityFromCam = GetDensityIntegralAnalytic(r, fCosZenithAngle, fDist);
        #endif

        float fRayLength = fDistToRayEnd - fDistToRayStart;
        if( fRayLength <= 10.0 )
        {
            #if CASCADE_PROCESSING_MODE == CASCADE_PROCESSING_MODE_SINGLE_PASS
                continue;
            #else
                if( int(uiCascadeInd) == g_PPAttribs.m_iNumCascades-1 )
                    // We need to process remaining part of the ray
                    break;
                else
                    return float3(0.0, 0.0, 0.0);
            #endif
        }

        // We trace the ray in the light projection space, not in the world space
        // Compute shadow map UV coordinates of the ray end point and its depth in the light space
        matrix mWorldToShadowMapUVDepth = g_LightAttribs.ShadowAttribs.mWorldToShadowMapUVDepth[uiCascadeInd];
        float3 f3StartUVAndDepthInLightSpace = WorldSpaceToShadowMapUV(f3RayStart, mWorldToShadowMapUVDepth);
        //f3StartUVAndDepthInLightSpace.z -= SHADOW_MAP_DEPTH_BIAS;
        float3 f3EndUVAndDepthInLightSpace = WorldSpaceToShadowMapUV(f3RayEnd, mWorldToShadowMapUVDepth);
        //f3EndUVAndDepthInLightSpace.z -= SHADOW_MAP_DEPTH_BIAS;

        // Calculate normalized trace direction in the light projection space and its length
        float3 f3ShadowMapTraceDir = f3EndUVAndDepthInLightSpace.xyz - f3StartUVAndDepthInLightSpace.xyz;
        // If the ray is directed exactly at the light source, trace length will be zero
        // Clamp to a very small positive value to avoid division by zero
        float fTraceLenInShadowMapUVSpace = max( length( f3ShadowMapTraceDir.xy ), 1e-7 );
        // Note that f3ShadowMapTraceDir.xy can be exactly zero
        f3ShadowMapTraceDir /= fTraceLenInShadowMapUVSpace;
    
        float fShadowMapUVStepLen = 0.0;
        float2 f2SliceOriginUV = float2(0.0, 0.0);
        float2 f2SliceDirUV = float2(0.0, 0.0);
        uint uiMinMaxTexYInd = 0u;

        #if USE_1D_MIN_MAX_TREE
        {
            // Get UV direction for this slice
            float4 f4SliceUVDirAndOrigin = g_tex2DSliceUVDirAndOrigin.Load( int3(uiEpipolarSliceInd,uiCascadeInd,0) );
            f2SliceDirUV = f4SliceUVDirAndOrigin.xy;
            //if( all(f4SliceUVDirAndOrigin == g_f4IncorrectSliceUVDirAndStart) )
            //{
            //    return float3(0,0,0);
            //}
            //return float3(f4SliceUVDirAndOrigin.xy,0);
            // Scale with the shadow map texel size
            fShadowMapUVStepLen = length(f2SliceDirUV);
            f2SliceOriginUV = f4SliceUVDirAndOrigin.zw;
         
            #if USE_COMBINED_MIN_MAX_TEXTURE
                uiMinMaxTexYInd = uiEpipolarSliceInd + (uiCascadeInd - uint(g_PPAttribs.m_iFirstCascade)) * g_PPAttribs.m_uiNumEpipolarSlices;
            #else
                uiMinMaxTexYInd = uiEpipolarSliceInd;
            #endif
        }
        #else
        {
            //Calculate length of the trace step in light projection space
            float fMaxTraceDirDim = max( abs(f3ShadowMapTraceDir.x), abs(f3ShadowMapTraceDir.y) );
            fShadowMapUVStepLen = (fMaxTraceDirDim > 0.0) ? (g_PPAttribs.m_f2ShadowMapTexelSize.x / fMaxTraceDirDim) : 0.0;
            // Take into account maximum number of steps specified by the g_MiscParams.fMaxStepsAlongRay
            fShadowMapUVStepLen = max(fTraceLenInShadowMapUVSpace/g_MiscParams.fMaxStepsAlongRay, fShadowMapUVStepLen);
        }
        #endif

        // Calcualte ray step length in world space
        float fRayStepLengthWS = fRayLength * (fShadowMapUVStepLen / fTraceLenInShadowMapUVSpace);
        // Note that fTraceLenInShadowMapUVSpace can be very small when looking directly at sun
        // Since fShadowMapUVStepLen is at least one shadow map texel in size, 
        // fShadowMapUVStepLen / fTraceLenInShadowMapUVSpace >> 1 in this case and as a result
        // fRayStepLengthWS >> fRayLength

        // March the ray
        float fDistanceMarchedInCascade = 0.0;
        float3 f3CurrShadowMapUVAndDepthInLightSpace = f3StartUVAndDepthInLightSpace.xyz;

        // The following variables are used only if 1D min map optimization is enabled
        uint uiMinLevel = 0u;
        // It is essential to round initial sample pos to the closest integer
        uint uiCurrSamplePos = uint( length(f3StartUVAndDepthInLightSpace.xy - f2SliceOriginUV.xy)/fShadowMapUVStepLen + 0.5 );
        uint uiCurrTreeLevel = 0u;
        // Note that min/max shadow map does not contain finest resolution level
        // The first level it contains corresponds to step == 2
        int iLevelDataOffset = -int(g_PPAttribs.m_uiMinMaxShadowMapResolution);
        float fStepScale = 1.0;
        float fMaxStepScale = g_PPAttribs.m_fMaxShadowMapStep;

        #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION
        {
            // In order for the numerical integration to be accurate enough, it is necessary to make 
            // at least 10 steps along the ray. To assure this, limit the maximum world step by 
            // 1/10 of the ray length.
            // To avoid aliasing artifacts due to unstable sampling along the view ray, do this for
            // each cascade separately
            float fMaxAllowedWorldStepLen = fRayLength/10.0;
            fMaxStepScale = min(fMaxStepScale, fMaxAllowedWorldStepLen/fRayStepLengthWS);
        
            // Make sure that the world step length is not greater than the maximum allowable length
            if( fRayStepLengthWS > fMaxAllowedWorldStepLen )
            {
                fRayStepLengthWS = fMaxAllowedWorldStepLen;
                // Recalculate shadow map UV step len
                fShadowMapUVStepLen = fTraceLenInShadowMapUVSpace * fRayStepLengthWS / fRayLength;
                // Disable 1D min/max optimization. Note that fMaxStepScale < 1 anyway since 
                // fRayStepLengthWS > fMaxAllowedWorldStepLen. Thus there is no real need to
                // make the max shadow map step negative. We do this just for clarity
                fMaxStepScale = -1.0;
            }
        }
        #endif

        // Scale trace direction in light projection space to calculate the step in shadow map
        float3 f3ShadowMapUVAndDepthStep = f3ShadowMapTraceDir * fShadowMapUVStepLen;
        
        [loop]
        while( fDistanceMarchedInCascade < fRayLength )
        {
            // Clamp depth to a very small positive value to avoid z-fighting at camera location
            float fCurrDepthInLightSpace = max(f3CurrShadowMapUVAndDepthInLightSpace.z, 1e-7);
            float IsInLight = 0.0;

            #if USE_1D_MIN_MAX_TREE
            {
                // If the step scale can be doubled without exceeding the maximum allowed scale and 
                // the sample is located at the appropriate position, advance to the next coarser level
                if( 2.0*fStepScale < fMaxStepScale && ((uiCurrSamplePos & ((2u<<uiCurrTreeLevel)-1u)) == 0u) )
                {
                    iLevelDataOffset += int(g_PPAttribs.m_uiMinMaxShadowMapResolution >> uiCurrTreeLevel);
                    uiCurrTreeLevel++;
                    fStepScale *= 2.f;
                }

                while(uiCurrTreeLevel > uiMinLevel)
                {
                    // Compute light space depths at the ends of the current ray section

                    // What we need here is actually depth which is divided by the camera view space z
                    // Thus depth can be correctly interpolated in screen space:
                    // http://www.comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf
                    // A subtle moment here is that we need to be sure that we can skip fStepScale samples 
                    // starting from 0 up to fStepScale-1. We do not need to do any checks against the sample fStepScale away:
                    //
                    //     --------------->
                    //
                    //          *
                    //               *         *
                    //     *              *     
                    //     0    1    2    3
                    //
                    //     |------------------>|
                    //        fStepScale = 4
                    float fNextLightSpaceDepth = f3CurrShadowMapUVAndDepthInLightSpace.z + f3ShadowMapUVAndDepthStep.z * (fStepScale-1.0);
                    float2 f2StartEndDepthOnRaySection = float2(f3CurrShadowMapUVAndDepthInLightSpace.z, fNextLightSpaceDepth);
                    f2StartEndDepthOnRaySection = f2StartEndDepthOnRaySection;//max(f2StartEndDepthOnRaySection, 1e-7);

                    // Load 1D min/max depths
                    float2 f2CurrMinMaxDepth = g_tex2DMinMaxLightSpaceDepth.Load( int3( int(uiCurrSamplePos>>uiCurrTreeLevel) + iLevelDataOffset, uiMinMaxTexYInd, 0) );
                
                    IsInLight = ( f2StartEndDepthOnRaySection.x < f2CurrMinMaxDepth.x && 
                                  f2StartEndDepthOnRaySection.y < f2CurrMinMaxDepth.x ) ? 1.f : 0.f;
                    bool bIsInShadow = f2StartEndDepthOnRaySection.x >= f2CurrMinMaxDepth.y && 
                                       f2StartEndDepthOnRaySection.y >= f2CurrMinMaxDepth.y;

                    if( IsInLight != 0.0 || bIsInShadow )
                        // If the ray section is fully lit or shadowed, we can break the loop
                        break;
                    // If the ray section is neither fully lit, nor shadowed, we have to go to the finer level
                    uiCurrTreeLevel--;
                    iLevelDataOffset -= int(g_PPAttribs.m_uiMinMaxShadowMapResolution >> uiCurrTreeLevel);
                    fStepScale /= 2.f;
                };

                // If we are at the finest level, sample the shadow map with PCF
                [branch]
                if( uiCurrTreeLevel <= uiMinLevel )
                {
                    #ifdef GLSL
                        // There is no OpenGL counterpart for Texture2DArray.SampleCmpLevelZero()
                        IsInLight = g_tex2DLightSpaceDepthMap.SampleCmp( g_tex2DLightSpaceDepthMap_sampler, float3(f3CurrShadowMapUVAndDepthInLightSpace.xy,fCascadeInd), fCurrDepthInLightSpace  );
                    #else
                        // We cannot use SampleCmp() under flow control in HLSL
                        IsInLight = g_tex2DLightSpaceDepthMap.SampleCmpLevelZero( g_tex2DLightSpaceDepthMap_sampler, float3(f3CurrShadowMapUVAndDepthInLightSpace.xy,fCascadeInd), fCurrDepthInLightSpace  );
                    #endif
                }
            }
            #else
            {
                #ifdef GLSL
                    // There is no OpenGL counterpart for Texture2DArray.SampleCmpLevelZero()
                    IsInLight = g_tex2DLightSpaceDepthMap.SampleCmp( g_tex2DLightSpaceDepthMap_sampler, float3( f3CurrShadowMapUVAndDepthInLightSpace.xy, fCascadeInd ), fCurrDepthInLightSpace );
                #else
                    // We cannot use SampleCmp() under flow control in HLSL
                    IsInLight = g_tex2DLightSpaceDepthMap.SampleCmpLevelZero( g_tex2DLightSpaceDepthMap_sampler, float3( f3CurrShadowMapUVAndDepthInLightSpace.xy, fCascadeInd ), fCurrDepthInLightSpace );
                #endif
            }
            #endif

            float fRemainingDist = max(fRayLength - fDistanceMarchedInCascade, 0.0);
            float fIntegrationStep = min(fRayStepLengthWS * fStepScale, fRemainingDist);
            float fIntegrationDist = fDistanceMarchedInCascade + fIntegrationStep/2.0;

            #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION
            {
                float3 f3CurrPos = f3RayStart + f3ViewDir * fIntegrationDist;

                // Calculate integration point height above the SPHERICAL Earth surface:
                float3 f3EarthCentreToPointDir = f3CurrPos - f3EarthCentre;
                float fDistToEarthCentre = length(f3EarthCentreToPointDir);
                f3EarthCentreToPointDir /= fDistToEarthCentre;
                float fHeightAboveSurface = fDistToEarthCentre - EARTH_RADIUS;

                float2 f2ParticleDensity = exp( -fHeightAboveSurface / PARTICLE_SCALE_HEIGHT );

                // Do not use this branch as it only degrades performance
                //if( IsInLight == 0)
                //    continue;

                // Get net particle density from the integration point to the top of the atmosphere:
                float fCosSunZenithAngle = dot( f3EarthCentreToPointDir, g_LightAttribs.f4DirOnLight.xyz );
                float2 f2NetParticleDensityToAtmTop = GetNetParticleDensity(fHeightAboveSurface, fCosSunZenithAngle);
        
                // Compute total particle density from the top of the atmosphere through the integraion point to camera
                float2 f2TotalParticleDensity = f2ParticleNetDensityFromCam + f2NetParticleDensityToAtmTop;
        
                // Update net particle density from the camera to the integration point:
                f2ParticleNetDensityFromCam += f2ParticleDensity * fIntegrationStep;

                // Get optical depth
                float3 f3TotalRlghOpticalDepth = g_MediaParams.f4RayleighExtinctionCoeff.rgb * f2TotalParticleDensity.x;
                float3 f3TotalMieOpticalDepth  = g_MediaParams.f4MieExtinctionCoeff.rgb      * f2TotalParticleDensity.y;
        
                // And total extinction for the current integration point:
                float3 f3TotalExtinction = exp( -(f3TotalRlghOpticalDepth + f3TotalMieOpticalDepth) );

                f2ParticleDensity *= fIntegrationStep * IsInLight;
                f3RayleighInscattering += f2ParticleDensity.x * f3TotalExtinction;
                f3MieInscattering      += f2ParticleDensity.y * f3TotalExtinction;
            }
            #endif

            #if MULTIPLE_SCATTERING_MODE == MULTIPLE_SCTR_MODE_OCCLUDED || SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT
                // Store the distance where the ray first enters the light
                fDistToFirstLitSection = (fDistToFirstLitSection < 0.0 && IsInLight > 0.0) ? fTotalMarchedLength : fDistToFirstLitSection;
            #endif
            f3CurrShadowMapUVAndDepthInLightSpace += f3ShadowMapUVAndDepthStep * fStepScale;
            uiCurrSamplePos += 1u << uiCurrTreeLevel; // int -> float conversions are slow
            fDistanceMarchedInCascade += fRayStepLengthWS * fStepScale;

            #if MULTIPLE_SCATTERING_MODE == MULTIPLE_SCTR_MODE_OCCLUDED || SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT
                fTotalLitLength += fIntegrationStep * IsInLight;
                fTotalMarchedLength += fIntegrationStep;
            #endif
        }
    }

    #if MULTIPLE_SCATTERING_MODE == MULTIPLE_SCTR_MODE_OCCLUDED || SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT
        // If the whole ray is in shadow, set the distance to the first lit section to the
        // total marched distance
        if( fDistToFirstLitSection < 0.0 )
            fDistToFirstLitSection = fTotalMarchedLength;
    #endif

    float3 f3RemainingRayStart = float3(0.0, 0.0, 0.0);
    float fRemainingLength = 0.0;
    if( 
#if CASCADE_PROCESSING_MODE != CASCADE_PROCESSING_MODE_SINGLE_PASS
        int(uiCascadeInd) == g_PPAttribs.m_iNumCascades-1 && 
#endif
        fRayEndCamSpaceZ > fCascadeEndCamSpaceZ 
       )
    {
        f3RemainingRayStart = f3RayEnd;
        f3RayEnd = f3CameraPos + fFullRayLength * f3ViewDir;
        fRemainingLength = length(f3RayEnd - f3RemainingRayStart);
        #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION
        {
            // Do not allow integration step to become less than 50 km
            // Maximum possible view ray length is 2023 km (from the top of the
            // atmosphere touching the Earth and then again to the top of the 
            // atmosphere).
            // For such ray, 41 integration step will be performed
            // Also assure that at least 20 steps are always performed
            float fMinStep = 50000.0;
            float fMumSteps = max(20.0, ceil(fRemainingLength/fMinStep) );
            ComputeInsctrIntegral(f3RemainingRayStart,
                                  f3RayEnd,
                                  f3EarthCentre,
                                  g_LightAttribs.f4DirOnLight.xyz,
                                  f2ParticleNetDensityFromCam,
                                  f3RayleighInscattering,
                                  f3MieInscattering,
                                  fMumSteps);
        }
        #endif
    }

    float3 f3InsctrIntegral = float3(0.0, 0.0, 0.0);

    #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION
        // Apply phase functions
        // Note that cosTheta = dot(DirOnCamera, LightDir) = dot(ViewDir, DirOnLight) because
        // DirOnCamera = -ViewDir and LightDir = -DirOnLight
        ApplyPhaseFunctions(f3RayleighInscattering, f3MieInscattering, cosTheta);

        f3InsctrIntegral = f3RayleighInscattering + f3MieInscattering;
    #endif

    #if CASCADE_PROCESSING_MODE == CASCADE_PROCESSING_MODE_SINGLE_PASS
        // Note that the first cascade used for ray marching must contain camera within it
        // otherwise this expression might fail
        f3RayStart = f3RestrainedCameraPos;
    #endif

    #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT || MULTIPLE_SCATTERING_MODE == MULTIPLE_SCTR_MODE_OCCLUDED
    {
        #if MULTIPLE_SCATTERING_MODE == MULTIPLE_SCTR_MODE_OCCLUDED
            #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT
                #define tex3DSctrLUT         g_tex3DMultipleSctrLUT
                #define tex3DSctrLUT_sampler g_tex3DMultipleSctrLUT_sampler
            #elif SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_NONE || SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION
                #define tex3DSctrLUT         g_tex3DHighOrderSctrLUT
                #define tex3DSctrLUT_sampler g_tex3DHighOrderSctrLUT_sampler
            #endif
        #else
            #define tex3DSctrLUT         g_tex3DSingleSctrLUT
            #define tex3DSctrLUT_sampler g_tex3DSingleSctrLUT_sampler
        #endif

        float3 f3MultipleScattering = float3(0.0, 0.0, 0.0);
        if( fTotalLitLength > 0.0 )
        {    
            float3 f3LitSectionStart = f3RayStart + fDistToFirstLitSection * f3ViewDir;
            float3 f3LitSectionEnd = f3LitSectionStart + fTotalLitLength * f3ViewDir;

            float3 f3ExtinctionToStart = GetExtinctionUnverified(f3RestrainedCameraPos, f3LitSectionStart, f3ViewDir, f3EarthCentre);
            float4 f4UVWQ = float4(-1.0, -1.0, -1.0, -1.0);
            f3MultipleScattering = f3ExtinctionToStart * LookUpPrecomputedScattering(f3LitSectionStart, f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ); 
        
            float3 f3ExtinctionToEnd = GetExtinctionUnverified(f3RestrainedCameraPos, f3LitSectionEnd, f3ViewDir,  f3EarthCentre);
            // To avoid artifacts, we must be consistent when performing look-ups into the scattering texture, i.e.
            // we must assure that if the first look-up is above (below) horizon, then the second look-up
            // is also above (below) horizon.
            // We provide previous look-up coordinates to the function so that it is able to figure out where the first look-up
            // was performed
            f3MultipleScattering -= f3ExtinctionToEnd * LookUpPrecomputedScattering(f3LitSectionEnd, f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ);
        
            f3InsctrIntegral += max(f3MultipleScattering, float3(0.0, 0.0, 0.0));
        }

        // Add contribution from the reminder of the ray behind the largest cascade
        if( fRemainingLength > 0.0 )
        {
            float3 f3Extinction = GetExtinctionUnverified(f3RestrainedCameraPos, f3RemainingRayStart, f3ViewDir, f3EarthCentre);
            float4 f4UVWQ = float4(-1.0, -1.0, -1.0, -1.0);
            float3 f3RemainingInsctr = 
                f3Extinction * LookUpPrecomputedScattering(f3RemainingRayStart, f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ);
        
            f3Extinction = GetExtinctionUnverified(f3RestrainedCameraPos, f3RayEnd, f3ViewDir, f3EarthCentre);
            f3RemainingInsctr -= 
                f3Extinction * LookUpPrecomputedScattering(f3RayEnd, f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ);

            f3InsctrIntegral += max(f3RemainingInsctr, float3(0.0, 0.0, 0.0));
        }
    }
    #undef tex3DSctrLUT
    #undef tex3DSctrLUT_sampler
    #endif // #if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT || MULTIPLE_SCATTERING_MODE == MULTIPLE_SCTR_MODE_OCCLUDED

    #if MULTIPLE_SCATTERING_MODE == MULTIPLE_SCTR_MODE_UNOCCLUDED
    {
        float3 f3HighOrderScattering = float3(0.0, 0.0, 0.0), f3Extinction = float3(0.0, 0.0, 0.0);
        
        float4 f4UVWQ = float4(-1.0, -1.0, -1.0, -1.0);
        f3Extinction = GetExtinctionUnverified(f3RestrainedCameraPos, f3RayStart, f3ViewDir, f3EarthCentre);
        f3HighOrderScattering += f3Extinction * LookUpPrecomputedScattering(f3RayStart, f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, g_tex3DHighOrderSctrLUT, g_tex3DHighOrderSctrLUT_sampler, f4UVWQ); 
        
        f3Extinction = GetExtinctionUnverified(f3RestrainedCameraPos, f3RayEnd, f3ViewDir, f3EarthCentre);
        // We provide previous look-up coordinates to the function so that it is able to figure out where the first look-up
        // was performed
        f3HighOrderScattering -= f3Extinction * LookUpPrecomputedScattering(f3RayEnd, f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, g_tex3DHighOrderSctrLUT, g_tex3DHighOrderSctrLUT_sampler, f4UVWQ); 

        f3InsctrIntegral += f3HighOrderScattering;
    }
    #endif

    return f3InsctrIntegral * g_LightAttribs.f4ExtraterrestrialSunColor.rgb;
}



void RayMarchPS(in ScreenSizeQuadVSOutput VSOut,
                in float4 f4PosPS : SV_Position,
                out float4 f4Inscattering : SV_TARGET)
{
    uint2 ui2SamplePosSliceInd = uint2(f4PosPS.xy);
    float2 f2SampleLocation = g_tex2DCoordinates.Load( int3(ui2SamplePosSliceInd, 0) );
    float fRayEndCamSpaceZ = g_tex2DEpipolarCamSpaceZ.Load( int3(ui2SamplePosSliceInd, 0) );

    [branch]
    if( any( Greater( abs( f2SampleLocation ), (1.0 + 1e-3) * F2ONE) ) )
    {
        f4Inscattering = F4ZERO;
        return;
    }
    f4Inscattering = F4ONE;
#if ENABLE_LIGHT_SHAFTS
    float fCascade = g_MiscParams.fCascadeInd + VSOut.m_fInstID;
    f4Inscattering.rgb = 
        ComputeShadowedInscattering(f2SampleLocation, 
                                    fRayEndCamSpaceZ,
                                    fCascade,
                                    ui2SamplePosSliceInd.y);
#else
    float3 f3Extinction;
    ComputeUnshadowedInscattering(f2SampleLocation, fRayEndCamSpaceZ, float(g_PPAttribs.m_uiInstrIntegralSteps), f4Inscattering.rgb, f3Extinction);
    f4Inscattering.rgb *= g_LightAttribs.f4ExtraterrestrialSunColor.rgb;
#endif
}


//float3 FixInscatteredRadiancePS(ScreenSizeQuadVSOutput VSOut) : SV_Target
//{
//    if( g_PPAttribs.m_bShowDepthBreaks )
//        return float3(0,1,0);
//
//    float fCascade = g_MiscParams.fCascadeInd + VSOut.m_fInstID;
//    float fRayEndCamSpaceZ = g_tex2DCamSpaceZ.SampleLevel( samLinearClamp, ProjToUV(VSOut.m_f2PosPS.xy), 0 );
//
//#if ENABLE_LIGHT_SHAFTS
//    return ComputeShadowedInscattering(VSOut.m_f2PosPS.xy, 
//                              fRayEndCamSpaceZ,
//                              fCascade,
//                              false, // We cannot use min/max optimization at depth breaks
//                              0 // Ignored
//                              );
//#else
//    float3 f3Inscattering, f3Extinction;
//    ComputeUnshadowedInscattering(VSOut.m_f2PosPS.xy, fRayEndCamSpaceZ, float(g_PPAttribs.m_uiInstrIntegralSteps), f3Inscattering, f3Extinction);
//    f3Inscattering *= g_LightAttribs.f4ExtraterrestrialSunColor.rgb;
//    return f3Inscattering;
//#endif
//
//}


void FixAndApplyInscatteredRadiancePS(ScreenSizeQuadVSOutput VSOut,
                                      in float4 f4PosPS : SV_Position,
                                      out float4 f4Color : SV_Target)
{
    f4Color = float4(0.0, 1.0, 0.0, 1.0);
    if( g_PPAttribs.m_bShowDepthBreaks )
        return;

    float fCamSpaceZ = g_tex2DCamSpaceZ.SampleLevel(g_tex2DCamSpaceZ_sampler, NormalizedDeviceXYToTexUV(VSOut.m_f2PosPS), 0 );
    float3 f3BackgroundColor = float3(0.0, 0.0, 0.0);
    [branch]
    if( !g_PPAttribs.m_bShowLightingOnly )
    {
        f3BackgroundColor = g_tex2DColorBuffer.Load( int3(f4PosPS.xy,0) ).rgb;
        f3BackgroundColor *= (fCamSpaceZ > g_CameraAttribs.fFarPlaneZ) ? g_LightAttribs.f4ExtraterrestrialSunColor.rgb : float3(1.0, 1.0, 1.0);
        float3 f3ReconstructedPosWS = ProjSpaceXYZToWorldSpace(float3(VSOut.m_f2PosPS.xy, fCamSpaceZ), g_CameraAttribs.mProj, g_CameraAttribs.mViewProjInv);
        float3 f3Extinction = GetExtinction(g_CameraAttribs.f4CameraPos.xyz, f3ReconstructedPosWS);
        f3BackgroundColor *= f3Extinction.rgb;
    }
    
    float fCascade = g_MiscParams.fCascadeInd + VSOut.m_fInstID;

#if ENABLE_LIGHT_SHAFTS
    float3 f3InsctrColor = 
        ComputeShadowedInscattering(VSOut.m_f2PosPS.xy, 
                              fCamSpaceZ,
                              fCascade,
                              0u // Ignored
                              );
#else
    float3 f3InsctrColor, f3Extinction;
    ComputeUnshadowedInscattering(VSOut.m_f2PosPS.xy, fCamSpaceZ, float(g_PPAttribs.m_uiInstrIntegralSteps), f3InsctrColor, f3Extinction);
    f3InsctrColor *= g_LightAttribs.f4ExtraterrestrialSunColor.rgb;
#endif

    f4Color.rgb = (f3BackgroundColor + f3InsctrColor);
#if PERFORM_TONE_MAPPING
    f4Color.rgb = ToneMap(f3BackgroundColor + f3InsctrColor);
#else
    const float DELTA = 0.00001;
    f4Color.rgb = log( max(DELTA, dot(f3BackgroundColor + f3InsctrColor, RGB_TO_LUMINANCE)) ) * F3ONE;
#endif
}
