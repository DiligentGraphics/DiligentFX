"#include \"ScreenSpaceReflectionStructures.fxh\"\n"
"#include \"PBR_Common.fxh\"\n"
"#include \"SSR_Common.fxh\"\n"
"\n"
"cbuffer cbScreenSpaceReflectionAttribs\n"
"{\n"
"    ScreenSpaceReflectionAttribs g_SSRAttribs;\n"
"}\n"
"\n"
"struct PSOutput\n"
"{\n"
"    float4 Specular     : SV_Target0;\n"
"    float4 DirectionPDF : SV_Target1;\n"
"};\n"
"\n"
"Texture2D<float3> g_TextureRadiance;\n"
"Texture2D<float3> g_TextureNormal;\n"
"Texture2D<float>  g_TextureRoughness;\n"
"\n"
"Texture2D<float2> g_TextureBlueNoise;\n"
"Texture2D<float>  g_TextureDepthHierarchy;\n"
"\n"
"float3 ScreenSpaceToViewSpace(float3 ScreenCoordUV)\n"
"{\n"
"    return InvProjectPosition(ScreenCoordUV, g_SSRAttribs.InvProjMatrix);\n"
"}\n"
"\n"
"float3 ScreenSpaceToWorldSpace(float3 ScreenCoordUV)\n"
"{\n"
"    return InvProjectPosition(ScreenCoordUV, g_SSRAttribs.InvViewProjMatrix);\n"
"}\n"
"\n"
"float2 GetMipResolution(float2 ScreenDimensions, int MipLevel)\n"
"{\n"
"    return ScreenDimensions * pow(0.5, MipLevel);\n"
"}\n"
"\n"
"float2 SampleRandomVector2D(uint2 PixelCoord)\n"
"{\n"
"    return g_TextureBlueNoise.Load(int3(PixelCoord % 128, 0));\n"
"}\n"
"\n"
"float SampleRoughness(uint2 PixelCoord)\n"
"{\n"
"    return g_TextureRoughness.Load(int3(PixelCoord, 0));\n"
"}\n"
"\n"
"float3 SampleNormalWS(uint2 PixelCoord)\n"
"{\n"
"    return g_TextureNormal.Load(int3(PixelCoord, 0));\n"
"}\n"
"\n"
"float SampleDepthHierarchy(uint2 PixelCoord, uint MipLevel)\n"
"{\n"
"    return g_TextureDepthHierarchy.Load(int3(PixelCoord, MipLevel));\n"
"}\n"
"\n"
"float3 SampleRadiance(uint2 PixelCoord)\n"
"{\n"
"    return g_TextureRadiance.Load(int3(PixelCoord, 0));\n"
"}\n"
"\n"
"void InitialAdvanceRay(float3 Origin, float3 Direction, float3 InvDirection, float2 CurrentMipResolution, float2 InvCurrentMipResolution, float2 FloorOffset, float2 UVOffset, out float3 Position, out float CurrentT)\n"
"{\n"
"    const float2 CurrentMipPosition = CurrentMipResolution * Origin.xy;\n"
"\n"
"    // Intersect ray with the half box that is pointing away from the ray origin.\n"
"    float2 XYPlane = floor(CurrentMipPosition) + FloorOffset;\n"
"    XYPlane = XYPlane * InvCurrentMipResolution + UVOffset;\n"
"\n"
"    // o + d * t = p\' => t = (p\' - o) / d\n"
"    float2 T = XYPlane * InvDirection.xy - Origin.xy * InvDirection.xy;\n"
"    CurrentT = min(T.x, T.y);\n"
"    Position = Origin + CurrentT * Direction;\n"
"}\n"
"\n"
"bool AdvanceRay(float3 Origin, float3 Direction, float3 InvDirection, float2 CurrentMipPosition, float2 InvCurrentMipResolution, float2 FloorOffset, float2 UVOffset, float SurfaceZ, inout float3 Position, inout float CurrentT)\n"
"{\n"
"    // Create boundary planes\n"
"    float2 XYPlane = floor(CurrentMipPosition) + FloorOffset;\n"
"    XYPlane = XYPlane * InvCurrentMipResolution + UVOffset;\n"
"    const float3 BoundaryPlanes = float3(XYPlane, SurfaceZ);\n"
"\n"
"    // Intersect ray with the half box that is pointing away from the ray origin.\n"
"    // o + d * t = p\' => t = (p\' - o) / d\n"
"    float3 T = BoundaryPlanes * InvDirection - Origin * InvDirection;\n"
"\n"
"    // Prevent using z plane when shooting out of the depth buffer.\n"
"#if SSR_OPTION_INVERTED_DEPTH\n"
"    T.z = Direction.z < 0 ? T.z : FLT_MAX;\n"
"#else\n"
"    T.z = Direction.z > 0 ? T.z : FLT_MAX;\n"
"#endif\n"
"\n"
"    // Choose nearest intersection with a boundary.\n"
"    const float TMin = min(min(T.x, T.y), T.z);\n"
"\n"
"#if SSR_OPTION_INVERTED_DEPTH\n"
"    // Larger z means closer to the camera.\n"
"    const bool AboveSurface = SurfaceZ < Position.z;\n"
"#else\n"
"    // Smaller z means closer to the camera.\n"
"    const bool AboveSurface = SurfaceZ > Position.z;\n"
"#endif\n"
"\n"
"    // Decide whether we are able to advance the ray until we hit the xy boundaries or if we had to clamp it at the surface.\n"
"    // We use the asuint comparison to avoid NaN / Inf logic, also we actually care about bitwise equality here to see if t_min is the t.z we fed into the min3 above.\n"
"    const bool SkippedTile = asuint(TMin) != asuint(T.z) && AboveSurface;\n"
"\n"
"    // Make sure to only advance the ray if we\'re still above the surface.\n"
"    CurrentT = AboveSurface ? TMin : CurrentT;\n"
"\n"
"    // Advance ray\n"
"    Position = Origin + CurrentT * Direction;\n"
"    return SkippedTile;\n"
"}\n"
"\n"
"// Requires origin and direction of the ray to be in screen space [0, 1] x [0, 1]\n"
"float3 HierarchicalRaymarch(float3 Origin, float3 Direction, float2 ScreenSize, int MostDetailedMip, uint MaxTraversalIntersections, out bool ValidHit)\n"
"{\n"
"    const float3 InvDirection = Direction != float3(0.0f, 0.0f, 0.0f) ? float3(1.0f, 1.0f, 1.0f) / Direction : float3(FLT_MAX, FLT_MAX, FLT_MAX);\n"
"\n"
"    // Start on mip with highest detail.\n"
"    int CurrentMip = MostDetailedMip;\n"
"\n"
"    // Could recompute these every iteration, but it\'s faster to hoist them out and update them.\n"
"    float2 CurrentMipResolution = GetMipResolution(ScreenSize, CurrentMip);\n"
"    float2 InvCurrentMipResolution = rcp(CurrentMipResolution);\n"
"\n"
"    // Offset to the bounding boxes uv space to intersect the ray with the center of the next pixel.\n"
"    // This means we ever so slightly over shoot into the next region.\n"
"    float2 UVOffset = 0.005 * exp2(MostDetailedMip) / ScreenSize;\n"
"    UVOffset.x = Direction.x < 0.0f ? -UVOffset.x : UVOffset.x;\n"
"    UVOffset.y = Direction.y < 0.0f ? -UVOffset.y : UVOffset.y;\n"
"\n"
"    // Offset applied depending on current mip resolution to move the boundary to the left/right upper/lower border depending on ray direction.\n"
"    float2 FloorOffset;\n"
"    FloorOffset.x = Direction.x < 0.0f ? 0.0f : 1.0f;\n"
"    FloorOffset.y = Direction.y < 0.0f ? 0.0f : 1.0f;\n"
"\n"
"    // Initially advance ray to avoid immediate self intersections.\n"
"    float CurrentT;\n"
"    float3 Position;\n"
"    InitialAdvanceRay(Origin, Direction, InvDirection, CurrentMipResolution, InvCurrentMipResolution, FloorOffset, UVOffset, Position, CurrentT);\n"
"\n"
"    uint Idx = 0;\n"
"    while (Idx < MaxTraversalIntersections && CurrentMip >= MostDetailedMip)\n"
"    {\n"
"        const float2 CurrentMipPosition = CurrentMipResolution * Position.xy;\n"
"        const float SurfaceZ = SampleDepthHierarchy(uint2(CurrentMipPosition), CurrentMip);\n"
"        const bool SkippedTile = AdvanceRay(Origin, Direction, InvDirection, CurrentMipPosition, InvCurrentMipResolution, FloorOffset, UVOffset, SurfaceZ, Position, CurrentT);\n"
"\n"
"        // Don\'t increase mip further than this because we did not generate it\n"
"        const bool NextMipIsOutOfRange = SkippedTile && (CurrentMip >= SSR_DEPTH_HIERARCHY_MAX_MIP);\n"
"        if (!NextMipIsOutOfRange)\n"
"        {\n"
"            CurrentMip += SkippedTile ? 1 : -1;\n"
"            CurrentMipResolution *= SkippedTile ? 0.5 : 2;\n"
"            InvCurrentMipResolution *= SkippedTile ? 2 : 0.5;\n"
"        }\n"
"        ++Idx;\n"
"    }\n"
"\n"
"    ValidHit = (Idx <= MaxTraversalIntersections);\n"
"    return Position;\n"
"}\n"
"\n"
"float ValidateHit(float3 Hit, float2 ScreenCoordUV, float3 RayDirectionWS, float2 ScreenSize, float DepthBufferThickness)\n"
"{\n"
"    // Reject hits outside the view frustum\n"
"    if (Hit.x < 0.0f || Hit.y < 0.0f || Hit.x > 1.0f || Hit.y > 1.0f)\n"
"        return 0.0;\n"
"\n"
"    // Reject the hit if we didn\'t advance the ray significantly to avoid immediate self reflection\n"
"    const float2 ManhattanDist = abs(Hit.xy - ScreenCoordUV);\n"
"    if (ManhattanDist.x < (2.0f / ScreenSize.x) && ManhattanDist.y < (2.0f / ScreenSize.y))\n"
"        return 0.0;\n"
"\n"
"    // Don\'t lookup radiance from the background.\n"
"    const int2 TexelCoords = int2(ScreenSize * Hit.xy);\n"
"    float SurfaceDepth = SampleDepthHierarchy(TexelCoords, 0);\n"
"\n"
"    if (IsBackground(SurfaceDepth))\n"
"        return 0.0;\n"
"\n"
"    // We check if ray hit below the surface\n"
"    const float3 SurfaceNormalWS = SampleNormalWS(uint2(ScreenCoordUV * ScreenSize));\n"
"    if (dot(SurfaceNormalWS, RayDirectionWS) < 0.0)\n"
"        return 0.0;\n"
"\n"
"    // We check if we hit the surface from the back, these should be rejected.\n"
"    const float3 HitNormalWS = SampleNormalWS(TexelCoords);\n"
"    if (dot(HitNormalWS, RayDirectionWS) > 0.0)\n"
"        return 0.0;\n"
"\n"
"    const float3 SurfaceVS = ScreenSpaceToViewSpace(float3(Hit.xy, SurfaceDepth));\n"
"    const float3 HitVS = ScreenSpaceToViewSpace(Hit);\n"
"    const float Distance = length(SurfaceVS - HitVS);\n"
"\n"
"    // Fade out hits near the screen borders\n"
"    const float2 FOV = 0.05 * float2(ScreenSize.y / ScreenSize.x, 1);\n"
"    const float2 Border = smoothstep(float2(0.0f, 0.0f), FOV, Hit.xy) * (1 - smoothstep(float2(1.0f, 1.0f) - FOV, float2(1.0f, 1.0f), Hit.xy));\n"
"    const float Vignette = Border.x * Border.y;\n"
"\n"
"    // We accept all hits that are within a reasonable minimum distance below the surface.\n"
"    // Add constant in linear space to avoid growing of the reflections toward the reflected objects.\n"
"    float Confidence = 1.0f - smoothstep(0.0f, DepthBufferThickness, Distance);\n"
"    Confidence *= Confidence;\n"
"\n"
"    return Vignette * Confidence;\n"
"}\n"
"\n"
"float3 SmithGGXSampleVisibleNormalEllipsoid(float3 View, float2 Alpha, float2 Xi)\n"
"{\n"
"    return SmithGGXSampleVisibleNormal(View, Alpha.x, Alpha.y, Xi.x, Xi.y);\n"
"}\n"
"\n"
"float3 SmithGGXSampleVisibleNormalHemisphere(float3 View, float Alpha, float2 Xi)\n"
"{\n"
"    return SmithGGXSampleVisibleNormal(View, Alpha, Alpha, Xi.x, Xi.y);\n"
"}\n"
"\n"
"float4 SampleReflectionVector(float3 View, float3 Normal, float AlphaRoughness, uint2 PixelCoord)\n"
"{\n"
"    const float3 N = Normal;\n"
"    const float3 T = normalize(cross(N, abs(N.y) > 0.5 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));\n"
"    const float3 B = cross(T, N);\n"
"    const float3x3 TangentToWorld = MatrixFromRows(T, B, N);\n"
"\n"
"    float2 Xi = SampleRandomVector2D(PixelCoord);\n"
"    Xi.x = lerp(Xi.x, 0.0f, g_SSRAttribs.GGXImportanceSampleBias);\n"
"\n"
"    const float3 NormalTS = float3(0.0, 0.0, 1.0);\n"
"    const float3 ViewDirTS = normalize(mul(View, transpose(TangentToWorld)));\n"
"    const float3 MicroNormalTS = SmithGGXSampleVisibleNormalHemisphere(ViewDirTS, AlphaRoughness, Xi);\n"
"    const float3 SampleDirTS = reflect(-ViewDirTS, MicroNormalTS);\n"
"\n"
"    // Normal sampled with PDF: Dv(Ne) / (4 * dot(Ve, Ne))\n"
"    // Dv(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z\n"
"    const float NdotV = ViewDirTS.z;\n"
"    const float NdotH = MicroNormalTS.z;\n"
"\n"
"    const float D = NormalDistribution_GGX(NdotH, AlphaRoughness);\n"
"    const float G1 = SmithGGXMasking(NdotV, AlphaRoughness);\n"
"    const float PDF = G1 * D / (4 * NdotV);\n"
"    return float4(normalize(mul(SampleDirTS, TangentToWorld)), PDF);\n"
"}\n"
"\n"
"SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL\n"
"PSOutput ComputeIntersectionPS(in float4 Position : SV_Position)\n"
"{\n"
"    const float2 ScreenCoordUV = Position.xy * g_SSRAttribs.InverseRenderSize;\n"
"    const float3 NormalWS = SampleNormalWS(uint2(Position.xy));\n"
"    const float Roughness = SampleRoughness(uint2(Position.xy));\n"
"\n"
"    const bool IsMirror = IsMirrorReflection(Roughness);\n"
"    const int MostDetailedMip = IsMirror ? 0 : int(g_SSRAttribs.MostDetailedMip);\n"
"    const float2 MipResolution = GetMipResolution(g_SSRAttribs.RenderSize, MostDetailedMip);\n"
"\n"
"    const float3 RayOriginSS = float3(ScreenCoordUV, SampleDepthHierarchy(uint2(ScreenCoordUV * MipResolution), MostDetailedMip));\n"
"    const float3 RayOriginVS = ScreenSpaceToViewSpace(RayOriginSS);\n"
"    const float3 NormalVS = mul(float4(NormalWS, 0), g_SSRAttribs.ViewMatrix).xyz;\n"
"\n"
"    const float4 RayDirectionVS = SampleReflectionVector(-normalize(RayOriginVS), NormalVS, Roughness, uint2(Position.xy));\n"
"    const float3 RayDirectionSS = ProjectDirection(RayOriginVS, RayDirectionVS.xyz, RayOriginSS, g_SSRAttribs.ProjMatrix);\n"
"\n"
"    bool ValidHit = false;\n"
"    float3 SurfaceHitSS = HierarchicalRaymarch(RayOriginSS, RayDirectionSS, g_SSRAttribs.RenderSize, MostDetailedMip, g_SSRAttribs.MaxTraversalIntersections, ValidHit);\n"
"\n"
"    const float3 RayOriginWS = ScreenSpaceToWorldSpace(RayOriginSS);\n"
"    const float3 SurfaceHitWS = ScreenSpaceToWorldSpace(SurfaceHitSS);\n"
"    const float3 RayDirectionWS = SurfaceHitWS - RayOriginWS.xyz;\n"
"\n"
"    const float Confidence = ValidHit ? ValidateHit(SurfaceHitSS, ScreenCoordUV, RayDirectionWS, g_SSRAttribs.RenderSize, g_SSRAttribs.DepthBufferThickness) : 0;\n"
"    const float3 ReflectionRadiance = Confidence > 0.0f ? SampleRadiance(uint2(g_SSRAttribs.RenderSize * SurfaceHitSS.xy)) : float3(0.0, 0.0, 0.0);\n"
"\n"
"    //TODO: Try to store inverse RayDirectionWS for more accuracy.\n"
"    PSOutput Output;\n"
"    Output.Specular = float4(ReflectionRadiance, Confidence);\n"
"    Output.DirectionPDF = float4(RayDirectionWS, RayDirectionVS.w);\n"
"    return Output;\n"
"}\n"
