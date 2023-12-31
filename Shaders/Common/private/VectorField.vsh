struct VectorFieldRenderAttribs
{
    float4 ScaleAndBias;

    float fGridWidth;
    float fGridHeight;
    uint  uGridWidth;
    uint  uGridHeight;
    
    float4 Colors[2];
};

cbuffer cbAttribs
{
    VectorFieldRenderAttribs g_Attribs;
}

Texture2D    g_tex2DVectorField;
SamplerState g_tex2DVectorField_sampler;

void main(in  uint   VertexId : SV_VertexID,
          out float4 Pos      : SV_Position,
          out float3 Color    : COLOR)
{
    uint Col = (VertexId / 2u) % g_Attribs.uGridWidth;
    uint Row = (VertexId / 2u) / g_Attribs.uGridWidth;
    
    float2 UV = float2(
        (float(Col) + 0.5) / g_Attribs.fGridWidth,
        (float(Row) + 0.5) / g_Attribs.fGridHeight);
    
    float2 MotionVec = g_tex2DVectorField.SampleLevel(g_tex2DVectorField_sampler, UV, 0.0).xy;
    MotionVec = (MotionVec + g_Attribs.ScaleAndBias.zw) * g_Attribs.ScaleAndBias.xy;
#   if !defined(DESKTOP_GL) && !defined(GL_ES)
    {
        MotionVec.y = -MotionVec.y;
    }
#   endif
    
    float2 PosXY = UV * float2(2.0, 2.0) - float2(1.0, 1.0);    
    if ((VertexId & 0x01u) != 0u)
    {
        PosXY += MotionVec;
    }
#   if !defined(DESKTOP_GL) && !defined(GL_ES)
    {
        PosXY.y = -PosXY.y;
    }
#   endif
    
    Pos   = float4(PosXY, 0.0, 1.0);
    Color = g_Attribs.Colors[VertexId & 0x01u].rgb;
}
