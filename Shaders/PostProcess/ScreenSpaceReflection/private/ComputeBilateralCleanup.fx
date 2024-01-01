#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float>  g_TextureDepth;
Texture2D<float3> g_TextureNormal;
Texture2D<float>  g_TextureRoughness;

Texture2DArray<float4> g_TextureRadiance;
Texture2DArray<float>  g_TextureVariance;


float SampleDepth(uint2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float SampleRoughness(uint2 PixelCoord)
{
    return g_TextureRoughness.Load(int3(PixelCoord, 0));
}

float3 SampleNormalWS(uint2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float4 SampleRadiance(uint2 PixelCoord)
{
    return g_TextureRadiance.Load(int4(PixelCoord, 0, 0));
}

float SampleVariance(uint2 PixelCoord)
{
    return g_TextureVariance.Load(int4(PixelCoord, 0, 0));
}

bool IsReflectionSample(float Roughness, float Depth)
{
    return Roughness < g_SSRAttribs.RoughnessThreshold && !IsBackground(Depth);
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
float4 ComputeBilateralCleanupPS(in float4 Position : SV_Position) : SV_Target0
{
    const float  Roughness   = SampleRoughness(uint2(Position.xy));
    const float  Variance    = SampleVariance(uint2(Position.xy));
    const float3 NormalWS    = SampleNormalWS(uint2(Position.xy));
    const float  LinearDepth = DepthToCameraZ(SampleDepth(uint2(Position.xy)), g_SSRAttribs.ProjMatrix);
    const float2 GradDepth   = float2(ddx(LinearDepth), ddy(LinearDepth));

    const float Radius = lerp(0.0, Variance > SSS_BILATERAL_VARIANCE_ESTIMATE_THRESHOLD ? 2.0 : 0, saturate(SSR_BILATERAL_ROUGHNESS_FACTOR * sqrt(Roughness)));
    const float Sigma = g_SSRAttribs.BilateralCleanupSpatialSigmaFactor;
    const int EffectiveRadius = int(min(2.0 * Sigma, Radius));
    float4 RadianceResult = SampleRadiance(uint2(Position.xy));

    if (Variance > SSR_BILATERAL_VARIANCE_EXIT_THRESHOLD && EffectiveRadius > 0)
    {
        float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);
        float WeightSum = 0.0f;
        for (int x = -EffectiveRadius; x <= EffectiveRadius; x++)
        {
            for (int y = -EffectiveRadius; y <= EffectiveRadius; y++)
            {
                const int2 Location = int2(Position.xy) + int2(x, y);
                if (IsInsideScreen(Location,g_SSRAttribs.RenderSize))
                {
                    const float  SampledDepth     = SampleDepth(Location);
                    const float  SampledRoughness = SampleRoughness(Location);
                    const float4 SampledRadiance  = SampleRadiance(Location);
                    const float3 SampledNormalWS  = SampleNormalWS(Location);

                    if (IsReflectionSample(SampledRoughness, SampledDepth))
                    {
                        const float SampledLinearDepth = DepthToCameraZ(SampledDepth, g_SSRAttribs.ProjMatrix);
                        const float WeightS = exp(-0.5 * (x * x + y * y) / (Sigma * Sigma));
                        const float WeightZ = exp(-abs(LinearDepth - SampledLinearDepth) / (SSR_BILATERAL_SIGMA_DEPTH * abs(dot(float2(x, y), GradDepth) + 1.e-6))) ;
                        const float WeightN = pow(max(0.0, dot(NormalWS, SampledNormalWS)), SSR_BILATERAL_SIGMA_NORMAL);
                        const float Weight = WeightS * WeightN * WeightZ;
                    
                        WeightSum += Weight;
                        ColorSum  += Weight * SampledRadiance;
                    }
                }
            }
        }

        RadianceResult = ColorSum / max(WeightSum, 1.0e-6f);
    }

    return RadianceResult;
}
