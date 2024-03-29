#ifndef _SCREEN_SPACE_REFLECTION_STRUCTURES_FXH_
#define _SCREEN_SPACE_REFLECTION_STRUCTURES_FXH_

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
        // Note that defining empty macros causes GL shader compilation error on Mac, because
        // it does not allow standalone semicolons outside of main.
        // On the other hand, adding semicolon at the end of the macro definition causes gcc error.
#       define CHECK_STRUCT_ALIGNMENT(s) static_assert( sizeof(s) % 16 == 0, "sizeof(" #s ") is not multiple of 16" )
#   endif

#   ifndef DEFAULT_VALUE
#       define DEFAULT_VALUE(x) =x
#   endif

#else

#   ifndef BOOL
#       define BOOL bool
#   endif

#   ifndef DEFAULT_VALUE
#       define DEFAULT_VALUE(x)
#   endif

#endif

#define SSR_DEPTH_HIERARCHY_MAX_MIP 6

#define SSR_SPATIAL_RECONSTRUCTION_SAMPLES 8

#define SSR_SPATIAL_RECONSTRUCTION_ROUGHNESS_FACTOR 5

#define SSR_SPATIAL_RECONSTRUCTION_SIGMA 0.9

#define SSR_DISOCCLUSION_DEPTH_WEIGHT 1.0

#define SSR_DISOCCLUSION_THRESHOLD 0.9

#define SSR_TEMPORAL_STANDARD_DEVIATION_SCALE 2.5

#define SSR_BILATERAL_SIGMA_NORMAL 128.0

#define SSR_BILATERAL_SIGMA_DEPTH 1.0

#define SSR_BILATERAL_VARIANCE_EXIT_THRESHOLD 0.00005

#define SSS_BILATERAL_VARIANCE_ESTIMATE_THRESHOLD 0.001

#define SSR_BILATERAL_ROUGHNESS_FACTOR 8

struct ScreenSpaceReflectionAttribs
{
    // A bias for accepting hits. Larger values can cause streaks, lower values can cause holes
    float DepthBufferThickness               DEFAULT_VALUE(0.025f);
    
    // Regions with a roughness value greater than this threshold won't spawn rays"
    float RoughnessThreshold                 DEFAULT_VALUE(0.2f);
    
    // The most detailed MIP map level in the depth hierarchy. Perfect mirrors always use 0 as the most detailed level
    uint  MostDetailedMip                    DEFAULT_VALUE(0);
    
    // Padding 0
    uint  Padding0                           DEFAULT_VALUE(0);

    // A boolean to describe the space used to store roughness in the materialParameters texture.
    BOOL  IsRoughnessPerceptual              DEFAULT_VALUE(TRUE);
    
    // The channel to read the roughness from the materialParameters texture
    uint  RoughnessChannel                   DEFAULT_VALUE(0);
    
    // Caps the maximum number of lookups that are performed from the depth buffer hierarchy. Most rays should terminate after approximately 20 lookups
    uint  MaxTraversalIntersections          DEFAULT_VALUE(128);
    
    // This parameter is aimed at reducing noise by modify sampling in the ray tracing stage. Increasing the value increases the deviation from the ground truth but reduces the noise
    float GGXImportanceSampleBias            DEFAULT_VALUE(0.3f);

    // The value controls the kernel size in the spatial reconstruction step. Increasing the value increases the deviation from the ground truth but reduces the noise
    float SpatialReconstructionRadius        DEFAULT_VALUE(4.0f);
    
    // A factor to control the accmulation of history values of radiance buffer. Higher values reduce noise, but are more likely to exhibit ghosting artefacts
    float TemporalRadianceStabilityFactor    DEFAULT_VALUE(1.0f);
    
    // A factor to control the accmulation of history values of variance buffer. Higher values reduce noise, but are more likely to exhibit ghosting artefacts
    float TemporalVarianceStabilityFactor    DEFAULT_VALUE(0.9f);
    
    // This parameter represents the standard deviation in the Gaussian kernel, which forms the spatial component of the bilateral filter
    float BilateralCleanupSpatialSigmaFactor DEFAULT_VALUE(0.9f);
};

#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(ScreenSpaceReflectionAttribs);
#endif

#endif //_SCREEN_SPACE_REFLECTION_STRUCTURES_FXH_
