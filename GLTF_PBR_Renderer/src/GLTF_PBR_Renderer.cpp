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

#include <cstring>

#include "GLTF_PBR_Renderer.h"
#include "../../../Utilities/include/DiligentFXShaderSourceStreamFactory.h"
#include "CommonlyUsedStates.h"
#include "HashUtils.h"
#include "ShaderMacroHelper.h"
#include "BasicMath.h"
#include "GraphicsUtilities.h"
#include "MapHelper.h"

namespace Diligent
{

#include "../../Shaders/GLTF_PBR/private/GLTF_PBR_Structures.fxh"



GLTF_PBR_Renderer::GLTF_PBR_Renderer(IRenderDevice*    pDevice,
                                     IDeviceContext*   pCtx,
                                     const CreateInfo& CI)
{
    PrecomputeBRDF(pDevice, pCtx);
    
    CreateUniformBuffer(pDevice, sizeof(GLTFNodeTransforms),   "GLTF node transforms CB",   &m_TransformsCB);
    CreateUniformBuffer(pDevice, sizeof(GLTFMaterialInfo),     "GLTF material info CB",     &m_MaterialInfoCB);
    CreateUniformBuffer(pDevice, sizeof(GLTFRenderParameters), "GLTF render parameters CB", &m_RenderParametersCB,
                        USAGE_DEFAULT, BIND_UNIFORM_BUFFER, CPU_ACCESS_NONE, &m_RenderParams);

    StateTransitionDesc Barriers[] = 
    {
        {m_TransformsCB,       RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, true},
        {m_MaterialInfoCB,     RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, true},
        {m_RenderParametersCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, true}
    };
    pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

    {
        static constexpr Uint32 TexDim = 8;
        TextureDesc TexDesc;
        TexDesc.Name        = "Dummy white texture for GLTF renderer";
        TexDesc.Type        = RESOURCE_DIM_TEX_2D;
        TexDesc.Usage       = USAGE_STATIC;
        TexDesc.BindFlags   = BIND_SHADER_RESOURCE;
        TexDesc.Width       = TexDim;
        TexDesc.Height      = TexDim;
        TexDesc.Format      = TEX_FORMAT_RGBA8_UNORM;
        TexDesc.MipLevels   = 1;
        std::vector<Uint32> Data(TexDim*TexDim, 0xFFFFFFFF);
        TextureSubResData Level0Data{Data.data(), TexDim * 4};
        TextureData InitData{&Level0Data, 1};
        RefCntAutoPtr<ITexture> pWhiteTex;
        pDevice->CreateTexture(TexDesc, &InitData, &pWhiteTex);
        m_pWhiteTexSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        for(auto& c : Data) c=0;
        RefCntAutoPtr<ITexture> pBlackTex;
        pDevice->CreateTexture(TexDesc, &InitData, &pBlackTex);
        m_pBlackTexSRV = pBlackTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        StateTransitionDesc Barriers[] = 
        {
            {pWhiteTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE},
            {pBlackTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE}
        };
        Barriers[0].UpdateResourceState = true;
        Barriers[1].UpdateResourceState = true;
        pCtx->TransitionResourceStates(_countof(Barriers), Barriers);

        RefCntAutoPtr<ISampler> pDefaultSampler;
        pDevice->CreateSampler(Sam_LinearClamp, &pDefaultSampler);
        m_pWhiteTexSRV->SetSampler(pDefaultSampler);
        m_pBlackTexSRV->SetSampler(pDefaultSampler);
    }

    CreatePSO(pDevice, CI.RTVFmt, CI.DSVFmt, CI.AllowDebugView);
}

void GLTF_PBR_Renderer::PrecomputeBRDF(IRenderDevice*   pDevice,
                                       IDeviceContext*  pCtx)
{
    TextureDesc TexDesc;
    TexDesc.Name        = "GLTF BRDF Look-up texture";
    TexDesc.Type        = RESOURCE_DIM_TEX_2D;
    TexDesc.Usage       = USAGE_DEFAULT;
    TexDesc.BindFlags   = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    TexDesc.Width       = BRDF_LUT_Dim;
    TexDesc.Height      = BRDF_LUT_Dim;
    TexDesc.Format      = TEX_FORMAT_RG16_FLOAT;
    TexDesc.MipLevels   = 1;
    RefCntAutoPtr<ITexture> pBRDF_LUT;
    pDevice->CreateTexture(TexDesc, nullptr, &pBRDF_LUT);
    m_pBRDF_LUT_SRV = pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    RefCntAutoPtr<IPipelineState> PrecomputeBRDF_PSO;
    {
        PipelineStateDesc PSODesc;
        PSODesc.Name = "Precompute GLTF BRDF LUT PSO";

        PSODesc.IsComputePipeline = false;
        PSODesc.GraphicsPipeline.NumRenderTargets             = 1;
        PSODesc.GraphicsPipeline.RTVFormats[0]                = TexDesc.Format;
        PSODesc.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PSODesc.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        PSODesc.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.UseCombinedTextureSamplers = true;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.EntryPoint      = "FullScreenTriangleVS";
            ShaderCI.Desc.Name       = "Full screen triangle VS";
            ShaderCI.FilePath        = "FullScreenTriangleVS.fx";
            pDevice->CreateShader(ShaderCI, &pVS);
        }

        // Create pixel shader
        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "PrecomputeBRDF_PS";
            ShaderCI.Desc.Name       = "Precompute GLTF BRDF PS";
            ShaderCI.FilePath        = "PrecomputeGLTF_BRDF.psh";
            pDevice->CreateShader(ShaderCI, &pPS);
        }

        // Finally, create the pipeline state
        PSODesc.GraphicsPipeline.pVS = pVS;
        PSODesc.GraphicsPipeline.pPS = pPS;
        pDevice->CreatePipelineState(PSODesc, &PrecomputeBRDF_PSO);
    }
    pCtx->SetPipelineState(PrecomputeBRDF_PSO);
    pCtx->CommitShaderResources(nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    ITextureView* pRTVs[] = {pBRDF_LUT->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET)};
    pCtx->SetRenderTargets(1, pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs attrs(3, DRAW_FLAG_VERIFY_ALL);
    pCtx->Draw(attrs);
}

void GLTF_PBR_Renderer::CreatePSO(IRenderDevice*   pDevice,
                                  TEXTURE_FORMAT   RTVFmt,
                                  TEXTURE_FORMAT   DSVFmt,
                                  bool             AllowDebugView)
{
    PipelineStateDesc PSODesc;
    PSODesc.Name = "Render GLTF PBR PSO";

    PSODesc.IsComputePipeline = false;
    PSODesc.GraphicsPipeline.NumRenderTargets             = 1;
    PSODesc.GraphicsPipeline.RTVFormats[0]                = RTVFmt;
    PSODesc.GraphicsPipeline.DSVFormat                    = DSVFmt;
    PSODesc.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSODesc.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_BACK;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.UseCombinedTextureSamplers = true;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();

    ShaderMacroHelper Macros;
    Macros.AddShaderMacro("MAX_NUM_JOINTS", GLTF::Mesh::TransformData::MaxNumJoints);
    Macros.AddShaderMacro("ALLOW_DEBUG_VIEW", AllowDebugView);
    ShaderCI.Macros = Macros;
    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "GLTF PBR VS";
        ShaderCI.FilePath        = "RenderGLTF_PBR.vsh";
        pDevice->CreateShader(ShaderCI, &pVS);
    }

    // Create pixel shader
    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "GLTF PBR PS";
        ShaderCI.FilePath        = "RenderGLTF_PBR.psh";
        pDevice->CreateShader(ShaderCI, &pPS);
    }

    LayoutElement Inputs[] =
    {
        {0, 0, 3, VT_FLOAT32},   //float3 Pos     : ATTRIB0;
        {1, 0, 3, VT_FLOAT32},   //float3 Normal  : ATTRIB1;
        {2, 0, 2, VT_FLOAT32},   //float2 UV0     : ATTRIB2;
        {3, 0, 2, VT_FLOAT32},   //float2 UV1     : ATTRIB3;
        {4, 0, 4, VT_FLOAT32},   //float4 Joint0  : ATTRIB4;
        {5, 0, 4, VT_FLOAT32}    //float4 Weight0 : ATTRIB5;
    };
    PSODesc.GraphicsPipeline.InputLayout.LayoutElements = Inputs;
    PSODesc.GraphicsPipeline.InputLayout.NumElements    = _countof(Inputs);

    PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    ShaderResourceVariableDesc Vars[] = 
    {
        {SHADER_TYPE_VERTEX, "cbTransforms",       SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_PIXEL,  "g_BRDF_LUT",         SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_PIXEL,  "cbMaterialInfo",     SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        {SHADER_TYPE_PIXEL,  "cbRenderParameters", SHADER_RESOURCE_VARIABLE_TYPE_STATIC}
    };
    PSODesc.ResourceLayout.NumVariables = _countof(Vars);
    PSODesc.ResourceLayout.Variables    = Vars;

    StaticSamplerDesc StaticSamplers[] = 
    {
        {SHADER_TYPE_PIXEL, "g_BRDF_LUT", Sam_LinearClamp}
    };
    PSODesc.ResourceLayout.NumStaticSamplers = _countof(StaticSamplers);
    PSODesc.ResourceLayout.StaticSamplers    = StaticSamplers;

    PSODesc.GraphicsPipeline.pVS = pVS;
    PSODesc.GraphicsPipeline.pPS = pPS;
    pDevice->CreatePipelineState(PSODesc, &m_pRenderGLTF_PBR_PSO);

    //m_pRenderGLTF_PBR_PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_BRDF_LUT")->Set(m_pBRDF_LUT_SRV);
    m_pRenderGLTF_PBR_PSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbTransforms")->Set(m_TransformsCB);
    m_pRenderGLTF_PBR_PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbMaterialInfo")->Set(m_MaterialInfoCB);
    m_pRenderGLTF_PBR_PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbRenderParameters")->Set(m_RenderParametersCB);
}

IShaderResourceBinding* GLTF_PBR_Renderer::CreateMaterialSRB(GLTF::Material&  Material,
                                                             IBuffer*         pCameraAttribs,
                                                             IBuffer*         pLightAttribs)
{
    RefCntAutoPtr<IShaderResourceBinding> pSRB;
    m_pRenderGLTF_PBR_PSO->CreateShaderResourceBinding(&pSRB, true);

    //pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap");
    //pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredMap");
    
    pSRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbCameraAttribs")->Set(pCameraAttribs);
    pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbCameraAttribs")->Set(pCameraAttribs);
    pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbLightAttribs")->Set(pLightAttribs);

    auto SetTexture = [&](ITexture* pTexture, ITextureView* pDefaultTexSRV, const char* VarName)
    {
        ITextureView* pTexSRV = pTexture != nullptr ? pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : pDefaultTexSRV;
        pSRB->GetVariableByName(SHADER_TYPE_PIXEL, VarName)->Set(pTexSRV);
    };
    SetTexture(Material.pBaseColorTexture,        m_pWhiteTexSRV, "g_ColorMap");
    SetTexture(Material.pMetallicRoughnessTexture,m_pWhiteTexSRV, "g_PhysicalDescriptorMap");
    SetTexture(Material.pNormalTexture,           m_pWhiteTexSRV, "g_NormalMap");
    SetTexture(Material.pOcclusionTexture,        m_pBlackTexSRV, "g_AOMap");
    SetTexture(Material.pEmissiveTexture,         m_pBlackTexSRV, "g_EmissiveMap");

    auto it = m_SRBCache.find(&Material);
    if (it != m_SRBCache.end())
    {
        it->second = std::move(pSRB);
        return it->second;
    }
    else
    {
        auto new_it = m_SRBCache.emplace(&Material, std::move(pSRB));
        VERIFY_EXPR(new_it.second);
        return new_it.first->second;
    }
}

void GLTF_PBR_Renderer::InitializeResourceBindings(GLTF::Model&   GLTFModel,
                                                   IBuffer*       pCameraAttribs,
                                                   IBuffer*       pLightAttribs)
{
	for (auto& mat : GLTFModel.Materials)
    {
        CreateMaterialSRB(mat, pCameraAttribs, pLightAttribs);
	}
}

void GLTF_PBR_Renderer::InitializeResourceBindings(GLTF::Model& GLTFModel)
{
	for (auto& mat : GLTFModel.Materials)
    {
        m_SRBCache.erase(&mat);
	}
}


void GLTF_PBR_Renderer::RenderGLTFNode(IDeviceContext*              pCtx,
                                       const GLTF::Node*            node,
                                       GLTF::Material::ALPHA_MODE   AlphaMode)
{
	if (node->Mesh)
    {
		// Render mesh primitives
		for (const auto& primitive : node->Mesh->Primitives)
        {
            if (primitive->material.AlphaMode != AlphaMode)
                continue;

            const auto& material = primitive->material;
            auto it = m_SRBCache.find(&material);
            IShaderResourceBinding* pSRB = nullptr;
            if (it != m_SRBCache.end())
            {
                pSRB = it->second;
            }
            else
            {
                LOG_ERROR_MESSAGE("Unable to find SRB for GLTF material. Please call GLTF_PBR_Renderer::InitializeSRBs()");
                continue;
            }

            {
                MapHelper<GLTFNodeTransforms> Transforms(pCtx, m_TransformsCB, MAP_WRITE, MAP_FLAG_DISCARD);
                Transforms->NodeMatrix  = node->Matrix * node->Mesh->Transforms.matrix;
                Transforms->JointCount  = node->Mesh->Transforms.jointcount;
                if (node->Mesh->Transforms.jointcount != 0)
                {
                    static_assert(sizeof(Transforms->JointMatrix) == sizeof(node->Mesh->Transforms.jointMatrix), "Incosistent sizes");
                    memcpy(Transforms->JointMatrix, node->Mesh->Transforms.jointMatrix, sizeof(node->Mesh->Transforms.jointMatrix));
                }
            }

            {
                MapHelper<GLTFMaterialInfo> MaterialInfo(pCtx, m_MaterialInfoCB, MAP_WRITE, MAP_FLAG_DISCARD);
			    MaterialInfo->EmissiveFactor      = material.EmissiveFactor;
                auto GetUVSelector = [](const ITexture* pTexture, Uint8 TexCoordSet)
                {
                    return pTexture != nullptr ? static_cast<float>(TexCoordSet) : -1;
                };
                MaterialInfo->BaseColorTextureUVSelector = GetUVSelector(material.pBaseColorTexture, material.TexCoordSets.BaseColor);
                MaterialInfo->NormalTextureUVSelector    = GetUVSelector(material.pNormalTexture,    material.TexCoordSets.Normal);
                MaterialInfo->OcclusionTextureUVSelector = GetUVSelector(material.pOcclusionTexture, material.TexCoordSets.Occlusion);
                MaterialInfo->EmissiveTextureUVSelector  = GetUVSelector(material.pEmissiveTexture,  material.TexCoordSets.Emissive);
			    MaterialInfo->UseAlphaMask        = material.AlphaMode == GLTF::Material::ALPHAMODE_MASK ? 1 : 0;
			    MaterialInfo->AlphaMaskCutoff     = material.AlphaCutoff;

			    // TODO: glTF specs states that metallic roughness should be preferred, even if specular glosiness is present
			    if (material.pbrWorkflows.MetallicRoughness)
                {
				    // Metallic roughness workflow
				    MaterialInfo->Workflow          = PBR_WORKFLOW_METALLIC_ROUGHNESS;
				    MaterialInfo->BaseColorFactor   = material.BaseColorFactor;
				    MaterialInfo->MetallicFactor    = material.MetallicFactor;
				    MaterialInfo->RoughnessFactor   = material.RoughnessFactor;
                    MaterialInfo->PhysicalDescriptorTextureUVSelector = GetUVSelector(material.pMetallicRoughnessTexture, material.TexCoordSets.MetallicRoughness);
                    MaterialInfo->BaseColorTextureUVSelector          = GetUVSelector(material.pBaseColorTexture,         material.TexCoordSets.BaseColor);
			    }

			    if (primitive->material.pbrWorkflows.SpecularGlossiness)
                {
				    // Specular glossiness workflow
				    MaterialInfo->Workflow                            = PBR_WORKFLOW_SPECULAR_GLOSINESS;
                    MaterialInfo->PhysicalDescriptorTextureUVSelector = GetUVSelector(material.extension.pSpecularGlossinessTexture, material.TexCoordSets.SpecularGlossiness);
                    MaterialInfo->BaseColorTextureUVSelector          = GetUVSelector(material.extension.pDiffuseTexture,            material.TexCoordSets.BaseColor);
				    MaterialInfo->DiffuseFactor                       = material.extension.DiffuseFactor;
				    MaterialInfo->SpecularFactor                      = float4(material.extension.SpecularFactor, 1.0f);
			    }
            }

            pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

			if (primitive->hasIndices)
            {
                DrawAttribs drawAttrs(primitive->IndexCount, VT_UINT32, DRAW_FLAG_VERIFY_ALL);
                drawAttrs.FirstIndexLocation = primitive->FirstIndex;
                pCtx->Draw(drawAttrs);
			}
            else
            {
                DrawAttribs drawAttrs(primitive->VertexCount, DRAW_FLAG_VERIFY_ALL);
                pCtx->Draw(drawAttrs);
			}
		}
	}

	for (const auto& child : node->Children)
    {
		RenderGLTFNode(pCtx, child.get(), AlphaMode);
	}
}

void GLTF_PBR_Renderer::UpdateRenderParams(IDeviceContext* pCtx)
{
    GLTFRenderParameters RenderParams;
    RenderParams.DebugViewType = static_cast<int>(m_RenderParams.DebugView);
    pCtx->UpdateBuffer(m_RenderParametersCB, 0, sizeof(RenderParams), &RenderParams, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    StateTransitionDesc Barrier{m_RenderParametersCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, true};
    pCtx->TransitionResourceStates(1, &Barrier);
}

void GLTF_PBR_Renderer::Render(IDeviceContext*    pCtx,
                               GLTF::Model&       GLTFModel,
                               const RenderInfo&  RenderParams)
{
    if (memcmp(&RenderParams, &m_RenderParams, sizeof(m_RenderParams)) != 0)
    {
        m_RenderParams = RenderParams;
        UpdateRenderParams(pCtx);
    }

    IBuffer* pVBs    [] = {GLTFModel.pVertexBuffer};
    Uint32   Offsets [] = {0};
    pCtx->SetVertexBuffers(0, _countof(pVBs), pVBs, Offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
	if (GLTFModel.pIndexBuffer)
    {
        pCtx->SetIndexBuffer(GLTFModel.pIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	}
    
    pCtx->SetPipelineState(m_pRenderGLTF_PBR_PSO);
	
    // Opaque primitives first
	for (const auto& node : GLTFModel.Nodes)
    {
		RenderGLTFNode(pCtx, node.get(), GLTF::Material::ALPHAMODE_OPAQUE);
	}

	// Alpha masked primitives
	for (const auto& node : GLTFModel.Nodes)
    {
		RenderGLTFNode(pCtx, node.get(), GLTF::Material::ALPHAMODE_MASK);
	}

	// Transparent primitives
	// TODO: Correct depth sorting
    pCtx->SetPipelineState(m_pRenderGLTF_PBR_PSO);
	for (const auto& node : GLTFModel.Nodes)
    {
		RenderGLTFNode(pCtx, node.get(), GLTF::Material::ALPHAMODE_BLEND);
	}
}

}
