"\n"
"#define NON_LINEAR_PARAMETERIZATION 1\n"
"#define SafetyHeightMargin 16.0\n"
"#define HeightPower 0.5\n"
"#define ViewZenithPower 0.2\n"
"#define SunViewPower 1.5\n"
"\n"
"\n"
"float GetCosHorizonAnlge(float fHeight)\n"
"{\n"
"    // Due to numeric precision issues, fHeight might sometimes be slightly negative\n"
"    fHeight = max(fHeight, 0.0);\n"
"    return -sqrt(fHeight * (2.0*EARTH_RADIUS + fHeight) ) / (EARTH_RADIUS + fHeight);\n"
"}\n"
"\n"
"float ZenithAngle2TexCoord(float fCosZenithAngle, float fHeight, in float fTexDim, float power, float fPrevTexCoord)\n"
"{\n"
"    fCosZenithAngle = fCosZenithAngle;\n"
"    float fTexCoord;\n"
"    float fCosHorzAngle = GetCosHorizonAnlge(fHeight);\n"
"    // When performing look-ups into the scattering texture, it is very important that all the look-ups are consistent\n"
"    // wrt to the horizon. This means that if the first look-up is above (below) horizon, then the second look-up\n"
"    // should also be above (below) horizon. \n"
"    // We use previous texture coordinate, if it is provided, to find out if previous look-up was above or below\n"
"    // horizon. If texture coordinate is negative, then this is the first look-up\n"
"    bool bIsAboveHorizon = fPrevTexCoord >= 0.5;\n"
"    bool bIsBelowHorizon = 0.0 <= fPrevTexCoord && fPrevTexCoord < 0.5;\n"
"    if(  bIsAboveHorizon || \n"
"        !bIsBelowHorizon && (fCosZenithAngle > fCosHorzAngle) )\n"
"    {\n"
"        // Scale to [0,1]\n"
"        fTexCoord = saturate( (fCosZenithAngle - fCosHorzAngle) / (1.0 - fCosHorzAngle) );\n"
"        fTexCoord = pow(fTexCoord, power);\n"
"        // Now remap texture coordinate to the upper half of the texture.\n"
"        // To avoid filtering across discontinuity at 0.5, we must map\n"
"        // the texture coordinate to [0.5 + 0.5/fTexDim, 1 - 0.5/fTexDim]\n"
"        //\n"
"        //      0.5   1.5               D/2+0.5        D-0.5  texture coordinate x dimension\n"
"        //       |     |                   |            |\n"
"        //    |  X  |  X  | .... |  X  ||  X  | .... |  X  |  \n"
"        //       0     1          D/2-1   D/2          D-1    texel index\n"
"        //\n"
"        fTexCoord = 0.5 + 0.5 / fTexDim + fTexCoord * (fTexDim/2.0 - 1.0) / fTexDim;\n"
"    }\n"
"    else\n"
"    {\n"
"        fTexCoord = saturate( (fCosHorzAngle - fCosZenithAngle) / (fCosHorzAngle - (-1.0)) );\n"
"        fTexCoord = pow(fTexCoord, power);\n"
"        // Now remap texture coordinate to the lower half of the texture.\n"
"        // To avoid filtering across discontinuity at 0.5, we must map\n"
"        // the texture coordinate to [0.5, 0.5 - 0.5/fTexDim]\n"
"        //\n"
"        //      0.5   1.5        D/2-0.5             texture coordinate x dimension\n"
"        //       |     |            |       \n"
"        //    |  X  |  X  | .... |  X  ||  X  | .... \n"
"        //       0     1          D/2-1   D/2        texel index\n"
"        //\n"
"        fTexCoord = 0.5f / fTexDim + fTexCoord * (fTexDim/2.0 - 1.0) / fTexDim;\n"
"    }    \n"
"\n"
"    return fTexCoord;\n"
"}\n"
"\n"
"float TexCoord2ZenithAngle(float fTexCoord, float fHeight, in float fTexDim, float power)\n"
"{\n"
"    float fCosZenithAngle;\n"
"\n"
"    float fCosHorzAngle = GetCosHorizonAnlge(fHeight);\n"
"    if( fTexCoord > 0.5 )\n"
"    {\n"
"        // Remap to [0,1] from the upper half of the texture [0.5 + 0.5/fTexDim, 1 - 0.5/fTexDim]\n"
"        fTexCoord = saturate( (fTexCoord - (0.5 + 0.5 / fTexDim)) * fTexDim / (fTexDim/2.0 - 1.0) );\n"
"        fTexCoord = pow(fTexCoord, 1.0/power);\n"
"        // Assure that the ray does NOT hit Earth\n"
"        fCosZenithAngle = max( (fCosHorzAngle + fTexCoord * (1.0 - fCosHorzAngle)), fCosHorzAngle + 1e-4);\n"
"    }\n"
"    else\n"
"    {\n"
"        // Remap to [0,1] from the lower half of the texture [0.5, 0.5 - 0.5/fTexDim]\n"
"        fTexCoord = saturate((fTexCoord - 0.5 / fTexDim) * fTexDim / (fTexDim/2.0 - 1.0));\n"
"        fTexCoord = pow(fTexCoord, 1.0/power);\n"
"        // Assure that the ray DOES hit Earth\n"
"        fCosZenithAngle = min( (fCosHorzAngle - fTexCoord * (fCosHorzAngle - (-1.0))), fCosHorzAngle - 1e-4);\n"
"    }\n"
"    return fCosZenithAngle;\n"
"}\n"
"\n"
"\n"
"void InsctrLUTCoords2WorldParams(in float4 f4UVWQ,\n"
"                                 out float fHeight,\n"
"                                 out float fCosViewZenithAngle,\n"
"                                 out float fCosSunZenithAngle,\n"
"                                 out float fCosSunViewAngle)\n"
"{\n"
"#if NON_LINEAR_PARAMETERIZATION\n"
"    // Rescale to exactly 0,1 range\n"
"    f4UVWQ.xzw = saturate(( f4UVWQ * PRECOMPUTED_SCTR_LUT_DIM - float4(0.5,0.5,0.5,0.5) ) / ( PRECOMPUTED_SCTR_LUT_DIM - float4(1.0,1.0,1.0,1.0) )).xzw;\n"
"\n"
"    f4UVWQ.x = pow( f4UVWQ.x, 1.0/HeightPower );\n"
"    // Allowable height range is limited to [SafetyHeightMargin, AtmTopHeight - SafetyHeightMargin] to\n"
"    // avoid numeric issues at the Earth surface and the top of the atmosphere\n"
"    fHeight = f4UVWQ.x * (g_MediaParams.fAtmTopHeight - 2.0*SafetyHeightMargin) + SafetyHeightMargin;\n"
"\n"
"    fCosViewZenithAngle = TexCoord2ZenithAngle(f4UVWQ.y, fHeight, PRECOMPUTED_SCTR_LUT_DIM.y, ViewZenithPower);\n"
"    \n"
"    // Use Eric Bruneton\'s formula for cosine of the sun-zenith angle\n"
"    fCosSunZenithAngle = tan((2.0 * f4UVWQ.z - 1.0 + 0.26) * 1.1) / tan(1.26 * 1.1);\n"
"\n"
"    f4UVWQ.w = sign(f4UVWQ.w - 0.5) * pow( abs((f4UVWQ.w - 0.5)*2.0), 1.0/SunViewPower)/2.0 + 0.5;\n"
"    fCosSunViewAngle = cos(f4UVWQ.w*PI);\n"
"#else\n"
"    // Rescale to exactly 0,1 range\n"
"    f4UVWQ = (f4UVWQ * PRECOMPUTED_SCTR_LUT_DIM - float4(0.5,0.5,0.5,0.5)) / (PRECOMPUTED_SCTR_LUT_DIM-float4(1.0,1.0,1.0,1.0));\n"
"\n"
"    // Allowable height range is limited to [SafetyHeightMargin, AtmTopHeight - SafetyHeightMargin] to\n"
"    // avoid numeric issues at the Earth surface and the top of the atmosphere\n"
"    fHeight = f4UVWQ.x * (g_MediaParams.fAtmTopHeight - 2*SafetyHeightMargin) + SafetyHeightMargin;\n"
"\n"
"    fCosViewZenithAngle = f4UVWQ.y * 2.0 - 1.0;\n"
"    fCosSunZenithAngle  = f4UVWQ.z * 2.0 - 1.0;\n"
"    fCosSunViewAngle    = f4UVWQ.w * 2.0 - 1.0;\n"
"#endif\n"
"\n"
"    fCosViewZenithAngle = clamp(fCosViewZenithAngle, -1.0, +1.0);\n"
"    fCosSunZenithAngle  = clamp(fCosSunZenithAngle,  -1.0, +1.0);\n"
"    // Compute allowable range for the cosine of the sun view angle for the given\n"
"    // view zenith and sun zenith angles\n"
"    float D = (1.0 - fCosViewZenithAngle * fCosViewZenithAngle) * (1.0 - fCosSunZenithAngle  * fCosSunZenithAngle);\n"
"    \n"
"    // !!!!  IMPORTANT NOTE regarding NVIDIA hardware !!!!\n"
"\n"
"    // There is a very weird issue on NVIDIA hardware with clamp(), saturate() and min()/max() \n"
"    // functions. No matter what function is used, fCosViewZenithAngle and fCosSunZenithAngle\n"
"    // can slightly fall outside [-1,+1] range causing D to be negative\n"
"    // Using saturate(D), max(D, 0) and even D>0?D:0 does not work!\n"
"    // The only way to avoid taking the square root of negative value and obtaining NaN is \n"
"    // to use max() with small positive value:\n"
"    D = sqrt( max(D, 1e-20) );\n"
"    \n"
"    // The issue was reproduceable on NV GTX 680, driver version 9.18.13.2723 (9/12/2013).\n"
"    // The problem does not arise on Intel hardware\n"
"\n"
"    float2 f2MinMaxCosSunViewAngle = fCosViewZenithAngle*fCosSunZenithAngle + float2(-D, +D);\n"
"    // Clamp to allowable range\n"
"    fCosSunViewAngle    = clamp(fCosSunViewAngle, f2MinMaxCosSunViewAngle.x, f2MinMaxCosSunViewAngle.y);\n"
"}\n"
"\n"
"float4 WorldParams2InsctrLUTCoords(float fHeight,\n"
"                                   float fCosViewZenithAngle,\n"
"                                   float fCosSunZenithAngle,\n"
"                                   float fCosSunViewAngle,\n"
"                                   in float4 f4RefUVWQ)\n"
"{\n"
"    float4 f4UVWQ;\n"
"\n"
"    // Limit allowable height range to [SafetyHeightMargin, AtmTopHeight - SafetyHeightMargin] to\n"
"    // avoid numeric issues at the Earth surface and the top of the atmosphere\n"
"    // (ray/Earth and ray/top of the atmosphere intersection tests are unstable when fHeight == 0 and\n"
"    // fHeight == AtmTopHeight respectively)\n"
"    fHeight = clamp(fHeight, SafetyHeightMargin, g_MediaParams.fAtmTopHeight - SafetyHeightMargin);\n"
"    f4UVWQ.x = saturate( (fHeight - SafetyHeightMargin) / (g_MediaParams.fAtmTopHeight - 2.0*SafetyHeightMargin) );\n"
"\n"
"#if NON_LINEAR_PARAMETERIZATION\n"
"    f4UVWQ.x = pow(f4UVWQ.x, HeightPower);\n"
"\n"
"    f4UVWQ.y = ZenithAngle2TexCoord(fCosViewZenithAngle, fHeight, PRECOMPUTED_SCTR_LUT_DIM.y, ViewZenithPower, f4RefUVWQ.y);\n"
"    \n"
"    // Use Eric Bruneton\'s formula for cosine of the sun-zenith angle\n"
"    f4UVWQ.z = (atan(max(fCosSunZenithAngle, -0.1975) * tan(1.26 * 1.1)) / 1.1 + (1.0 - 0.26)) * 0.5;\n"
"\n"
"    fCosSunViewAngle = clamp(fCosSunViewAngle, -1.0, +1.0);\n"
"    f4UVWQ.w = acos(fCosSunViewAngle) / PI;\n"
"    f4UVWQ.w = sign(f4UVWQ.w - 0.5) * pow( abs((f4UVWQ.w - 0.5)/0.5), SunViewPower)/2.0 + 0.5;\n"
"    \n"
"    f4UVWQ.xzw = ((f4UVWQ * (PRECOMPUTED_SCTR_LUT_DIM - F4ONE) + 0.5) / PRECOMPUTED_SCTR_LUT_DIM).xzw;\n"
"#else\n"
"    f4UVWQ.y = (fCosViewZenithAngle+1.f) / 2.f;\n"
"    f4UVWQ.z = (fCosSunZenithAngle +1.f) / 2.f;\n"
"    f4UVWQ.w = (fCosSunViewAngle   +1.f) / 2.f;\n"
"\n"
"    f4UVWQ = (f4UVWQ * (PRECOMPUTED_SCTR_LUT_DIM - float4(1.0,1.0,1.0,1.0)) + float4(0.5,0.5,0.5,0.5)) / PRECOMPUTED_SCTR_LUT_DIM;\n"
"#endif\n"
"\n"
"    return f4UVWQ;\n"
"}\n"
"\n"
"float4 WorldParams2InsctrLUTCoords(float fHeight,\n"
"                                   float fCosViewZenithAngle,\n"
"                                   float fCosSunZenithAngle,\n"
"                                   float fCosSunViewAngle ) \n"
"{\n"
"    return WorldParams2InsctrLUTCoords( fHeight, fCosViewZenithAngle, fCosSunZenithAngle, fCosSunViewAngle,\n"
"                                        float4(-1.0, -1.0, -1.0, -1.0) );\n"
"}\n"
"\n"
"\n"
"float3 ComputeViewDir(in float fCosViewZenithAngle)\n"
"{\n"
"    return float3(sqrt(saturate(1.0 - fCosViewZenithAngle*fCosViewZenithAngle)), fCosViewZenithAngle, 0.0);\n"
"}\n"
"\n"
"float3 ComputeLightDir(in float3 f3ViewDir, in float fCosSunZenithAngle, in float fCosSunViewAngle)\n"
"{\n"
"    float3 f3DirOnLight;\n"
"    f3DirOnLight.x = (f3ViewDir.x > 0.0) ? (fCosSunViewAngle - fCosSunZenithAngle * f3ViewDir.y) / f3ViewDir.x : 0.0;\n"
"    f3DirOnLight.y = fCosSunZenithAngle;\n"
"    f3DirOnLight.z = sqrt( saturate(1.0 - dot(f3DirOnLight.xy, f3DirOnLight.xy)) );\n"
"    // Do not normalize f3DirOnLight! Even if its length is not exactly 1 (which can \n"
"    // happen because of fp precision issues), all the dot products will still be as \n"
"    // specified, which is essentially important. If we normalize the vector, all the \n"
"    // dot products will deviate, resulting in wrong pre-computation.\n"
"    // Since fCosSunViewAngle is clamped to allowable range, f3DirOnLight should always\n"
"    // be normalized. However, due to some issues on NVidia hardware sometimes\n"
"    // it may not be as that (see IMPORTANT NOTE regarding NVIDIA hardware)\n"
"    //f3DirOnLight = normalize(f3DirOnLight);\n"
"    return f3DirOnLight;\n"
"}\n"
"\n"
"\n"
"float3 LookUpPrecomputedScattering(float3 f3StartPoint, \n"
"                                   float3 f3ViewDir, \n"
"                                   float3 f3EarthCentre,\n"
"                                   float3 f3DirOnLight,\n"
"                                   in Texture3D<float3> tex3DScatteringLUT,\n"
"                                   in SamplerState tex3DScatteringLUT_sampler,\n"
"                                   inout float4 f4UVWQ)\n"
"{\n"
"    float3 f3EarthCentreToPointDir = f3StartPoint - f3EarthCentre;\n"
"    float fDistToEarthCentre = length(f3EarthCentreToPointDir);\n"
"    f3EarthCentreToPointDir /= fDistToEarthCentre;\n"
"    float fHeightAboveSurface = fDistToEarthCentre - EARTH_RADIUS;\n"
"    float fCosViewZenithAngle = dot( f3EarthCentreToPointDir, f3ViewDir    );\n"
"    float fCosSunZenithAngle  = dot( f3EarthCentreToPointDir, f3DirOnLight );\n"
"    float fCosSunViewAngle    = dot( f3ViewDir,               f3DirOnLight );\n"
"\n"
"    // Provide previous look-up coordinates\n"
"    f4UVWQ = WorldParams2InsctrLUTCoords(fHeightAboveSurface, fCosViewZenithAngle,\n"
"                                         fCosSunZenithAngle, fCosSunViewAngle, \n"
"                                         f4UVWQ);\n"
"\n"
"    float3 f3UVW0; \n"
"    f3UVW0.xy = f4UVWQ.xy;\n"
"    float fQ0Slice = floor(f4UVWQ.w * PRECOMPUTED_SCTR_LUT_DIM.w - 0.5);\n"
"    fQ0Slice = clamp(fQ0Slice, 0.0, PRECOMPUTED_SCTR_LUT_DIM.w-1.0);\n"
"    float fQWeight = (f4UVWQ.w * PRECOMPUTED_SCTR_LUT_DIM.w - 0.5) - fQ0Slice;\n"
"    fQWeight = max(fQWeight, 0.0);\n"
"    float2 f2SliceMinMaxZ = float2(fQ0Slice, fQ0Slice+1.0)/PRECOMPUTED_SCTR_LUT_DIM.w + float2(0.5,-0.5) / (PRECOMPUTED_SCTR_LUT_DIM.z*PRECOMPUTED_SCTR_LUT_DIM.w);\n"
"    f3UVW0.z =  (fQ0Slice + f4UVWQ.z) / PRECOMPUTED_SCTR_LUT_DIM.w;\n"
"    f3UVW0.z = clamp(f3UVW0.z, f2SliceMinMaxZ.x, f2SliceMinMaxZ.y);\n"
"    \n"
"    float fQ1Slice = min(fQ0Slice+1.0, PRECOMPUTED_SCTR_LUT_DIM.w-1.0);\n"
"    float fNextSliceOffset = (fQ1Slice - fQ0Slice) / PRECOMPUTED_SCTR_LUT_DIM.w;\n"
"    float3 f3UVW1 = f3UVW0 + float3(0.0, 0.0, fNextSliceOffset);\n"
"    float3 f3Insctr0 = tex3DScatteringLUT.SampleLevel(tex3DScatteringLUT_sampler/*LinearClamp*/, f3UVW0, 0.0);\n"
"    float3 f3Insctr1 = tex3DScatteringLUT.SampleLevel(tex3DScatteringLUT_sampler/*LinearClamp*/, f3UVW1, 0.0);\n"
"    float3 f3Inscattering = lerp(f3Insctr0, f3Insctr1, fQWeight);\n"
"\n"
"    return f3Inscattering;\n"
"}\n"