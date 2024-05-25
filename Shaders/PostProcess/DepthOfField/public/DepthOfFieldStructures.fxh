#ifndef _DEPTH_OF_FIELD_STRUCTURES_FXH_
#define _DEPTH_OF_FIELD_STRUCTURES_FXH_

#ifndef __cplusplus
#   include "ShaderDefinitions.fxh"
#elif !defined(_SHADER_DEFINITIONS_FXH_)
#   error "Include ShaderDefinitions.fxh before including this file"
#endif

#define DOF_TEMPORAL_VARIANCE_GAMMA         2.5

// Number of rings in the bokeh kernel for flood-filling.
#define DOF_BOKEH_KERNEL_SMALL_RING_COUNT   3

// Number of samples within each ring of the bokeh kernel for flood-filling.
#define DOF_BOKEH_KERNEL_SMALL_RING_DENSITY 5

// Gaussian kernel radius for blurring the dilated CoC texture
#define DOF_GAUSS_KERNEL_RADIUS             6

// Gaussian kernel sigma for blurring the dilated CoC texture
#define DOF_GAUSS_KERNEL_SIGMA              5.0

// Macro for selecting the direction of CoC blur
#define DOF_CIRCLE_OF_CONFUSION_BLUR_X      0

// Macro for selecting the direction of CoC blur
#define DOF_CIRCLE_OF_CONFUSION_BLUR_Y      1


struct DepthOfFieldAttribs
{
    // This is the maximum size of CoC in texture coordinates for a pixel.
    // This parameter affects the strength of the bokeh effect.
    // In reality, such a parameter does not exist, but unfortunately, we have to use it for performance reasons.
    float MaxCircleOfConfusion    DEFAULT_VALUE(0.01f);

    // This parameter is used to control the stability of the temporal accumulation of the CoC.
    float TemporalStabilityFactor DEFAULT_VALUE(0.9375f);

    // The number of rings in the Octaweb kernel
    int   BokehKernelRingCount    DEFAULT_VALUE(5);

    // The number of samples within each ring of the Octaweb kernel.
    int   BokehKernelRingDensity  DEFAULT_VALUE(7);

};
#ifdef CHECK_STRUCT_ALIGNMENT
    CHECK_STRUCT_ALIGNMENT(DepthOfFieldAttribs);
#endif

#endif // _DEPTH_OF_FIELD_STRUCTURES_FXH_
