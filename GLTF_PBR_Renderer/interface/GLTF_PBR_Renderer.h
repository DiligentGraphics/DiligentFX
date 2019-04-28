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
        bool            AllowDebugView = false;
    };

    GLTF_PBR_Renderer(IRenderDevice*    pDevice,
                      IDeviceContext*   pCtx,
                      const CreateInfo& CI);

    struct RenderInfo
    {
        enum class DebugViewType : int
        {
            None            = 0,
            BaseColor       = 1,
            NormalMap       = 2,
            Occlusion       = 3,
            Emissive        = 4,
            Metallic        = 5,
            Roughness       = 6,
            DiffuseColor    = 7,
            SpecularColor   = 8,
            Reflectance90   = 9,
            MeshNormal      = 10,
            PerturbedNormal = 11,
            NdotV           = 12
        };

        DebugViewType DebugView         = DebugViewType::None;
        float         OcclusionStrength = 1;
        float         EmissionScale     = 1;
    };
    void Render(IDeviceContext*    pCtx,
                GLTF::Model&       GLTFModel,
                const RenderInfo&  RenderParams);

    void InitializeResourceBindings(GLTF::Model& GLTFModel,
                                    IBuffer*     pCameraAttribs,
                                    IBuffer*     pLightAttribs);

    void InitializeResourceBindings(GLTF::Model& GLTFModel);

private:
    void PrecomputeBRDF(IRenderDevice*  pDevice,
                        IDeviceContext* pCtx);

    void CreatePSO(IRenderDevice* pDevice,
                   TEXTURE_FORMAT RTVFmt,
                   TEXTURE_FORMAT DSVFmt,
                   bool           AllowDebugView);

    void RenderGLTFNode(IDeviceContext*             pCtx,
                       const GLTF::Node*            node,
                       GLTF::Material::ALPHA_MODE   AlphaMode);
    
    void UpdateRenderParams(IDeviceContext* pCtx);

    IShaderResourceBinding* CreateMaterialSRB(GLTF::Material&   Material,
                                              IBuffer*          pCameraAttribs,
                                              IBuffer*          pLightAttribs);

    static constexpr Uint32 BRDF_LUT_Dim = 512;
    RefCntAutoPtr<ITextureView>   m_pBRDF_LUT_SRV;
    RefCntAutoPtr<IPipelineState> m_pRenderGLTF_PBR_PSO;

    RefCntAutoPtr<ITextureView>   m_pWhiteTexSRV;
    RefCntAutoPtr<ITextureView>   m_pBlackTexSRV;
    RefCntAutoPtr<ITextureView>   m_pDefaultNormalMapSRV;
    std::unordered_map<const GLTF::Material*, RefCntAutoPtr<IShaderResourceBinding>> m_SRBCache;

    RenderInfo m_RenderParams;

    RefCntAutoPtr<IBuffer>    m_TransformsCB;
    RefCntAutoPtr<IBuffer>    m_MaterialInfoCB;
    RefCntAutoPtr<IBuffer>    m_RenderParametersCB;
};

}
