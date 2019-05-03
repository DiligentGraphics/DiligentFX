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

#include <unordered_map>
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "../../../DiligentTools/AssetLoader/interface/GLTFLoader.h"

namespace Diligent
{

class GLTF_PBR_Renderer
{
public:
    struct CreateInfo
    {
        TEXTURE_FORMAT  RTVFmt         = TEX_FORMAT_UNKNOWN;
        TEXTURE_FORMAT  DSVFmt         = TEX_FORMAT_UNKNOWN;
        bool            FrontCCW       = false;
        bool            AllowDebugView = false;
        bool            UseIBL         = false;
    };

    GLTF_PBR_Renderer(IRenderDevice*    pDevice,
                      IDeviceContext*   pCtx,
                      const CreateInfo& CI);

    struct RenderInfo
    {
        float4x4 ModelTransform = float4x4::Identity();  

        enum class DebugViewType : int
        {
            None            = 0,
            BaseColor       = 1,
            Transparency    = 2,
            NormalMap       = 3,
            Occlusion       = 4,
            Emissive        = 5,
            Metallic        = 6,
            Roughness       = 7,
            DiffuseColor    = 8,
            SpecularColor   = 9,
            Reflectance90   = 10,
            MeshNormal      = 11,
            PerturbedNormal = 12,
            NdotV           = 13,
            DiffuseIBL      = 14,
            SpecularIBL     = 15
        };
        DebugViewType DebugView         = DebugViewType::None;

        float         OcclusionStrength = 1;
        float         EmissionScale     = 1;
        float         IBLScale          = 1;
        float         AverageLogLum     = 0.2f;
        float         MiddleGray        = 0.18f;
        float         WhitePoint        = 3.f;
    };
    void Render(IDeviceContext*    pCtx,
                GLTF::Model&       GLTFModel,
                const RenderInfo&  RenderParams);

    void InitializeResourceBindings(GLTF::Model& GLTFModel,
                                    IBuffer*     pCameraAttribs,
                                    IBuffer*     pLightAttribs);

    void ReleaseResourceBindings(GLTF::Model& GLTFModel);

    void PrecomputeCubemaps(IRenderDevice*     pDevice,
                            IDeviceContext*    pCtx,
                            ITextureView*      pEnvironmentMap);

    ITextureView* GetIrradianceCubeSRV()
    {
        return m_pIrradianceCubeSRV;
    }

    ITextureView* GetPrefilteredEnvMapSRV()
    {
        return m_pPrefilteredEnvMapSRV;
    }

private:
    void PrecomputeBRDF(IRenderDevice*  pDevice,
                        IDeviceContext* pCtx);

    void CreatePSO(IRenderDevice* pDevice);

    void RenderGLTFNode(IDeviceContext*             pCtx,
                        const GLTF::Node*           node,
                        GLTF::Material::ALPHA_MODE  AlphaMode,
                        const float4x4&             ModelTransform);
    
    void UpdateRenderParams(IDeviceContext* pCtx);

    IShaderResourceBinding* CreateMaterialSRB(GLTF::Material&   Material,
                                              IBuffer*          pCameraAttribs,
                                              IBuffer*          pLightAttribs);

    const CreateInfo m_Settings;

    static constexpr Uint32 BRDF_LUT_Dim = 512;
    RefCntAutoPtr<ITextureView>   m_pBRDF_LUT_SRV;
    RefCntAutoPtr<IPipelineState> m_pRenderGLTF_PBR_PSO;
    RefCntAutoPtr<IPipelineState> m_pRenderGLTF_PBR_AlphaBlend_PSO;

    RefCntAutoPtr<ITextureView>   m_pWhiteTexSRV;
    RefCntAutoPtr<ITextureView>   m_pBlackTexSRV;
    RefCntAutoPtr<ITextureView>   m_pDefaultNormalMapSRV;
    std::unordered_map<const GLTF::Material*, RefCntAutoPtr<IShaderResourceBinding>> m_SRBCache;

    static constexpr TEXTURE_FORMAT IrradianceCubeFmt    = TEX_FORMAT_RGBA32_FLOAT;
    static constexpr TEXTURE_FORMAT PrefilteredEnvMapFmt = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr Uint32         IrradianceCubeDim    = 64;
    static constexpr Uint32         PrefilteredEnvMapDim = 256;
    RefCntAutoPtr<ITextureView>             m_pIrradianceCubeSRV;
    RefCntAutoPtr<ITextureView>             m_pPrefilteredEnvMapSRV;
    RefCntAutoPtr<IPipelineState>           m_pPrecomputeIrradianceCubePSO;
    RefCntAutoPtr<IPipelineState>           m_pPrefilterEnvMapPSO;
    RefCntAutoPtr<IShaderResourceBinding>   m_pPrecomputeIrradianceCubeSRB;
    RefCntAutoPtr<IShaderResourceBinding>   m_pPrefilterEnvMapSRB;

    RenderInfo m_RenderParams;

    RefCntAutoPtr<IBuffer>    m_TransformsCB;
    RefCntAutoPtr<IBuffer>    m_MaterialInfoCB;
    RefCntAutoPtr<IBuffer>    m_RenderParametersCB;
    RefCntAutoPtr<IBuffer>    m_PrecomputeEnvMapAttribsCB;
};

}
