"#include \"ToneMapping.fxh\"\n"
"#include \"HnPostProcessStructures.fxh\"\n"
"\n"
"struct PSInput\n"
"{\n"
"    float4 Pos : SV_POSITION;\n"
"};\n"
"\n"
"cbuffer cbPostProcessAttribs\n"
"{\n"
"    PostProcessAttribs g_Attribs;\n"
"}\n"
"\n"
"Texture2D g_ColorBuffer;\n"
"Texture2D g_SelectionDepth;\n"
"Texture2D g_Depth;\n"
"\n"
"void main(in PSInput PSIn,\n"
"          out float4 Color : SV_Target0)\n"
"{\n"
"    Color = g_ColorBuffer.Load(int3(PSIn.Pos.xy, 0));\n"
"\n"
"#if TONE_MAPPING_MODE > TONE_MAPPING_MODE_NONE\n"
"    Color.rgb = ToneMap(Color.rgb, g_Attribs.ToneMapping, g_Attribs.AverageLogLum);\n"
"#endif\n"
"\n"
"    float SelectionDepth = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2(0.0, 0.0), 0)).r;\n"
"    float Depth          = g_Depth.Load(int3(PSIn.Pos.xy + float2(0.0, 0.0), 0)).r;\n"
"\n"
"    float IsSelected0 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2(-1.0, -1.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"    float IsSelected1 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2( 0.0, -1.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"    float IsSelected2 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2(+1.0, -1.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"\n"
"    float IsSelected3 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2(-1.0, 0.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"    float IsSelected4 = SelectionDepth                                                    != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"    float IsSelected5 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2(+1.0, 0.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"\n"
"    float IsSelected6 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2(-1.0, +1.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"    float IsSelected7 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2( 0.0, +1.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"    float IsSelected8 = g_SelectionDepth.Load(int3(PSIn.Pos.xy + float2(+1.0, +1.0), 0)).r != g_Attribs.ClearDepth ? 1.0 : 0.0;\n"
"\n"
"    float Outline = IsSelected0 + IsSelected1 + IsSelected2 + IsSelected3 + IsSelected4 + IsSelected5 + IsSelected6 + IsSelected7 + IsSelected8;\n"
"    Outline = (Outline > 4.0 && Outline < 9.0) ? 1.0 : 0.0;\n"
"\n"
"    float DesatFactor = saturate(1.0 - IsSelected4);\n"
"    if (SelectionDepth != Depth)\n"
"        DesatFactor = 1.0;\n"
"    DesatFactor *= g_Attribs.NonselectionDesaturationFactor;\n"
"\n"
"    float Luminance = dot(Color.rgb, float3(0.2126, 0.7152, 0.0722));\n"
"    Color.rgb = lerp(Color.rgb, float3(Luminance, Luminance, Luminance), DesatFactor);\n"
"\n"
"    Color.rgb = lerp(Color.rgb, g_Attribs.SelectionOutlineColor.rgb, Outline);\n"
"\n"
"#if CONVERT_OUTPUT_TO_SRGB\n"
"    Color.rgb = pow(Color.rgb, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));\n"
"#endif\n"
"\n"
"    Color.a = 1.0;\n"
"}\n"
