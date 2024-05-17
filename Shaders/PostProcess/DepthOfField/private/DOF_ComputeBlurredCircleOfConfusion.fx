#include "FullScreenTriangleVSOutput.fxh"
#include "DepthOfFieldStructures.fxh"
#include "PostFX_Common.fxh"

Texture2D<float> g_TextureCoC;
Texture2D<float> g_TextureGaussKernel;

float ComputeBlurredCoCPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    int2 TextureDimension;
    g_TextureCoC.GetDimensions(TextureDimension.x, TextureDimension.y);

    int2 Position = int2(VSOut.f4PixelPos.xy);

    float ResultSum = 0.0;
    for (int SampleIdx = -DOF_GAUSS_KERNEL_RADIUS; SampleIdx <= DOF_GAUSS_KERNEL_RADIUS; SampleIdx++)
    {
#if   DOF_CIRCLE_OF_CONFUSION_BLUR_TYPE == DOF_CIRCLE_OF_CONFUSION_BLUR_X
        float CoC = g_TextureCoC.Load(int3(ClampScreenCoord(Position + int2(SampleIdx, 0), TextureDimension), 0));
#elif DOF_CIRCLE_OF_CONFUSION_BLUR_TYPE == DOF_CIRCLE_OF_CONFUSION_BLUR_Y
        float CoC = g_TextureCoC.Load(int3(ClampScreenCoord(Position + int2(0, SampleIdx), TextureDimension), 0));
#endif
        float Weight = g_TextureGaussKernel.Load(int3(SampleIdx + DOF_GAUSS_KERNEL_RADIUS, 0, 0));
        ResultSum += CoC * Weight;
    }
        
    return ResultSum;
}
