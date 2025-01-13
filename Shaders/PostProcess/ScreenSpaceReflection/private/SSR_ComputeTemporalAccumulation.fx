#include "ScreenSpaceReflectionStructures.fxh"
#include "SSR_Common.fxh"
#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_CurrCamera;
    CameraAttribs g_PrevCamera;
}

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

Texture2D<float2> g_TextureMotion;
Texture2D<float>  g_TextureHitDepth;

Texture2D<float>  g_TextureCurrDepth;
Texture2D<float4> g_TextureCurrRadiance;
Texture2D<float>  g_TextureCurrVariance;

Texture2D<float>  g_TexturePrevDepth;
Texture2D<float4> g_TexturePrevRadiance;
Texture2D<float>  g_TexturePrevVariance;

SamplerState g_TexturePrevDepth_sampler;
SamplerState g_TexturePrevRadiance_sampler;
SamplerState g_TexturePrevVariance_sampler;

struct ProjectionDesc
{
    float4 Color;
    float2 PrevCoord;
    bool IsSuccess;
};

struct PixelStatistic
{
    float4 Mean;
    float4 Variance;
    float4 StdDev;
};

struct PSOutput
{
    float4 Radiance : SV_Target0;
    float  Variance : SV_Target1;
};

float2 LoadMotion(int2 PixelCoord)
{
    return g_TextureMotion.Load(int3(PixelCoord, 0)) * F3NDC_XYZ_TO_UVD_SCALE.xy;
}

float LoadCurrDepth(int2 PixelCoord)
{
    return g_TextureCurrDepth.Load(int3(PixelCoord, 0));
}

float LoadPrevDepth(int2 PixelCoord)
{
    return g_TexturePrevDepth.Load(int3(PixelCoord, 0));
}

float LoadHitDepth(int2 PixelCoord)
{
    return g_TextureHitDepth.Load(int3(PixelCoord, 0));
}

float4 LoadCurrRadiance(int2 PixelCoord)
{
    return g_TextureCurrRadiance.Load(int3(PixelCoord, 0));
}

float LoadCurrVariance(int2 PixelCoord)
{
    return g_TextureCurrVariance.Load(int3(PixelCoord, 0));
}

float4 LoadPrevRadiance(int2 PixelCoord)
{
    return g_TexturePrevRadiance.Load(int3(PixelCoord, 0));
}

float LoadPrevVariance(int2 PixelCoord)
{
    return g_TexturePrevVariance.Load(int3(PixelCoord, 0));
}

float4 SamplePrevRadianceLinear(float2 PixelCoord)
{
    float2 Texcoord = PixelCoord * g_CurrCamera.f4ViewportSize.zw;
    return g_TexturePrevRadiance.SampleLevel(g_TexturePrevRadiance_sampler, Texcoord, 0);
}

float SamplePrevVarianceLinear(float2 PixelCoord)
{
    float2 Texcoord = PixelCoord * g_CurrCamera.f4ViewportSize.zw;
    return g_TexturePrevVariance.SampleLevel(g_TexturePrevVariance_sampler, Texcoord, 0);
}

float2 ComputeReflectionHitPosition(int2 PixelCoord, float Depth)
{
    float2 Texcoord = (float2(PixelCoord) + 0.5) * g_CurrCamera.f4ViewportSize.zw + F3NDC_XYZ_TO_UVD_SCALE.xy * g_CurrCamera.f2Jitter;
    float3 PositionWS = InvProjectPosition(float3(Texcoord, Depth), g_CurrCamera.mViewProjInv);
    float3 PrevCoordUV = ProjectPosition(PositionWS, g_PrevCamera.mViewProj);
    return (PrevCoordUV.xy - F3NDC_XYZ_TO_UVD_SCALE.xy * g_PrevCamera.f2Jitter) * g_CurrCamera.f4ViewportSize.xy;
}

// TODO: Use normals to compute disocclusion
float ComputeDisocclusion(float CurrCameraZ, float PrevCameraZ)
{
    CurrCameraZ = abs(CurrCameraZ);
    PrevCameraZ = abs(PrevCameraZ);
    return exp(-abs(CurrCameraZ - PrevCameraZ) / max(max(CurrCameraZ, PrevCameraZ), 1e-6));
}

// Welford's online algorithm:
//  https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
PixelStatistic ComputePixelStatistic(int2 PixelCoord)
{
    PixelStatistic Desc;
    float4 M1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 M2 = float4(0.0, 0.0, 0.0, 0.0);

    const int StatisticRadius = 1;
    for (int x = -StatisticRadius; x <= StatisticRadius; x++)
    {
        for (int y = -StatisticRadius; y <= StatisticRadius; y++)
        {
            int2 Location = ClampScreenCoord(PixelCoord + int2(x, y), int2(g_CurrCamera.f4ViewportSize.xy));
            float4 SampleColor = g_TextureCurrRadiance.Load(int3(Location, 0));

            M1 += SampleColor;
            M2 += SampleColor * SampleColor;
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
 
    ProjectionDesc Desc;
 
    {
        float PrevCamZ = DepthToCameraZ(LoadPrevDepth(int2(PrevPos)), g_PrevCamera.mProj);     
        Desc.PrevCoord = PrevPos;
        Desc.Color     = SamplePrevRadianceLinear(Desc.PrevCoord);
        Desc.IsSuccess = ComputeDisocclusion(CurrCamZ, PrevCamZ) > SSR_DISOCCLUSION_THRESHOLD;
    }
    
    int2 DepthDim;
    g_TextureCurrDepth.GetDimensions(DepthDim.x, DepthDim.y);
    if (!Desc.IsSuccess)
    {
        float4 BestWeights     = float4(0.0, 0.0, 0.0, 0.0);
        int4   BestFetchCoords = int4(0, 0, 0, 0);
        float  BestTotalWeight = 0.0;

        const int   SearchRadius = 1;
        const float BestTotalWeightEarlyExitThreshold = 0.9;
        for (int y = -SearchRadius; y <= SearchRadius; y++)
        {
            for (int x = -SearchRadius; x <= SearchRadius; x++)
            {
                float2 Location = PrevPos + float2(x, y);

                int4   FetchCoords;
                float4 Weights;
                GetBilinearSamplingInfoUC(Location, DepthDim, FetchCoords, Weights);
 
                float PrevZ00 = DepthToCameraZ(LoadPrevDepth(FetchCoords.xy), g_PrevCamera.mProj);
                float PrevZ10 = DepthToCameraZ(LoadPrevDepth(FetchCoords.zy), g_PrevCamera.mProj);
                float PrevZ01 = DepthToCameraZ(LoadPrevDepth(FetchCoords.xw), g_PrevCamera.mProj);
                float PrevZ11 = DepthToCameraZ(LoadPrevDepth(FetchCoords.zw), g_PrevCamera.mProj);
 
                Weights.x *= ComputeDisocclusion(CurrCamZ, PrevZ00) > (SSR_DISOCCLUSION_THRESHOLD / 2.0) ? 1.0 : 0.0;
                Weights.y *= ComputeDisocclusion(CurrCamZ, PrevZ10) > (SSR_DISOCCLUSION_THRESHOLD / 2.0) ? 1.0 : 0.0;
                Weights.z *= ComputeDisocclusion(CurrCamZ, PrevZ01) > (SSR_DISOCCLUSION_THRESHOLD / 2.0) ? 1.0 : 0.0;
                Weights.w *= ComputeDisocclusion(CurrCamZ, PrevZ11) > (SSR_DISOCCLUSION_THRESHOLD / 2.0) ? 1.0 : 0.0;

                float TotalWeight = dot(Weights, float4(1.0, 1.0, 1.0, 1.0));
                if (TotalWeight > BestTotalWeight)
                {
                    BestTotalWeight = TotalWeight;
                    BestWeights     = Weights;
                    BestFetchCoords = FetchCoords;
                    Desc.PrevCoord  = Location;
                    
                    if (BestTotalWeight > BestTotalWeightEarlyExitThreshold)
                        break;
                }
            }
            
            if (BestTotalWeight > BestTotalWeightEarlyExitThreshold)
                break; 
        }

        Desc.IsSuccess = BestTotalWeight > 0.1;
        if (Desc.IsSuccess)
        {
            Desc.Color = (
                LoadPrevRadiance(BestFetchCoords.xy) * BestWeights.x + 
                LoadPrevRadiance(BestFetchCoords.zy) * BestWeights.y +
                LoadPrevRadiance(BestFetchCoords.xw) * BestWeights.z +
                LoadPrevRadiance(BestFetchCoords.zw) * BestWeights.w
            ) / BestTotalWeight;
        }
    }

    Desc.IsSuccess = Desc.IsSuccess && IsInsideScreen(Desc.PrevCoord, g_CurrCamera.f4ViewportSize.xy);
    return Desc;
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
PSOutput ComputeTemporalAccumulationPS(in FullScreenTriangleVSOutput VSOut)
{
    float4 Position = VSOut.f4PixelPos;

    // Secondary reprojection based on ray lengths:
    // https://www.ea.com/seed/news/seed-dd18-presentation-slides-raytracing (Slide 45)
    PixelStatistic PixelStat = ComputePixelStatistic(int2(Position.xy));
    float Depth = LoadCurrDepth(int2(Position.xy));
    float HitDepth = LoadHitDepth(int2(Position.xy));
    float2 Motion = LoadMotion(int2(Position.xy));

    float2 PrevIncidentPoint = Position.xy - Motion * float2(g_CurrCamera.f4ViewportSize.xy);
    float2 PrevReflectionHit = ComputeReflectionHitPosition(int2(Position.xy), HitDepth);

    float4 PrevColorIncidentPoint = SamplePrevRadianceLinear(PrevIncidentPoint); 
    float4 PrevColorReflectionHit = SamplePrevRadianceLinear(PrevReflectionHit);

    float PrevDistanceIncidentPoint = abs(Luminance(PrevColorIncidentPoint.rgb) - Luminance(PixelStat.Mean.rgb));
    float PrevDistanceReflectionHit = abs(Luminance(PrevColorReflectionHit.rgb) - Luminance(PixelStat.Mean.rgb));

    float2 PrevCoord = PrevDistanceIncidentPoint < PrevDistanceReflectionHit ? PrevIncidentPoint : PrevReflectionHit;
    ProjectionDesc Reprojection = ComputeReprojection(PrevCoord, Depth);

    PSOutput Output;
    if (Reprojection.IsSuccess)
    {
        float4 ColorMin = PixelStat.Mean - SSR_TEMPORAL_VARIANCE_GAMMA * PixelStat.StdDev;
        float4 ColorMax = PixelStat.Mean + SSR_TEMPORAL_VARIANCE_GAMMA * PixelStat.StdDev;
        float4 PrevRadiance = clamp(Reprojection.Color, ColorMin, ColorMax);
        float  PrevVariance = SamplePrevVarianceLinear(Reprojection.PrevCoord);
        Output.Radiance = lerp(LoadCurrRadiance(int2(Position.xy)), PrevRadiance, g_SSRAttribs.TemporalRadianceStabilityFactor);
        Output.Variance = lerp(LoadCurrVariance(int2(Position.xy)), PrevVariance, g_SSRAttribs.TemporalVarianceStabilityFactor);
    }
    else
    {
        Output.Radiance = LoadCurrRadiance(int2(Position.xy));
        Output.Variance = 1.0f;
    }
    return Output;
}
