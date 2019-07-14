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
#include "AdvancedMath.h"

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
                                          ShadowMapAttribs&            ShadowAttribs)
{
    VERIFY(Info.pCameraView, "Camera view matrix must not be null");
    VERIFY(Info.pCameraProj, "Camera projection matrix must not be null");
    VERIFY(Info.pLightDir, "Light direction must not be null");
    VERIFY(Info.pCameraPos, "Camera position must not be null");
    VERIFY(m_pDevice, "Shadow map manager is not initialized");

    const auto& DevCaps = m_pDevice->GetDeviceCaps();
    const auto IsGL = DevCaps.IsGLDevice();
    const auto& SMDesc = m_pShadowMapSRV->GetTexture()->GetDesc();
    float2 f2CascadeSize = float2(static_cast<float>(SMDesc.Width), static_cast<float>(SMDesc.Height));

    float3 LightSpaceX, LightSpaceY, LightSpaceZ;
    LightSpaceZ = *Info.pLightDir;
    VERIFY(length(LightSpaceZ) > 1e-5, "Light direction vector length is zero");
    LightSpaceZ = normalize(LightSpaceZ);

    auto min_cmp = std::min(std::min(std::abs(Info.pLightDir->x), std::abs(Info.pLightDir->y)), std::abs(Info.pLightDir->z));
    if (min_cmp == std::abs(Info.pLightDir->x))
        LightSpaceX =  float3(1, 0, 0);
    else if (min_cmp == std::abs(Info.pLightDir->y))
        LightSpaceX =  float3(0, 1, 0);
    else
        LightSpaceX =  float3(0, 0, 1);

    LightSpaceY = cross(LightSpaceZ, LightSpaceX);
    LightSpaceX = cross(LightSpaceY, LightSpaceZ);
    LightSpaceX = normalize(LightSpaceX);
    LightSpaceY = normalize(LightSpaceY);
    

    float4x4 WorldToLightViewSpaceMatr =
        float4x4::ViewFromBasis( LightSpaceX, LightSpaceY, LightSpaceZ );

    ShadowAttribs.mWorldToLightViewT = WorldToLightViewSpaceMatr.Transpose();

    float3 f3CameraPosInLightSpace = *Info.pCameraPos * WorldToLightViewSpaceMatr;

    float fMainCamNearPlane, fMainCamFarPlane;
    Info.pCameraProj->GetNearFarClipPlanes(fMainCamNearPlane, fMainCamFarPlane, IsGL);
    if(Info.AdjustCascadeRange)
    {
        Info.AdjustCascadeRange(-1, fMainCamNearPlane, fMainCamFarPlane);
    }

    for(int i=0; i < MAX_CASCADES; ++i)
        ShadowAttribs.fCascadeCamSpaceZEnd[i] = +FLT_MAX;

    const auto& CameraWorld = Info.pCameraWorld != nullptr ? *Info.pCameraWorld : Info.pCameraView->Inverse();

    // Render cascades
    int iNumShadowCascades = SMDesc.ArraySize;
    m_CascadeTransforms.resize(iNumShadowCascades);
    for(int iCascade = 0; iCascade < iNumShadowCascades; ++iCascade)
    {
        auto &CurrCascade = ShadowAttribs.Cascades[iCascade];
        float fCascadeNearZ = (iCascade == 0) ? fMainCamNearPlane : ShadowAttribs.fCascadeCamSpaceZEnd[iCascade-1];
        float &fCascadeFarZ = ShadowAttribs.fCascadeCamSpaceZEnd[iCascade];
        if (iCascade < iNumShadowCascades-1) 
        {
            float ratio = fMainCamFarPlane / fMainCamNearPlane;
            float power = (float)(iCascade+1) / (float)iNumShadowCascades;
            float logZ = fMainCamNearPlane * pow(ratio, power);
        
            float range = fMainCamFarPlane - fMainCamNearPlane;
            float uniformZ = fMainCamNearPlane + range * power;

            fCascadeFarZ = ShadowAttribs.fCascadePartitioningFactor * (logZ - uniformZ) + uniformZ;
        }
        else
        {
            fCascadeFarZ = fMainCamFarPlane;
        }

        if(Info.AdjustCascadeRange)
        {
            Info.AdjustCascadeRange(iCascade, fCascadeNearZ, fCascadeFarZ);
        }
        VERIFY(fCascadeNearZ > 0.f, "Near plane distance can't be zero");
        CurrCascade.f4StartEndZ.x = fCascadeNearZ;
        CurrCascade.f4StartEndZ.y = fCascadeFarZ;
        
        // Set reference minimums and maximums for each coordinate
        float3 f3MinXYZ = float3(+FLT_MAX, +FLT_MAX, +FLT_MAX);
        float3 f3MaxXYZ = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

        if (Info.StabilizeExtents)
        {
            // We need to make sure that cascade extents are independent of the camera position and orientation.
            // For that, we compute the minimum bounding sphere of a cascade camera frustum.
            float3 f3MinimalSphereCenter;
            float  fMinimalSphereRadius;
            GetFrustumMinimumBoundingSphere(Info.pCameraProj->_11, Info.pCameraProj->_22, fCascadeNearZ, fCascadeFarZ, f3MinimalSphereCenter, fMinimalSphereRadius);
            auto f3CenterLightSpace = f3MinimalSphereCenter * CameraWorld * WorldToLightViewSpaceMatr;
            f3MinXYZ = f3CenterLightSpace - float3(fMinimalSphereRadius, fMinimalSphereRadius, fMinimalSphereRadius);
            f3MaxXYZ = f3CenterLightSpace + float3(fMinimalSphereRadius, fMinimalSphereRadius, fMinimalSphereRadius);
        }
        else
        {
            float4x4 CascadeFrustumProjMatrix =  *Info.pCameraProj;
            CascadeFrustumProjMatrix.SetNearFarClipPlanes(fCascadeNearZ, fCascadeFarZ, IsGL);
            float4x4 CascadeFrustumViewProjMatr = *Info.pCameraView * CascadeFrustumProjMatrix;
            float4x4 CascadeFrustumProjSpaceToWorldSpace = CascadeFrustumViewProjMatr.Inverse();
            float4x4 CascadeFrustumProjSpaceToLightSpace = CascadeFrustumProjSpaceToWorldSpace * WorldToLightViewSpaceMatr;
            for(int i=0; i < 8; ++i)
            {
                float3 f3FrustumCornerProjSpace
                {
                    (i & 0x01) ? +1.f : - 1.f,
                    (i & 0x02) ? +1.f : - 1.f,
                    (i & 0x04) ? +1.f : (IsGL ? -1.f : 0.f)
                };
                float3 f3CornerLightSpace = f3FrustumCornerProjSpace * CascadeFrustumProjSpaceToLightSpace;
                f3MinXYZ = std::min(f3MinXYZ, f3CornerLightSpace);
                f3MaxXYZ = std::max(f3MaxXYZ, f3CornerLightSpace);
            }
        }
        
        float3 f3CascadeExtent = f3MaxXYZ - f3MinXYZ;
        float3 f3CascadeCenter = (f3MaxXYZ + f3MinXYZ) * 0.5f;
        if (Info.EqualizeExtents)
        {
            f3CascadeExtent.x = f3CascadeExtent.y = std::max(f3CascadeExtent.x, f3CascadeExtent.y);
        }

        float2 f2Extension = Info.MaxFixedFilterRadius * 2.f + (Info.SnapCascades ? float2(1, 1) : float2(0,0));

        // We need to remap the whole extent N x N to (N-ext) x (N-ext)
        VERIFY_EXPR(f2CascadeSize.x > f2Extension.x && f2CascadeSize.y > f2Extension.y);
        f3CascadeExtent.x *= f2CascadeSize.x / (f2CascadeSize.x - f2Extension.x);
        f3CascadeExtent.y *= f2CascadeSize.y / (f2CascadeSize.y - f2Extension.y);

        // Filter radius is defined in projection space, thus x2
        CurrCascade.f4MaxFilterRadiusProjSpace.x = Info.MaxFixedFilterRadius.x * 2.f / f2CascadeSize.x;
        CurrCascade.f4MaxFilterRadiusProjSpace.y = Info.MaxFixedFilterRadius.y * 2.f / f2CascadeSize.y;

        // Align cascade center with the shadow map texels to alleviate temporal aliasing
        if (Info.SnapCascades)
        {
            float fTexelXSize = f3CascadeExtent.x / f2CascadeSize.x;
            float fTexelYSize = f3CascadeExtent.y / f2CascadeSize.y;
            f3CascadeCenter.x = std::floor(f3CascadeCenter.x/fTexelXSize) * fTexelXSize;
            f3CascadeCenter.y = std::floor(f3CascadeCenter.y/fTexelYSize) * fTexelYSize;
        }

        // Extend cascade Z range to allow room for filtering
        float fZExtension = std::max(Info.MaxFixedFilterRadius.x / f2CascadeSize.x, Info.MaxFixedFilterRadius.y / f2CascadeSize.y) * ShadowAttribs.ReceiverPlaneDepthBiasClamp;
        fZExtension = std::min(fZExtension, 0.25f);
        CurrCascade.f4MaxFilterRadiusProjSpace.z = fZExtension * (IsGL ? 2.f : 1.f);
        CurrCascade.f4MaxFilterRadiusProjSpace.w = fZExtension * (IsGL ? 2.f : 1.f);
        f3CascadeExtent.z *= 1.f / (1.f - fZExtension * 2.f);

        // Compute new cascade min/max xy coords
        f3MinXYZ = f3CascadeCenter - f3CascadeExtent / 2.f;
        f3MaxXYZ = f3CascadeCenter + f3CascadeExtent / 2.f;

        CurrCascade.f4LightSpaceScale.x =  2.f / f3CascadeExtent.x;
        CurrCascade.f4LightSpaceScale.y =  2.f / f3CascadeExtent.y;
        CurrCascade.f4LightSpaceScale.z =  (IsGL ? 2.f : 1.f) / f3CascadeExtent.z;
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
        ShadowAttribs.mWorldToShadowMapUVDepthT[iCascade] = WorldToShadowMapUVDepthMatr.Transpose();
    }
}

}
