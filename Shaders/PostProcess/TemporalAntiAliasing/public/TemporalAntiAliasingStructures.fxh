#ifndef _TEMPORAL_ANTI_ALIASING_STRUCTURES_FXH_
#define _TEMPORAL_ANTI_ALIASING_STRUCTURES_FXH_

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


// This parameter sets the minimum value for the variance gamma.
// The variance gamma is used to adjust the influence of historical data in the anti-aliasing process.
// A lower value means that the algorithm is less influenced by past frames, making it more responsive to changes but potentially less smooth
#define TAA_MIN_VARIANCE_GAMMA           0.75 

// This parameter sets the maximum value for the variance gamma.
// A higher maximum value allows the algorithm to rely more heavily on historical data,
// which can produce smoother results but may also introduce more motion blur or ghosting effects in fast-moving scenes.
#define TAA_MAX_VARIANCE_GAMMA           2.5

// This parameter defines the threshold for pixel velocity difference that determines whether a pixel is considered to have "no history."
// If the difference in motion vectors between the current frame and the previous frame exceeds this value, the pixel is treated as if it has no historical data.
// This helps to prevent ghosting effects by not blending pixels with significantly different motion vectors.
#define TAA_MOTION_VECTOR_PIXEL_DIFF     8.0

// This parameter sets the threshold for depth disocclusion. It is used to determine how much a change in depth between frames should be considered as disocclusion,
// which occurs when previously occluded objects become visible. A small threshold value means that only significant depth changes will be treated as disocclusion,
// which can help in maintaining the stability of the image but may ignore some smaller, yet visually important changes.
#define TAA_DEPTH_DISOCCLUSION_THRESHOLD 0.9

// This parameter sets the max "distance" between source colour and target colour.
// Setting this to a larger value allows more bright pixels from the history buffer to be leaved unchanged.
#define TAA_VARIANCE_INTERSECTION_MAX_T  100.0

struct TemporalAntiAliasingAttribs
{
    // The value is responsible for interpolating between the current and previous frame.
    // Increasing the value increases temporal stability but may introduce ghosting
    float TemporalStabilityFactor    DEFAULT_VALUE(0.9375f);
    // If this parameter is set to true,
    // the current frame will be written to the current history buffer without interpolation with the previous history buffer
    BOOL  ResetAccumulation          DEFAULT_VALUE(FALSE);
    BOOL  SkipRejection              DEFAULT_VALUE(FALSE);
    float Padding1                   DEFAULT_VALUE(0.0);
};

#endif //_TEMPORAL_ANTI_ALIASING_STRUCTURES_FXH_
