
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
    float3 f3TotalMieOpticalDepth  = g_MediaParams.f4MieExtinctionCoeff.rgb      * f2ParticleDensity.y;
        
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
