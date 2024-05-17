#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"
#include "PostFX_Common.fxh"
#include "BasicStructures.fxh"
#include "DOF_Common.fx"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbDepthOfFieldAttribs
{
    DepthOfFieldAttribs g_DOFAttribs;
}

Texture2D<float3> g_TextureRadiance;
Texture2D<float4> g_TextureColorCoCNear;
Texture2D<float4> g_TextureColorCoCFar;
Texture2D<float2> g_TextureBokehKernel;

SamplerState g_TextureRadiance_sampler;
SamplerState g_TextureColorCoCNear_sampler;
SamplerState g_TextureColorCoCFar_sampler;

struct PSOutput 
{
    float4 ForegroundColor : SV_Target0;
    float4 BackgroundColor : SV_Target1;
};

float4 SampleColorCoCNear(float2 Texcoord, float2 Offset)
{
    return g_TextureColorCoCNear.SampleLevel(g_TextureColorCoCNear_sampler, Texcoord + Offset, 0.0);
}

float4 SampleColorCoCFar(float2 Texcoord, float2 Offset)
{
    return g_TextureColorCoCFar.SampleLevel(g_TextureColorCoCFar_sampler, Texcoord + Offset, 0.0);
}

float3 SampleRadiance(float2 Texcoord, float2 Offset)
{
    return g_TextureRadiance.SampleLevel(g_TextureRadiance_sampler, Texcoord + Offset, 0.0);
}

PSOutput ComputeBokehPS(in FullScreenTriangleVSOutput VSOut) 
{
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);
    float CoCNear = g_TextureColorCoCNear.SampleLevel(g_TextureColorCoCNear_sampler, CenterTexcoord, 0.0).a;
    float CoCFar  = g_TextureColorCoCFar.SampleLevel(g_TextureColorCoCFar_sampler, CenterTexcoord, 0.0).a;

    float4 ForegroundColor = float4(0.0, 0.0, 0.0, 0.0);
	float4 BackgroundColor = float4(0.0, 0.0, 0.0, 0.0);

    float AspectRatio = g_Camera.f4ViewportSize.x * g_Camera.f4ViewportSize.w;
    int SampleCount = ComputeSampleCount(g_DOFAttribs.BokehKernelRingCount, g_DOFAttribs.BokehKernelRingDensity);

    [branch]
    if (CoCNear > 0.0)
    {
        for (int SampleIdx = 0; SampleIdx < SampleCount; SampleIdx++)
        {
            float2 KernelSample = g_TextureBokehKernel.Load(int3(SampleIdx, 0, 0));

            float2 SamplePosition = 0.5 * KernelSample * CoCNear * g_DOFAttribs.MaxCircleOfConfusion;
            float2 SampleTexcoord = float2(SamplePosition.x, AspectRatio * SamplePosition.y);
            float4 SampledColor = SampleColorCoCNear(CenterTexcoord, SampleTexcoord);

#if DOF_OPTION_KARIS_INVERSE
            float Weight = ComputeHDRWeight(SampleRadiance(CenterTexcoord, SampleTexcoord));
#else
            float Weight = 1.0;
#endif
            ForegroundColor += float4(SampledColor.rgb, 1.0) * Weight;
        }
    }

    [branch]
    if (CoCFar > 0.0)
    {
        for (int SampleIdx = 0; SampleIdx < SampleCount; SampleIdx++)
        {
            float2 KernelSample = g_TextureBokehKernel.Load(int3(SampleIdx, 0, 0));

            float2 SamplePosition = 0.5 * KernelSample * CoCFar * g_DOFAttribs.MaxCircleOfConfusion;
            float2 SampleTexcoord = float2(SamplePosition.x, AspectRatio * SamplePosition.y);
            float4 SampledColor = SampleColorCoCFar(CenterTexcoord, SampleTexcoord);

#if DOF_OPTION_KARIS_INVERSE
            float Weight = ComputeHDRWeight(SampleRadiance(CenterTexcoord, SampleTexcoord));
#else
            float Weight = 1.0;
#endif
            BackgroundColor += float4(SampledColor.rgb, 1.0) * Weight * float(SampledColor.a >= CoCFar);
        }
    }

    PSOutput Output;
    Output.ForegroundColor = float4(ForegroundColor.xyz * rcp(ForegroundColor.w + float(ForegroundColor.w == 0.0)), CoCNear);
    Output.BackgroundColor = float4(BackgroundColor.xyz * rcp(BackgroundColor.w + float(BackgroundColor.w == 0.0)), CoCFar);

    return Output;
}
