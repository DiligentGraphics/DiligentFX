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
    float DepthBufferThickness               DEFAULT_VALUE(0.025f);
    float RoughnessThreshold                 DEFAULT_VALUE(0.2f);
    uint  MostDetailedMip                    DEFAULT_VALUE(0);
    uint  Padding0                           DEFAULT_VALUE(0);

    BOOL  IsRoughnessPerceptual              DEFAULT_VALUE(TRUE);
    uint  RoughnessChannel                   DEFAULT_VALUE(0);
    uint  MaxTraversalIntersections          DEFAULT_VALUE(128);
    float GGXImportanceSampleBias            DEFAULT_VALUE(0.3f);

    float SpatialReconstructionRadius        DEFAULT_VALUE(4.0f);
    float TemporalRadianceStabilityFactor    DEFAULT_VALUE(1.0f);
    float TemporalVarianceStabilityFactor    DEFAULT_VALUE(0.9f);
    float BilateralCleanupSpatialSigmaFactor DEFAULT_VALUE(0.9f);
};

#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(ScreenSpaceReflectionAttribs);
#endif

#endif //_SCREEN_SPACE_REFLECTION_STRUCTURES_FXH_
