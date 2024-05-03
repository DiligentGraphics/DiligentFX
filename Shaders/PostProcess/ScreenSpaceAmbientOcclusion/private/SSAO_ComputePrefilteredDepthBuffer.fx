#include "SSAO_Common.fxh"
#include "BasicStructures.fxh"
#include "ScreenSpaceAmbientOcclusionStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbScreenSpaceAmbientOcclusionAttribs
{
    ScreenSpaceAmbientOcclusionAttribs g_SSAOAttribs;
}

#if SUPPORTED_SHADER_SRV
Texture2D<float> g_TextureLastMip;
#else
Texture2D<float> g_TextureMips;
SamplerState     g_TextureMips_sampler;
#endif

#if SUPPORTED_SHADER_SRV
float SampleDepth(int2 Location, int2 Offset, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location + Offset, Dimension.xy);
    return g_TextureLastMip.Load(int3(Position, 0));
}
#else
float SampleDepth(int2 Location, int2 Offset, int3 Dimension)
{
    int2 Position = ClampScreenCoord(Location + Offset, Dimension.xy);
    return g_TextureMips.Load(int3(Position, Dimension.z));
}
#endif

float SampleDepthViewSpace(int2 Location, int2 Offset, int3 Dimension)
{
    return DepthToCameraZ(SampleDepth(Location, Offset, Dimension), g_Camera.mProj);
}

float ComputeDepthMIPFiltered(in float SampledDepth[9], uint Count)
{
    float WeightDepth = SampledDepth[0];
    {
        for (uint Idx = 1u; Idx < Count; Idx++)
            WeightDepth = min(WeightDepth, SampledDepth[Idx]);
    }
    
    float DepthRangeScaleFactor = 0.75; // Found empirically :)
    float EffectRadius = DepthRangeScaleFactor * g_SSAOAttribs.EffectRadius * g_SSAOAttribs.RadiusMultiplier;
    float FalloffRange = g_SSAOAttribs.EffectFalloffRange * EffectRadius;

    // Fadeout precompute optimisation
    float FalloffFrom = EffectRadius - FalloffRange;
    float FalloffMul = -1.0f / (FalloffRange);
    float FalloffAdd = FalloffFrom / FalloffRange + 1.0f;

    float DepthSum = 0.0f;
    float WeightSum = 0.0f;
    {
        for (uint Idx = 0u; Idx < Count; Idx++)
        {
            float Weight = saturate(abs(WeightDepth - SampledDepth[Idx]) * FalloffMul + FalloffAdd);
            DepthSum += Weight * SampledDepth[Idx];
            WeightSum += Weight;
        }
    }
    
    return DepthSum / WeightSum;
}

void ArrayAppend(float Element, inout float Array[9], inout uint Index)
{
    Array[Index] = Element;
    Index++;
}

float ComputePrefilteredDepthBufferPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    int3 LastMipDimension;
#if SUPPORTED_SHADER_SRV
    g_TextureLastMip.GetDimensions(LastMipDimension.x, LastMipDimension.y);
#else
    int Dummy;
    g_TextureMips.GetDimensions(0, LastMipDimension.x, LastMipDimension.y, Dummy);
    LastMipDimension.x = int(floor(float(LastMipDimension.x) / exp2(float(VSOut.uInstID))));
    LastMipDimension.y = int(floor(float(LastMipDimension.y) / exp2(float(VSOut.uInstID))));
    LastMipDimension.z = int(VSOut.uInstID);
#endif

    int2 RemappedPosition = int2(2.0 * floor(VSOut.f4PixelPos.xy));

    uint SampleCount = 0u;
    float SampledPixels[9];
    ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(0, 0), LastMipDimension), SampledPixels, SampleCount);
    ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(0, 1), LastMipDimension), SampledPixels, SampleCount);
    ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(1, 0), LastMipDimension), SampledPixels, SampleCount);
    ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(1, 1), LastMipDimension), SampledPixels, SampleCount);

    bool IsWidthOdd  = (LastMipDimension.x & 1) != 0;
    bool IsHeightOdd = (LastMipDimension.y & 1) != 0;
    
    if (IsWidthOdd)
    {
        ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(2, 0), LastMipDimension), SampledPixels, SampleCount);
        ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(2, 1), LastMipDimension), SampledPixels, SampleCount);
    }
    
    if (IsHeightOdd)
    {
        ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(0, 2), LastMipDimension), SampledPixels, SampleCount);
        ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(1, 2), LastMipDimension), SampledPixels, SampleCount);
    }
    
    if (IsWidthOdd && IsHeightOdd)
    {
        ArrayAppend(SampleDepthViewSpace(RemappedPosition, int2(2, 2), LastMipDimension), SampledPixels, SampleCount);
    }

    return saturate(CameraZToDepth(ComputeDepthMIPFiltered(SampledPixels, SampleCount), g_Camera.mProj));
}
