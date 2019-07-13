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

#include "ShadowMapManager.h"

namespace Diligent
{

ShadowMapManager::ShadowMapManager()
{
    
}

void ShadowMapManager::Initialize(IRenderDevice* pDevice, const InitInfo& initInfo)
{
    VERIFY_EXPR(pDevice != nullptr);
    VERIFY(initInfo.Fmt != TEX_FORMAT_UNKNOWN, "Undefined shadow map format");
    VERIFY(initInfo.NumCascades != 0, "Number of cascades must not be zero");
    VERIFY(initInfo.Resolution != 0, "Shadow map resolution must not be zero");

    m_pDevice = pDevice;

    TextureDesc ShadowMapDesc;
    ShadowMapDesc.Name      = "Shadow map SRV";
    ShadowMapDesc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    ShadowMapDesc.Width     = initInfo.Resolution;
    ShadowMapDesc.Height    = initInfo.Resolution;
    ShadowMapDesc.MipLevels = 1;
    ShadowMapDesc.ArraySize = initInfo.NumCascades;
    ShadowMapDesc.Format    = initInfo.Fmt;
    ShadowMapDesc.BindFlags = BIND_SHADER_RESOURCE | BIND_DEPTH_STENCIL;

	RefCntAutoPtr<ITexture> ptex2DShadowMap;
	pDevice->CreateTexture(ShadowMapDesc, nullptr, &ptex2DShadowMap);

    m_pShadowMapSRV.Release();
    m_pShadowMapSRV = ptex2DShadowMap->GetDefaultView( TEXTURE_VIEW_SHADER_RESOURCE );
    if (initInfo.pComparisonSampler != nullptr)
        m_pShadowMapSRV->SetSampler(initInfo.pComparisonSampler);

    m_pShadowMapDSVs.clear();
    m_pShadowMapDSVs.resize(ShadowMapDesc.ArraySize);
    for (Uint32 iArrSlice=0; iArrSlice < ShadowMapDesc.ArraySize; iArrSlice++)
    {
        TextureViewDesc ShadowMapDSVDesc;
        ShadowMapDSVDesc.Name            = "Shadow map cascade DSV";
        ShadowMapDSVDesc.ViewType        = TEXTURE_VIEW_DEPTH_STENCIL;
        ShadowMapDSVDesc.FirstArraySlice = iArrSlice;
        ShadowMapDSVDesc.NumArraySlices  = 1;
        ptex2DShadowMap->CreateView(ShadowMapDSVDesc, &m_pShadowMapDSVs[iArrSlice]);
    }
}

void ShadowMapManager::DistributeCascades(const DistributeCascadeInfo& Info,
                                          ShadowMapAttribs&            shadowMapAttribs)
{
    VERIFY(Info.pCameraView, "Camera view matrix must not be null");
    VERIFY(Info.pCameraProj, "Camera projection matrix must not be null");
    VERIFY(Info.pLightDir, "Light direction must not be null");
    VERIFY(Info.pCameraPos, "Camera position must not be null");
    VERIFY(m_pDevice, "Shadow map manager is not initialized");

    const auto& DevCaps = m_pDevice->GetDeviceCaps();
    const auto IsGL = DevCaps.IsGLDevice();
    const auto& SMDesc = m_pShadowMapSRV->GetTexture()->GetDesc();

    float3 LightSpaceX, LightSpaceY, LightSpaceZ;
    LightSpaceZ = *Info.pLightDir;
    LightSpaceX = float3( 1.0f, 0.0, 0.0 );
    LightSpaceY = cross(LightSpaceX, LightSpaceZ);
    LightSpaceX = cross(LightSpaceZ, LightSpaceY);

    LightSpaceX = normalize( LightSpaceX );
    LightSpaceY = normalize( LightSpaceY );
    LightSpaceZ = normalize( LightSpaceZ );

    float4x4 WorldToLightViewSpaceMatr =
        float4x4::ViewFromBasis( LightSpaceX, LightSpaceY, LightSpaceZ );

    shadowMapAttribs.mWorldToLightViewT = WorldToLightViewSpaceMatr.Transpose();

    float3 f3CameraPosInLightSpace = *Info.pCameraPos * WorldToLightViewSpaceMatr;

    float fMainCamNearPlane, fMainCamFarPlane;
    Info.pCameraProj->GetNearFarClipPlanes(fMainCamNearPlane, fMainCamFarPlane, IsGL);

    for(int i=0; i < MAX_CASCADES; ++i)
        shadowMapAttribs.fCascadeCamSpaceZEnd[i] = +FLT_MAX;

    // Render cascades
    int iNumShadowCascades = SMDesc.ArraySize;
    m_CascadeTransforms.resize(iNumShadowCascades);
    for(int iCascade = 0; iCascade < iNumShadowCascades; ++iCascade)
    {
        auto &CurrCascade = shadowMapAttribs.Cascades[iCascade];
        float4x4 CascadeFrustumProjMatrix;
        float &fCascadeFarZ = shadowMapAttribs.fCascadeCamSpaceZEnd[iCascade];
        float fCascadeNearZ = (iCascade == 0) ? fMainCamNearPlane : shadowMapAttribs.fCascadeCamSpaceZEnd[iCascade-1];
        fCascadeFarZ = fMainCamFarPlane;

        if (iCascade < iNumShadowCascades-1) 
        {
            float ratio = fMainCamFarPlane / fMainCamNearPlane;
            float power = (float)(iCascade+1) / (float)iNumShadowCascades;
            float logZ = fMainCamNearPlane * pow(ratio, power);
        
            float range = fMainCamFarPlane - fMainCamNearPlane;
            float uniformZ = fMainCamNearPlane + range * power;

            fCascadeFarZ = shadowMapAttribs.fCascadePartitioningFactor * (logZ - uniformZ) + uniformZ;
        }

        if(Info.AdjustCascadeRange)
        {
            Info.AdjustCascadeRange(iCascade, fCascadeNearZ, fCascadeFarZ);
        }
        CurrCascade.f4StartEndZ.x = fCascadeNearZ;
        CurrCascade.f4StartEndZ.y = fCascadeFarZ;
        
        CascadeFrustumProjMatrix =  *Info.pCameraProj;
        CascadeFrustumProjMatrix.SetNearFarClipPlanes(fCascadeNearZ, fCascadeFarZ, IsGL);

        float4x4 CascadeFrustumViewProjMatr = *Info.pCameraView * CascadeFrustumProjMatrix;
        float4x4 CascadeFrustumProjSpaceToWorldSpace = CascadeFrustumViewProjMatr.Inverse();
        float4x4 CascadeFrustumProjSpaceToLightSpace = CascadeFrustumProjSpaceToWorldSpace * WorldToLightViewSpaceMatr;

        // Set reference minimums and maximums for each coordinate
        float3 f3MinXYZ = float3(+FLT_MAX, +FLT_MAX, +FLT_MAX);
        float3 f3MaxXYZ = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        for(int iClipPlaneCorner=0; iClipPlaneCorner < 8; ++iClipPlaneCorner)
        {
            float3 f3PlaneCornerProjSpace( (iClipPlaneCorner & 0x01) ? +1.f : - 1.f, 
                                           (iClipPlaneCorner & 0x02) ? +1.f : - 1.f,
                                            // Since we use complimentary depth buffering, 
                                            // far plane has depth 0
                                           (iClipPlaneCorner & 0x04) ? 1.f : (IsGL ? -1.f : 0.f));
            float3 f3PlaneCornerLightSpace = f3PlaneCornerProjSpace * CascadeFrustumProjSpaceToLightSpace;
            f3MinXYZ = min(f3MinXYZ, f3PlaneCornerLightSpace);
            f3MaxXYZ = max(f3MaxXYZ, f3PlaneCornerLightSpace);
        }
        
        float fCascadeXExt = (f3MaxXYZ.x - f3MinXYZ.x) * (1 + 1.f/(float)SMDesc.Width);
        float fCascadeYExt = (f3MaxXYZ.y - f3MinXYZ.y) * (1 + 1.f/(float)SMDesc.Height);
        //fCascadeXExt = fCascadeYExt = std::max(fCascadeXExt, fCascadeYExt);
        // Align cascade extent to the closest power of two
        //const float fExtStep = 2.f;
        //fCascadeXExt = pow( fExtStep, ceil( log(fCascadeXExt)/log(fExtStep) ) );
        //fCascadeYExt = pow( fExtStep, ceil( log(fCascadeYExt)/log(fExtStep) ) );
        // Align cascade center with the shadow map texels to alleviate temporal aliasing
        float fCascadeXCenter = (f3MaxXYZ.x + f3MinXYZ.x)/2.f;
        float fCascadeYCenter = (f3MaxXYZ.y + f3MinXYZ.y)/2.f;
        float fTexelXSize = fCascadeXExt / (float)SMDesc.Width;
        float fTexelYSize = fCascadeYExt / (float)SMDesc.Height;
        fCascadeXCenter = floor(fCascadeXCenter/fTexelXSize) * fTexelXSize;
        fCascadeYCenter = floor(fCascadeYCenter/fTexelYSize) * fTexelYSize;
        // Compute new cascade min/max xy coords
        f3MaxXYZ.x = fCascadeXCenter + fCascadeXExt/2.f;
        f3MinXYZ.x = fCascadeXCenter - fCascadeXExt/2.f;
        f3MaxXYZ.y = fCascadeYCenter + fCascadeYExt/2.f;
        f3MinXYZ.y = fCascadeYCenter - fCascadeYExt/2.f;

        CurrCascade.f4LightSpaceScale.x =  2.f / (f3MaxXYZ.x - f3MinXYZ.x);
        CurrCascade.f4LightSpaceScale.y =  2.f / (f3MaxXYZ.y - f3MinXYZ.y);
        CurrCascade.f4LightSpaceScale.z =  
                    (IsGL ? 2.f : 1.f) / (f3MaxXYZ.z - f3MinXYZ.z);
        // Apply bias to shift the extent to [-1,1]x[-1,1]x[0,1] for DX or to [-1,1]x[-1,1]x[-1,1] for GL
        // Find bias such that f3MinXYZ -> (-1,-1,0) for DX or (-1,-1,-1) for GL
        CurrCascade.f4LightSpaceScaledBias.x = -f3MinXYZ.x * CurrCascade.f4LightSpaceScale.x - 1.f;
        CurrCascade.f4LightSpaceScaledBias.y = -f3MinXYZ.y * CurrCascade.f4LightSpaceScale.y - 1.f;
        CurrCascade.f4LightSpaceScaledBias.z = -f3MinXYZ.z * CurrCascade.f4LightSpaceScale.z + (IsGL ? -1.f : 0.f);

        float4x4 ScaleMatrix = float4x4::Scale(CurrCascade.f4LightSpaceScale.x, CurrCascade.f4LightSpaceScale.y, CurrCascade.f4LightSpaceScale.z);
        float4x4 ScaledBiasMatrix = float4x4::Translation( CurrCascade.f4LightSpaceScaledBias.x, CurrCascade.f4LightSpaceScaledBias.y, CurrCascade.f4LightSpaceScaledBias.z ) ;

        // Note: bias is applied after scaling!
        float4x4& CascadeProjMatr = m_CascadeTransforms[iCascade].Proj;
        CascadeProjMatr = ScaleMatrix * ScaledBiasMatrix;
        
        // Adjust the world to light space transformation matrix
        float4x4& WorldToLightProjSpaceMatr = m_CascadeTransforms[iCascade].WorldToLightProjSpace;
        WorldToLightProjSpaceMatr = WorldToLightViewSpaceMatr * CascadeProjMatr;

        const auto& NDCAttribs = DevCaps.GetNDCAttribs();
        float4x4 ProjToUVScale = float4x4::Scale( 0.5f, NDCAttribs.YtoVScale, NDCAttribs.ZtoDepthScale );
        float4x4 ProjToUVBias = float4x4::Translation( 0.5f, 0.5f, NDCAttribs.GetZtoDepthBias());
        
        float4x4 WorldToShadowMapUVDepthMatr = WorldToLightProjSpaceMatr * ProjToUVScale * ProjToUVBias;
        shadowMapAttribs.mWorldToShadowMapUVDepthT[iCascade] = WorldToShadowMapUVDepthMatr.Transpose();
    }
}

}
