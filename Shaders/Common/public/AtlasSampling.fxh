#ifndef _ATLAS_SAMPLING_FXH_
#define _ATLAS_SAMPLING_FXH_

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

    /// Smooth texture coordinate gradient in x direction.
    float2 f2dSmoothUV_dx;

    /// Smooth texture coordinate gradient in y direction.
    float2 f2dSmoothUV_dy;

    /// The exponent factor that is applied to the LOD-based margin part:
    ///
    ///     f2LodMargin = 0.5 / f2AtlasDim * exp2(ceil(LOD) * Attribs.fLodMarginExpFactor);
    ///
    /// The default value is 1.0, but if textures in the atlas have significant
    /// non-power-of-two dimensions, larger factor may be necessary as higher
    /// mip levels will become considerably misaligned.
    float fLodMarginExpFactor; /* = 1.0 */

    /// The scaling factor that is applied to the gradient-based margin part:
    ///
    ///     f2GradientMargin = 0.5 * (abs(f2dUV_dx) + abs(f2dUV_dy)) * Attribs.fGradientMarginScale;
    ///
    /// The default value is 1.0, but if textures in the atlas have significant
    /// non-power-of-two dimensions, larger factor may be necessary.
    float fGradientMarginScale; /* = 1.0 */

    /// The dimension of the smallest mip level that contains valid data.
    /// For example, for a 4x4 block-compressed texture atlas, the dimension of the smallest
    /// level will be 4. This is because 4x4 blocks at higher mip levels require data from
    /// neighboring regions, which may not be available at the time the region is packed into
    /// the atlas.
    float fSmallestValidLevelDim; /* = 4.0 */
};


/// Samples texture atlas in a way that avoids artifacts at the texture region boundaries.

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

    // Calculate the texture LOD using smooth coordinates
    float LOD = Atlas.CalculateLevelOfDetail(Atlas_sampler, Attribs.f2SmoothUV);

    // Make sure that texture filtering does not use samples outside of the texture region.
    float2 f2AtlasDim;
    float  fElements;
    Atlas.GetDimensions(f2AtlasDim.x, f2AtlasDim.y, fElements);
    // The margin must be no less than half the pixel size in the selected LOD.
    float2 f2LodMargin = 0.5 / f2AtlasDim * exp2(ceil(LOD) * Attribs.fLodMarginExpFactor);


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
    float2 f2GradientMargin = 0.5 * (abs(f2dUV_dx) + abs(f2dUV_dy)) * Attribs.fGradientMarginScale;

    float2 f2Margin = f2LodMargin + f2GradientMargin;
    // Limit the margin by 1/2 of the texture region size to prevent boundaries from overlapping.
    f2Margin = min(f2Margin, f4UVRegion.xy * 0.5);

    // Clamp UVs using the margin.
    float2 f2UV = clamp(Attribs.f2UV,
                        f4UVRegion.zw + f2Margin,
                        f4UVRegion.zw + f4UVRegion.xy - f2Margin);

    // Compute the maximum gradient length in pixels
    float2 f2dIJ_dx = f2dUV_dx * f2AtlasDim.xy;
    float2 f2dIJ_dy = f2dUV_dy * f2AtlasDim.xy;
    float  fMaxGrad = max(length(f2dIJ_dx), length(f2dIJ_dy));

    // Compute the region's minimum dimension in pixels.
    float fMinRegionDim = min(f2AtlasDim.x * f4UVRegion.x, f2AtlasDim.y * f4UVRegion.y);
    // If the smallest valid level dimension is N, we should avoid the maximum gradient
    // becoming larger than fMinRegionDim/N.
    // This will guarantee that we will not sample levels above the smallest valid one.
    float fMaxGradLimit = Attribs.fSmallestValidLevelDim > 0.0 ? fMinRegionDim / Attribs.fSmallestValidLevelDim : 1e+6;

    // Rescale the gradients to avoid sampling above the level with the smallest valid dimension.
    float GradScale = min(1.0, fMaxGradLimit / fMaxGrad);
    f2dUV_dx *= GradScale;
    f2dUV_dy *= GradScale;
    float4 f4Color = Atlas.SampleGrad(Atlas_sampler, float3(f2UV, Attribs.fSlice), f2dUV_dx, f2dUV_dy);

    if (fMaxGrad > fMaxGradLimit)
    {
        // Manually compute the mean color from the coarsest available level.
        float LastValidLOD = log2(fMaxGradLimit);
        float4 f4MeanColor = (Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.25, 0.25) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD) +
                              Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.75, 0.25) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD) +
                              Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.25, 0.75) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD) +
                              Atlas.SampleLevel(Atlas_sampler, float3(f4UVRegion.zw + float2(0.75, 0.75) * f4UVRegion.xy, Attribs.fSlice), LastValidLOD)) *
                             0.25;

        // Smoothly fade-out to mean color when the gradient is in the range [fMaxGradLimit, fMaxGradLimit * 2.0]
        f4Color = lerp(f4Color, f4MeanColor, saturate((fMaxGrad - fMaxGradLimit) / fMaxGradLimit));
    }

    return f4Color;
}

#endif //_ATLAS_SAMPLING_FXH_
