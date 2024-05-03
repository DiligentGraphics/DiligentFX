#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"
#include "TemporalAntiAliasingStructures.fxh"
#include "ScreenSpaceAmbientOcclusionStructures.fxh"
#include "SSAO_Common.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_CurrCamera;
    CameraAttribs g_PrevCamera;
}

cbuffer cbScreenSpaceAmbientOcclusionAttribs
{
    ScreenSpaceAmbientOcclusionAttribs g_SSAOAttribs;
}

Texture2D<float>  g_TextureCurrOcclusion;
Texture2D<float>  g_TexturePrevOcclusion;
Texture2D<float>  g_TextureHistory;
Texture2D<float>  g_TextureCurrDepth;
Texture2D<float>  g_TexturePrevDepth;
Texture2D<float2> g_TextureMotion;

struct PSOutput
{
    float Occlusion : SV_Target0;
    float History   : SV_Target1;
};

struct PixelStatistic
{
    float Mean;
    float Variance;
    float StdDev;
};

struct ProjectionDesc
{
    float  Occlusion;
    float  History;
    bool   IsSuccess;
};

float SampleCurrOcclusion(int2 PixelCoord)
{
    return g_TextureCurrOcclusion.Load(int3(PixelCoord, 0));
}

float SampleCurrDepth(int2 PixelCoord)
{
    return g_TextureCurrDepth.Load(int3(PixelCoord, 0));
}

float SamplePrevOcclusion(int2 PixelCoord)
{
    return g_TexturePrevOcclusion.Load(int3(PixelCoord, 0));
}

float SamplePrevDepth(int2 PixelCoord)
{
    return g_TexturePrevDepth.Load(int3(PixelCoord, 0));
}

float SampleHistory(int2 PixelCoord)
{
    return g_TextureHistory.Load(int3(PixelCoord, 0));
}

float2 SampleMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

bool IsDepthSimilar(float CurrDepth, float PrevDepth)
{
    float LinearDepthCurr = DepthToCameraZ(CurrDepth, g_PrevCamera.mProj);
    float LinearDepthPrev = DepthToCameraZ(PrevDepth, g_PrevCamera.mProj);
    return abs(1.0 - LinearDepthCurr / LinearDepthPrev) < SSAO_DISOCCLUSION_DEPTH_THRESHOLD;
}

bool IsInsideScreenMinusOne(int2 PixelCoord, int2 Dimension)
{
    return PixelCoord.x > 0 &&
           PixelCoord.y > 0 &&
           PixelCoord.x < (Dimension.x - 1) &&
           PixelCoord.y < (Dimension.y - 1);
}

PixelStatistic ComputePixelStatistic(int2 PixelCoord)
{
    PixelStatistic Desc;
    float M1 = 0.0;
    float M2 = 0.0;

    const int SearchRadius = 1;
    for (int x = -SearchRadius; x <= SearchRadius; ++x)
    {
        for (int y = -SearchRadius; y <= SearchRadius; ++y)
        {
            int2 Location = ClampScreenCoord(PixelCoord + int2(x, y), int2(g_CurrCamera.f4ViewportSize.xy));
            float SampleOcclusion = SampleCurrOcclusion(Location);
            M1 += SampleOcclusion;
            M2 += SampleOcclusion * SampleOcclusion;
        }
    }

    Desc.Mean = M1 / 9.0;
    Desc.Variance = (M2 / 9.0) - (Desc.Mean * Desc.Mean);
    Desc.StdDev = sqrt(max(Desc.Variance, 0.0f));
    return Desc;
}

ProjectionDesc ComputeReprojection(float2 PrevPos, float CurrDepth)
{
    ProjectionDesc Desc;
    
    int2 PrevPosi = int2(PrevPos - 0.5);
    float x = frac(PrevPos.x + 0.5);
    float y = frac(PrevPos.y + 0.5);

    float Weight[4];
    Weight[0] = (1.0 - x) * (1.0 - y);
    Weight[1] = x * (1.0 - y);
    Weight[2] = (1.0 - x) * y;
    Weight[3] = x * y;

    {
        for (int SampleIdx = 0; SampleIdx < 4; ++SampleIdx)
        {
            int2 Location = PrevPosi + int2(SampleIdx & 0x01, SampleIdx >> 1);
            float PrevDepth = SamplePrevDepth(Location);
            Weight[SampleIdx] *= float(IsDepthSimilar(CurrDepth, PrevDepth));
            Weight[SampleIdx] *= float(IsInsideScreenMinusOne(Location, int2(g_CurrCamera.f4ViewportSize.xy)));
        }
    }

    float WeightSum = 0.0;
    float OcclusionSum = 0.0;
    float HistorySum = 0.0;

    {
        for (int SampleIdx = 0; SampleIdx < 4; ++SampleIdx)
        {
            int2 Location = PrevPosi + int2(SampleIdx & 0x01, SampleIdx >> 1);
            OcclusionSum += Weight[SampleIdx] * SamplePrevOcclusion(Location);
            HistorySum += Weight[SampleIdx] * min(16.0, SampleHistory(Location) + 1.0);;
            WeightSum += Weight[SampleIdx];
        }
    }

    Desc.IsSuccess = WeightSum > 0.0 && !g_SSAOAttribs.ResetAccumulation;
    Desc.Occlusion = Desc.IsSuccess ? OcclusionSum / WeightSum : 1.0;
    Desc.History = Desc.IsSuccess ? HistorySum / WeightSum : 1.0;
   
    return Desc;
}

PSOutput ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut)
{
    float4 Position = VSOut.f4PixelPos;
    float Depth = SampleCurrDepth(int2(Position.xy));
    if (IsBackground(Depth))
        discard;
        
    float2 Motion = SampleMotion(int2(Position.xy));
    float2 PrevLocation = Position.xy - Motion * g_CurrCamera.f4ViewportSize.xy;
    ProjectionDesc Reprojection = ComputeReprojection(PrevLocation, Depth);
  
    PSOutput Output;
    if (Reprojection.IsSuccess)
    {
        PixelStatistic PixelStat = ComputePixelStatistic(int2(Position.xy));
        
        float AspectRatio = g_CurrCamera.f4ViewportSize.x * g_CurrCamera.f4ViewportSize.w;
        float MotionFactor = saturate(1.025 - length(float2(Motion.x * AspectRatio, Motion.y)) * SSAO_TEMPORAL_MOTION_VECTOR_DIFF_FACTOR);
        float VarianceGamma = lerp(SSAO_TEMPORAL_MIN_VARIANCE_GAMMA, SSAO_TEMPORAL_MAX_VARIANCE_GAMMA, MotionFactor * MotionFactor);
        
        float OcclusionMin = PixelStat.Mean - VarianceGamma * PixelStat.StdDev;
        float OcclusionMax = PixelStat.Mean + VarianceGamma * PixelStat.StdDev;
        
        bool IsInsideRange = OcclusionMin < Reprojection.Occlusion && Reprojection.Occlusion < OcclusionMax;
        Reprojection.History = IsInsideRange ? Reprojection.History : max(1.0, MotionFactor * Reprojection.History);
    }
  
    float Alpha = rcp(Reprojection.History);
    Output.Occlusion = lerp(Reprojection.Occlusion, SampleCurrOcclusion(int2(Position.xy)), Alpha);
    Output.History = Reprojection.History;
    return Output;
}
