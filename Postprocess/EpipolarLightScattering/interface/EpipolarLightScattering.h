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
#include "Structures.fxh"
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

    RefCntAutoPtr<IPipelineState> CreateScreenSizeQuadPSO(IRenderDevice*               pDevice,
                                                          const char*                  PSOName,
                                                          IShader*                     PixelShader,
                                                          const DepthStencilStateDesc& DSSDesc,
                                                          const BlendStateDesc&        BSDesc,
                                                          Uint8                        NumRTVs,
                                                          TEXTURE_FORMAT               RTVFmts[],
                                                          TEXTURE_FORMAT               DSVFmt = TEX_FORMAT_UNKNOWN);

    void RenderScreenSizeQuad(IDeviceContext*         pDeviceContext, 
                              IPipelineState*         PSO,
                              IShaderResourceBinding* SRB,
                              Uint8                   StencilRef = 0,
                              Uint32                  NumQuads   = 1);

    void DefineMacros(ShaderMacroHelper& Macros);
    
    const TEXTURE_FORMAT m_BackBufferFmt;
    const TEXTURE_FORMAT m_DepthBufferFmt;
    const TEXTURE_FORMAT m_OffscreenBackBufferFmt;

    static constexpr TEXTURE_FORMAT CoordinateTexFmt           = TEX_FORMAT_RG32_FLOAT;
    static constexpr TEXTURE_FORMAT SliceEndpointsFmt          = TEX_FORMAT_RGBA32_FLOAT;
    static constexpr TEXTURE_FORMAT InterpolationSourceTexFmt  = TEX_FORMAT_RGBA32_UINT;
    static constexpr TEXTURE_FORMAT EpipolarCamSpaceZFmt       = TEX_FORMAT_R32_FLOAT;
    static constexpr TEXTURE_FORMAT EpipolarInsctrTexFmt       = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT EpipolarImageDepthFmt      = TEX_FORMAT_D24_UNORM_S8_UINT;
    static constexpr TEXTURE_FORMAT EpipolarExtinctionFmt      = TEX_FORMAT_RGBA8_UNORM;
    static constexpr TEXTURE_FORMAT AmbientSkyLightTexFmt      = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT LuminanceTexFmt            = TEX_FORMAT_R16_FLOAT;
    static constexpr TEXTURE_FORMAT SliceUVDirAndOriginTexFmt  = TEX_FORMAT_RGBA32_FLOAT;
    static constexpr TEXTURE_FORMAT CamSpaceZFmt               = TEX_FORMAT_R32_FLOAT;


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

    RefCntAutoPtr<IShader> m_pQuadVS;
    RefCntAutoPtr<IShader> m_pRefineSampleLocationsCS;
    RefCntAutoPtr<IPipelineState>         m_pRefineSampleLocationsPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRefineSampleLocationsSRB;
    
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

    RefCntAutoPtr<IPipelineState>         m_pRenderSampleLocationsPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRenderSampleLocationsSRB;
    RefCntAutoPtr<ISampler> m_pPointClampSampler, m_pLinearClampSampler;

    RefCntAutoPtr<IShader> m_pPrecomputeSingleSctrCS;
    RefCntAutoPtr<IShader> m_pComputeSctrRadianceCS;
    RefCntAutoPtr<IShader> m_pComputeScatteringOrderCS;
    RefCntAutoPtr<IShader> m_pInitHighOrderScatteringCS, m_pUpdateHighOrderScatteringCS;
    RefCntAutoPtr<IShader> m_pCombineScatteringOrdersCS;

    RefCntAutoPtr<IPipelineState>         m_pReconstrCamSpaceZPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pReconstrCamSpaceZSRB;
    RefCntAutoPtr<IPipelineState>         m_pRendedSliceEndpointsPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRendedSliceEndpointsSRB;
    RefCntAutoPtr<IPipelineState>         m_pRendedCoordTexPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRendedCoordTexSRB;
    RefCntAutoPtr<IPipelineState>         m_pRenderCoarseUnshadowedInsctrPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRenderCoarseUnshadowedInsctrSRB;
    RefCntAutoPtr<IPipelineState>         m_pMarkRayMarchingSamplesInStencilPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pMarkRayMarchingSamplesInStencilSRB;
    RefCntAutoPtr<IPipelineState>         m_pRenderSliceUVDirInSM_PSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRenderSliceUVDirInSM_SRB;
    RefCntAutoPtr<IPipelineState>         m_pInitializeMinMaxShadowMapPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pInitializeMinMaxShadowMapSRB;
    RefCntAutoPtr<IPipelineState>         m_pComputeMinMaxSMLevelPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pComputeMinMaxSMLevelSRB[2];
    RefCntAutoPtr<IPipelineState>         m_pDoRayMarchPSO[2]; // 0 - min/max optimization disabled; 1 - min/max optimization enabled
    RefCntAutoPtr<IShaderResourceBinding> m_pDoRayMarchSRB[2];
    RefCntAutoPtr<IPipelineState>         m_pInterpolateIrradiancePSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pInterpolateIrradianceSRB;
    RefCntAutoPtr<IPipelineState>         m_pUnwarpEpipolarSctrImgPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pUnwarpEpipolarSctrImgSRB;
    RefCntAutoPtr<IPipelineState>         m_pUnwarpAndRenderLuminancePSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pUnwarpAndRenderLuminanceSRB;
    RefCntAutoPtr<IPipelineState>         m_pUpdateAverageLuminancePSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pUpdateAverageLuminanceSRB;
    RefCntAutoPtr<IPipelineState>         m_pFixInsctrAtDepthBreaksPSO[3]; // 0 - Luminance Only,
                                                                           // 1 - Fix Inscattering
                                                                           // 2 - Full Screen Ray Marching
    RefCntAutoPtr<IShaderResourceBinding> m_pFixInsctrAtDepthBreaksSRB[3];

    RefCntAutoPtr<IPipelineState>         m_pPrecomputeNetDensityToAtmTopPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pPrecomputeNetDensityToAtmTopSRB;
    RefCntAutoPtr<IPipelineState>         m_pPrecomputeSingleSctrPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pPrecomputeSingleSctrSRB;
    RefCntAutoPtr<IPipelineState>         m_pComputeSctrRadiancePSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pComputeSctrRadianceSRB;
    RefCntAutoPtr<IPipelineState>         m_pComputeScatteringOrderPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pComputeScatteringOrderSRB;
    RefCntAutoPtr<IPipelineState>         m_pInitHighOrderScatteringPSO, m_pUpdateHighOrderScatteringPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pInitHighOrderScatteringSRB, m_pUpdateHighOrderScatteringSRB;
    RefCntAutoPtr<IPipelineState>         m_pCombineScatteringOrdersPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pCombineScatteringOrdersSRB;
    RefCntAutoPtr<IPipelineState>         m_pRenderSunPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRenderSunSRB;
    RefCntAutoPtr<IPipelineState>         m_pPrecomputeAmbientSkyLightPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pPrecomputeAmbientSkyLightSRB;

    
    RefCntAutoPtr<ITexture> m_ptex3DHighOrderSctr, m_ptex3DHighOrderSctr2;

    RefCntAutoPtr<IBuffer> m_pcbPostProcessingAttribs;
    RefCntAutoPtr<IBuffer> m_pcbMediaAttribs;
    RefCntAutoPtr<IBuffer> m_pcbMiscParams;

    Uint32 m_uiBackBufferWidth, m_uiBackBufferHeight;
    
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
