#include "AtmosphereShadersCommon.fxh"

ScreenSizeQuadVSOutput ScreenSizeQuadVS(in uint VertexId : SV_VertexID,
                                        in uint InstID : SV_InstanceID,
                                        // IMPORTANT: PS input arguments must go in the same order as VS outputs.
                                        // Moreover, even if the shader is not using the argument,
                                        // it still must be declared.
                                        out float4 f4Pos : SV_Position)
{
    ScreenSizeQuadVSOutput VSOut;
    float4 Bounds = float4(-1.0, -1.0, 1.0, 1.0);
    
    float2 PosXY[4];
    PosXY[0] = Bounds.xy;
    PosXY[1] = Bounds.xw;
    PosXY[2] = Bounds.zy;
    PosXY[3] = Bounds.zw;

    float2 f2XY = PosXY[VertexId];
    VSOut.m_f2PosPS = f2XY;
    VSOut.m_fInstID = float( InstID );
    
    // Write 0 to the depth buffer
    // NDC_MIN_Z ==  0 in DX
    // NDC_MIN_Z == -1 in GL
    float z = NDC_MIN_Z;
    f4Pos = float4(f2XY, z, 1.0);

    return VSOut;
}
