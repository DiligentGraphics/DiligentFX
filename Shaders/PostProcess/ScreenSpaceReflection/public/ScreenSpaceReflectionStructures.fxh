#ifndef _SCREEN_SPACE_REFLECTION_STRUCTURES_FXH_
#define _SCREEN_SPACE_REFLECTION_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ShaderDefinitions.fxh"
#elif !defined(_SHADER_DEFINITIONS_FXH_)
#   error "Include ShaderDefinitions.fxh before including this file"
#endif

// Maximum mip level of depth buffer used in the Hi-Z tracing
#define SSR_DEPTH_HIERARCHY_MAX_MIP 6

// Number of samples on the Poisson disc used at the stage of spatial reconstruction
#define SSR_SPATIAL_RECONSTRUCTION_SAMPLES 8

// Parameter regulates from which level of roughness the maximum radius will be used at the stage of spatial reconstruction
#define SSR_SPATIAL_RECONSTRUCTION_ROUGHNESS_FACTOR 5

// Sets the sigma in Gaussian weighting for points on the Poisson disk at the stage of spatial reconstruction
#define SSR_SPATIAL_RECONSTRUCTION_SIGMA 0.9

// Determines the similarity threshold of depth between the current and previous frame to calculate disocclusion in the temporal accumulation step.
#define SSR_DISOCCLUSION_THRESHOLD 0.9

// Sets the value for the variance gamma in the temporal accumulation step
#define SSR_TEMPORAL_VARIANCE_GAMMA 2.5

// Defines the factor for edge-stopping function on world-space normals in the bilateral filtering step
#define SSR_BILATERAL_SIGMA_NORMAL 128.0

// Defines the factor for edge-stopping function on linear depth buffer in the bilateral filtering step
#define SSR_BILATERAL_SIGMA_DEPTH 1.0

// Defines the variance threshold at which the bilateral filtering should be launched
#define SSR_BILATERAL_VARIANCE_EXIT_THRESHOLD 0.00005

// Defines the variance threshold at which the maximum radius of the bilateral filter is used
#define SSS_BILATERAL_VARIANCE_ESTIMATE_THRESHOLD 0.001

// Parameter regulates from which level of roughness the maximum radius will be used at the stage of bilateral filtering
#define SSR_BILATERAL_ROUGHNESS_FACTOR 8

struct ScreenSpaceReflectionAttribs
{
    // A bias for accepting hits. Larger values may cause streaks, lower values may cause holes
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
