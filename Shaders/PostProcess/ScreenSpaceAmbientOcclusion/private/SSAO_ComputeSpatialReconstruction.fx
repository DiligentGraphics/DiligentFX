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

Texture2D<float> g_TextureOcclusion;
Texture2D<float> g_TextureDepth;

float SampleOcclusion(int2 PixelCoord)
{
    return g_TextureOcclusion.Load(int3(PixelCoord, 0));
}

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float GuidedFilter(int2 Position)
{
    float X = 0.0f;
    float Y = 0.0f;
    float XY = 0.0f;
    float X2 = 0.0f;
    float X3 = 0.0f;
    float X4 = 0.0f;
    float X2Y = 0.0f;

    float WeightSum = 0.0;
    for (int x = -SSAO_SPATIAL_RECONSTRUCTION_RADIUS; x <= SSAO_SPATIAL_RECONSTRUCTION_RADIUS; x++)
    {
        for (int y = -SSAO_SPATIAL_RECONSTRUCTION_RADIUS; y <= SSAO_SPATIAL_RECONSTRUCTION_RADIUS; y++)
        {
            int2 Location = ClampScreenCoord(Position + int2(x, y), int2(g_Camera.f4ViewportSize.xy));
          
            float SampledSignal = SampleOcclusion(Location);
            float SampledGuided = DepthToCameraZ(SampleDepth(Location), g_Camera.mProj);
            float WeightSpatial = exp(-0.5 * float(x * x + y * y) / (SSAO_SPATIAL_RECONSTRUCTION_SIGMA * SSAO_SPATIAL_RECONSTRUCTION_SIGMA));

            X += WeightSpatial * SampledGuided;
            Y += WeightSpatial * SampledSignal;
            XY += WeightSpatial * SampledGuided * SampledSignal;
            X2 += WeightSpatial * SampledGuided * SampledGuided;
            X3 += WeightSpatial * SampledGuided * SampledGuided * SampledGuided;
            X4 += WeightSpatial * SampledGuided * SampledGuided * (SampledGuided * SampledGuided);
            X2Y += WeightSpatial * SampledGuided * SampledGuided * SampledSignal;

            WeightSum += WeightSpatial;
        }
    }

    X /= WeightSum;
    Y /= WeightSum;
    XY /= WeightSum;
    X2 /= WeightSum;
    X3 /= WeightSum;
    X4 /= WeightSum;
    X2Y /= WeightSum;

    float CYX = XY - X * Y;
    float CYX2 = X2Y - X2 * Y;
    float CXX2 = X3 - X2 * X;
    float VX1 = X2 - X * X;
    float VX2 = X4 - X2 * X2;

    float Divider = (VX1 * VX2 - CXX2 * CXX2);
    float LinearDepth = DepthToCameraZ(SampleDepth(Position.xy), g_Camera.mProj);

    float Beta1 = (CYX * VX2 - CYX2 * CXX2) / Divider;
    float Beta2 = (CYX2 * VX1 - CYX * CXX2) / Divider;
    float Alpha = Y - Beta1 * X - Beta2 * X2;
    return Beta1 * LinearDepth + Beta2 * LinearDepth * LinearDepth + Alpha;
}

float BilateralFilter(int2 Position)
{
    float LinearDepth = DepthToCameraZ(SampleDepth(Position), g_Camera.mProj);
    float OcclusionSum = 1.e-8f;
    float WeightSum = 1.e-8f;
    for (int x = -SSAO_SPATIAL_RECONSTRUCTION_RADIUS; x <= SSAO_SPATIAL_RECONSTRUCTION_RADIUS; x++)
    {
        for (int y = -SSAO_SPATIAL_RECONSTRUCTION_RADIUS; y <= SSAO_SPATIAL_RECONSTRUCTION_RADIUS; y++)
        {
            int2 Location = ClampScreenCoord(Position + int2(x, y), int2(g_Camera.f4ViewportSize.xy));
          
            float SampledSignal = SampleOcclusion(Location);
            float SampledGuided = DepthToCameraZ(SampleDepth(Location), g_Camera.mProj);
            float WeightS = exp(-0.5 * float(x * x + y * y) / (SSAO_SPATIAL_RECONSTRUCTION_SIGMA * SSAO_SPATIAL_RECONSTRUCTION_SIGMA));
            float WeightZ = exp(-0.5 * pow(LinearDepth - SampledGuided, 2.0) / (SSAO_SPATIAL_RECONSTRUCTION_DEPTH_SIGMA * SSAO_SPATIAL_RECONSTRUCTION_DEPTH_SIGMA));

            OcclusionSum += WeightS * WeightZ * SampledSignal;
            WeightSum    += WeightS * WeightZ;
        }
    }
    return OcclusionSum / WeightSum;
}

// Implemented based on this article
// https://bartwronski.com/2019/09/22/local-linear-models-guided-filter/
// https://colab.research.google.com/github/bartwronski/BlogPostsExtraMaterial/blob/master/Bilateral_and_guided_SSAO.ipynb
float ComputeSpatialReconstructionPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;
    if (IsBackground(SampleDepth(int2(Position.xy))))
        return 1.0;

#if SSAO_OPTION_GUIDED_FILTER
    return GuidedFilter(int2(Position.xy));
#else
    return BilateralFilter(int2(Position.xy));
#endif

}
