"#include \"AtmosphereShadersCommon.fxh\"\n"
"\n"
"cbuffer cbCameraAttribs\n"
"{\n"
"    CameraAttribs g_CameraAttribs;\n"
"}\n"
"\n"
"cbuffer cbLightParams\n"
"{\n"
"    LightAttribs g_LightAttribs;\n"
"}\n"
"\n"
"#define fSunAngularRadius (32.0/2.0 / 60.0 * ((2.0 * PI)/180.0)) // Sun angular DIAMETER is 32 arc minutes\n"
"#define fTanSunAngularRadius tan(fSunAngularRadius)\n"
"\n"
"struct SunVSOutput\n"
"{\n"
"    float2 f2PosPS : PosPS; // Position in projection space [-1,1]x[-1,1]\n"
"};\n"
"\n"
"void SunVS(in uint VertexId : SV_VertexID,\n"
"           out SunVSOutput VSOut, \n"
"           // IMPORTANT: non-system generated pixel shader input\n"
"           // arguments must have the exact same name as vertex shader \n"
"           // outputs and must go in the same order.\n"
"           // Moreover, even if the shader is not using the argument,\n"
"           // it still must be declared.\n"
"\n"
"           out float4 f4Pos : SV_Position)\n"
"{\n"
"    float2 fCotanHalfFOV = float2( MATRIX_ELEMENT(g_CameraAttribs.mProj, 0, 0), MATRIX_ELEMENT(g_CameraAttribs.mProj, 1, 1) );\n"
"    float2 f2SunScreenPos = g_LightAttribs.f4LightScreenPos.xy;\n"
"    float2 f2SunScreenSize = fTanSunAngularRadius * fCotanHalfFOV;\n"
"    float4 MinMaxUV = f2SunScreenPos.xyxy + float4(-1.0, -1.0, 1.0, 1.0) * f2SunScreenSize.xyxy;\n"
" \n"
"    float2 Verts[4];\n"
"    Verts[0] = MinMaxUV.xy;\n"
"    Verts[1] = MinMaxUV.xw;\n"
"    Verts[2] = MinMaxUV.zy;\n"
"    Verts[3] = MinMaxUV.zw;\n"
"\n"
"    VSOut.f2PosPS = Verts[VertexId];\n"
"    f4Pos = float4(Verts[VertexId], 1.0, 1.0);\n"
"}\n"
"\n"
"void SunPS(SunVSOutput VSOut,\n"
"           out float4 f4Color : SV_Target)\n"
"{\n"
"    float2 fCotanHalfFOV = float2( MATRIX_ELEMENT(g_CameraAttribs.mProj, 0, 0), MATRIX_ELEMENT(g_CameraAttribs.mProj, 1, 1) );\n"
"    float2 f2SunScreenSize = fTanSunAngularRadius * fCotanHalfFOV;\n"
"    float2 f2dXY = (VSOut.f2PosPS - g_LightAttribs.f4LightScreenPos.xy) / f2SunScreenSize;\n"
"    f4Color.rgb = sqrt(saturate(1.0 - dot(f2dXY, f2dXY))) * F3ONE;\n"
"    f4Color.a = 1.0;\n"
"}\n"