#ifndef _ATMOSPHERE_SHADERS_COMMON_FXH_
#define _ATMOSPHERE_SHADERS_COMMON_FXH_

#include "Structures.fxh"

#define FLT_MAX 3.402823466e+38f

#define F4ZERO float4(0.0, 0.0, 0.0, 0.0)
#define F4ONE float4(1.0, 1.0, 1.0, 1.0)
#define F3ZERO float3(0.0, 0.0, 0.0)
#define F3ONE float3(1.0, 1.0, 1.0)
#define F2ZERO float2(0.0, 0.0)
#define F2ONE float2(1.0, 1.0)

#ifndef NUM_EPIPOLAR_SLICES
#   define NUM_EPIPOLAR_SLICES 1024
#endif

#ifndef MAX_SAMPLES_IN_SLICE
#   define MAX_SAMPLES_IN_SLICE 512
#endif

#ifndef SCREEN_RESLOUTION
#   define SCREEN_RESLOUTION float2(1024.0, 768.0)
#endif

#ifndef OPTIMIZE_SAMPLE_LOCATIONS
#   define OPTIMIZE_SAMPLE_LOCATIONS 1
#endif

#ifndef CORRECT_INSCATTERING_AT_DEPTH_BREAKS
#   define CORRECT_INSCATTERING_AT_DEPTH_BREAKS 0
#endif

//#define SHADOW_MAP_DEPTH_BIAS 1e-4

#ifndef TRAPEZOIDAL_INTEGRATION
#   define TRAPEZOIDAL_INTEGRATION 1
#endif

#ifndef EARTH_RADIUS
#   define EARTH_RADIUS 6360000.0
#endif

#ifndef ATM_TOP_HEIGHT
#   define ATM_TOP_HEIGHT 80000.0
#endif

#ifndef ATM_TOP_RADIUS
#   define ATM_TOP_RADIUS (EARTH_RADIUS+ATM_TOP_HEIGHT)
#endif

#ifndef PARTICLE_SCALE_HEIGHT
#   define PARTICLE_SCALE_HEIGHT float2(7994.0, 1200.0)
#endif

#ifndef ENABLE_LIGHT_SHAFTS
#   define ENABLE_LIGHT_SHAFTS 1
#endif

#ifndef USE_1D_MIN_MAX_TREE
#   define USE_1D_MIN_MAX_TREE 1
#endif

#ifndef IS_32BIT_MIN_MAX_MAP
#   define IS_32BIT_MIN_MAX_MAP 0
#endif

#ifndef SINGLE_SCATTERING_MODE
#   define SINGLE_SCATTERING_MODE SINGLE_SCTR_MODE_INTEGRATION
#endif

#ifndef MULTIPLE_SCATTERING_MODE
#   define MULTIPLE_SCATTERING_MODE MULTIPLE_SCTR_MODE_NONE
#endif

#ifndef PRECOMPUTED_SCTR_LUT_DIM
#   define PRECOMPUTED_SCTR_LUT_DIM float4(32.0, 128.0, 32.0, 16.0)
#endif

#ifndef NUM_RANDOM_SPHERE_SAMPLES
#   define NUM_RANDOM_SPHERE_SAMPLES 128
#endif

#ifndef PERFORM_TONE_MAPPING
#   define PERFORM_TONE_MAPPING 1
#endif

#ifndef LOW_RES_LUMINANCE_MIPS
#   define LOW_RES_LUMINANCE_MIPS 7
#endif

#ifndef TONE_MAPPING_MODE
#   define TONE_MAPPING_MODE TONE_MAPPING_MODE_REINHARD_MOD
#endif

#ifndef LIGHT_ADAPTATION
#   define LIGHT_ADAPTATION 1
#endif

#ifndef CASCADE_PROCESSING_MODE
#   define CASCADE_PROCESSING_MODE CASCADE_PROCESSING_MODE_SINGLE_PASS
#endif

#ifndef USE_COMBINED_MIN_MAX_TEXTURE
#   define USE_COMBINED_MIN_MAX_TEXTURE 1
#endif

#ifndef EXTINCTION_EVAL_MODE
#   define EXTINCTION_EVAL_MODE EXTINCTION_EVAL_MODE_EPIPOLAR
#endif

#ifndef AUTO_EXPOSURE
#   define AUTO_EXPOSURE 1
#endif

#define INVALID_EPIPOLAR_LINE float4(-1000.0, -1000.0, -100.0, -100.0)

#define RGB_TO_LUMINANCE float3(0.212671, 0.715160, 0.072169)

struct ScreenSizeQuadVSOutput
{
    float2 m_f2PosPS : PosPS; // Position in projection space [-1,1]x[-1,1]
    float m_fInstID : InstanceID;
};

// GLSL compiler is so bad that it cannot properly handle matrices passed as structure members!
float3 ProjSpaceXYZToWorldSpace(in float3 f3PosPS, in float4x4 mProj, in float4x4 mViewProjInv /*CameraAttribs CamAttribs <- DO NOT DO THIS*/)
{
    // We need to compute normalized device z before applying view-proj inverse matrix

    // It does not matter if we are in HLSL or GLSL. The way normalized device
    // coordinates are computed is the same in both APIs - simply transform by
    // matrix and then divide by w. Consequently, the inverse transform is also 
    // the same.
    // What differs is that in GL, NDC z is transformed from [-1,+1] to [0,1]
    // before storing in the depth buffer
    float fNDC_Z = MATRIX_ELEMENT(mProj,2,2) + MATRIX_ELEMENT(mProj,3,2) / f3PosPS.z;
    float4 ReconstructedPosWS = mul( float4(f3PosPS.xy, fNDC_Z, 1.0), mViewProjInv );
    ReconstructedPosWS /= ReconstructedPosWS.w;
    return ReconstructedPosWS.xyz;
}

float3 WorldSpaceToShadowMapUV(in float3 f3PosWS, in matrix mWorldToShadowMapUVDepth)
{
    float4 f4ShadowMapUVDepth = mul( float4(f3PosWS, 1), mWorldToShadowMapUVDepth );
    // Shadow map projection matrix is orthographic, so we do not need to divide by w
    //f4ShadowMapUVDepth.xyz /= f4ShadowMapUVDepth.w;
    
    // Applying depth bias results in light leaking through the opaque objects when looking directly
    // at the light source
    return f4ShadowMapUVDepth.xyz;
}


void GetRaySphereIntersection(in float3 f3RayOrigin,
                              in float3 f3RayDirection,
                              in float3 f3SphereCenter,
                              in float fSphereRadius,
                              out float2 f2Intersections)
{
    // http://wiki.cgsociety.org/index.php/Ray_Sphere_Intersection
    f3RayOrigin -= f3SphereCenter;
    float A = dot(f3RayDirection, f3RayDirection);
    float B = 2.0 * dot(f3RayOrigin, f3RayDirection);
    float C = dot(f3RayOrigin,f3RayOrigin) - fSphereRadius*fSphereRadius;
    float D = B*B - 4.0*A*C;
    // If discriminant is negative, there are no real roots hence the ray misses the
    // sphere
    if( D<0.0 )
    {
        f2Intersections = float2(-1.0, -1.0);
    }
    else
    {
        D = sqrt(D);
        f2Intersections = float2(-B - D, -B + D) / (2.0*A); // A must be positive here!!
    }
}

void GetRaySphereIntersection2(in float3 f3RayOrigin,
                               in float3 f3RayDirection,
                               in float3 f3SphereCenter,
                               in float2 f2SphereRadius,
                               out float4 f4Intersections)
{
    // http://wiki.cgsociety.org/index.php/Ray_Sphere_Intersection
    f3RayOrigin -= f3SphereCenter;
    float A = dot(f3RayDirection, f3RayDirection);
    float B = 2.0 * dot(f3RayOrigin, f3RayDirection);
    float2 C = dot(f3RayOrigin,f3RayOrigin) - f2SphereRadius*f2SphereRadius;
    float2 D = B*B - 4.0*A*C;
    // If discriminant is negative, there are no real roots hence the ray misses the
    // sphere
    float2 f2RealRootMask = float2(D.x >= 0.0 ? 1.0 : 0.0, D.y >= 0.0 ? 1.0 : 0.0);
    D = sqrt( max(D,0.0) );
    f4Intersections =   f2RealRootMask.xxyy * float4(-B - D.x, -B + D.x, -B - D.y, -B + D.y) / (2.0*A) + 
                      (F4ONE - f2RealRootMask.xxyy) * float4(-1.0,-1.0,-1.0,-1.0);
}


float4 GetOutermostScreenPixelCoords()
{
    // The outermost visible screen pixels centers do not lie exactly on the boundary (+1 or -1), but are biased by
    // 0.5 screen pixel size inwards
    //
    //                                        2.0
    //    |<---------------------------------------------------------------------->|
    //
    //       2.0/Res
    //    |<--------->|
    //    |     X     |      X     |     X     |    ...    |     X     |     X     |
    //   -1     |                                                            |    +1
    //          |                                                            |
    //          |                                                            |
    //      -1 + 1.0/Res                                                  +1 - 1.0/Res
    //
    // Using shader macro is much more efficient than using constant buffer variable
    // because the compiler is able to optimize the code more aggressively
    // return float4(-1,-1,1,1) + float4(1, 1, -1, -1)/g_PPAttribs.m_f2ScreenResolution.xyxy;
    return float4(-1.0,-1.0,1.0,1.0) + float4(1.0, 1.0, -1.0, -1.0) / SCREEN_RESLOUTION.xyxy;
}


// When checking if a point is inside the screen, we must test against 
// the biased screen boundaries 
bool IsValidScreenLocation(in float2 f2XY)
{
    const float2 SAFETY_EPSILON = float2(0.2, 0.2);
    return all( LessEqual( abs(f2XY), F2ONE - (F2ONE - SAFETY_EPSILON) / SCREEN_RESLOUTION.xy ) );
}


#endif //_ATMOSPHERE_SHADERS_COMMON_FXH_
