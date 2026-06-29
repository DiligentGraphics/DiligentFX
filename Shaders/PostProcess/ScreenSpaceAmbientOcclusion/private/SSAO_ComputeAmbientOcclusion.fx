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
SamplerState      g_TexturePrefilteredDepth_sampler;

Texture2D<float3> g_TextureNormal;
SamplerState      g_TextureNormal_sampler;

Texture2D<float2> g_TextureBlueNoise;

float2 LoadRandomVector2D(int2 PixelCoord)
{
    return g_TextureBlueNoise.Load(int3(PixelCoord & 127, 0));
}

float3 LoadNormalWS(float2 ScreenCoordUV)
{
    return g_TextureNormal.SampleLevel(g_TextureNormal_sampler, ScreenCoordUV, 0.0);
}

float SamplePrefilteredDepth(float2 ScreenCoordUV, float MipLevel)
{
    return g_TexturePrefilteredDepth.SampleLevel(g_TexturePrefilteredDepth_sampler, ScreenCoordUV, MipLevel);
}

float2 ComputeSliceDirection(float Xi, int Index)
{
    float Rotation = float(Index) / 3.0;
    float Phi = (Xi + Rotation) * M_PI;
    return float2(cos(Phi), sin(Phi));
}

float FastACos(float Value)
{
    float AbsValue = abs(Value);
    float Result = -0.156583 * AbsValue + M_HALF_PI;
    Result *= sqrt(1.0 - AbsValue);
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

float2 GetInvViewportSize()
{
#if SSAO_OPTION_HALF_RESOLUTION
    return 2.0 * g_Camera.f4ViewportSize.zw;
#else
    return g_Camera.f4ViewportSize.zw;
#endif
}

uint ComputeOccludedSectors(float MinHorizon, float MaxHorizon, uint OccludedBitfield)
{
    MinHorizon = saturate(MinHorizon);
    MaxHorizon = saturate(MaxHorizon);

    uint Result = OccludedBitfield;
    if (MaxHorizon > MinHorizon)
    {
        uint SectorCount = uint(SSAO_BITMASK_SECTOR_COUNT);
        uint StartInt    = min(uint(MinHorizon * float(SectorCount)), SectorCount - 1u);
        uint EndInt      = min(uint(ceil(MaxHorizon * float(SectorCount))), SectorCount);

        if (EndInt > StartInt)
        {
            uint AngleInt      = EndInt - StartInt;
            uint AngleBitfield = AngleInt >= 32u ? 0xFFFFFFFFu : ((1u << AngleInt) - 1u);
            Result |= AngleBitfield << StartInt;
        }
    }

    return Result;
}

uint ComputeSampleOcclusion(float3 SamplePositionVS0, float3 SamplePositionVS1, float3 PositionVS, float3 ViewVS, float NSlice, float FalloffMul, float FalloffAdd, uint OccludedBitfield)
{
    float3 DeltaPos0     = SamplePositionVS0 - PositionVS;
    float3 DeltaPos1     = SamplePositionVS1 - PositionVS;
    float3 ViewThickness = ViewVS * g_SSAOAttribs.BitmaskThickness;

    float2 Weight = saturate(float2(length(DeltaPos0), length(DeltaPos1)) * FalloffMul + FalloffAdd);

    float4 FrontBack = float4(
        FastACos(dot(normalize(DeltaPos0), ViewVS)), FastACos(dot(normalize(DeltaPos0 - ViewThickness), ViewVS)),
        FastACos(dot(normalize(DeltaPos1), ViewVS)), FastACos(dot(normalize(DeltaPos1 - ViewThickness), ViewVS)));

    FrontBack = saturate((float4(-FrontBack.xy, FrontBack.zw) - NSlice + M_HALF_PI) / M_PI);

    if (Weight.x > 0.0)
        OccludedBitfield = ComputeOccludedSectors(FrontBack.y, FrontBack.x, OccludedBitfield);
    if (Weight.y > 0.0)
        OccludedBitfield = ComputeOccludedSectors(FrontBack.z, FrontBack.w, OccludedBitfield);
    return OccludedBitfield;
}

float2 ComputeSampleHorizons(float3 SamplePositionVS0, float3 SamplePositionVS1, float3 PositionVS, float3 ViewVS, float2 MinCosHorizons, float2 MaxCosHorizons, float FalloffMul, float FalloffAdd)
{
    float3 SampleDifference0 = SamplePositionVS0 - PositionVS;
    float3 SampleDifference1 = SamplePositionVS1 - PositionVS;

    float2 SampleDistance   = float2(length(SampleDifference0), length(SampleDifference1));
    float2 SampleCosHorizon = float2(dot(SampleDifference0 / SampleDistance.x, ViewVS), dot(SampleDifference1 / SampleDistance.y, ViewVS));
    float2 Weight           = saturate(SampleDistance * FalloffMul + FalloffAdd);
    return max(MaxCosHorizons, lerp(MinCosHorizons, SampleCosHorizon, Weight));
}

float ComputeAmbientOcclusionPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float2 Position = VSOut.f4PixelPos.xy;

    float2 ScreenCoordUV = Position * GetInvViewportSize();
    float3 PositionSS = float3(ScreenCoordUV, SamplePrefilteredDepth(ScreenCoordUV, 0.0));

    if (IsBackground(PositionSS.z))
        discard;

    float3 NormalVS = mul(float4(LoadNormalWS(ScreenCoordUV), 0.0), g_Camera.mView).xyz; 
    float3 PositionVS = ScreenXYDepthToViewSpace(PositionSS, g_Camera.mProj);

    // Fix self-occlusion
#if SSAO_OPTION_HALF_PRECISION_DEPTH
    float Offset = 0.005;
#else
    float Offset = 0.00001;
#endif
    PositionVS += NormalVS * Offset * PositionVS.z;

    float3 ViewVS = -normalize(PositionVS);
    float2 Xi = LoadRandomVector2D(int2(Position));

    float EffectRadius = g_SSAOAttribs.EffectRadius * g_SSAOAttribs.RadiusMultiplier;
    float FalloffRange = g_SSAOAttribs.EffectFalloffRange * EffectRadius;
    float FalloffFrom = EffectRadius - FalloffRange;

    float FalloffMul = -1.0 / FalloffRange;
    float FalloffAdd = FalloffFrom / FalloffRange + 1.0;
    float SampleRadius = 0.5 * EffectRadius * MATRIX_ELEMENT(g_Camera.mProj, 0, 0);
    if (g_Camera.mProj[3][3] == 0.0)
    {
        // Perspective
        SampleRadius /= PositionVS.z;
    }

    float Visibility = 0.0;
    for (int SliceIdx = 0; SliceIdx < SSAO_SLICE_COUNT; SliceIdx++)
    {
        float2 Omega = ComputeSliceDirection(Xi.x, SliceIdx);

        float3 SliceDirection = float3(Omega, 0.0);
        float3 OrthoSliceDir = SliceDirection - dot(SliceDirection, ViewVS) * ViewVS;
        float3 Axis = normalize(cross(SliceDirection, ViewVS));
        float3 ProjNormal = NormalVS - Axis * dot(NormalVS, Axis);

        float ProjNormalLen = length(ProjNormal);
        float CosNorm = saturate(dot(ProjNormal / ProjNormalLen, ViewVS));
        float N = sign(dot(OrthoSliceDir, ProjNormal)) * FastACos(CosNorm);

#if SSAO_ALGORITHM == SSAO_ALGORITHM_VBAO
        uint OccludedBitfield = 0u;
        float NBitmask = -N;
#else
        float2 MinCosHorizons;
        MinCosHorizons.x = cos(N + M_HALF_PI);
        MinCosHorizons.y = cos(N - M_HALF_PI);

        float2 MaxCosHorizons;
        MaxCosHorizons.x = MinCosHorizons.x;
        MaxCosHorizons.y = MinCosHorizons.y;
#endif

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
            float3 SamplePositionVS0 = ScreenXYDepthToViewSpace(float3(SamplePositionSS0, SamplePrefilteredDepth(SamplePositionSS0, MipLevel)), g_Camera.mProj);
            float3 SamplePositionVS1 = ScreenXYDepthToViewSpace(float3(SamplePositionSS1, SamplePrefilteredDepth(SamplePositionSS1, MipLevel)), g_Camera.mProj);

#if SSAO_ALGORITHM == SSAO_ALGORITHM_VBAO
            OccludedBitfield = ComputeSampleOcclusion(SamplePositionVS0, SamplePositionVS1, PositionVS, ViewVS, NBitmask, FalloffMul, FalloffAdd, OccludedBitfield);
#else
            MaxCosHorizons = ComputeSampleHorizons(SamplePositionVS0, SamplePositionVS1, PositionVS, ViewVS, MinCosHorizons, MaxCosHorizons, FalloffMul, FalloffAdd);
#endif
        }

#if SSAO_ALGORITHM == SSAO_ALGORITHM_VBAO
        Visibility += 1.0 - float(countbits(OccludedBitfield)) / float(SSAO_BITMASK_SECTOR_COUNT);
#elif SSAO_ALGORITHM == SSAO_ALGORITHM_HBAO
        float2 HorizonAngles = float2(+FastACos(MaxCosHorizons.x), -FastACos(MaxCosHorizons.y));
        Visibility += 0.5 * IntegrateArcUniform(HorizonAngles.x, HorizonAngles.y);
#else
        float2 HorizonAngles = float2(+FastACos(MaxCosHorizons.x), -FastACos(MaxCosHorizons.y));
        Visibility += ProjNormalLen * IntegrateArcCosWeighted(HorizonAngles.x, HorizonAngles.y, N, CosNorm);
#endif
    }

    return Visibility / float(SSAO_SLICE_COUNT);
}
