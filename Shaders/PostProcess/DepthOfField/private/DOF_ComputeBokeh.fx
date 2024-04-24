#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"
#include "PostFX_Common.fxh"
#include "BasicStructures.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbDepthOfFieldAttribs
{
    DepthOfFieldAttribs g_DOFAttribs;
}

Texture2D<float4> g_TextureColorCoC;
SamplerState      g_TextureColorCoC_sampler;

float ComputeWeigh(float CoC, float Radius) 
{
	return saturate((CoC - Radius + 2.0) / 2.0);
}

float4 SampleColor(float2 Texcoord, float2 Offset)
{
    return g_TextureColorCoC.SampleLevel(g_TextureColorCoC_sampler, Texcoord + Offset, 0.0);
}

float4 ComputeBokehPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
	// rings = 3
	// points per ring = 7
	// https://github.com/Unity-Technologies/Graphics/blob/master/com.unity.postprocessing/PostProcessing/Shaders/Builtins/DiskKernels.hlsl
    float2 Kernel[DOF_KERNEL_SAMPLE_COUNT];
	Kernel[0]  = float2(0.0, 0.0);
	Kernel[1]  = float2(0.53333336, 0.0);
	Kernel[2]  = float2(0.3325279, 0.4169768);
	Kernel[3]  = float2(-0.11867785, 0.5199616);
	Kernel[4]  = float2(-0.48051673, 0.2314047);
	Kernel[5]  = float2(-0.48051673, -0.23140468);
	Kernel[6]  = float2(-0.11867763, -0.51996166);
	Kernel[7]  = float2(0.33252785, -0.4169769);
	Kernel[8]  = float2(1.0, 0.0);
	Kernel[9]  = float2(0.90096885, 0.43388376);
	Kernel[10] = float2(0.6234898, 0.7818315);
	Kernel[11] = float2(0.22252098, 0.9749279);
	Kernel[12] = float2(-0.22252095, 0.9749279);
	Kernel[13] = float2(-0.62349, 0.7818314);
	Kernel[14] = float2(-0.90096885, 0.43388382);
	Kernel[15] = float2(-1.0, 0.0);
	Kernel[16] = float2(-0.90096885, -0.43388376);
	Kernel[17] = float2(-0.6234896, -0.7818316);
	Kernel[18] = float2(-0.22252055, -0.974928);
	Kernel[19] = float2(0.2225215, -0.9749278);
	Kernel[20] = float2(0.6234897, -0.7818316);
	Kernel[21] = float2(0.90096885, -0.43388376);

    float2 CenterTexcoord = NormalizedDeviceXYToTexUV(VSOut.f2NormalizedXY.xy);
    float CoC = g_TextureColorCoC.SampleLevel(g_TextureColorCoC_sampler, CenterTexcoord, 0.0).a; 
					
	float4 BackgroudColor = float4(0.0, 0.0, 0.0, 0.0);
	float4 ForegroundColor = float4(0.0, 0.0, 0.0, 0.0);
	
	for (int SampleIdx = 0; SampleIdx < DOF_KERNEL_SAMPLE_COUNT; SampleIdx++) 
    {
		float2 SamplePosition = Kernel[SampleIdx] * g_DOFAttribs.BokehRadius;
		float4 SampledColor = SampleColor(CenterTexcoord, 2.0 * g_Camera.f4ViewportSize.zw * SamplePosition);
		float Radius = length(SamplePosition);

		float BackgroudWeight  = ComputeWeigh(max(0.0, min(SampledColor.a, CoC)), Radius);
		float ForegroundWeight = ComputeWeigh(-SampledColor.a, Radius);

		BackgroudColor.xyz += SampledColor.rgb * BackgroudWeight;
		BackgroudColor.w   += BackgroudWeight;

		ForegroundColor.xyz += SampledColor.rgb * ForegroundWeight;
		ForegroundColor.w   += ForegroundWeight;
	}

	BackgroudColor.xyz  *= 1.0 / (BackgroudColor.w  + float(BackgroudColor.w  == 0.0));
	ForegroundColor.xyz *= 1.0 / (ForegroundColor.w + float(ForegroundColor.w == 0.0));

	float Alpha = min(1.0, ForegroundColor.w * M_PI / float(DOF_KERNEL_SAMPLE_COUNT));
	float3 Color = lerp(BackgroudColor.xyz, ForegroundColor.xyz, Alpha);
	return float4(Color, Alpha);
}
