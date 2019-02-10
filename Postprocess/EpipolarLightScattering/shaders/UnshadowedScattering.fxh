

void ComputeUnshadowedInscattering(float2 f2SampleLocation, 
                                   float fCamSpaceZ,
                                   const float fNumSteps,
                                   out float3 f3Inscattering,
                                   out float3 f3Extinction)
{
    f3Inscattering = float3(0.0, 0.0, 0.0);
    f3Extinction = float3(1.0, 1.0, 1.0);
    float3 f3RayTermination = ProjSpaceXYZToWorldSpace( float3(f2SampleLocation, fCamSpaceZ), g_CameraAttribs.mProj, g_CameraAttribs.mViewProjInv );
    float3 f3CameraPos = g_CameraAttribs.f4CameraPos.xyz;
    float3 f3ViewDir = f3RayTermination - f3CameraPos;
    float fRayLength = length(f3ViewDir);
    f3ViewDir /= fRayLength;

    float3 f3EarthCentre =  - float3(0.0, 1.0, 0.0) * EARTH_RADIUS;
    float2 f2RayAtmTopIsecs;
    GetRaySphereIntersection( f3CameraPos, f3ViewDir, f3EarthCentre, 
                              ATM_TOP_RADIUS, 
                              f2RayAtmTopIsecs);
    if( f2RayAtmTopIsecs.y <= 0.0 )
        return;

    float3 f3RayStart = f3CameraPos + f3ViewDir * max(0.0, f2RayAtmTopIsecs.x);
    if( fCamSpaceZ > g_CameraAttribs.fFarPlaneZ ) // fFarPlaneZ is pre-multiplied with 0.999999f
        fRayLength = +FLT_MAX;
    float3 f3RayEnd = f3CameraPos + f3ViewDir * min(fRayLength, f2RayAtmTopIsecs.y);

#if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_INTEGRATION
    IntegrateUnshadowedInscattering(f3RayStart, 
                                    f3RayEnd,
                                    f3ViewDir,
                                    f3EarthCentre,
                                    g_LightAttribs.f4DirOnLight.xyz,
                                    fNumSteps,
                                    f3Inscattering,
                                    f3Extinction);
#endif

#if SINGLE_SCATTERING_MODE == SINGLE_SCTR_MODE_LUT || MULTIPLE_SCATTERING_MODE > MULTIPLE_SCTR_MODE_NONE

#if MULTIPLE_SCATTERING_MODE > MULTIPLE_SCTR_MODE_NONE
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

    f3Extinction = GetExtinctionUnverified(f3RayStart, f3RayEnd, f3ViewDir, f3EarthCentre);

    // To avoid artifacts, we must be consistent when performing look-ups into the scattering texture, i.e.
    // we must assure that if the first look-up is above (below) horizon, then the second look-up
    // is also above (below) horizon. 
    float4 f4UVWQ = float4(-1.0, -1.0, -1.0, -1.0);
    f3Inscattering +=                LookUpPrecomputedScattering(f3RayStart, f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ); 
    // Provide previous look-up coordinates to the function to assure that look-ups are consistent
    f3Inscattering -= f3Extinction * LookUpPrecomputedScattering(f3RayEnd,   f3ViewDir, f3EarthCentre, g_LightAttribs.f4DirOnLight.xyz, tex3DSctrLUT, tex3DSctrLUT_sampler, f4UVWQ);
    #undef tex3DSctrLUT
    #undef tex3DSctrLUT_sampler
#endif

}
