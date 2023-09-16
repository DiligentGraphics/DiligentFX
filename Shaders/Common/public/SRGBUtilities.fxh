#ifndef _SRGB_UTILITIES_FXH_
#define _SRGB_UTILITIES_FXH_

float3 SRGBToLinear(float3 sRGB)
{
    float3 bLess = step(float3(0.04045, 0.04045, 0.04045), sRGB);
    return lerp(sRGB / 12.92,
                pow(saturate((sRGB + float3(0.055, 0.055, 0.055)) / 1.055), float3(2.4, 2.4, 2.4)),
                bLess);
}

float4 SRGBAToLinear(float4 sRGBA)
{
    return float4(SRGBToLinear(sRGBA.rgb), sRGBA.a);
}

float3 FastSRGBToLinear(float3 sRGB)
{
    return pow(sRGB, float3(2.2, 2.2, 2.2));
}

float4 FastSRGBAToLinear(float4 sRGBA)
{
    return float4(FastSRGBToLinear(sRGBA.rgb), sRGBA.a);
}

float3 LinearToSRGB(float3 RGB)
{
    float3 bGreater = step(float3(0.0031308, 0.0031308, 0.0031308), RGB);
    return lerp(RGB * 12.92,
                (pow(RGB, float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) * 1.055) - float3(0.055, 0.055, 0.055),
                bGreater);
}

float4 LinearToSRGBA(float4 RGBA)
{
    return float4(LinearToSRGB(RGBA.rgb), RGBA.a);
}

float3 FastLinearToSRGB(float3 RGB)
{
    return pow(RGB, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
}

float4 FastLinearToSRGBA(float4 RGBA)
{
    return float4(FastLinearToSRGB(RGBA.rgb), RGBA.a);
}

#endif // _SRGB_UTILITIES_FXH_
