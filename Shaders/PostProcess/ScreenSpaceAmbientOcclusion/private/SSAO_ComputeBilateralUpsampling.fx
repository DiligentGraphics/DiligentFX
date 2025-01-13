#include "SSAO_Common.fxh"
#include "PostFX_Common.fxh"
#include "BasicStructures.fxh"
#include "ScreenSpaceAmbientOcclusionStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbScreenSpaceAmbientOcclusionAttribs
{
    ScreenSpaceAmbientOcclusionAttribs g_SSAOAttribs;
}

Texture2D<float> g_TextureDepth;
SamplerState     g_TextureDepth_sampler;

Texture2D<float> g_TextureOcclusion;
SamplerState     g_TextureOcclusion_sampler;

float LoadDepth(int2 Location)
{
    return g_TextureDepth.Load(int3(Location, 0));
}

float LoadOcclusion(int2 Location)
{
    return g_TextureOcclusion.Load(int3(Location, 0));
}

float LoadOcclusionLinear(float2 Texcoord)
{
    return g_TextureOcclusion.SampleLevel(g_TextureOcclusion_sampler, Texcoord, 0.0);
}

float LoadDepthLinear(float2 Texcoord)
{
#if defined(WEBGPU)
    float2 Position = g_Camera.f4ViewportSize.xy * Texcoord;
    int2 Positioni = int2(Position - 0.5);
    
    float x = frac(Position + 0.5);
    float y = frac(Position + 0.5);

    float4 Weight;
    Weight.x = (1.0 - x) * (1.0 - y);
    Weight.y = x * (1.0 - y);
    Weight.z = (1.0 - x) * y;
    Weight.w = x * y;
    
    float4 Gather;
    Gather.x = g_TextureDepth.Load(int3(Positioni + int2(0, 0), 0));
    Gather.y = g_TextureDepth.Load(int3(Positioni + int2(1, 0), 0));
    Gather.z = g_TextureDepth.Load(int3(Positioni + int2(0, 1), 0));
    Gather.w = g_TextureDepth.Load(int3(Positioni + int2(1, 1), 0));
    
    return dot(Weight, Gather);
#else
    return g_TextureDepth.SampleLevel(g_TextureDepth_sampler, Texcoord, 0.0);
#endif
}

float ComputeDepthWeight(float CenterDepth, float GuideDepth, float Sigma)
{
    float LinearDepth0 = DepthToCameraZ(CenterDepth, g_Camera.mProj);
    float LinearDepth1 = DepthToCameraZ(GuideDepth, g_Camera.mProj);
    float Alpha = abs(LinearDepth0 - LinearDepth1) / max(LinearDepth0, 1e-6);
    return exp(-(Alpha * Alpha) / (2.0 * Sigma * Sigma));
}

float ComputeBilateralUpsamplingPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 Position = VSOut.f4PixelPos.xy;
    float CenterDepth = LoadDepth(int2(Position));
    if (IsBackground(CenterDepth))
       return 1.0;

    /*
    // Alternative version with 4 samples 
    float OcclusionSum = 0.0;
    float WeightSum = 0.0;

    float2 HalfPosition = 0.5 * Position;
    int2 HalfPositioni = int2(HalfPosition - 0.5);

    float x = frac(HalfPosition + 0.5);
    float y = frac(HalfPosition + 0.5);

    float4 Weight;
    Weight.x = (1.0 - x) * (1.0 - y);
    Weight.y = x * (1.0 - y);
    Weight.z = (1.0 - x) * y;
    Weight.w = x * y;

    for (int SampleIdx = 0; SampleIdx < 4; SampleIdx++)
    {
        int2 Location = HalfPositioni + int2(SampleIdx & 0x01, SampleIdx >> 1);

        float SampledSignal = LoadOcclusion(Location);
        float SampledGuided = LoadDepthLinear(2.0 * (float2(Location) + 0.5) * g_Camera.f4ViewportSize.zw);

        float WeightS = Weight[SampleIdx];
        float WeightZ = ComputeDepthWeight(CenterDepth, SampledGuided, SSAO_BILATERAL_UPSAMPLING_DEPTH_SIGMA);
        OcclusionSum += SampledSignal * WeightS * WeightZ;
        WeightSum += WeightS *  WeightZ;
    }
    */

    // We need to add half a pixel offset relative to the half-res texture
    //float2 Location = floor(Position) * g_Camera.f4ViewportSize.zw
    //float2 Texcoord = floor(Position) * g_Camera.f4ViewportSize.zw + g_Camera.f4ViewportSize.zw;

    int2 CenterLocation = int2(0.5 * floor(Position.xy));
    
    float OcclusionSum = 0.0;
    float WeightSum = 0.0;
    
    const int UpsamplingRadius = 1;
    for (int x = -UpsamplingRadius; x <= UpsamplingRadius; x++)
    {
        for (int y = -UpsamplingRadius; y <= UpsamplingRadius; y++)
        {
            int2 Location = ClampScreenCoord(CenterLocation + int2(x, y), int2(0.5 * g_Camera.f4ViewportSize.xy));
            float2 Texcoord = 2.0 * (float2(Location) + 0.5) * g_Camera.f4ViewportSize.zw;

            float SampledSignal = LoadOcclusion(Location);
            float SampledGuided = LoadDepthLinear(Texcoord);
            float WeightS = ComputeSpatialWeight(float(x * x + y * y), SSAO_BILATERAL_UPSAMPLING_SIGMA);
            float WeightZ = ComputeDepthWeight(CenterDepth, SampledGuided, SSAO_BILATERAL_UPSAMPLING_DEPTH_SIGMA);

            OcclusionSum += WeightS * WeightZ * SampledSignal;
            WeightSum += WeightS * WeightZ;
        }
    }

    return WeightSum > 0.0 ? OcclusionSum / WeightSum : LoadOcclusionLinear(2.0 * (float2(CenterLocation) + 0.5) * g_Camera.f4ViewportSize.zw);
}
