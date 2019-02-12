/*     Copyright 2015-2019 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

// This file is derived from the open source project provided by Intel Corportaion that
// requires the following notice to be kept:
//--------------------------------------------------------------------------------------
// Copyright 2013 Intel Corporation
// All Rights Reserved
//
// Permission is granted to use, copy, distribute and prepare derivative works of this
// software for any purpose and without fee, provided, that the above copyright notice
// and this statement appear in all copies.  Intel makes no representations about the
// suitability of this software for any purpose.  THIS SOFTWARE IS PROVIDED "AS IS."
// INTEL SPECIFICALLY DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, AND ALL LIABILITY,
// INCLUDING CONSEQUENTIAL AND OTHER INDIRECT DAMAGES, FOR THE USE OF THIS SOFTWARE,
// INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PROPRIETARY RIGHTS, AND INCLUDING THE
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  Intel does not
// assume any responsibility for any errors which may appear in this software nor any
// responsibility to update it.
//--------------------------------------------------------------------------------------

#pragma once

#include "ShaderMacroHelper.h"
#include "EpipolarLightScatteringStructures.fxh"
#include "BasicStructures.fxh"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Buffer.h"
#include "Texture.h"
#include "BufferView.h"
#include "TextureView.h"
#include "RefCntAutoPtr.h"

namespace Diligent
{

struct FrameAttribs
{
    IRenderDevice*  pDevice         = nullptr;
    IDeviceContext* pDeviceContext  = nullptr;
    
    double dElapsedTime             = 0;

    LightAttribs*   pLightAttribs    = nullptr;
    IBuffer*        pcbLightAttribs  = nullptr;
    IBuffer*        pcbCameraAttribs = nullptr;

    //CameraAttribs CameraAttribs;
    
    ITextureView*   ptex2DSrcColorBufferSRV = nullptr;
    ITextureView*   ptex2DSrcColorBufferRTV = nullptr;
    ITextureView*   ptex2DSrcDepthBufferDSV = nullptr;
    ITextureView*   ptex2DSrcDepthBufferSRV = nullptr;
    ITextureView*   ptex2DShadowMapSRV      = nullptr;
    ITextureView*   pDstRTV                 = nullptr;
};

class EpipolarLightScattering
{
public:
    EpipolarLightScattering(IRenderDevice*  in_pDevice, 
                            IDeviceContext* in_pContext,
                            TEXTURE_FORMAT  BackBufferFmt,
                            TEXTURE_FORMAT  DepthBufferFmt,
                            TEXTURE_FORMAT  OffscreenBackBuffer);
    ~EpipolarLightScattering();


    void OnWindowResize(IRenderDevice* pDevice, Uint32 uiBackBufferWidth, Uint32 uiBackBufferHeight);

    void PerformPostProcessing(FrameAttribs&            FrameAttribs,
                               PostProcessingAttribs&   PPAttribs);

    void ComputeSunColor(const float3&  vDirectionOnSun,
                         const float4&  f4ExtraterrestrialSunColor,
                         float4&        f4SunColorAtGround,
                         float4&        f4AmbientLight);
    
    void RenderSun(FrameAttribs&    FrameAttribs);
    IBuffer* GetMediaAttribsCB(){return m_pcbMediaAttribs;}
    ITextureView* GetPrecomputedNetDensitySRV(){return m_ptex2DOccludedNetDensityToAtmTopSRV;}
    ITextureView* GetAmbientSkyLightSRV(IRenderDevice* pDevice, IDeviceContext* pContext);

private:
    void ReconstructCameraSpaceZ    (FrameAttribs& FrameAttribs);
    void RenderSliceEndpoints       (FrameAttribs& FrameAttribs);
    void RenderCoordinateTexture    (FrameAttribs& FrameAttribs);
    void RenderCoarseUnshadowedInctr(FrameAttribs& FrameAttribs);
    void RefineSampleLocations      (FrameAttribs& FrameAttribs);
    void MarkRayMarchingSamples     (FrameAttribs& FrameAttribs);
    void RenderSliceUVDirAndOrig    (FrameAttribs& FrameAttribs);
    void Build1DMinMaxMipMap        (FrameAttribs& FrameAttribs, int iCascadeIndex);
    void DoRayMarching              (FrameAttribs& FrameAttribs, Uint32 uiMaxStepsAlongRay, int iCascadeIndex);
    void InterpolateInsctrIrradiance(FrameAttribs& FrameAttribs);
    void UnwarpEpipolarScattering   (FrameAttribs& FrameAttribs, bool bRenderLuminance);
    void UpdateAverageLuminance     (FrameAttribs& FrameAttribs);
    enum class EFixInscatteringMode
    {
        LuminanceOnly = 0,
        FixInscattering = 1,
        FullScreenRayMarching = 2
    };
    void FixInscatteringAtDepthBreaks(FrameAttribs& FrameAttribs, Uint32 uiMaxStepsAlongRay, EFixInscatteringMode Mode);
    void RenderSampleLocations       (FrameAttribs& FrameAttribs);

    void CreatePrecomputedOpticalDepthTexture(IRenderDevice *pDevice, IDeviceContext *pContext);
    void CreatePrecomputedScatteringLUT      (IRenderDevice *pDevice, IDeviceContext *pContext);
    void CreateRandomSphereSamplingTexture   (IRenderDevice *pDevice);
    void ComputeAmbientSkyLightTexture       (IRenderDevice* pDevice, IDeviceContext *pContext);
    void ComputeScatteringCoefficients       (IDeviceContext* pDeviceCtx = nullptr);
    void CreateAuxTextures                   (IRenderDevice* pDevice);
    void CreateExtinctionTexture             (IRenderDevice* pDevice);
    void CreateAmbientSkyLightTexture        (IRenderDevice* pDevice);
    void CreateLowResLuminanceTexture        (IRenderDevice* pDevice, IDeviceContext* pDeviceCtx);
    void CreateSliceUVDirAndOriginTexture    (IRenderDevice* pDevice);
    void CreateCamSpaceZTexture              (IRenderDevice* pDevice);
    void ResetShaderResourceBindings();

    void DefineMacros(ShaderMacroHelper& Macros);
    
    const TEXTURE_FORMAT m_BackBufferFmt;
    const TEXTURE_FORMAT m_DepthBufferFmt;
    const TEXTURE_FORMAT m_OffscreenBackBufferFmt;

    static constexpr TEXTURE_FORMAT PrecomputedNetDensityTexFmt = TEX_FORMAT_RG32_FLOAT;
    static constexpr TEXTURE_FORMAT CoordinateTexFmt            = TEX_FORMAT_RG32_FLOAT;
    static constexpr TEXTURE_FORMAT SliceEndpointsFmt           = TEX_FORMAT_RGBA32_FLOAT;
    static constexpr TEXTURE_FORMAT InterpolationSourceTexFmt   = TEX_FORMAT_RGBA32_UINT;
    static constexpr TEXTURE_FORMAT EpipolarCamSpaceZFmt        = TEX_FORMAT_R32_FLOAT;
    static constexpr TEXTURE_FORMAT EpipolarInsctrTexFmt        = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT EpipolarImageDepthFmt       = TEX_FORMAT_D24_UNORM_S8_UINT;
    static constexpr TEXTURE_FORMAT EpipolarExtinctionFmt       = TEX_FORMAT_RGBA8_UNORM;
    static constexpr TEXTURE_FORMAT AmbientSkyLightTexFmt       = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT LuminanceTexFmt             = TEX_FORMAT_R16_FLOAT;
    static constexpr TEXTURE_FORMAT SliceUVDirAndOriginTexFmt   = TEX_FORMAT_RGBA32_FLOAT;
    static constexpr TEXTURE_FORMAT CamSpaceZFmt                = TEX_FORMAT_R32_FLOAT;


    PostProcessingAttribs m_PostProcessingAttribs;
    bool m_bUseCombinedMinMaxTexture;
    Uint32 m_uiSampleRefinementCSThreadGroupSize;
    Uint32 m_uiSampleRefinementCSMinimumThreadGroupSize;

    RefCntAutoPtr<ITextureView> m_ptex2DMinMaxShadowMapSRV[2];
    RefCntAutoPtr<ITextureView> m_ptex2DMinMaxShadowMapRTV[2];

    static const int sm_iNumPrecomputedHeights = 1024;
    static const int sm_iNumPrecomputedAngles  = 1024;

    static const int sm_iPrecomputedSctrUDim = 32;
    static const int sm_iPrecomputedSctrVDim = 128;
    static const int sm_iPrecomputedSctrWDim = 64;
    static const int sm_iPrecomputedSctrQDim = 16;

    RefCntAutoPtr<ITextureView> m_ptex3DSingleScatteringSRV;
    RefCntAutoPtr<ITextureView> m_ptex3DHighOrderScatteringSRV;
    RefCntAutoPtr<ITextureView> m_ptex3DMultipleScatteringSRV;
    
    const Uint32 m_uiNumRandomSamplesOnSphere;
    
    RefCntAutoPtr<ITextureView> m_ptex2DSphereRandomSamplingSRV;

    void CreateMinMaxShadowMap(IRenderDevice* pDevice);

    static const int sm_iLowResLuminanceMips = 7; // 64x64
    RefCntAutoPtr<ITextureView> m_ptex2DLowResLuminanceRTV, m_ptex2DLowResLuminanceSRV;
    
    static const int sm_iAmbientSkyLightTexDim = 1024;
    RefCntAutoPtr<ITextureView> m_ptex2DAmbientSkyLightSRV;
    RefCntAutoPtr<ITextureView> m_ptex2DAmbientSkyLightRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DOccludedNetDensityToAtmTopSRV;
    RefCntAutoPtr<ITextureView> m_ptex2DOccludedNetDensityToAtmTopRTV;

    RefCntAutoPtr<IShader> m_pFullScreenTriangleVS;
    
    RefCntAutoPtr<IResourceMapping> m_pResMapping;

    RefCntAutoPtr<ITextureView> m_ptex2DCoordinateTextureRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DSliceEndpointsRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DEpipolarCamSpaceZRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DEpipolarInscatteringRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DEpipolarExtinctionRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DEpipolarImageDSV;
    RefCntAutoPtr<ITextureView> m_ptex2DInitialScatteredLightRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DAverageLuminanceRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DSliceUVDirAndOriginRTV;
    RefCntAutoPtr<ITextureView> m_ptex2DCamSpaceZRTV;

    RefCntAutoPtr<ISampler> m_pPointClampSampler, m_pLinearClampSampler;

    struct RenderTechnique
    {
        RefCntAutoPtr<IPipelineState>         PSO;
        RefCntAutoPtr<IShaderResourceBinding> SRB;
        Uint32                                PSODependencyFlags = 0;
        Uint32                                SRBDependencyFlags = 0;
        
        void InitializeFullScreenTriangleTechnique(IRenderDevice*               pDevice,
                                                   const char*                  PSOName,
                                                   IShader*                     VertexShader,
                                                   IShader*                     PixelShader,
                                                   Uint8                        NumRTVs,
                                                   TEXTURE_FORMAT               RTVFmts[],
                                                   TEXTURE_FORMAT               DSVFmt,
                                                   const DepthStencilStateDesc& DSSDesc,
                                                   const BlendStateDesc&        BSDesc);

        void InitializeFullScreenTriangleTechnique(IRenderDevice*               pDevice,
                                                   const char*                  PSOName,
                                                   IShader*                     VertexShader,
                                                   IShader*                     PixelShader,
                                                   TEXTURE_FORMAT               RTVFmt,
                                                   TEXTURE_FORMAT               DSVFmt,
                                                   const DepthStencilStateDesc& DSSDesc,
                                                   const BlendStateDesc&        BSDesc);

        void InitializeComputeTechnique(IRenderDevice*   pDevice,
                                        const char*      PSOName,
                                        IShader*         ComputeShader);

        void PrepareSRB(IRenderDevice* pDevice, IResourceMapping* pResMapping, Uint32 Flags);

        void Render(IDeviceContext* pDeviceContext, Uint8 StencilRef = 0, Uint32 NumQuads = 1);

        void DispatchCompute(IDeviceContext* pDeviceContext, const DispatchComputeAttribs& DispatchAttrs);

        void CheckStaleFlags(Uint32 StalePSODependencies, Uint32 StaleSRBDependencies);
    };

    enum RENDER_TECH
    {
        RENDER_TECH_RECONSTRUCT_CAM_SPACE_Z = 0,
        RENDER_TECH_RENDER_SLICE_END_POINTS,
        RENDER_TECH_RENDER_COORD_TEXTURE,
        RENDER_TECH_RENDER_COARSE_UNSHADOWED_INSCTR,
        RENDER_TECH_REFINE_SAMPLE_LOCATIONS,
        RENDER_TECH_MARK_RAY_MARCHING_SAMPLES,
        RENDER_TECH_RENDER_SLICE_UV_DIRECTION,
        RENDER_TECH_INIT_MIN_MAX_SHADOW_MAP,
        RENDER_TECH_COMPUTE_MIN_MAX_SHADOW_MAP_LEVEL,
        RENDER_TECH_RAY_MARCH_NO_MIN_MAX_OPT,
        RENDER_TECH_RAY_MARCH_MIN_MAX_OPT,
        RENDER_TECH_INTERPOLATE_IRRADIANCE,
        RENDER_TECH_UNWARP_EPIPOLAR_SCATTERING,
        RENDER_TECH_UNWARP_AND_RENDER_LUMINANCE,
        RENDER_TECH_UPDATE_AVERAGE_LUMINANCE,
        RENDER_TECH_FIX_INSCATTERING_LUM_ONLY,
        RENDER_TECH_FIX_INSCATTERING,
        RENDER_TECH_BRUTE_FORCE_RAY_MARCHING,
        RENDER_TECH_RENDER_SUN,
        RENDER_TECH_RENDER_SAMPLE_LOCATIONS,

        // Precomputation techniques
        RENDER_TECH_PRECOMPUTE_NET_DENSITY_TO_ATM_TOP,
        RENDER_TECH_PRECOMPUTE_SINGLE_SCATTERING,
        RENDER_TECH_COMPUTE_SCATTERING_RADIANCE,
        RENDER_TECH_COMPUTE_SCATTERING_ORDER,
        RENDER_TECH_INIT_HIGH_ORDER_SCATTERING,
        RENDER_TECH_UPDATE_HIGH_ORDER_SCATTERING,
        RENDER_TECH_COMBINE_SCATTERING_ORDERS,
        RENDER_TECH_PRECOMPUTE_AMBIENT_SKY_LIGHT,

        RENDER_TECH_TOTAL_TECHNIQUES
    };

    RenderTechnique m_RenderTech[RENDER_TECH_TOTAL_TECHNIQUES];

    enum PSO_DEPENDENCY_FLAGS
    {
        PSO_DEPENDENCY_NUM_EPIPOLAR_SLICES       = 0x00001,
        PSO_DEPENDENCY_MAX_SAMPLES_IN_SLICE      = 0x00002,
        PSO_DEPENDENCY_INITIAL_SAMPLE_STEP       = 0x00004,
        PSO_DEPENDENCY_EPIPOLE_SAMPLING_DENSITY  = 0x00008,
        PSO_DEPENDENCY_CORRECT_SCATTERING        = 0x00010,
        PSO_DEPENDENCY_OPTIMIZE_SAMPLE_LOCATIONS = 0x00020,
        PSO_DEPENDENCY_ENABLE_LIGHT_SHAFTS       = 0x00040,
        PSO_DEPENDENCY_USE_1D_MIN_MAX_TREE       = 0x00080,
        PSO_DEPENDENCY_USE_COMBINED_MIN_MAX_TEX  = 0x00100,
        PSO_DEPENDENCY_LIGHT_SCTR_TECHNIQUE      = 0x00200,
        PSO_DEPENDENCY_CASCADE_PROCESSING_MODE   = 0x00400,
        PSO_DEPENDENCY_REFINEMENT_CRITERION      = 0x00800,
        PSO_DEPENDENCY_IS_32_BIT_MIN_MAX_TREE    = 0x01000,
        PSO_DEPENDENCY_MULTIPLE_SCATTERING_MODE  = 0x02000,
        PSO_DEPENDENCY_SINGLE_SCATTERING_MODE    = 0x04000,
        PSO_DEPENDENCY_AUTO_EXPOSURE             = 0x08000,
        PSO_DEPENDENCY_TONE_MAPPING_MODE         = 0x10000,
        PSO_DEPENDENCY_LIGHT_ADAPTATION          = 0x20000,
        PSO_DEPENDENCY_EXTINCTION_EVAL_MODE      = 0x40000
    };

    RefCntAutoPtr<IShaderResourceBinding> m_pComputeMinMaxSMLevelSRB[2];
   
    RefCntAutoPtr<ITexture> m_ptex3DHighOrderSctr, m_ptex3DHighOrderSctr2;

    RefCntAutoPtr<IBuffer> m_pcbPostProcessingAttribs;
    RefCntAutoPtr<IBuffer> m_pcbMediaAttribs;
    RefCntAutoPtr<IBuffer> m_pcbMiscParams;

    Uint32 m_uiBackBufferWidth  = 0;
    Uint32 m_uiBackBufferHeight = 0;
    
    //const float m_fTurbidity = 1.02f;
    AirScatteringAttribs m_MediaParams;

    enum UpToDateResourceFlags
    {
        AuxTextures                 = 0x001,
        ExtinctionTexture           = 0x002,
        SliceUVDirAndOriginTex      = 0x004,
        PrecomputedOpticalDepthTex  = 0x008,
        LowResLuminamceTex          = 0x010,
        AmbientSkyLightTex          = 0x020,
        PrecomputedIntegralsTex     = 0x040
    };
    Uint32 m_uiUpToDateResourceFlags;
    RefCntAutoPtr<ITextureView> m_ptex2DShadowMapSRV;
};

}
