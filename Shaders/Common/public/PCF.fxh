#ifndef _PCF_FXH_
#define _PCF_FXH_

#if defined(PCF_FILTER_SIZE) && PCF_FILTER_SIZE > 0

// The method used in The Witness
float FilterShadowMapFixedPCF(in Texture2DArray<float>  tex2DShadowMap,
                              in SamplerComparisonState tex2DShadowMap_sampler,
                              in float4                 f4ShadowMapSize,
                              in float2                 f2UV,
                              in float                  fSlice,
                              in float                  fLightSpaceDepth,
                              in float2                 f2ReceiverPlaneDepthBias)
{
    float2 uv = f2UV.xy * f4ShadowMapSize.xy;
    float2 base_uv = floor(uv + float2(0.5, 0.5));
    float s = (uv.x + 0.5 - base_uv.x);
    float t = (uv.y + 0.5 - base_uv.y);
    base_uv -= float2(0.5, 0.5);
    base_uv *= f4ShadowMapSize.zw;

    float sum = 0.0;

    // It is essential to clamp biased depth to 0 to avoid shadow leaks at near cascade depth boundary.
    //        
    //            No clamping                 With clamping
    //                                      
    //              \ |                             ||    
    //       ==>     \|                             ||
    //                |                             ||         
    // Light ==>      |\                            |\         
    //                | \Receiver plane             | \ Receiver plane
    //       ==>      |  \                          |  \   
    //                0   ...   1                   0   ...   1
    //
    // Note that clamping at far depth boundary makes no difference as 1 < 1 produces 0 and so does 1+x < 1
    const float DepthClamp = 1e-8;
#ifdef GLSL
    // There is no OpenGL counterpart for Texture2DArray.SampleCmpLevelZero()
    #define SAMPLE_SHADOW_MAP(u, v) tex2DShadowMap.SampleCmp(tex2DShadowMap_sampler, float3(base_uv.xy + float2(u,v) * f4ShadowMapSize.zw, fSlice), max(fLightSpaceDepth + dot(float2(u, v), f2ReceiverPlaneDepthBias), DepthClamp))
#else
    #define SAMPLE_SHADOW_MAP(u, v) tex2DShadowMap.SampleCmpLevelZero(tex2DShadowMap_sampler, float3(base_uv.xy + float2(u,v) * f4ShadowMapSize.zw, fSlice), max(fLightSpaceDepth + dot(float2(u, v), f2ReceiverPlaneDepthBias), DepthClamp))
#endif

    #if PCF_FILTER_SIZE == 2

        #ifdef GLSL
            return tex2DShadowMap.SampleCmp(tex2DShadowMap_sampler, float3(f2UV, fSlice), max(fLightSpaceDepth, DepthClamp));
        #else
            return tex2DShadowMap.SampleCmpLevelZero(tex2DShadowMap_sampler, float3(f2UV, fSlice), max(fLightSpaceDepth, DepthClamp));
        #endif

    #elif PCF_FILTER_SIZE == 3

        float uw0 = (3.0 - 2.0 * s);
        float uw1 = (1.0 + 2.0 * s);

        float u0 = (2.0 - s) / uw0 - 1.0;
        float u1 = s / uw1 + 1.0;

        float vw0 = (3.0 - 2.0 * t);
        float vw1 = (1.0 + 2.0 * t);

        float v0 = (2.0 - t) / vw0 - 1.0;
        float v1 = t / vw1 + 1.0;

        sum += uw0 * vw0 * SAMPLE_SHADOW_MAP(u0, v0);
        sum += uw1 * vw0 * SAMPLE_SHADOW_MAP(u1, v0);
        sum += uw0 * vw1 * SAMPLE_SHADOW_MAP(u0, v1);
        sum += uw1 * vw1 * SAMPLE_SHADOW_MAP(u1, v1);

        return sum * 1.0 / 16.0;

    #elif PCF_FILTER_SIZE == 5

        float uw0 = (4.0 - 3.0 * s);
        float uw1 = 7.0;
        float uw2 = (1.0 + 3.0 * s);

        float u0 = (3.0 - 2.0 * s) / uw0 - 2.0;
        float u1 = (3.0 + s) / uw1;
        float u2 = s / uw2 + 2.0;

        float vw0 = (4.0 - 3.0 * t);
        float vw1 = 7.0;
        float vw2 = (1.0 + 3.0 * t);

        float v0 = (3.0 - 2.0 * t) / vw0 - 2.0;
        float v1 = (3.0 + t) / vw1;
        float v2 = t / vw2 + 2.0;

        sum += uw0 * vw0 * SAMPLE_SHADOW_MAP(u0, v0);
        sum += uw1 * vw0 * SAMPLE_SHADOW_MAP(u1, v0);
        sum += uw2 * vw0 * SAMPLE_SHADOW_MAP(u2, v0);

        sum += uw0 * vw1 * SAMPLE_SHADOW_MAP(u0, v1);
        sum += uw1 * vw1 * SAMPLE_SHADOW_MAP(u1, v1);
        sum += uw2 * vw1 * SAMPLE_SHADOW_MAP(u2, v1);

        sum += uw0 * vw2 * SAMPLE_SHADOW_MAP(u0, v2);
        sum += uw1 * vw2 * SAMPLE_SHADOW_MAP(u1, v2);
        sum += uw2 * vw2 * SAMPLE_SHADOW_MAP(u2, v2);

        return sum * 1.0 / 144.0;

    #elif PCF_FILTER_SIZE == 7

        float uw0 = (5.0 * s - 6.0);
        float uw1 = (11.0 * s - 28.0);
        float uw2 = -(11.0 * s + 17.0);
        float uw3 = -(5.0 * s + 1.0);

        float u0 = (4.0 * s - 5.0) / uw0 - 3.0;
        float u1 = (4.0 * s - 16.0) / uw1 - 1.0;
        float u2 = -(7.0 * s + 5.0) / uw2 + 1.0;
        float u3 = -s / uw3 + 3.0;

        float vw0 = (5.0 * t - 6.0);
        float vw1 = (11.0 * t - 28.0);
        float vw2 = -(11.0 * t + 17.0);
        float vw3 = -(5.0 * t + 1.0);

        float v0 = (4.0 * t - 5.0) / vw0 - 3.0;
        float v1 = (4.0 * t - 16.0) / vw1 - 1.0;
        float v2 = -(7.0 * t + 5.0) / vw2 + 1.0;
        float v3 = -t / vw3 + 3.0;

        sum += uw0 * vw0 * SAMPLE_SHADOW_MAP(u0, v0);
        sum += uw1 * vw0 * SAMPLE_SHADOW_MAP(u1, v0);
        sum += uw2 * vw0 * SAMPLE_SHADOW_MAP(u2, v0);
        sum += uw3 * vw0 * SAMPLE_SHADOW_MAP(u3, v0);

        sum += uw0 * vw1 * SAMPLE_SHADOW_MAP(u0, v1);
        sum += uw1 * vw1 * SAMPLE_SHADOW_MAP(u1, v1);
        sum += uw2 * vw1 * SAMPLE_SHADOW_MAP(u2, v1);
        sum += uw3 * vw1 * SAMPLE_SHADOW_MAP(u3, v1);

        sum += uw0 * vw2 * SAMPLE_SHADOW_MAP(u0, v2);
        sum += uw1 * vw2 * SAMPLE_SHADOW_MAP(u1, v2);
        sum += uw2 * vw2 * SAMPLE_SHADOW_MAP(u2, v2);
        sum += uw3 * vw2 * SAMPLE_SHADOW_MAP(u3, v2);

        sum += uw0 * vw3 * SAMPLE_SHADOW_MAP(u0, v3);
        sum += uw1 * vw3 * SAMPLE_SHADOW_MAP(u1, v3);
        sum += uw2 * vw3 * SAMPLE_SHADOW_MAP(u2, v3);
        sum += uw3 * vw3 * SAMPLE_SHADOW_MAP(u3, v3);

        return sum * 1.0 / 2704.0;
    #else
        return 0.0;
    #endif
#undef SAMPLE_SHADOW_MAP
}

#else

float FilterShadowMapVaryingPCF(in Texture2DArray<float>  tex2DShadowMap,
                                in SamplerComparisonState tex2DShadowMap_sampler,
                                in float4                 f4ShadowMapSize,
                                in float2                 f2UV,
                                in float                  fSlice,
                                in float                  fLightSpaceDepth,
                                in float2                 f2ReceiverPlaneDepthBias,
                                in float2                 f2FilterSize)
{

    f2FilterSize = max(f2FilterSize * f4ShadowMapSize.xy, float2(1.0, 1.0));
    float2 f2CenterTexel = f2UV * f4ShadowMapSize.xy;
    // Clamp to the full texture extent, no need for 0.5 texel padding
    float2 f2MinBnd = clamp(f2CenterTexel - f2FilterSize / 2.0, float2(0.0, 0.0), f4ShadowMapSize.xy);
    float2 f2MaxBnd = clamp(f2CenterTexel + f2FilterSize / 2.0, float2(0.0, 0.0), f4ShadowMapSize.xy);
    //
    // StartTexel                                     EndTexel
    //   |  MinBnd                         MaxBnd        |
    //   V    V                              V           V
    //   |    :  X       |       X       |   :   X       |
    //   n              n+1             n+2             n+3
    //
    int2 StartTexelXY = int2(floor(f2MinBnd));
    int2 EndTexelXY   = int2(ceil (f2MaxBnd));

    float TotalWeight = 0.0;
    float Sum = 0.0;

    // Handle as many as 2x2 texels in one iteration
    [loop]
    for (int x = StartTexelXY.x; x < EndTexelXY.x; x += 2)
    {
        float U0 = float(x) + 0.5;
        // Compute horizontal coverage of this and the adjacent texel to the right
        //
        //        U0         U1                  U0         U1                  U0         U1
        //   |    X     |    X     |        |    X     |    X     |        |    X     |    X     |
        //    ####-----------------          ------###------------          ---############------
        //     0.4          0.0                    0.3     0.0                  0.7     0.5
        //
        float LeftTexelCoverage  = max(min(U0 + 0.5, f2MaxBnd.x) - max(U0 - 0.5, f2MinBnd.x), 0.0);
        float RightTexelCoverage = max(min(U0 + 1.5, f2MaxBnd.x) - max(U0 + 0.5, f2MinBnd.x), 0.0);
        float dU = RightTexelCoverage / max(RightTexelCoverage + LeftTexelCoverage, 1e-6);
        float HorzWeight = RightTexelCoverage + LeftTexelCoverage;

        [loop]
        for (int y = StartTexelXY.y; y < EndTexelXY.y; y += 2)
        {
            // Compute vertical coverage of this and the top adjacent texels 
            float V0 = float(y) + 0.5;
            float BottomTexelCoverage = max(min(V0 + 0.5, f2MaxBnd.y) - max(V0 - 0.5, f2MinBnd.y), 0.0);
            float TopTexelCoverage    = max(min(V0 + 1.5, f2MaxBnd.y) - max(V0 + 0.5, f2MinBnd.y), 0.0);
            float dV = TopTexelCoverage / max(BottomTexelCoverage + TopTexelCoverage, 1e-6);
            float VertWeight = BottomTexelCoverage + TopTexelCoverage;

            float2 f2UV = float2(U0 + dU, V0 + dV);

            float Weight = HorzWeight * VertWeight;
            const float DepthClamp = 1e-8;
            float fDepth = max(fLightSpaceDepth + dot(f2UV - f2CenterTexel, f2ReceiverPlaneDepthBias), DepthClamp);
            f2UV *= f4ShadowMapSize.zw;
            #ifdef GLSL
                // There is no OpenGL counterpart for Texture2DArray.SampleCmpLevelZero()
                Sum += tex2DShadowMap.SampleCmp(tex2DShadowMap_sampler, float3(f2UV, fSlice), fDepth) * Weight;
            #else
                Sum += tex2DShadowMap.SampleCmpLevelZero(tex2DShadowMap_sampler, float3(f2UV, fSlice), fDepth) * Weight;
            #endif
            TotalWeight += Weight;
        }
    }
    return TotalWeight > 0.0 ? Sum / TotalWeight : 1.0;
}

#endif

#endif //_PCF_FXH_
