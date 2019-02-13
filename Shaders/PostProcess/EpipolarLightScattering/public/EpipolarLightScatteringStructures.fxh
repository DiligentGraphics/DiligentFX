#ifndef _EPIPOLAR_LIGHT_SCATTERING_STRCUTURES_FXH_
#define _EPIPOLAR_LIGHT_SCATTERING_STRCUTURES_FXH_

#define PI 3.1415928f

#ifdef __cplusplus

#   ifndef BOOL
#      define BOOL int32_t // Do not use bool, because sizeof(bool)==1 !
#   endif

#   ifndef TRUE
#      define TRUE 1
#   endif

#   ifndef FALSE
#      define FALSE 0
#   endif

#   ifndef CHECK_STRUCT_ALIGNMENT
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" );
#   endif

#   ifndef DEFAULT_VALUE
#       define DEFAULT_VALUE(x) =x
#   endif

#else

#   ifndef BOOL
#       define BOOL bool
#   endif

#   ifndef CHECK_STRUCT_ALIGNMENT
#       define CHECK_STRUCT_ALIGNMENT(s)
#   endif

#   ifndef DEFAULT_VALUE
#       define DEFAULT_VALUE(x)
#   endif

#endif


#define LIGHT_SCTR_TECHNIQUE_EPIPOLAR_SAMPLING 0
#define LIGHT_SCTR_TECHNIQUE_BRUTE_FORCE 1

#define CASCADE_PROCESSING_MODE_SINGLE_PASS 0
#define CASCADE_PROCESSING_MODE_MULTI_PASS 1
#define CASCADE_PROCESSING_MODE_MULTI_PASS_INST 2

#define REFINEMENT_CRITERION_DEPTH_DIFF 0
#define REFINEMENT_CRITERION_INSCTR_DIFF 1

// Extinction evaluation mode used when attenuating background
#define EXTINCTION_EVAL_MODE_PER_PIXEL 0// Evaluate extinction for each pixel using analytic formula 
                                        // by Eric Bruneton
#define EXTINCTION_EVAL_MODE_EPIPOLAR 1 // Render extinction in epipolar space and perform
                                        // bilateral filtering in the same manner as for
                                        // inscattering

#define SINGLE_SCTR_MODE_NONE 0
#define SINGLE_SCTR_MODE_INTEGRATION 1
#define SINGLE_SCTR_MODE_LUT 2

#define MULTIPLE_SCTR_MODE_NONE 0
#define MULTIPLE_SCTR_MODE_UNOCCLUDED 1
#define MULTIPLE_SCTR_MODE_OCCLUDED 2

#define TONE_MAPPING_MODE_EXP 0
#define TONE_MAPPING_MODE_REINHARD 1
#define TONE_MAPPING_MODE_REINHARD_MOD 2
#define TONE_MAPPING_MODE_UNCHARTED2 3
#define TONE_MAPPING_FILMIC_ALU 4
#define TONE_MAPPING_LOGARITHMIC 5
#define TONE_MAPPING_ADAPTIVE_LOG 6


struct PostProcessingAttribs
{
    uint m_uiNumEpipolarSlices              DEFAULT_VALUE(512);
    uint m_uiMaxSamplesInSlice              DEFAULT_VALUE(256);
    uint m_uiInitialSampleStepInSlice       DEFAULT_VALUE(16);
    // Note that sampling near the epipole is very cheap since only a few steps
    // required to perform ray marching
    uint m_uiEpipoleSamplingDensityFactor   DEFAULT_VALUE(2);

    float m_fRefinementThreshold            DEFAULT_VALUE(0.03f);
    // do not use bool, because sizeof(bool)==1 and as a result bool variables
    // will be incorrectly mapped on GPU constant buffer
    BOOL m_bShowSampling                    DEFAULT_VALUE(FALSE); 
    BOOL m_bCorrectScatteringAtDepthBreaks  DEFAULT_VALUE(FALSE); 
    BOOL m_bShowDepthBreaks                 DEFAULT_VALUE(FALSE); 

    BOOL m_bShowLightingOnly                DEFAULT_VALUE(FALSE);
    BOOL m_bOptimizeSampleLocations         DEFAULT_VALUE(TRUE);
    BOOL m_bEnableLightShafts               DEFAULT_VALUE(TRUE);
    uint m_uiInstrIntegralSteps             DEFAULT_VALUE(30);
    
    float2 m_f2ShadowMapTexelSize           DEFAULT_VALUE(float2(0,0));
    uint m_uiShadowMapResolution            DEFAULT_VALUE(0);
    uint m_uiMinMaxShadowMapResolution      DEFAULT_VALUE(0);

    BOOL m_bUse1DMinMaxTree                 DEFAULT_VALUE(TRUE);
    float m_fMaxShadowMapStep               DEFAULT_VALUE(16.f);
    float m_fMiddleGray                     DEFAULT_VALUE(0.18f);
    uint m_uiLightSctrTechnique             DEFAULT_VALUE(LIGHT_SCTR_TECHNIQUE_EPIPOLAR_SAMPLING);

    int m_iNumCascades                      DEFAULT_VALUE(0);
    int m_iFirstCascade                     DEFAULT_VALUE(2);
    float m_fNumCascades                    DEFAULT_VALUE(0);
    float m_fFirstCascade                   DEFAULT_VALUE(1);

    uint m_uiCascadeProcessingMode          DEFAULT_VALUE(CASCADE_PROCESSING_MODE_SINGLE_PASS);
    uint m_uiRefinementCriterion            DEFAULT_VALUE(REFINEMENT_CRITERION_INSCTR_DIFF);
    BOOL m_bIs32BitMinMaxMipMap             DEFAULT_VALUE(FALSE);
    uint m_uiMultipleScatteringMode         DEFAULT_VALUE(MULTIPLE_SCTR_MODE_UNOCCLUDED);

    uint m_uiSingleScatteringMode           DEFAULT_VALUE(SINGLE_SCTR_MODE_INTEGRATION);
    BOOL m_bAutoExposure                    DEFAULT_VALUE(TRUE);
    uint m_uiToneMappingMode                DEFAULT_VALUE(TONE_MAPPING_MODE_UNCHARTED2);
    BOOL m_bLightAdaptation                 DEFAULT_VALUE(TRUE);

    float m_fWhitePoint                     DEFAULT_VALUE(3.f);
    float m_fLuminanceSaturation            DEFAULT_VALUE(1.f);
    float2 f2Dummy                          DEFAULT_VALUE(float2(0,0));
    
    uint m_uiExtinctionEvalMode             DEFAULT_VALUE(EXTINCTION_EVAL_MODE_EPIPOLAR);
    BOOL m_bUseCustomSctrCoeffs             DEFAULT_VALUE(FALSE);
    float m_fAerosolDensityScale            DEFAULT_VALUE(1.f);
    float m_fAerosolAbsorbtionScale         DEFAULT_VALUE(0.1f);

    float4 m_f4CustomRlghBeta               DEFAULT_VALUE(float4(5.8e-6f, 13.5e-6f, 33.1e-6f, 0.f));
    float4 m_f4CustomMieBeta                DEFAULT_VALUE(float4(2.e-5f, 2.e-5f, 2.e-5f, 0.f));


    // Members below are automatically set by the effect. User-provided values are ignored
    float4 m_f4ScreenResolution             DEFAULT_VALUE(float4(0,0,0,0));
    float4 f4LightScreenPos                 DEFAULT_VALUE(float4(0,0,0,0));
    BOOL   bIsLightOnScreen                 DEFAULT_VALUE(FALSE);
    int    Padding0                         DEFAULT_VALUE(0);
    int    Padding1                         DEFAULT_VALUE(0);
    int    Padding2                         DEFAULT_VALUE(0);
};
CHECK_STRUCT_ALIGNMENT(PostProcessingAttribs)


struct AirScatteringAttribs
{
    // Angular Rayleigh scattering coefficient contains all the terms exepting 1 + cos^2(Theta):
    // Pi^2 * (n^2-1)^2 / (2*N) * (6+3*Pn)/(6-7*Pn)
    float4 f4AngularRayleighSctrCoeff;
    // Total Rayleigh scattering coefficient is the integral of angular scattering coefficient in all directions
    // and is the following:
    // 8 * Pi^3 * (n^2-1)^2 / (3*N) * (6+3*Pn)/(6-7*Pn)
    float4 f4TotalRayleighSctrCoeff;
    float4 f4RayleighExtinctionCoeff;

    // Note that angular scattering coefficient is essentially a phase function multiplied by the
    // total scattering coefficient
    float4 f4AngularMieSctrCoeff;
    float4 f4TotalMieSctrCoeff;
    float4 f4MieExtinctionCoeff;

    float4 f4TotalExtinctionCoeff;
    // Cornette-Shanks phase function (see Nishita et al. 93) normalized to unity has the following form:
    // F(theta) = 1/(4*PI) * 3*(1-g^2) / (2*(2+g^2)) * (1+cos^2(theta)) / (1 + g^2 - 2g*cos(theta))^(3/2)
    float4 f4CS_g; // x == 3*(1-g^2) / (2*(2+g^2))
                   // y == 1 + g^2
                   // z == -2*g

    float fEarthRadius              DEFAULT_VALUE(6360000.f);
    float fAtmTopHeight             DEFAULT_VALUE(80000.f);
    float2 f2ParticleScaleHeight    DEFAULT_VALUE(float2(7994.f, 1200.f));
    
    float fTurbidity                DEFAULT_VALUE(1.02f);
    float fAtmTopRadius             DEFAULT_VALUE(fEarthRadius + fAtmTopHeight);
    float m_fAerosolPhaseFuncG      DEFAULT_VALUE(0.76f);
    float m_fDummy;
};
CHECK_STRUCT_ALIGNMENT(AirScatteringAttribs)


struct MiscDynamicParams
{
    float fMaxStepsAlongRay;   // Maximum number of steps during ray tracing
    float fCascadeInd;
    float fElapsedTime;
    float fDummy;

#ifdef __cplusplus
    uint ui4SrcMinMaxLevelXOffset;
    uint ui4SrcMinMaxLevelYOffset;
    uint ui4DstMinMaxLevelXOffset;
    uint ui4DstMinMaxLevelYOffset;
#else
    uint4 ui4SrcDstMinMaxLevelOffset;
#endif
};
CHECK_STRUCT_ALIGNMENT(MiscDynamicParams)

#endif //_EPIPOLAR_LIGHT_SCATTERING_STRCUTURES_FXH_
