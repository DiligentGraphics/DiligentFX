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

float LoadCurrOcclusion(int2 PixelCoord)
{
    return g_TextureCurrOcclusion.Load(int3(PixelCoord, 0));
}

float LoadCurrDepth(int2 PixelCoord)
{
    return g_TextureCurrDepth.Load(int3(PixelCoord, 0));
}

float LoadPrevOcclusion(int2 PixelCoord)
{
    return g_TexturePrevOcclusion.Load(int3(PixelCoord, 0));
}

float LoadPrevDepth(int2 PixelCoord)
{
    return g_TexturePrevDepth.Load(int3(PixelCoord, 0));
}

float LoadHistory(int2 PixelCoord)
{
    return g_TextureHistory.Load(int3(PixelCoord, 0));
}

float2 LoadMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

float IsCameraZSimilar(float CurrCamZ, float PrevCamZ)
{
    return abs(1.0 - CurrCamZ / PrevCamZ) < SSAO_DISOCCLUSION_DEPTH_THRESHOLD ? 1.0 : 0.0;
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
            float SampleOcclusion = LoadCurrOcclusion(Location);
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
    float CurrCamZ = DepthToCameraZ(CurrDepth, g_CurrCamera.mProj);

    int4   FetchCoords;
    float4 Weights;
    GetBilinearSamplingInfoUC(PrevPos, int2(g_CurrCamera.f4ViewportSize.xy), FetchCoords, Weights);
 
    float PrevCamZ00 = DepthToCameraZ(LoadPrevDepth(FetchCoords.xy), g_PrevCamera.mProj);
    float PrevCamZ10 = DepthToCameraZ(LoadPrevDepth(FetchCoords.zy), g_PrevCamera.mProj);
    float PrevCamZ01 = DepthToCameraZ(LoadPrevDepth(FetchCoords.xw), g_PrevCamera.mProj);
    float PrevCamZ11 = DepthToCameraZ(LoadPrevDepth(FetchCoords.zw), g_PrevCamera.mProj);
    
    Weights.x *= IsCameraZSimilar(CurrCamZ, PrevCamZ00);
    Weights.y *= IsCameraZSimilar(CurrCamZ, PrevCamZ10);
    Weights.z *= IsCameraZSimilar(CurrCamZ, PrevCamZ01);
    Weights.w *= IsCameraZSimilar(CurrCamZ, PrevCamZ11);

    float TotalWeight = dot(Weights, float4(1.0, 1.0, 1.0, 1.0));
    
    ProjectionDesc Desc;
    Desc.Occlusion = 1.0;
    Desc.History   = 1.0;
    Desc.IsSuccess = TotalWeight > 0.01 && !g_SSAOAttribs.ResetAccumulation;
    if (Desc.IsSuccess)
    {
        float4 PrevOcclusion = float4(
            LoadPrevOcclusion(FetchCoords.xy),
            LoadPrevOcclusion(FetchCoords.zy),
            LoadPrevOcclusion(FetchCoords.xw),
            LoadPrevOcclusion(FetchCoords.zw)
        );
        float4 History = float4(
            LoadHistory(FetchCoords.xy),
            LoadHistory(FetchCoords.zy),
            LoadHistory(FetchCoords.xw),
            LoadHistory(FetchCoords.zw)
        );
        History = min(float4(16.0, 16.0, 16.0, 16.0), History + float4(1.0, 1.0, 1.0, 1.0));
        Desc.Occlusion = dot(PrevOcclusion, Weights) / TotalWeight;
        Desc.History = dot(History, Weights) / TotalWeight;
    }
   
    return Desc;
}

PSOutput ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut)
{
    float4 Position = VSOut.f4PixelPos;
    float Depth = LoadCurrDepth(int2(Position.xy));
    if (IsBackground(Depth))
        discard;
        
    float2 Motion = LoadMotion(int2(Position.xy));
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
    Output.Occlusion = lerp(Reprojection.Occlusion, LoadCurrOcclusion(int2(Position.xy)), Alpha);
    Output.History = Reprojection.History;
    return Output;
}
