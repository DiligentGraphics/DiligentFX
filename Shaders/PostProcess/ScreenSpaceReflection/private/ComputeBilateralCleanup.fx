#include "ScreenSpaceReflectionStructures.fxh"
#include "BasicStructures.fxh"
#include "SSR_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float>  g_TextureDepth;
Texture2D<float3> g_TextureNormal;
Texture2D<float>  g_TextureRoughness;

Texture2D<float4> g_TextureRadiance;
Texture2D<float>  g_TextureVariance;

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float SampleRoughness(int2 PixelCoord)
{
    return g_TextureRoughness.Load(int3(PixelCoord, 0));
}

float3 SampleNormalWS(int2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float4 SampleRadiance(int2 PixelCoord)
{
    return g_TextureRadiance.Load(int3(PixelCoord, 0));
}

float SampleVariance(int2 PixelCoord)
{
    return g_TextureVariance.Load(int3(PixelCoord, 0));
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
float4 ComputeBilateralCleanupPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;

    float  Roughness   = SampleRoughness(int2(Position.xy));
    float  Variance    = SampleVariance(int2(Position.xy));
    float3 NormalWS    = SampleNormalWS(int2(Position.xy));
    float  LinearDepth = DepthToCameraZ(SampleDepth(int2(Position.xy)), g_Camera.mProj);
    float2 GradDepth   = float2(ddx(LinearDepth), ddy(LinearDepth));

    float RoughnessTarget = saturate(float(SSR_BILATERAL_ROUGHNESS_FACTOR) * Roughness);
    float Radius = lerp(0.0, Variance > SSS_BILATERAL_VARIANCE_ESTIMATE_THRESHOLD ? 2.0 : 0.0, RoughnessTarget);
    float Sigma = g_SSRAttribs.BilateralCleanupSpatialSigmaFactor;
    int EffectiveRadius = int(min(2.0 * Sigma, Radius));
    float4 RadianceResult = SampleRadiance(int2(Position.xy));

    if (Variance > SSR_BILATERAL_VARIANCE_EXIT_THRESHOLD && EffectiveRadius > 0)
    {
        float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);
        float WeightSum = 0.0f;
        for (int x = -EffectiveRadius; x <= EffectiveRadius; x++)
        {
            for (int y = -EffectiveRadius; y <= EffectiveRadius; y++)
            {
                int2 Location = ClampScreenCoord(int2(Position.xy) + int2(x, y), int2(g_Camera.f4ViewportSize.xy));
                float  SampledDepth     = SampleDepth(Location);
                float  SampledRoughness = SampleRoughness(Location);
                float4 SampledRadiance  = SampleRadiance(Location);
                float3 SampledNormalWS  = SampleNormalWS(Location);

                if (IsReflectionSample(SampledRoughness, SampledDepth, g_SSRAttribs.RoughnessThreshold))
                {
                    float SampledLinearDepth = DepthToCameraZ(SampledDepth, g_Camera.mProj);
                    float WeightS = exp(-0.5 * dot(float2(x, y), float2(x, y)) / (Sigma * Sigma));
                    float WeightZ = exp(-abs(LinearDepth - SampledLinearDepth) / (SSR_BILATERAL_SIGMA_DEPTH * abs(dot(float2(x, y), GradDepth)+1.e-6)));
                    float WeightN = pow(max(0.0, dot(NormalWS, SampledNormalWS)), SSR_BILATERAL_SIGMA_NORMAL);
                    float Weight = WeightS * WeightN * WeightZ;

                    WeightSum += Weight;
                    ColorSum  += Weight * SampledRadiance;
                }
                
            }
        }

        RadianceResult = ColorSum / max(WeightSum, 1.0e-6f);
    }

    return RadianceResult;
}
