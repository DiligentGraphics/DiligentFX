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

Texture2D<float4> g_TextureColorCoCNear;
Texture2D<float4> g_TextureColorCoCFar;
Texture2D<float2> g_TextureBokehKernel;

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

PSOutput ComputeBokehPS(in FullScreenTriangleVSOutput VSOut)
{
    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);

    float4 ForegroundColor = g_TextureColorCoCNear.SampleLevel(g_TextureColorCoCNear_sampler, CenterTexcoord, 0.0);
    float4 BackgroundColor = g_TextureColorCoCFar.SampleLevel(g_TextureColorCoCFar_sampler, CenterTexcoord, 0.0);

    float CoCNear = ForegroundColor.a;
    float CoCFar = BackgroundColor.a;

    float AspectRatio = g_Camera.f4ViewportSize.x * g_Camera.f4ViewportSize.w;
    int SampleCount = ComputeSampleCount(DOF_BOKEH_KERNEL_SMALL_RING_COUNT, DOF_BOKEH_KERNEL_SMALL_RING_DENSITY);

    [branch]
    if (CoCNear > 0.0)
    {
        for (int SampleIdx = 0; SampleIdx < SampleCount; SampleIdx++)
        {
            float2 KernelSample = g_TextureBokehKernel.Load(int3(SampleIdx, 0, 0));

            float2 SamplePosition = 0.25 * KernelSample * CoCNear * g_DOFAttribs.MaxCircleOfConfusion;
            float2 SampleTexcoord = float2(SamplePosition.x, AspectRatio * SamplePosition.y);
            float4 SampledColor = SampleColorCoCNear(CenterTexcoord, SampleTexcoord);
            ForegroundColor.xyz = max(SampledColor.rgb, ForegroundColor.xyz);
        }
    }

    [branch]
    if (CoCFar > 0.0)
    {
        for (int SampleIdx = 0; SampleIdx < SampleCount; SampleIdx++)
        {
            float2 KernelSample = g_TextureBokehKernel.Load(int3(SampleIdx, 0, 0));

            float2 SamplePosition = 0.25 * KernelSample * CoCFar * g_DOFAttribs.MaxCircleOfConfusion;
            float2 SampleTexcoord = float2(SamplePosition.x, AspectRatio * SamplePosition.y);
            float4 SampledColor = SampleColorCoCFar(CenterTexcoord, SampleTexcoord);
            BackgroundColor.xyz = max(SampledColor.rgb * float(SampledColor.a >= CoCFar), BackgroundColor.xyz);
        }
    }

    PSOutput Output;
    Output.ForegroundColor = float4(ForegroundColor.xyz, CoCNear);
    Output.BackgroundColor = float4(BackgroundColor.xyz, CoCFar);
    return Output;
}
