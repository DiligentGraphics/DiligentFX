#ifndef _ATLAS_SAMPLING_FXH_
#define _ATLAS_SAMPLING_FXH_

// Full mip chain for a 16384x16384 texture, including the base level.
#define ATLAS_MAX_MIP_LEVEL_COUNT 15u

/// Attributes of the SampleTextureAtlas function
struct SampleTextureAtlasAttribs
{
    /// Sampling location in the texture atlas.
    ///
    /// \remarks    This is the final location, *not* relative
    ///             location within the region.
    float2 f2UV;

    /// Smooth texture coordinates that will be used to compute the level of detail.
    ///
    /// \remarks    When using texture atlas, texture coordinates may need to be
    ///             explicitly wrapped using the frac() function. If these
    ///             coordinates are used for the level-of-detail calculation, the
    ///             result will be incorrect at the texture region boundaries.
    ///             To avoid this, smooth texture coordinates are required.
    ///
    ///             Smooth coordinates are not used for sampling, only for
    ///             gradient calculation by the GPU. Therefore, they may
    ///             use arbitrary translation.
    float2 f2SmoothUV;

    /// Texture array slice.
    float fSlice;

    /// Texture region in the atlas:
    ///   - (x,y) - region size
    ///   - (z,w) - region offset
    float4 f4UVRegion;

    /// Smooth texture coordinate gradient in x direction (ddx(f2SmoothUV)).
    float2 f2dSmoothUV_dx;

    /// Smooth texture coordinate gradient in y direction (ddy(f2SmoothUV)).
    float2 f2dSmoothUV_dy;

    /// The dimension of the smallest mip level that contains valid data.
    /// For example, for a 4x4 block-compressed texture atlas, the dimension of the smallest
    /// level will be 4. This is because 4x4 blocks at higher mip levels require data from
    /// neighboring regions, which may not be available at the time the region is packed into
    /// the atlas.
    float fSmallestValidLevelDim; /* = 4.0 */

    /// Number of consecutive uploaded mip levels for the region, starting at mip zero.
    /// Must be between 1 and ATLAS_MAX_MIP_LEVEL_COUNT. Use ATLAS_MAX_MIP_LEVEL_COUNT
    /// to apply only the existing region-based sampling limits.
    /// Explicit counts derive the LOD limit from the region dimensions and uploaded
    /// mip count, assuming a full atlas mip chain in the texture view and no additional
    /// sampler LOD clamps. The count must be uniform across invocations in a fragment quad.
    uint MipLevelCount;

    /// Indicates if the texture data is non-filterable (e.g. material indices).
    bool IsNonFilterable;
    
    /// Maximum anisotropy.
    ///
    /// \remarks    This value is only used on GLES where textureQueryLod is not
    ///             supported and we have to manually compute the LOD.
    float fMaxAnisotropy;
};


/// Samples texture atlas in a way that avoids artifacts at the texture region boundaries.
/// This function is intended to be used with the dynamic texture atlas (IDynamicTextureAtlas).

/// \param [in] Atlas         - Texture atlas.
/// \param [in] Atlas_sampler - Sampler state for the texture atlas.
/// \param [in] Attribs       - Texture atlas sampling attributes.
float4 SampleTextureAtlas(Texture2DArray            Atlas,
                          SamplerState              Atlas_sampler,
                          SampleTextureAtlasAttribs Attribs)
{
    // Properly handle upside-down and mirrored regions.
    float4 f4UVRegion;
    f4UVRegion.xy = abs(Attribs.f4UVRegion.xy);
    f4UVRegion.zw = min(Attribs.f4UVRegion.zw, Attribs.f4UVRegion.zw + Attribs.f4UVRegion.xy);

    float2 f2dUV_dx = Attribs.f2dSmoothUV_dx;
    float2 f2dUV_dy = Attribs.f2dSmoothUV_dy;

    float2 f2AtlasDim;
    float  fElements;
    Atlas.GetDimensions(f2AtlasDim.x, f2AtlasDim.y, fElements);

    float2 f2RegionDim      = f2AtlasDim * f4UVRegion.xy;
    float  fLastUploadedLOD = float(Attribs.MipLevelCount - 1u);
    if (Attribs.MipLevelCount < ATLAS_MAX_MIP_LEVEL_COUNT)
    {
        float fMaxRegionDim = max(f2RegionDim.x, f2RegionDim.y);

        // Reconstructing integer texel dimensions from normalized UVs may produce a value
        // slightly below the original (e.g. 127.99999 instead of 128). Round first so
        // floor(log2(...)) does not underestimate the last mip level by one.
        fMaxRegionDim = round(fMaxRegionDim);
        fMaxRegionDim = max(fMaxRegionDim, 1.0);

        float fLastRegionLOD = floor(log2(fMaxRegionDim));
        fLastUploadedLOD = min(fLastUploadedLOD, fLastRegionLOD);
    }
    
    // Compute the maximum gradient length in pixels
    float2 f2GradX   = f2dUV_dx * f2AtlasDim;
    float2 f2GradY   = f2dUV_dy * f2AtlasDim;
    float  fGradXSqr = dot(f2GradX, f2GradX);
    float  fGradYSqr = dot(f2GradY, f2GradY);
    float  fMaxGrad  = max(sqrt(max(fGradXSqr, fGradYSqr)), 1e-5);
    float  fMinGrad  = max(sqrt(min(fGradXSqr, fGradYSqr)), 1e-5); // Only used on GLES and WebGPU
    
    float LOD;
#if !defined(GL_ES) && !defined(WEBGPU)
    if (Attribs.MipLevelCount < ATLAS_MAX_MIP_LEVEL_COUNT)
    {
        // Radient uses the region/upload limit with an unrestricted sampler LOD range.
        // Use the unclamped footprint to reduce gradients only when that limit is exceeded.
        LOD = Atlas.CalculateLevelOfDetailUnclamped(Atlas_sampler, Attribs.f2SmoothUV);
    }
    else
    {
        // Preserve sampler and texture-view limits when no uploaded-mip limit is supplied.
        LOD = Atlas.CalculateLevelOfDetail(Atlas_sampler, Attribs.f2SmoothUV);
    }
#else
    {
        // textureQueryLod is not supported even in GLES3.2.
        // Follow Section 8.14 (Texture Minification) from OpenGL4.6 spec.
        float Aniso = min(fMaxGrad / fMinGrad, Attribs.fMaxAnisotropy);
        LOD = log2(fMaxGrad / Aniso);
    }
#endif
    float fMipGradScale = exp2(min(fLastUploadedLOD - LOD, 0.0));
    LOD = clamp(LOD, 0.0, fLastUploadedLOD);

    // Make sure that texture filtering does not use samples outside of the texture region.
    // The margin must be no less than half the pixel size in the selected LOD.
    float2 f2LodMargin = 0.5 / f2AtlasDim * exp2(ceil(LOD));


    // Use gradients to make sure that the sampling area does not
    // go beyond the texture region.
    //  ____________________                     ________
    // |               .'.  |                  .'.       A
    // |             .'  .' |                .'  .'      |
    // |           .' *.'   |              .'  .'  |     | abs(f2dUV_dx.y) + abs(f2dUV_dy.y)
    // |          '. .'     |            .'  .'    |     |
    // |            '       |           | '.'______|_____V
    // |                    |           |          |
    // |____________________|            <-------->
    //                                       abs(f2dUV_dx.x) + abs(f2dUV_dy.x)
    //
    float2 f2GradientMargin = 0.5 * (abs(f2dUV_dx) + abs(f2dUV_dy)) * fMipGradScale;

    float2 f2Margin = f2LodMargin + f2GradientMargin;
    // Limit the margin by 1/2 of the texture region size to prevent boundaries from overlapping.
    f2Margin = min(f2Margin, f4UVRegion.xy * 0.5);

    // Clamp UVs using the margin.
    float2 f2UV = clamp(Attribs.f2UV,
                        f4UVRegion.zw + f2Margin,
                        f4UVRegion.zw + f4UVRegion.xy - f2Margin);

    // Note that dynamic texture atlas aligns allocations using the minimum dimension.
    // This guarantees that filtering will not sample from the neighboring regions in all levels
    // up to the one where the smallest dimension becomes max(1, Attribs.fSmallestValidLevelDim).
    //
    //    8   [ * ]
    //        |
    //   16   [ *  * ]
    //        |
    //   32   [ *  *  *  * ]
    //        |
    //   64   [ *  *  *  *  *  *  *  * ]
    //    |
    // Aligned placement

    // Compute the region's minimum dimension in pixels.
    float fMinRegionDim = min(f2RegionDim.x, f2RegionDim.y);
    // Avoid division by zero
    fMinRegionDim = max(fMinRegionDim, 1.0);
    // If the smallest valid level dimension is N, we should avoid the maximum gradient
    // becoming larger than fMinRegionDim/N.
    // This will guarantee that we will not sample levels above the smallest valid one.
    // Clamp the smallest valid level dimension to 2.0 so that we fully fade-out to mean
    // color when the gradient becomes equal the min dimension.
    float fSmallestValidLevelDim = max(Attribs.fSmallestValidLevelDim, 2.0);
    float fMaxGradLimit = fMinRegionDim / fSmallestValidLevelDim;

    // Smoothly fade-out to mean color when the gradient is in the range [fMaxGradLimit, fMaxGradLimit * 2.0].
    // Keep this threshold based on region size so a missing mip tail does not make the fade start earlier.
    float fMeanColorFadeoutFactor = Attribs.IsNonFilterable ? 0.0 : saturate((fMaxGrad - fMaxGradLimit) / fMaxGradLimit);
    float4 f4Color = float4(0.0, 0.0, 0.0, 0.0);

    if (fMeanColorFadeoutFactor < 1.0)
    {
        // Rescale the gradients to respect both region boundaries and the uploaded mip count.
        float GradScale = min(fMipGradScale, fMaxGradLimit / max(fMaxGrad, 1e-5));
        f2dUV_dx *= GradScale;
        f2dUV_dy *= GradScale;
        f4Color = Atlas.SampleGrad(Atlas_sampler, float3(f2UV, Attribs.fSlice), f2dUV_dx, f2dUV_dy);
    }

    if (fMeanColorFadeoutFactor > 0.0)
    {
        // Manually compute the mean color from the coarsest available level.
        float LastValidLOD = min(log2(fMaxGradLimit), fLastUploadedLOD);
        float4 f4MeanColor = (Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.25, 0.25) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD) +
                              Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.75, 0.25) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD) +
                              Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.25, 0.75) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD) +
                              Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.75, 0.75) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD)) *
                             0.25;

        f4Color = lerp(f4Color, f4MeanColor, fMeanColorFadeoutFactor);
    }

    return f4Color;
}

#endif //_ATLAS_SAMPLING_FXH_
