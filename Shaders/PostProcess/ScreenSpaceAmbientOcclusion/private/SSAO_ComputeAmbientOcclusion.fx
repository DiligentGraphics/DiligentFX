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

Texture2D<float>  g_TexturePrefilteredDepth;
Texture2D<float3> g_TextureNormal;
Texture2D<float2> g_TextureBlueNoise;

SamplerState g_TexturePrefilteredDepth_sampler;

float2 SampleRandomVector2D(int2 PixelCoord)
{
    return g_TextureBlueNoise.Load(int3(PixelCoord & 127, 0));
}

float3 SampleNormalWS(int2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float SamplePrefilteredDepth(float2 ScreenCoordUV, float MipLevel)
{
    return g_TexturePrefilteredDepth.SampleLevel(g_TexturePrefilteredDepth_sampler, ScreenCoordUV, MipLevel);
}

float2 ComputeSliceDirection(float Xi, int Index)
{
    float3 Rotations = float3(0.0, 120.0, 240.0);
    float Rotation = Rotations[Index] / 360.0;
    float Phi = (Xi + Rotation) * M_PI;
    return float2(cos(Phi), sin(Phi));
}

float3 FastReconstructPosition(float3 Coord, float4x4 Transform)
{
    float3 NDC = float3(TexUVToNormalizedDeviceXY(Coord.xy), DepthToCameraZ(Coord.z, Transform));
    return float3(NDC.z * NDC.x / MATRIX_ELEMENT(Transform, 0, 0), NDC.z * NDC.y / MATRIX_ELEMENT(Transform, 1, 1), NDC.z);
}

float FastACos(float Value)
{
    float Result = -0.156583 * Value + M_HALF_PI;
    Result *= sqrt(1.0 - abs(Value));
    return (Value >= 0.0) ? Result : M_PI - Result;
}

float IntegrateArcUniform(float HorizonX, float HorizonY)
{
    return (1.0f - cos(HorizonX) + (1.0 - cos(HorizonY)));
}

float IntegrateArcCosWeighted(float HorizonX, float HorizonY, float N, float CosN)
{
    float H1 = HorizonX * 2.0;
    float H2 = HorizonY * 2.0;
    float SinN = sin(N);
    return 0.25 * ((-cos(H1 - N) + CosN + H1 * SinN) + (-cos(H2 - N) + CosN + H2 * SinN));
}

float ComputeAmbientOcclusionPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;
    float2 ScreenCoordUV = Position.xy * g_Camera.f4ViewportSize.zw;
    float3 PositionSS = float3(ScreenCoordUV, SamplePrefilteredDepth(ScreenCoordUV, 0.0));

    if (IsBackground(PositionSS.z))
        discard;

    // Trying to fix self-occlusion. Maybe there's a better way
    PositionSS.z *= 0.99999;

    float3 NormalVS = mul(float4(SampleNormalWS(int2(Position.xy)), 0.0), g_Camera.mView).xyz;
    float3 PositionVS = FastReconstructPosition(PositionSS, g_Camera.mProj);
    float3 ViewVS = -normalize(PositionVS);
    float2 Xi = SampleRandomVector2D(int2(Position.xy));

    float EffectRadius = g_SSAOAttribs.EffectRadius * g_SSAOAttribs.RadiusMultiplier;
    float FalloffRange = g_SSAOAttribs.EffectFalloffRange * EffectRadius;
    float FalloffFrom = EffectRadius * (1.0 - g_SSAOAttribs.EffectFalloffRange);

    float FalloffMul = -1.0 / FalloffRange;
    float FalloffAdd = FalloffFrom / FalloffRange + 1.0;
    float SampleRadius = 0.5 * EffectRadius * MATRIX_ELEMENT(g_Camera.mProj, 0, 0) / PositionVS.z;

    float Visibility = 0.0;
    for (int SliceIdx = 0; SliceIdx < SSAO_SLICE_COUNT; SliceIdx++)
    {
        float2 Omega = ComputeSliceDirection(Xi.x, SliceIdx);

        float3 SliceDirection = float3(Omega, 0.0);
        float3 OrthoSliceDir = SliceDirection - dot(SliceDirection, ViewVS) * ViewVS;
        float3 Axis = cross(SliceDirection, ViewVS);
        float3 ProjNormal = NormalVS - Axis * dot(NormalVS, Axis);

        float ProjNormalLen = length(ProjNormal);
        float CosNorm = saturate(dot(ProjNormal / ProjNormalLen, ViewVS));
        float N = sign(dot(OrthoSliceDir, ProjNormal)) * FastACos(CosNorm);

        float2 MinCosHorizons;
        MinCosHorizons.x = cos(N + M_HALF_PI);
        MinCosHorizons.y = cos(N - M_HALF_PI);

        float2 MaxCosHorizons;
        MaxCosHorizons.x = MinCosHorizons.x;
        MaxCosHorizons.y = MinCosHorizons.y;

        float2 SampleDirection = float2(Omega.x, Omega.y) * F3NDC_XYZ_TO_UVD_SCALE.xy * SampleRadius;
        SampleDirection.x *= g_Camera.f4ViewportSize.y * g_Camera.f4ViewportSize.z; // Aspect ratio correction

        for (int SampleIdx = 0; SampleIdx < SSAO_SAMPLES_PER_SLICE; SampleIdx++)
        {
            float Noise = frac(Xi.y + float(SliceIdx + SampleIdx * SSAO_SAMPLES_PER_SLICE) * 0.6180339887498948482);
            float Sample = (float(SampleIdx) + Noise) / float(SSAO_SAMPLES_PER_SLICE);

            float2 SampleOffset = Sample * Sample * SampleDirection;
            float2 SamplePositionSS0 = PositionSS.xy + SampleOffset;
            float2 SamplePositionSS1 = PositionSS.xy - SampleOffset;

            float MipLevel = clamp(log2(length(SampleOffset * g_Camera.f4ViewportSize.xy)) - g_SSAOAttribs.DepthMIPSamplingOffset, 0.0, float(SSAO_DEPTH_PREFILTERED_MAX_MIP));
            float3 SamplePositionVS0 = FastReconstructPosition(float3(SamplePositionSS0, SamplePrefilteredDepth(SamplePositionSS0, MipLevel)), g_Camera.mProj);
            float3 SamplePositionVS1 = FastReconstructPosition(float3(SamplePositionSS0, SamplePrefilteredDepth(SamplePositionSS1, MipLevel)), g_Camera.mProj);

            float3 SampleDifference0 = SamplePositionVS0 - PositionVS;
            float3 SampleDifference1 = SamplePositionVS1 - PositionVS;

            float SampleDistance0 = length(SampleDifference0);
            float SampleCosHorizon0 = dot(SampleDifference0 / SampleDistance0, ViewVS);
            float Weight0 = saturate(SampleDistance0 * FalloffMul + FalloffAdd);

            float SampleDistance1 = length(SampleDifference1);
            float SampleCosHorizon1 = dot(SampleDifference1 / SampleDistance1, ViewVS);
            float Weight1 = saturate(SampleDistance1 * FalloffMul + FalloffAdd);

            MaxCosHorizons.x = max(MaxCosHorizons.x, lerp(MinCosHorizons.x, SampleCosHorizon0, Weight0));
            MaxCosHorizons.y = max(MaxCosHorizons.y, lerp(MinCosHorizons.y, SampleCosHorizon1, Weight1));
        }

        float2 HorizonAngles;
        HorizonAngles.x = +FastACos(MaxCosHorizons.x);
        HorizonAngles.y = -FastACos(MaxCosHorizons.y);

#if SSAO_OPTION_UNIFORM_WEIGHTING
        Visibility += 0.5 * IntegrateArcUniform(HorizonAngles.x, HorizonAngles.y);
#else
        Visibility += ProjNormalLen * IntegrateArcCosWeighted(HorizonAngles.x, HorizonAngles.y, N, CosNorm);
#endif
    }

    return Visibility / float(SSAO_SLICE_COUNT);
}
