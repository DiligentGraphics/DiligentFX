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

#include <vector>
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.h"
#include "../../../DiligentCore/Common/interface/BasicMath.h"

namespace Diligent
{

#include "../../Shaders/Common/public/BasicStructures.fxh"

class ShadowMapManager
{
public:
    ShadowMapManager();

    struct InitInfo
    {
        TEXTURE_FORMAT Fmt                         = TEX_FORMAT_UNKNOWN;
        Uint32         Resolution                  = 0;
        Uint32         NumCascades                 = 0;
        ISampler*      pComparisonSampler          = nullptr;
        ISampler*      pFilterableShadowMapSampler = nullptr;
        int            ShadowMode                  = 0;
        bool           Is32BitFilterableFmt        = false;
    };
    void Initialize(IRenderDevice* pDevice, const InitInfo& initInfo);

    ITextureView* GetSRV()                     { return m_pShadowMapSRV;           }
    ITextureView* GetCascadeDSV(Uint32 Cascade){ return m_pShadowMapDSVs[Cascade]; }
    ITextureView* GetFilterableSRV()           { return m_pFilterableShadowMapSRV; }

    struct DistributeCascadeInfo
    {
        const float4x4*    pCameraView  = nullptr;
        const float4x4*    pCameraWorld = nullptr;
        const float4x4*    pCameraProj  = nullptr;
        const float3*      pCameraPos   = nullptr;
        const float3*      pLightDir    = nullptr;

        // Snap cascades to texels in light view space
        bool               SnapCascades       = true;
        
        // Stabilize cascade extents in light view space
        bool               StabilizeExtents   = true;

        // Use same extents for X and Y axis. Enabled automatically if StabilizeExtents == true
        bool               EqualizeExtents    = true;

        // Maximum shadow filter radius
        float2             MaxFixedFilterRadius;

        // Callback that allows the application to adjust z range of every cascade.
        // The callback is also called with cascade value -1 to adjust that entire camera range.
        std::function<void(int, float&, float&)> AdjustCascadeRange;
    };

    struct CascadeTransforms
    {
        float4x4 Proj;
        float4x4 WorldToLightProjSpace;
    };

    void DistributeCascades(const DistributeCascadeInfo& Info,
                            ShadowMapAttribs&            shadowMapAttribs);

    void ConvertToFilterable(IDeviceContext* pCtx, const ShadowMapAttribs& ShadowAttribs);

    const CascadeTransforms& GetCascadeTranform(Uint32 Cascade) const {return m_CascadeTransforms[Cascade];}

private:
    void InitializeConversionTechniques(TEXTURE_FORMAT FilterableShadowMapFmt);
    void InitializeResourceBindings();

    int                                      m_ShadowMode = 0;
    RefCntAutoPtr<IRenderDevice>             m_pDevice;
    RefCntAutoPtr<ITextureView>              m_pShadowMapSRV;
    std::vector<RefCntAutoPtr<ITextureView>> m_pShadowMapDSVs;
    RefCntAutoPtr<ITextureView>              m_pFilterableShadowMapSRV;
    std::vector<RefCntAutoPtr<ITextureView>> m_pFilterableShadowMapRTVs;
    RefCntAutoPtr<IBuffer>                   m_pConversionAttribsBuffer;
    std::vector<CascadeTransforms>           m_CascadeTransforms;
    struct ShadowConversionTechnique
    {
        RefCntAutoPtr<IPipelineState> HorzPassPSO;
        RefCntAutoPtr<IPipelineState> VertPassPSO;
        RefCntAutoPtr<IShaderResourceBinding> SRB;
    };
    ShadowConversionTechnique m_ConversionTech[SHADOW_MODE_EVSM4];
};

}
