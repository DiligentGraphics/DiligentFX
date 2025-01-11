#include "FullScreenTriangleVSOutput.fxh"

Texture2D<float> g_TextureCoC;

float ComputeSeparatedCoCPS(in FullScreenTriangleVSOutput VSOut) : SV_Target0 
{
    float2 Position = VSOut.f4PixelPos.xy;

    float CoC = g_TextureCoC.Load(int3(Position, 0));
    return abs(CoC) * float(CoC < 0.0);
}
