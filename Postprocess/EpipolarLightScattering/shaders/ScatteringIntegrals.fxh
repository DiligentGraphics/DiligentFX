
// This function for analytical evaluation of particle density integral is 
// provided by Eric Bruneton
// http://www-evasion.inrialpes.fr/Membres/Eric.Bruneton/
//
// optical depth for ray (r,mu) of length d, using analytic formula
// (mu=cos(view zenith angle)), intersections with ground ignored
float2 GetDensityIntegralAnalytic(float r, float mu, float d) 
{
    float2 f2A = sqrt( (0.5/PARTICLE_SCALE_HEIGHT.xy) * r );
    float4 f4A01 = f2A.xxyy * float2(mu, mu + d / r).xyxy;
    float4 f4A01s = sign(f4A01);
    float4 f4A01sq = f4A01*f4A01;
    
    float2 f2X;
    f2X.x = f4A01s.y > f4A01s.x ? exp(f4A01sq.x) : 0.0;
    f2X.y = f4A01s.w > f4A01s.z ? exp(f4A01sq.z) : 0.0;
    
    float4 f4Y = f4A01s / (2.3193*abs(f4A01) + sqrt(1.52*f4A01sq + 4.0)) * float3(1.0, exp(-d/PARTICLE_SCALE_HEIGHT.xy*(d/(2.0*r)+mu))).xyxz;

    return sqrt((6.2831*PARTICLE_SCALE_HEIGHT)*r) * exp((EARTH_RADIUS-r)/PARTICLE_SCALE_HEIGHT.xy) * (f2X + float2( dot(f4Y.xy, float2(1.0, -1.0)), dot(f4Y.zw, float2(1.0, -1.0)) ));
}


float3 GetExtinctionUnverified(in float3 f3StartPos, in float3 f3EndPos, float3 f3EyeDir, float3 f3EarthCentre)
{
#if 0
    float2 f2ParticleDensity = IntegrateParticleDensity(f3StartPos, f3EndPos, f3EarthCentre, 20);
#else
    float r = length(f3StartPos-f3EarthCentre);
    float fCosZenithAngle = dot(f3StartPos-f3EarthCentre, f3EyeDir) / r;
    float2 f2ParticleDensity = GetDensityIntegralAnalytic(r, fCosZenithAngle, length(f3StartPos - f3EndPos));
#endif

    // Get optical depth
    float3 f3TotalRlghOpticalDepth = g_MediaParams.f4RayleighExtinctionCoeff.rgb * f2ParticleDensity.x;
    float3 f3TotalMieOpticalDepth  = g_MediaParams.f4MieExtinctionCoeff.rgb * f2ParticleDensity.y;
        
    // Compute extinction
    float3 f3Extinction = exp( -(f3TotalRlghOpticalDepth + f3TotalMieOpticalDepth) );
    return f3Extinction;
}

float3 GetExtinction(in float3 f3StartPos, in float3 f3EndPos)
{
    float3 f3EyeDir = f3EndPos - f3StartPos;
    float fRayLength = length(f3EyeDir);
    f3EyeDir /= fRayLength;

    float3 f3EarthCentre = /*g_CameraAttribs.f4CameraPos.xyz*float3(1,0,1)*/ - float3(0.0, 1.0, 0.0) * EARTH_RADIUS;

    float2 f2RayAtmTopIsecs = float2(0.0, 0.0); 
    // Compute intersections of the view ray with the atmosphere
    GetRaySphereIntersection(f3StartPos, f3EyeDir, f3EarthCentre, ATM_TOP_RADIUS, f2RayAtmTopIsecs);
    // If the ray misses the atmosphere, there is no extinction
    if( f2RayAtmTopIsecs.y < 0.0 )return float3(1.0, 1.0, 1.0);

    // Do not let the start and end point be outside the atmosphere
    f3EndPos = f3StartPos + f3EyeDir * min(f2RayAtmTopIsecs.y, fRayLength);
    f3StartPos += f3EyeDir * max(f2RayAtmTopIsecs.x, 0.0);

    return GetExtinctionUnverified(f3StartPos, f3EndPos, f3EyeDir, f3EarthCentre);
}



float2 GetNetParticleDensity(in float fHeightAboveSurface,
                             in float fCosZenithAngle)
{
    float fRelativeHeightAboveSurface = fHeightAboveSurface / ATM_TOP_HEIGHT;
    return g_tex2DOccludedNetDensityToAtmTop.SampleLevel(g_tex2DOccludedNetDensityToAtmTop_sampler, float2(fRelativeHeightAboveSurface, fCosZenithAngle*0.5+0.5), 0);
}

float2 GetNetParticleDensity(in float3 f3Pos,
                             in float3 f3EarthCentre,
                             in float3 f3RayDir)
{
    float3 f3EarthCentreToPointDir = f3Pos - f3EarthCentre;
    float fDistToEarthCentre = length(f3EarthCentreToPointDir);
    f3EarthCentreToPointDir /= fDistToEarthCentre;
    float fHeightAboveSurface = fDistToEarthCentre - EARTH_RADIUS;
    float fCosZenithAngle = dot( f3EarthCentreToPointDir, f3RayDir );
    return GetNetParticleDensity(fHeightAboveSurface, fCosZenithAngle);
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
void GetAtmosphereProperties(in float3 f3Pos,
                             in float3 f3EarthCentre,
                             in float3 f3DirOnLight,
                             out float2 f2ParticleDensity,
                             out float2 f2NetParticleDensityToAtmTop)
{
    // Calculate the point height above the SPHERICAL Earth surface:
    float3 f3EarthCentreToPointDir = f3Pos - f3EarthCentre;
    float fDistToEarthCentre = length(f3EarthCentreToPointDir);
    f3EarthCentreToPointDir /= fDistToEarthCentre;
    float fHeightAboveSurface = fDistToEarthCentre - EARTH_RADIUS;

    f2ParticleDensity = exp( -fHeightAboveSurface / PARTICLE_SCALE_HEIGHT );

    // Get net particle density from the integration point to the top of the atmosphere:
    float fCosSunZenithAngleForCurrPoint = dot( f3EarthCentreToPointDir, f3DirOnLight );
    f2NetParticleDensityToAtmTop = GetNetParticleDensity(fHeightAboveSurface, fCosSunZenithAngleForCurrPoint);
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

void ComputeInsctrIntegral(in float3 f3RayStart,
                           in float3 f3RayEnd,
                           in float3 f3EarthCentre,
                           in float3 f3DirOnLight,
                           inout float2 f2NetParticleDensityFromCam,
                           inout float3 f3RayleighInscattering,
                           inout float3 f3MieInscattering,
                           const float fNumSteps)
{
    float3 f3Step = (f3RayEnd - f3RayStart) / fNumSteps;
    float fStepLen = length(f3Step);

#if TRAPEZOIDAL_INTEGRATION
    // For trapezoidal integration we need to compute some variables for the starting point of the ray
    float2 f2PrevParticleDensity = float2(0.0, 0.0);
    float2 f2NetParticleDensityToAtmTop = float2(0.0, 0.0);
    GetAtmosphereProperties(f3RayStart, f3EarthCentre, f3DirOnLight, f2PrevParticleDensity, f2NetParticleDensityToAtmTop);

    float3 f3PrevDiffRInsctr = float3(0.0, 0.0, 0.0), f3PrevDiffMInsctr = float3(0.0, 0.0, 0.0);
    ComputePointDiffInsctr(f2PrevParticleDensity, f2NetParticleDensityFromCam, f2NetParticleDensityToAtmTop, f3PrevDiffRInsctr, f3PrevDiffMInsctr);
#endif


#if TRAPEZOIDAL_INTEGRATION
    // With trapezoidal integration, we will evaluate the function at the end of each section and 
    // compute area of a trapezoid
    for(float fStepNum = 1.0; fStepNum <= fNumSteps; fStepNum += 1.0)
#else
    // With stair-step integration, we will evaluate the function at the middle of each section and 
    // compute area of a rectangle
    for(float fStepNum = 0.5; fStepNum < fNumSteps; fStepNum += 1.0)
#endif
    {
        float3 f3CurrPos = f3RayStart + f3Step * fStepNum;
        float2 f2ParticleDensity, f2NetParticleDensityToAtmTop;
        GetAtmosphereProperties(f3CurrPos, f3EarthCentre, f3DirOnLight, f2ParticleDensity, f2NetParticleDensityToAtmTop);

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


void IntegrateUnshadowedInscattering(in float3 f3RayStart, 
                                     in float3 f3RayEnd,
                                     in float3 f3ViewDir,
                                     in float3 f3EarthCentre,
                                     in float3 f3DirOnLight,
                                     const float fNumSteps,
                                     out float3 f3Inscattering,
                                     out float3 f3Extinction)
{
    float2 f2NetParticleDensityFromCam = float2(0.0, 0.0);
    float3 f3RayleighInscattering = float3(0.0, 0.0, 0.0);
    float3 f3MieInscattering = float3(0.0, 0.0, 0.0);
    ComputeInsctrIntegral( f3RayStart,
                           f3RayEnd,
                           f3EarthCentre,
                           f3DirOnLight,
                           f2NetParticleDensityFromCam,
                           f3RayleighInscattering,
                           f3MieInscattering,
                           fNumSteps);

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
