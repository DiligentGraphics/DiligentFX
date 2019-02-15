#ifndef _BASIC_STRUCTURES_FXH_
#define _BASIC_STRUCTURES_FXH_


#ifdef __cplusplus

#   ifndef BOOL
#      define BOOL int32_t // Do not use bool, because sizeof(bool)==1 !
#   endif

#   ifndef CHECK_STRUCT_ALIGNMENT
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" )
#   endif

#else

#   ifndef BOOL
#       define BOOL bool
#   endif

#   ifndef CHECK_STRUCT_ALIGNMENT
#       define CHECK_STRUCT_ALIGNMENT(s)
#   endif

#endif


struct CascadeAttribs
{
	float4 f4LightSpaceScale;
	float4 f4LightSpaceScaledBias;
    float4 f4StartEndZ;
};
CHECK_STRUCT_ALIGNMENT(CascadeAttribs);


#define MAX_CASCADES 8
struct ShadowMapAttribs
{
    // 0
#ifdef __cplusplus
    float4x4 mWorldToLightViewT; // Matrices in HLSL are COLUMN-major while float4x4 is ROW major
#else
    matrix mWorldToLightView;  // Transform from view space to light projection space
#endif
    // 16
    CascadeAttribs Cascades[MAX_CASCADES];

#ifdef __cplusplus
    float fCascadeCamSpaceZEnd[MAX_CASCADES];
    float4x4 mWorldToShadowMapUVDepthT[MAX_CASCADES];
#else
	float4 f4CascadeCamSpaceZEnd[MAX_CASCADES/4];
    matrix mWorldToShadowMapUVDepth[MAX_CASCADES];
#endif

    // Do not use bool, because sizeof(bool)==1 !
	BOOL bVisualizeCascades;

    // float3 f3Padding;
    // OpenGL compiler does not handle 3-component vectors properly
    // and screws up the structure layout.
    // Opengl.org suggests not using vec3 at all
    int Padding0;
    int Padding1;
    int Padding2;
};
CHECK_STRUCT_ALIGNMENT(ShadowMapAttribs);


struct LightAttribs
{
    float4 f4Direction;
    float4 f4AmbientLight;
    float4 f4Intensity; // Extraterrestrial sun radiance

    ShadowMapAttribs ShadowAttribs;
};
CHECK_STRUCT_ALIGNMENT(LightAttribs);


struct CameraAttribs
{
    float4 f4CameraPos;            ///< Camera world position
    float fNearPlaneZ; 
    float fFarPlaneZ; // fNearPlaneZ < fFarPlaneZ
    float2 f2Dummy;

#ifdef __cplusplus
    float4x4 mViewProjT;
    //float4x4 mViewT;
    float4x4 mProjT;
    float4x4 mViewProjInvT;
#else
    matrix mViewProj;
    //matrix mView;
    matrix mProj;
    matrix mViewProjInv;
#endif
};
CHECK_STRUCT_ALIGNMENT(CameraAttribs);


#endif //_BASIC_STRUCTURES_FXH_
