#include "BasicStructures.fxh"
#include "FullScreenTriangleVSOutput.fxh"
#include "PostFX_Common.fxh"

cbuffer cbCameraAttribs
{
    CameraAttribs g_CurrCamera;
    CameraAttribs g_PrevCamera;
}

Texture2D<float> g_TextureDepth;

float SampleDepth(int2 PixelCoord)
{
    return g_TextureDepth.Load(int3(PixelCoord, 0));
}

float ComputeReprojectedDepthPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    float4 Position = VSOut.f4PixelPos;
    float Depth = SampleDepth(int2(Position.xy));

    float3 CurrScreenCoord = float3(Position.xy * g_CurrCamera.f4ViewportSize.zw, Depth);
    CurrScreenCoord.xy += F3NDC_XYZ_TO_UVD_SCALE.xy * g_CurrCamera.f2Jitter;

    float3 WorldPosition = InvProjectPosition(CurrScreenCoord, g_CurrCamera.mViewProjInv);
    float3 PrevScreenCoord = ProjectPosition(WorldPosition, g_PrevCamera.mViewProj);

    return PrevScreenCoord.z;
}
