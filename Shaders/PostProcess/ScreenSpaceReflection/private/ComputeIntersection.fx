#include "ScreenSpaceReflectionStructures.fxh"
#include "PBR_Common.fxh"
#include "SSR_Common.fxh"

cbuffer cbScreenSpaceReflectionAttribs
{
    ScreenSpaceReflectionAttribs g_SSRAttribs;
}

struct PSOutput
{
    float4 Specular     : SV_Target0;
    float4 DirectionPDF : SV_Target1;
};

Texture2D<float3> g_TextureRadiance;
Texture2D<float3> g_TextureNormal;
Texture2D<float>  g_TextureRoughness;

Texture2D<float2> g_TextureBlueNoise;
Texture2D<float>  g_TextureDepthHierarchy;

float3 ScreenSpaceToViewSpace(float3 ScreenCoordUV)
{
    return InvProjectPosition(ScreenCoordUV, g_SSRAttribs.InvProjMatrix);
}

float3 ScreenSpaceToWorldSpace(float3 ScreenCoordUV)
{
    return InvProjectPosition(ScreenCoordUV, g_SSRAttribs.InvViewProjMatrix);
}

float2 GetMipResolution(float2 ScreenDimensions, int MipLevel)
{
    return ScreenDimensions * pow(0.5, MipLevel);
}

float2 SampleRandomVector2D(uint2 PixelCoord)
{
    return g_TextureBlueNoise.Load(int3(PixelCoord % 128, 0));
}

float SampleRoughness(uint2 PixelCoord)
{
    return g_TextureRoughness.Load(int3(PixelCoord, 0));
}

float3 SampleNormalWS(uint2 PixelCoord)
{
    return g_TextureNormal.Load(int3(PixelCoord, 0));
}

float SampleDepthHierarchy(uint2 PixelCoord, uint MipLevel)
{
    return g_TextureDepthHierarchy.Load(int3(PixelCoord, MipLevel));
}

float3 SampleRadiance(uint2 PixelCoord)
{
    return g_TextureRadiance.Load(int3(PixelCoord, 0));
}

void InitialAdvanceRay(float3 Origin, float3 Direction, float3 InvDirection, float2 CurrentMipResolution, float2 InvCurrentMipResolution, float2 FloorOffset, float2 UVOffset, out float3 Position, out float CurrentT)
{
    const float2 CurrentMipPosition = CurrentMipResolution * Origin.xy;

    // Intersect ray with the half box that is pointing away from the ray origin.
    float2 XYPlane = floor(CurrentMipPosition) + FloorOffset;
    XYPlane = XYPlane * InvCurrentMipResolution + UVOffset;

    // o + d * t = p' => t = (p' - o) / d
    float2 T = XYPlane * InvDirection.xy - Origin.xy * InvDirection.xy;
    CurrentT = min(T.x, T.y);
    Position = Origin + CurrentT * Direction;
}

bool AdvanceRay(float3 Origin, float3 Direction, float3 InvDirection, float2 CurrentMipPosition, float2 InvCurrentMipResolution, float2 FloorOffset, float2 UVOffset, float SurfaceZ, inout float3 Position, inout float CurrentT)
{
    // Create boundary planes
    float2 XYPlane = floor(CurrentMipPosition) + FloorOffset;
    XYPlane = XYPlane * InvCurrentMipResolution + UVOffset;
    const float3 BoundaryPlanes = float3(XYPlane, SurfaceZ);

    // Intersect ray with the half box that is pointing away from the ray origin.
    // o + d * t = p' => t = (p' - o) / d
    float3 T = BoundaryPlanes * InvDirection - Origin * InvDirection;

    // Prevent using z plane when shooting out of the depth buffer.
#if SSR_OPTION_INVERTED_DEPTH
    T.z = Direction.z < 0 ? T.z : FLT_MAX;
#else
    T.z = Direction.z > 0 ? T.z : FLT_MAX;
#endif

    // Choose nearest intersection with a boundary.
    const float TMin = min(min(T.x, T.y), T.z);

#if SSR_OPTION_INVERTED_DEPTH
    // Larger z means closer to the camera.
    const bool AboveSurface = SurfaceZ < Position.z;
#else
    // Smaller z means closer to the camera.
    const bool AboveSurface = SurfaceZ > Position.z;
#endif

    // Decide whether we are able to advance the ray until we hit the xy boundaries or if we had to clamp it at the surface.
    // We use the asuint comparison to avoid NaN / Inf logic, also we actually care about bitwise equality here to see if t_min is the t.z we fed into the min3 above.
    const bool SkippedTile = asuint(TMin) != asuint(T.z) && AboveSurface;

    // Make sure to only advance the ray if we're still above the surface.
    CurrentT = AboveSurface ? TMin : CurrentT;

    // Advance ray
    Position = Origin + CurrentT * Direction;
    return SkippedTile;
}

// Requires origin and direction of the ray to be in screen space [0, 1] x [0, 1]
float3 HierarchicalRaymarch(float3 Origin, float3 Direction, float2 ScreenSize, int MostDetailedMip, uint MaxTraversalIntersections, out bool ValidHit)
{
    const float3 InvDirection = Direction != float3(0.0f, 0.0f, 0.0f) ? float3(1.0f, 1.0f, 1.0f) / Direction : float3(FLT_MAX, FLT_MAX, FLT_MAX);

    // Start on mip with highest detail.
    int CurrentMip = MostDetailedMip;

    // Could recompute these every iteration, but it's faster to hoist them out and update them.
    float2 CurrentMipResolution = GetMipResolution(ScreenSize, CurrentMip);
    float2 InvCurrentMipResolution = rcp(CurrentMipResolution);
    
    // Offset to the bounding boxes uv space to intersect the ray with the center of the next pixel.
    // This means we ever so slightly over shoot into the next region.
    float2 UVOffset = 0.005 * exp2(MostDetailedMip) / ScreenSize;
    UVOffset.x = Direction.x < 0.0f ? -UVOffset.x : UVOffset.x;
    UVOffset.y = Direction.y < 0.0f ? -UVOffset.y : UVOffset.y;

    // Offset applied depending on current mip resolution to move the boundary to the left/right upper/lower border depending on ray direction.
    float2 FloorOffset;
    FloorOffset.x = Direction.x < 0.0f ? 0.0f : 1.0f;
    FloorOffset.y = Direction.y < 0.0f ? 0.0f : 1.0f;

    // Initially advance ray to avoid immediate self intersections.
    float CurrentT;
    float3 Position;
    InitialAdvanceRay(Origin, Direction, InvDirection, CurrentMipResolution, InvCurrentMipResolution, FloorOffset, UVOffset, Position, CurrentT);

    uint Idx = 0;
    while (Idx < MaxTraversalIntersections && CurrentMip >= MostDetailedMip)
    {
        const float2 CurrentMipPosition = CurrentMipResolution * Position.xy;
        const float SurfaceZ = SampleDepthHierarchy(uint2(CurrentMipPosition), CurrentMip);
        const bool SkippedTile = AdvanceRay(Origin, Direction, InvDirection, CurrentMipPosition, InvCurrentMipResolution, FloorOffset, UVOffset, SurfaceZ, Position, CurrentT);
        
        // Don't increase mip further than this because we did not generate it
        const bool NextMipIsOutOfRange = SkippedTile && (CurrentMip >= SSR_DEPTH_HIERARCHY_MAX_MIP);
        if (!NextMipIsOutOfRange)
        {
            CurrentMip += SkippedTile ? 1 : -1;
            CurrentMipResolution *= SkippedTile ? 0.5 : 2;
            InvCurrentMipResolution *= SkippedTile ? 2 : 0.5;
        }
        ++Idx;
    }

    ValidHit = (Idx <= MaxTraversalIntersections);
    return Position;
}

float ValidateHit(float3 Hit, float2 ScreenCoordUV, float3 RayDirectionWS, float2 ScreenSize, float DepthBufferThickness)
{
    // Reject hits outside the view frustum
    if (Hit.x < 0.0f || Hit.y < 0.0f || Hit.x > 1.0f || Hit.y > 1.0f)
        return 0.0;

    // Reject the hit if we didn't advance the ray significantly to avoid immediate self reflection
    const float2 ManhattanDist = abs(Hit.xy - ScreenCoordUV);
    if (ManhattanDist.x < (2.0f / ScreenSize.x) && ManhattanDist.y < (2.0f / ScreenSize.y))
        return 0.0;

    // Don't lookup radiance from the background.
    const int2 TexelCoords = int2(ScreenSize * Hit.xy);
    float SurfaceDepth = SampleDepthHierarchy(TexelCoords, 0);

    if (IsBackground(SurfaceDepth))
        return 0.0;

    // We check if ray hit below the surface
    const float3 SurfaceNormalWS = SampleNormalWS(uint2(ScreenCoordUV * ScreenSize));
    if (dot(SurfaceNormalWS, RayDirectionWS) < 0.0)
        return 0.0;

    // We check if we hit the surface from the back, these should be rejected.
    const float3 HitNormalWS = SampleNormalWS(TexelCoords);
    if (dot(HitNormalWS, RayDirectionWS) > 0.0)
        return 0.0;

    const float3 SurfaceVS = ScreenSpaceToViewSpace(float3(Hit.xy, SurfaceDepth));
    const float3 HitVS = ScreenSpaceToViewSpace(Hit);
    const float Distance = length(SurfaceVS - HitVS);

    // Fade out hits near the screen borders
    const float2 FOV = 0.05 * float2(ScreenSize.y / ScreenSize.x, 1);
    const float2 Border = smoothstep(float2(0.0f, 0.0f), FOV, Hit.xy) * (1 - smoothstep(float2(1.0f, 1.0f) - FOV, float2(1.0f, 1.0f), Hit.xy));
    const float Vignette = Border.x * Border.y;

    // We accept all hits that are within a reasonable minimum distance below the surface.
    // Add constant in linear space to avoid growing of the reflections toward the reflected objects.
    float Confidence = 1.0f - smoothstep(0.0f, DepthBufferThickness, Distance);
    Confidence *= Confidence;

    return Vignette * Confidence;
}

float3 SmithGGXSampleVisibleNormalEllipsoid(float3 View, float2 Alpha, float2 Xi)
{
    return SmithGGXSampleVisibleNormal(View, Alpha.x, Alpha.y, Xi.x, Xi.y);
}

float3 SmithGGXSampleVisibleNormalHemisphere(float3 View, float Alpha, float2 Xi)
{
    return SmithGGXSampleVisibleNormal(View, Alpha, Alpha, Xi.x, Xi.y);
}

float4 SampleReflectionVector(float3 View, float3 Normal, float AlphaRoughness, uint2 PixelCoord)
{
    const float3 N = Normal;
    const float3 T = normalize(cross(N, abs(N.y) > 0.5 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0)));
    const float3 B = cross(T, N);
    const float3x3 TangentToWorld = MatrixFromRows(T, B, N);

    float2 Xi = SampleRandomVector2D(PixelCoord);
    Xi.x = lerp(Xi.x, 0.0f, g_SSRAttribs.GGXImportanceSampleBias);

    const float3 NormalTS = float3(0.0, 0.0, 1.0);
    const float3 ViewDirTS = normalize(mul(View, transpose(TangentToWorld)));
    const float3 MicroNormalTS = SmithGGXSampleVisibleNormalHemisphere(ViewDirTS, AlphaRoughness, Xi);
    const float3 SampleDirTS = reflect(-ViewDirTS, MicroNormalTS);

    // Normal sampled with PDF: Dv(Ne) / (4 * dot(Ve, Ne))
    // Dv(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
    const float NdotV = ViewDirTS.z;
    const float NdotH = MicroNormalTS.z;

    const float D = NormalDistribution_GGX(NdotH, AlphaRoughness); 
    const float G1 = SmithGGXMasking(NdotV, AlphaRoughness);
    const float PDF = G1 * D / (4 * NdotV);
    return float4(normalize(mul(SampleDirTS, TangentToWorld)), PDF);
}

SSR_ATTRIBUTE_EARLY_DEPTH_STENCIL
PSOutput ComputeIntersectionPS(in float4 Position : SV_Position)
{
    const float2 ScreenCoordUV = Position.xy * g_SSRAttribs.InverseRenderSize;
    const float3 NormalWS = SampleNormalWS(uint2(Position.xy));
    const float Roughness = SampleRoughness(uint2(Position.xy));

    const bool IsMirror = IsMirrorReflection(Roughness);
    const int MostDetailedMip = IsMirror ? 0 : int(g_SSRAttribs.MostDetailedMip);
    const float2 MipResolution = GetMipResolution(g_SSRAttribs.RenderSize, MostDetailedMip);

    const float3 RayOriginSS = float3(ScreenCoordUV, SampleDepthHierarchy(uint2(ScreenCoordUV * MipResolution), MostDetailedMip));
    const float3 RayOriginVS = ScreenSpaceToViewSpace(RayOriginSS);
    const float3 NormalVS = mul(float4(NormalWS, 0), g_SSRAttribs.ViewMatrix).xyz;

    const float4 RayDirectionVS = SampleReflectionVector(-normalize(RayOriginVS), NormalVS, Roughness, uint2(Position.xy));
    const float3 RayDirectionSS = ProjectDirection(RayOriginVS, RayDirectionVS.xyz, RayOriginSS, g_SSRAttribs.ProjMatrix);

    bool ValidHit = false;
    float3 SurfaceHitSS = HierarchicalRaymarch(RayOriginSS, RayDirectionSS, g_SSRAttribs.RenderSize, MostDetailedMip, g_SSRAttribs.MaxTraversalIntersections, ValidHit);

    const float3 RayOriginWS = ScreenSpaceToWorldSpace(RayOriginSS);
    const float3 SurfaceHitWS = ScreenSpaceToWorldSpace(SurfaceHitSS);
    const float3 RayDirectionWS = SurfaceHitWS - RayOriginWS.xyz;

    const float Confidence = ValidHit ? ValidateHit(SurfaceHitSS, ScreenCoordUV, RayDirectionWS, g_SSRAttribs.RenderSize, g_SSRAttribs.DepthBufferThickness) : 0;
    const float3 ReflectionRadiance = Confidence > 0.0f ? SampleRadiance(uint2(g_SSRAttribs.RenderSize * SurfaceHitSS.xy)) : float3(0.0, 0.0, 0.0);

    //TODO: Try to store inverse RayDirectionWS for more accuracy.
    PSOutput Output;
    Output.Specular = float4(ReflectionRadiance, Confidence);
    Output.DirectionPDF = float4(RayDirectionWS, RayDirectionVS.w);
    return Output;
}
