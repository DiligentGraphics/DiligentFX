#ifndef _SPHERE_MAP_SAMPLING_FXH_
#define _SPHERE_MAP_SAMPLING_FXH_

// Samples one integer mip of an atlas-packed equirectangular image.
// MipRegion contains the image size (xy) and origin (zw), in mip-level texels.
// The sampler must use linear filtering; wrapping applies to the image, not the atlas.
float3 SampleSphereMapAtlasMip(Texture2DArray Atlas,
                               SamplerState   Atlas_sampler,
                               float2         UV,
                               float          Slice,
                               float          MipLevel,
                               float4         MipRegion,
                               float2         AtlasMipSize)
{
    float TexelX = UV.x * MipRegion.x - 0.5;
    float LeftX  = floor(TexelX);
    float Weight = TexelX - LeftX;
    float RightX = LeftX + 1.0;

    LeftX  = LeftX < 0.0 ? LeftX + MipRegion.x : LeftX;
    RightX = RightX >= MipRegion.x ? RightX - MipRegion.x : RightX;

    // Keep vertical filtering inside the image, including one-texel-high mips.
    float TexelY = clamp(UV.y * MipRegion.y, 0.5, MipRegion.y - 0.5);
    float2 LeftUV  = (MipRegion.zw + float2(LeftX + 0.5, TexelY)) / AtlasMipSize;
    float2 RightUV = (MipRegion.zw + float2(RightX + 0.5, TexelY)) / AtlasMipSize;

    float3 LeftColor  = Atlas.SampleLevel(Atlas_sampler, float3(LeftUV, Slice), MipLevel).rgb;
    float3 RightColor = Atlas.SampleLevel(Atlas_sampler, float3(RightUV, Slice), MipLevel).rgb;
    return lerp(LeftColor, RightColor, Weight);
}

// Preserves trilinear filtering while wrapping U and clamping V at each mip's
// image boundary. MipCount is the image's valid mip count, not the atlas's.
float3 SampleSphereMapAtlas(Texture2DArray Atlas,
                            SamplerState   Atlas_sampler,
                            float2         UV,
                            float          Slice,
                            float4         UVScaleBias,
                            float2         ImageSize,
                            float          MipLevel,
                            float          MipCount)
{
    uint2 AtlasSize;
    uint  SliceCount;
    Atlas.GetDimensions(AtlasSize.x, AtlasSize.y, SliceCount);

    uint2 ImageOrigin = uint2(floor(UVScaleBias.zw * float2(AtlasSize) + 0.5));
    uint2 ImageSizeU  = uint2(ImageSize);
    MipLevel = clamp(MipLevel, 0.0, MipCount - 1.0);
    uint FineMip = uint(MipLevel);

    // Match upload placement: dimensions and origins are shifted independently.
    // Scaling normalized coordinates alone is not exact for non-power-of-two sizes.
    float4 FineRegion = float4(float2(max(ImageSizeU >> FineMip, uint2(1, 1))),
                               float2(ImageOrigin >> FineMip));
    float2 FineAtlasSize = float2(max(AtlasSize >> FineMip, uint2(1, 1)));
    float3 FineColor = SampleSphereMapAtlasMip(Atlas, Atlas_sampler, UV, Slice,
                                              float(FineMip), FineRegion, FineAtlasSize);

    float MipWeight = MipLevel - float(FineMip);
    if (MipWeight == 0.0)
        return FineColor;

    uint CoarseMip = FineMip + 1u;
    float4 CoarseRegion = float4(float2(max(ImageSizeU >> CoarseMip, uint2(1, 1))),
                                 float2(ImageOrigin >> CoarseMip));
    float2 CoarseAtlasSize = float2(max(AtlasSize >> CoarseMip, uint2(1, 1)));
    float3 CoarseColor = SampleSphereMapAtlasMip(Atlas, Atlas_sampler, UV, Slice,
                                                float(CoarseMip), CoarseRegion, CoarseAtlasSize);
    return lerp(FineColor, CoarseColor, MipWeight);
}

#endif // _SPHERE_MAP_SAMPLING_FXH_
