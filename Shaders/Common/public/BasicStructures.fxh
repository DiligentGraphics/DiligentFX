#ifndef _BASIC_STRUCTURES_FXH_
#define _BASIC_STRUCTURES_FXH_

#include "ShaderDefinitions.fxh"

struct CascadeAttribs
{
	float4 f4LightSpaceScale;
	float4 f4LightSpaceScaledBias;
    float4 f4StartEndZ;

    // Cascade margin in light projection space ([-1, +1] x [-1, +1] x [-1(GL) or 0, +1])
    float4 f4MarginProjSpace;
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(CascadeAttribs);
#endif

#define SHADOW_MODE_PCF 1
#define SHADOW_MODE_VSM 2
#define SHADOW_MODE_EVSM2 3
#define SHADOW_MODE_EVSM4 4
#ifndef SHADOW_MODE
#   define SHADOW_MODE SHADOW_MODE_PCF
#endif

#define MAX_CASCADES 8
struct ShadowMapAttribs
{
    // 0
    float4x4 mWorldToLightView;  // Transform from view space to light projection space
    
    // 16
    CascadeAttribs Cascades[MAX_CASCADES];

    float4x4 mWorldToShadowMapUVDepth[MAX_CASCADES];

#ifdef __cplusplus
    float  fCascadeCamSpaceZEnd [MAX_CASCADES];
#else
    float4 f4CascadeCamSpaceZEnd[MAX_CASCADES/4];
#endif

    float4 f4ShadowMapDim;    // Width, Height, 1/Width, 1/Height

    // Number of shadow cascades
    int   iNumCascades                  DEFAULT_VALUE(0);
    float fNumCascades                  DEFAULT_VALUE(0);
    // Do not use bool, because sizeof(bool)==1 !
	BOOL  bVisualizeCascades            DEFAULT_VALUE(0);
    BOOL  bVisualizeShadowing           DEFAULT_VALUE(0);

    float fReceiverPlaneDepthBiasClamp  DEFAULT_VALUE(10);
    float fFixedDepthBias               DEFAULT_VALUE(1e-5f);
    float fCascadeTransitionRegion      DEFAULT_VALUE(0.1f);
    int   iMaxAnisotropy                DEFAULT_VALUE(4);

    float fVSMBias                      DEFAULT_VALUE(1e-4f);
    float fVSMLightBleedingReduction    DEFAULT_VALUE(0);
    float fEVSMPositiveExponent         DEFAULT_VALUE(40);
    float fEVSMNegativeExponent         DEFAULT_VALUE(5);

    BOOL  bIs32BitEVSM                  DEFAULT_VALUE(1);
    int   iFixedFilterSize              DEFAULT_VALUE(3); // 3x3 filter
    float fFilterWorldSize              DEFAULT_VALUE(0);
    BOOL  fDummy;
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(ShadowMapAttribs);
#endif

struct LightAttribs
{
    float4 f4Direction      DEFAULT_VALUE(float4(0, 0,-1, 0));
    float4 f4AmbientLight   DEFAULT_VALUE(float4(0, 0, 0, 0));
    float4 f4Intensity      DEFAULT_VALUE(float4(1, 1, 1, 1));

    ShadowMapAttribs ShadowAttribs;
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(LightAttribs);
#endif

struct CameraAttribs
{
    float4 f4Position;     // Camera world position
    float4 f4ViewportSize; // (width, height, 1/width, 1/height)

    float fNearPlaneZ; 
    float fFarPlaneZ;  // fNearPlaneZ < fFarPlaneZ
    float fNearPlaneDepth;
    float fFarPlaneDepth;
    
    float fHandness;   // +1.0 for right-handed coordinate system, -1.0 for left-handed
    uint  uiFrameIndex;
    float Padding0;
    float Padding1;
    
    // Distance to the point of focus
    float fFocusDistance DEFAULT_VALUE(10.0f); 
    // Ratio of the aperture (known as f-stop or f-number)
    float fFStop         DEFAULT_VALUE(5.6f);
    // Distance between the lens and the film in mm
    float fFocalLength   DEFAULT_VALUE(50.0f);
    // Sensor width in mm
    float fSensorWidth   DEFAULT_VALUE(36.0f);
    
    // Sensor height in mm
    float  fSensorHeight  DEFAULT_VALUE(24.0f);
    // 	Exposure adjustment as a log base-2 value.
    float  fExposure      DEFAULT_VALUE(0.0f);
    // TAA jitter
    float2 f2Jitter;
    
    float4x4 mView;
    float4x4 mProj;
    float4x4 mViewProj;
    float4x4 mViewInv;
    float4x4 mProjInv;
    float4x4 mViewProjInv;

    float4 f4ExtraData[5]; // Any appliation-specific data
};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(CameraAttribs);
#endif

#endif //_BASIC_STRUCTURES_FXH_
