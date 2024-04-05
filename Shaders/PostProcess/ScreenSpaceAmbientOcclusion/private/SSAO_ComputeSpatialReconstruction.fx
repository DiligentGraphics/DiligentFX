#include "ScreenSpaceAmbientOcclusionStructures.fxh"
#include "BasicStructures.fxh"
#include "PBR_Common.fxh"
#include "PostFX_Common.fxh"
#include "SSAO_Common.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_Camera;
}

cbuffer cbScreenSpaceAmbientOcclusionAttribs
{
    ScreenSpaceAmbientOcclusionAttribs g_SSAOAttribs;
}

Texture2D<float>  g_TextureOcclusion;
Texture2D<float>  g_TextureHistory;
Texture2D<float>  g_TextureDepth;
Texture2D<float3> g_TextureNormal;

float SampleOcclusion(int2 PixelCoord)
{
    return g_TextureOcclusion.Load(int3(PixelCoord, 0));
}

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float SampleHistory(int2 PixelCoord)
{
    return g_TextureHistory.Load(int3(PixelCoord, 0));
}

float3 SampleNormalWS(int2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}
 
float4 ComputeBlurKernelRotation(uint2 PixelCoord, uint FrameIndex)
{
    float Angle = Bayer4x4(PixelCoord, FrameIndex);
    return GetRotator(2.0 * M_PI * Angle);
}

float ComputeSpatialReconstructionPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    // samples = 8, min distance = 0.5, average samples on radius = 2
    float3 Poisson[SSAO_SPATIAL_RECONSTRUCTION_SAMPLES];
    Poisson[0] = float3(-0.4706069, -0.4427112, +0.6461146);
    Poisson[1] = float3(-0.9057375, +0.3003471, +0.9542373);
    Poisson[2] = float3(-0.3487388, +0.4037880, +0.5335386);
    Poisson[3] = float3(+0.1023042, +0.6439373, +0.6520134);
    Poisson[4] = float3(+0.5699277, +0.3513750, +0.6695386);
    Poisson[5] = float3(+0.2939128, -0.1131226, +0.3149309);
    Poisson[6] = float3(+0.7836658, -0.4208784, +0.8895339);
    Poisson[7] = float3(+0.1564120, -0.8198990, +0.8346850);
    
    float4 Position = VSOut.f4PixelPos;
    float History = SampleHistory(int2(Position.xy));
    float Depth = SampleDepth(int2(Position.xy));
    float AccumulationFactor = pow(abs((History - 1.0) / float(SSAO_OCCLUSION_HISTORY_MAX_FRAMES_WITH_DENOISING)), 0.2);
    
    if (IsBackground(Depth) || AccumulationFactor >= 1.0)
        return SampleOcclusion(int2(Position.xy));

    float3 PositionSS = float3(Position.xy * g_Camera.f4ViewportSize.zw, Depth);
    float3 PositionVS = ScreenXYDepthToViewSpace(PositionSS, g_Camera.mProj);
    float3 NormalVS = mul(float4(SampleNormalWS(int2(Position.xy)), 0.0), g_Camera.mView).xyz;
    float4 Rotator = ComputeBlurKernelRotation(uint2(Position.xy), g_Camera.uiFrameIndex);
    float Radius = lerp(0.0, g_SSAOAttribs.SpatialReconstructionRadius, 1.0 - saturate(AccumulationFactor));
    float PlaneNormalFactor = 10.0 / (1.0 + DepthToCameraZ(Depth, g_Camera.mProj));
   
    float OcclusionSum = 0.0;
    float WeightSum = 0.0;
        
    for (int SampleIdx = 0; SampleIdx < SSAO_SPATIAL_RECONSTRUCTION_SAMPLES; SampleIdx++)
    {
        float2 Xi = RotateVector(Rotator, Poisson[SampleIdx].xy);
        int2 SampleCoord = ClampScreenCoord(int2(Position.xy + Radius * Xi), int2(g_Camera.f4ViewportSize.xy));
        
        float SampledDepth = SampleDepth(SampleCoord);
        float SampledOcclusion = SampleOcclusion(SampleCoord);

        float3 SamplePositionSS = float3((float2(SampleCoord) + 0.5) * g_Camera.f4ViewportSize.zw, SampledDepth);
        float3 SamplePositionVS = ScreenXYDepthToViewSpace(SamplePositionSS, g_Camera.mProj);
                
        float WeightS = ComputeSpatialWeight(Poisson[SampleIdx].z * Poisson[SampleIdx].z, SSAO_SPATIAL_RECONSTRUCTION_SIGMA);
        float WeightZ = ComputeGeometryWeight(PositionVS, SamplePositionVS, NormalVS, PlaneNormalFactor);
        
        OcclusionSum += WeightS * WeightZ * SampledOcclusion;
        WeightSum    += WeightS * WeightZ;
    }

    return WeightSum > 0.0 ? OcclusionSum / WeightSum : SampleOcclusion(int2(Position.xy));
}
