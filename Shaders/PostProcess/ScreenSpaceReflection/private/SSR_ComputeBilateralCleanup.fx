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

float LoadDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float LoadRoughness(int2 PixelCoord)
{
    return g_TextureRoughness.Load(int3(PixelCoord, 0));
}

float3 LoadNormalWS(int2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float4 LoadRadiance(int2 PixelCoord)
{
    return g_TextureRadiance.Load(int3(PixelCoord, 0));
}

float LoadVariance(int2 PixelCoord)
{
    return g_TextureVariance.Load(int3(PixelCoord, 0));
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
float4 ComputeBilateralCleanupPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    int2 PixelCoord = int2(VSOut.f4PixelPos.xy);

    float  Roughness = LoadRoughness(PixelCoord);
    float  Variance  = LoadVariance(PixelCoord);
    float3 NormalWS  = LoadNormalWS(PixelCoord);
    float  CameraZ   = DepthToCameraZ(LoadDepth(PixelCoord), g_Camera.mProj);
    float2 GradCamZ  = float2(ddx(CameraZ), ddy(CameraZ));

    float RoughnessTarget = saturate(float(SSR_BILATERAL_ROUGHNESS_FACTOR) * Roughness);
    float Radius = lerp(0.0, Variance > SSS_BILATERAL_VARIANCE_ESTIMATE_THRESHOLD ? 2.0 : 0.0, RoughnessTarget);
    float Sigma = g_SSRAttribs.BilateralCleanupSpatialSigmaFactor;
    int EffectiveRadius = int(min(2.0 * Sigma, Radius));
    float4 RadianceResult = LoadRadiance(PixelCoord);

    if (Variance > SSR_BILATERAL_VARIANCE_EXIT_THRESHOLD && EffectiveRadius > 0)
    {
        float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);
        float WeightSum = 0.0f;
        for (int x = -EffectiveRadius; x <= EffectiveRadius; x++)
        {
            for (int y = -EffectiveRadius; y <= EffectiveRadius; y++)
            {
                int2 Location = ClampScreenCoord(PixelCoord + int2(x, y), int2(g_Camera.f4ViewportSize.xy));
                float  SampledDepth     = LoadDepth(Location);
                float  SampledRoughness = LoadRoughness(Location);
                float4 SampledRadiance  = LoadRadiance(Location);
                float3 SampledNormalWS  = LoadNormalWS(Location);

                if (IsReflectionSample(SampledRoughness, SampledDepth, g_SSRAttribs.RoughnessThreshold))
                {
                    float SampledCameraZ = DepthToCameraZ(SampledDepth, g_Camera.mProj);
                    float WeightS = exp(-0.5 * dot(float2(x, y), float2(x, y)) / (Sigma * Sigma));
                    float WeightZ = exp(-abs(CameraZ - SampledCameraZ) / (SSR_BILATERAL_SIGMA_DEPTH * (abs(dot(float2(x, y), GradCamZ)) + 1e-6)));
                    float WeightN = pow(max(0.0, dot(NormalWS, SampledNormalWS)), SSR_BILATERAL_SIGMA_NORMAL);
                    float Weight = WeightS * WeightN * WeightZ;

                    WeightSum += Weight;
                    ColorSum  += Weight * SampledRadiance;
                }
            }
        }

        RadianceResult = ColorSum / max(WeightSum, 1.0e-6f);
    }

    return float4(RadianceResult.rgb, RadianceResult.a * g_SSRAttribs.AlphaInterpolation);
}
