#include "AtmosphereShadersCommon.fxh"

Texture2D<float4> g_tex2DSliceUVDirAndOrigin;
Texture2DArray<float> g_tex2DLightSpaceDepthMap;
SamplerState g_tex2DLightSpaceDepthMap_sampler;

cbuffer cbPostProcessingAttribs
{
    PostProcessingAttribs g_PPAttribs;
};

cbuffer cbMiscDynamicParams
{
    MiscDynamicParams g_MiscParams;
}

// Note that min/max shadow map does not contain finest resolution level
// The first level it contains corresponds to step == 2
void InitializeMinMaxShadowMapPS(in ScreenSizeQuadVSOutput VSOut,
                                 in float4 f4Pos : SV_Position,
                                 out float2 f2MinMaxDepth : SV_Target)
{
    uint uiSliceInd;
    float fCascadeInd;
#if USE_COMBINED_MIN_MAX_TEXTURE
    fCascadeInd = floor(f4Pos.y / float(NUM_EPIPOLAR_SLICES));
    uiSliceInd = uint(f4Pos.y - fCascadeInd * float(NUM_EPIPOLAR_SLICES));
    fCascadeInd += g_PPAttribs.m_fFirstCascade;
#else
    uiSliceInd = uint(f4Pos.y);
    fCascadeInd = g_MiscParams.fCascadeInd;
#endif
    // Load slice direction in shadow map
    float4 f4SliceUVDirAndOrigin = g_tex2DSliceUVDirAndOrigin.Load( uint3(uiSliceInd, fCascadeInd, 0) );
    // Calculate current sample position on the ray
    float2 f2CurrUV = f4SliceUVDirAndOrigin.zw + f4SliceUVDirAndOrigin.xy * floor(f4Pos.x) * 2.f;
    
    float4 f4MinDepth = F4ONE;
    float4 f4MaxDepth = F4ZERO;
    // Gather 8 depths which will be used for PCF filtering for this sample and its immediate neighbor 
    // along the epipolar slice
    // Note that if the sample is located outside the shadow map, Gather() will return 0 as 
    // specified by the samLinearBorder0. As a result volumes outside the shadow map will always be lit
    for( float i=0.0; i<=1.0; ++i )
    {
        float4 f4Depths = g_tex2DLightSpaceDepthMap.Gather(g_tex2DLightSpaceDepthMap_sampler, float3(f2CurrUV + i * f4SliceUVDirAndOrigin.xy, fCascadeInd) );
        f4MinDepth = min(f4MinDepth, f4Depths);
        f4MaxDepth = max(f4MaxDepth, f4Depths);
    }

    f4MinDepth.xy = min(f4MinDepth.xy, f4MinDepth.zw);
    f4MinDepth.x = min(f4MinDepth.x, f4MinDepth.y);

    f4MaxDepth.xy = max(f4MaxDepth.xy, f4MaxDepth.zw);
    f4MaxDepth.x = max(f4MaxDepth.x, f4MaxDepth.y);
#if !IS_32BIT_MIN_MAX_MAP
    const float R16_UNORM_PRECISION = 1.0 / float(1<<16);
    f4MinDepth.x = floor(f4MinDepth.x/R16_UNORM_PRECISION)*R16_UNORM_PRECISION;
    f4MaxDepth.x =  ceil(f4MaxDepth.x/R16_UNORM_PRECISION)*R16_UNORM_PRECISION;
#endif
    f2MinMaxDepth = float2(f4MinDepth.x, f4MaxDepth.x);
}
