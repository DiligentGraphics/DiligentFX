#include "FullScreenTriangleVSOutput.fxh"

Texture2D<float> g_TextureDepth;
            
float CopyDepthPS(FullScreenTriangleVSOutput VSOut) : SV_Target0
{
    return g_TextureDepth.Load(int3(VSOut.f4PixelPos.xy, 0));
}
