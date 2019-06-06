
float2 GetNetParticleDensity(in float fHeightAboveSurface,
                             in float fCosZenithAngle,
                             in float fAtmTopHeight)
{
    float fRelativeHeightAboveSurface = fHeightAboveSurface / fAtmTopHeight;
    return g_tex2DOccludedNetDensityToAtmTop.SampleLevel(g_tex2DOccludedNetDensityToAtmTop_sampler, float2(fRelativeHeightAboveSurface, fCosZenithAngle*0.5+0.5), 0);
}

float2 GetNetParticleDensity(in float3 f3Pos,
                             in float3 f3EarthCentre,
                             in float  fEarthRadius,
							 in float  fAtmTopHeight,
                             in float3 f3RayDir)
{
    float3 f3EarthCentreToPointDir = f3Pos - f3EarthCentre;
    float fDistToEarthCentre = length(f3EarthCentreToPointDir);
    f3EarthCentreToPointDir /= fDistToEarthCentre;
    float fHeightAboveSurface = fDistToEarthCentre - fEarthRadius;
    float fCosZenithAngle = dot( f3EarthCentreToPointDir, f3RayDir );
    return GetNetParticleDensity(fHeightAboveSurface, fCosZenithAngle, fAtmTopHeight);
}

void ApplyPhaseFunctions(inout float3 f3RayleighInscattering,
                         inout float3 f3MieInscattering,
                         in float cosTheta)
{
    f3RayleighInscattering *= g_MediaParams.f4AngularRayleighSctrCoeff.rgb * (1.0 + cosTheta*cosTheta);
    
    // Apply Cornette-Shanks phase function (see Nishita et al. 93):
    // F(theta) = 1/(4*PI) * 3*(1-g^2) / (2*(2+g^2)) * (1+cos^2(theta)) / (1 + g^2 - 2g*cos(theta))^(3/2)
    // f4CS_g = ( 3*(1-g^2) / (2*(2+g^2)), 1+g^2, -2g, 1 )
    float fDenom = rsqrt( dot(g_MediaParams.f4CS_g.yz, float2(1.0, cosTheta)) ); // 1 / (1 + g^2 - 2g*cos(theta))^(1/2)
    float fCornettePhaseFunc = g_MediaParams.f4CS_g.x * (fDenom*fDenom*fDenom) * (1.0 + cosTheta*cosTheta);
    f3MieInscattering *= g_MediaParams.f4AngularMieSctrCoeff.rgb * fCornettePhaseFunc;
}

// This function computes atmospheric properties in the given point
void GetAtmosphereProperties(in float3  f3Pos,
                             in float3  f3EarthCentre,
                             in float   fEarthRadius,
                             in float   fAtmTopHeight,
							 in float4  f4ParticleScaleHeight,
                             in float3  f3DirOnLight,
                             out float2 f2ParticleDensity,
                             out float2 f2NetParticleDensityToAtmTop)
{
    // Calculate the point height above the SPHERICAL Earth surface:
    float3 f3EarthCentreToPointDir = f3Pos - f3EarthCentre;
    float fDistToEarthCentre = length(f3EarthCentreToPointDir);
    f3EarthCentreToPointDir /= fDistToEarthCentre;
    float fHeightAboveSurface = fDistToEarthCentre - fEarthRadius;

    f2ParticleDensity = exp( -fHeightAboveSurface * f4ParticleScaleHeight.zw );

    // Get net particle density from the integration point to the top of the atmosphere:
    float fCosSunZenithAngleForCurrPoint = dot( f3EarthCentreToPointDir, f3DirOnLight );
    f2NetParticleDensityToAtmTop = GetNetParticleDensity(fHeightAboveSurface, fCosSunZenithAngleForCurrPoint, fAtmTopHeight);
}

// This function computes differential inscattering for the given particle densities 
// (without applying phase functions)
void ComputePointDiffInsctr(in float2 f2ParticleDensityInCurrPoint,
                            in float2 f2NetParticleDensityFromCam,
                            in float2 f2NetParticleDensityToAtmTop,
                            out float3 f3DRlghInsctr,
                            out float3 f3DMieInsctr)
{
    // Compute total particle density from the top of the atmosphere through the integraion point to camera
    float2 f2TotalParticleDensity = f2NetParticleDensityFromCam + f2NetParticleDensityToAtmTop;
        
    // Get optical depth
    float3 f3TotalRlghOpticalDepth = g_MediaParams.f4RayleighExtinctionCoeff.rgb * f2TotalParticleDensity.x;
    float3 f3TotalMieOpticalDepth  = g_MediaParams.f4MieExtinctionCoeff.rgb      * f2TotalParticleDensity.y;
        
    // And total extinction for the current integration point:
    float3 f3TotalExtinction = exp( -(f3TotalRlghOpticalDepth + f3TotalMieOpticalDepth) );

    f3DRlghInsctr = f2ParticleDensityInCurrPoint.x * f3TotalExtinction;
    f3DMieInsctr  = f2ParticleDensityInCurrPoint.y * f3TotalExtinction; 
}

void ComputeInsctrIntegral(in float3    f3RayStart,
                           in float3    f3RayEnd,
                           in float3    f3EarthCentre,
                           in float     fEarthRadius,
                           in float     fAtmTopHeight,
						   in float4    f4ParticleScaleHeight,
                           in float3    f3DirOnLight,
                           in uint      uiNumSteps,
                           inout float2 f2NetParticleDensityFromCam,
                           inout float3 f3RayleighInscattering,
                           inout float3 f3MieInscattering)
{
    float3 f3Step = (f3RayEnd - f3RayStart) / float(uiNumSteps);
    float fStepLen = length(f3Step);

#if TRAPEZOIDAL_INTEGRATION
    // For trapezoidal integration we need to compute some variables for the starting point of the ray
    float2 f2PrevParticleDensity = float2(0.0, 0.0);
    float2 f2NetParticleDensityToAtmTop = float2(0.0, 0.0);
    GetAtmosphereProperties(f3RayStart, f3EarthCentre, fEarthRadius, fAtmTopHeight, f4ParticleScaleHeight, f3DirOnLight, f2PrevParticleDensity, f2NetParticleDensityToAtmTop);

    float3 f3PrevDiffRInsctr = float3(0.0, 0.0, 0.0), f3PrevDiffMInsctr = float3(0.0, 0.0, 0.0);
    ComputePointDiffInsctr(f2PrevParticleDensity, f2NetParticleDensityFromCam, f2NetParticleDensityToAtmTop, f3PrevDiffRInsctr, f3PrevDiffMInsctr);
#endif


    for (uint uiStepNum = 0u; uiStepNum < uiNumSteps; ++uiStepNum)
    {
#if TRAPEZOIDAL_INTEGRATION
        // With trapezoidal integration, we will evaluate the function at the end of each section and 
        // compute area of a trapezoid
        float3 f3CurrPos = f3RayStart + f3Step * (float(uiStepNum) + 1.0);
#else
        // With stair-step integration, we will evaluate the function at the middle of each section and 
        // compute area of a rectangle
        float3 f3CurrPos = f3RayStart + f3Step * (float(uiStepNum) + 0.5);
#endif
        
        float2 f2ParticleDensity, f2NetParticleDensityToAtmTop;
        GetAtmosphereProperties(f3CurrPos, f3EarthCentre, fEarthRadius, fAtmTopHeight, f4ParticleScaleHeight, f3DirOnLight, f2ParticleDensity, f2NetParticleDensityToAtmTop);

        // Accumulate net particle density from the camera to the integration point:
#if TRAPEZOIDAL_INTEGRATION
        f2NetParticleDensityFromCam += (f2PrevParticleDensity + f2ParticleDensity) * (fStepLen / 2.0);
        f2PrevParticleDensity = f2ParticleDensity;
#else
        f2NetParticleDensityFromCam += f2ParticleDensity * fStepLen;
#endif

        float3 f3DRlghInsctr, f3DMieInsctr;
        ComputePointDiffInsctr(f2ParticleDensity, f2NetParticleDensityFromCam, f2NetParticleDensityToAtmTop, f3DRlghInsctr, f3DMieInsctr);

#if TRAPEZOIDAL_INTEGRATION
        f3RayleighInscattering += (f3DRlghInsctr + f3PrevDiffRInsctr) * (fStepLen / 2.0);
        f3MieInscattering      += (f3DMieInsctr  + f3PrevDiffMInsctr) * (fStepLen / 2.0);

        f3PrevDiffRInsctr = f3DRlghInsctr;
        f3PrevDiffMInsctr = f3DMieInsctr;
#else
        f3RayleighInscattering += f3DRlghInsctr * fStepLen;
        f3MieInscattering      += f3DMieInsctr * fStepLen;
#endif
    }
}


void IntegrateUnshadowedInscattering(in float3   f3RayStart, 
                                     in float3   f3RayEnd,
                                     in float3   f3ViewDir,
                                     in float3   f3EarthCentre,
                                     in float    fEarthRadius,
                                     in float    fAtmTopHeight,
							         in float4   f4ParticleScaleHeight,
                                     in float3   f3DirOnLight,
                                     in uint     uiNumSteps,
                                     out float3  f3Inscattering,
                                     out float3  f3Extinction)
{
    float2 f2NetParticleDensityFromCam = float2(0.0, 0.0);
    float3 f3RayleighInscattering = float3(0.0, 0.0, 0.0);
    float3 f3MieInscattering = float3(0.0, 0.0, 0.0);
    ComputeInsctrIntegral( f3RayStart,
                           f3RayEnd,
                           f3EarthCentre,
                           fEarthRadius,
                           fAtmTopHeight,
                           f4ParticleScaleHeight,
                           f3DirOnLight,
                           uiNumSteps,
                           f2NetParticleDensityFromCam,
                           f3RayleighInscattering,
                           f3MieInscattering);

    float3 f3TotalRlghOpticalDepth = g_MediaParams.f4RayleighExtinctionCoeff.rgb * f2NetParticleDensityFromCam.x;
    float3 f3TotalMieOpticalDepth  = g_MediaParams.f4MieExtinctionCoeff.rgb      * f2NetParticleDensityFromCam.y;
    f3Extinction = exp( -(f3TotalRlghOpticalDepth + f3TotalMieOpticalDepth) );

    // Apply phase function
    // Note that cosTheta = dot(DirOnCamera, LightDir) = dot(ViewDir, DirOnLight) because
    // DirOnCamera = -ViewDir and LightDir = -DirOnLight
    float cosTheta = dot(f3ViewDir, f3DirOnLight);
    ApplyPhaseFunctions(f3RayleighInscattering, f3MieInscattering, cosTheta);

    f3Inscattering = f3RayleighInscattering + f3MieInscattering;
}
