/*
 *  Copyright 2023-2024 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
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

#include "ScreenSpaceReflection.hpp"
#include "RenderStateCache.hpp"
#include "CommonlyUsedStates.h"
#include "GraphicsUtilities.h"
#include "MapHelper.hpp"
#include "ScopedDebugGroup.hpp"
#include "ShaderMacroHelper.hpp"
#include "Utilities/include/DiligentFXShaderSourceStreamFactory.hpp"

namespace Diligent
{

namespace NoiseBuffers
{
#include "SamplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_1spp.cpp"
}

static DILIGENT_CONSTEXPR DepthStencilStateDesc DSS_StencilWrite{
    False,                // DepthEnable
    False,                // DepthWriteEnable
    COMPARISON_FUNC_LESS, // DepthFunc
    True,                 // StencilEnable
    0xFF,                 // StencilReadMask
    0xFF,                 // StencilWriteMask
    {
        STENCIL_OP_KEEP,       // StencilFailOp
        STENCIL_OP_KEEP,       // StencilDepthFailOp
        STENCIL_OP_REPLACE,    // StencilPassOp
        COMPARISON_FUNC_ALWAYS // StencilFunc
    },
};

static DILIGENT_CONSTEXPR DepthStencilStateDesc DSS_StencilReadComparisonEqual{
    False,                // DepthEnable
    False,                // DepthWriteEnable
    COMPARISON_FUNC_LESS, // DepthFunc
    True,                 // StencilEnable
    0xFF,                 // StencilReadMask
    0xFF,                 // StencilWriteMask
    {
        STENCIL_OP_KEEP,      // StencilFailOp
        STENCIL_OP_KEEP,      // StencilDepthFailOp
        STENCIL_OP_KEEP,      // StencilPassOp
        COMPARISON_FUNC_EQUAL // StencilFunc
    },
};

namespace
{

RefCntAutoPtr<IShader> CreateShaderFromFile(IRenderDevice*          pDevice,
                                            IRenderStateCache*      pStateCache,
                                            const Char*             FileName,
                                            const Char*             EntryPoint,
                                            SHADER_TYPE             Type,
                                            const ShaderMacroArray& Macros = {})
{
    ShaderCreateInfo ShaderCI;
    ShaderCI.EntryPoint                      = EntryPoint;
    ShaderCI.FilePath                        = FileName;
    ShaderCI.Macros                          = Macros;
    ShaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.ShaderType                 = Type;
    ShaderCI.Desc.Name                       = EntryPoint;
    ShaderCI.pShaderSourceStreamFactory      = &DiligentFXShaderSourceStreamFactory::GetInstance();
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    return RenderDeviceWithCache<false>{pDevice, pStateCache}.CreateShader(ShaderCI);
}

RefCntAutoPtr<IShader> CreateShaderFromSource(IRenderDevice*          pDevice,
                                              IRenderStateCache*      pStateCache,
                                              const Char*             Source,
                                              const Char*             EntryPoint,
                                              SHADER_TYPE             Type,
                                              const ShaderMacroArray& Macros = {})
{
    ShaderCreateInfo ShaderCI;
    ShaderCI.EntryPoint                      = EntryPoint;
    ShaderCI.Source                          = Source;
    ShaderCI.Macros                          = Macros;
    ShaderCI.SourceLanguage                  = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.ShaderType                 = Type;
    ShaderCI.Desc.Name                       = EntryPoint;
    ShaderCI.pShaderSourceStreamFactory      = &DiligentFXShaderSourceStreamFactory::GetInstance();
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    return RenderDeviceWithCache<false>{pDevice, pStateCache}.CreateShader(ShaderCI);
}

ITextureView* GetInternalResourceSRV(const RefCntAutoPtr<IDeviceObject>& pDeviceObject)
{
    return StaticCast<ITexture*>(pDeviceObject)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
};

ITextureView* GetInternalResourceRTV(const RefCntAutoPtr<IDeviceObject>& pDeviceObject)
{
    return StaticCast<ITexture*>(pDeviceObject)->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
};

ITextureView* GetInternalResourceDSV(const RefCntAutoPtr<IDeviceObject>& pDeviceObject)
{
    return StaticCast<ITexture*>(pDeviceObject)->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
};

TEXTURE_FORMAT GetInternalResourceFormat(const RefCntAutoPtr<IDeviceObject>& pDeviceObject)
{
    return StaticCast<ITexture*>(pDeviceObject)->GetDesc().Format;
};

} // namespace

void ScreenSpaceReflection::RenderTechnique::InitializePSO(IRenderDevice*                    pDevice,
                                                           IRenderStateCache*                pStateCache,
                                                           const char*                       PSOName,
                                                           IShader*                          VertexShader,
                                                           IShader*                          PixelShader,
                                                           const PipelineResourceLayoutDesc& ResourceLayout,
                                                           std::vector<TEXTURE_FORMAT>       RTVFmts,
                                                           TEXTURE_FORMAT                    DSVFmt,
                                                           const DepthStencilStateDesc&      DSSDesc,
                                                           const BlendStateDesc&             BSDesc,
                                                           bool                              IsDSVReadOnly)
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PipelineStateDesc&              PSODesc = PSOCreateInfo.PSODesc;

    PSODesc.Name           = PSOName;
    PSODesc.ResourceLayout = ResourceLayout;

    auto& GraphicsPipeline = PSOCreateInfo.GraphicsPipeline;

    GraphicsPipeline.RasterizerDesc.FillMode              = FILL_MODE_SOLID;
    GraphicsPipeline.RasterizerDesc.CullMode              = CULL_MODE_BACK;
    GraphicsPipeline.RasterizerDesc.FrontCounterClockwise = false;
    GraphicsPipeline.DepthStencilDesc                     = DSSDesc;
    GraphicsPipeline.BlendDesc                            = BSDesc;
    PSOCreateInfo.pVS                                     = VertexShader;
    PSOCreateInfo.pPS                                     = PixelShader;
    GraphicsPipeline.PrimitiveTopology                    = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    GraphicsPipeline.NumRenderTargets                     = static_cast<Uint8>(RTVFmts.size());
    GraphicsPipeline.DSVFormat                            = DSVFmt;
    GraphicsPipeline.ReadOnlyDSV                          = IsDSVReadOnly;

    for (Uint32 RTIndex = 0; RTIndex < RTVFmts.size(); ++RTIndex)
        GraphicsPipeline.RTVFormats[RTIndex] = RTVFmts[RTIndex];

    PSO.Release();
    SRB.Release();
    PSO = RenderDeviceWithCache<false>{pDevice, pStateCache}.CreateGraphicsPipelineState(PSOCreateInfo);
}

bool ScreenSpaceReflection::RenderTechnique::IsInitialized() const
{
    return SRB && PSO;
}

ScreenSpaceReflection::ScreenSpaceReflection(IRenderDevice* pDevice)
{
    m_IsSupportTransitionSubresources  = pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D12 || pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_VULKAN;
    m_IsSupportedShaderSubresourceView = pDevice->GetDeviceInfo().Type != RENDER_DEVICE_TYPE_GLES;
    m_IsSupportCopyDepthToColor        = pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D12 || pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11;

    RenderDeviceWithCache_N Device{pDevice};
    {
        RefCntAutoPtr<IBuffer> pBuffer;
        CreateUniformBuffer(pDevice, sizeof(ScreenSpaceReflectionAttribs), "ScreenSpaceReflection::ConstantBuffer", &pBuffer);
        m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER] = pBuffer;
    }

    if (!m_IsSupportedShaderSubresourceView)
    {
        RefCntAutoPtr<IBuffer> pBuffer;
        CreateUniformBuffer(pDevice, sizeof(Uint32), "ScreenSpaceReflection::IntermediateConstantBuffer", &pBuffer);
        m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE] = pBuffer;
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::SobolBuffer";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = 256;
        Desc.Height    = 256;
        Desc.Format    = TEX_FORMAT_R32_UINT;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE;

        TextureSubResData SubResData;
        SubResData.pData  = NoiseBuffers::Sobol_256spp_256d;
        SubResData.Stride = 4ull * Desc.Width;

        TextureData Data;
        Data.pContext        = nullptr;
        Data.NumSubresources = 1;
        Data.pSubResources   = &SubResData;

        m_Resources[RESOURCE_IDENTIFIER_SOBOL_BUFFER] = Device.CreateTexture(Desc, &Data);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::ScramblingTileBuffer";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = 128 * 4;
        Desc.Height    = 128 * 2;
        Desc.Format    = TEX_FORMAT_R32_UINT;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE;

        TextureSubResData SubResData;
        SubResData.pData  = NoiseBuffers::ScramblingTile;
        SubResData.Stride = 4ull * Desc.Width;

        TextureData Data;
        Data.pContext        = nullptr;
        Data.NumSubresources = 1;
        Data.pSubResources   = &SubResData;

        m_Resources[RESOURCE_IDENTIFIER_SCRAMBLING_TILE_BUFFER] = Device.CreateTexture(Desc, &Data);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::BlueNoiseTexture";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = 128;
        Desc.Height    = 128;
        Desc.Format    = TEX_FORMAT_RG8_UNORM;
        Desc.MipLevels = 1;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE] = Device.CreateTexture(Desc);
    }
}

ScreenSpaceReflection::~ScreenSpaceReflection() = default;

void ScreenSpaceReflection::SetBackBufferSize(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, Uint32 BackBufferWidth, Uint32 BackBufferHeight)
{
    if (m_BackBufferWidth == BackBufferWidth && m_BackBufferHeight == BackBufferHeight)
        return;

    m_BackBufferWidth  = BackBufferWidth;
    m_BackBufferHeight = BackBufferHeight;

    RenderDeviceWithCache_N Device{pDevice};

    constexpr Uint32 DepthHierarchyMipCount = SSR_DEPTH_HIERARCHY_MAX_MIP + 1;
    {
        m_HierarchicalDepthMipMapRTV.clear();
        m_HierarchicalDepthMipMapSRV.clear();

        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthHierarchy";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(BackBufferWidth, BackBufferHeight), DepthHierarchyMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        RefCntAutoPtr<ITexture> pTexture = Device.CreateTexture(Desc);

        m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY] = pTexture;
        m_HierarchicalDepthMipMapSRV.resize(Desc.MipLevels);
        m_HierarchicalDepthMipMapRTV.resize(Desc.MipLevels);

        for (Uint32 MipLevel = 0; MipLevel < Desc.MipLevels; MipLevel++)
        {
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_RENDER_TARGET;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                pTexture->CreateView(ViewDesc, &m_HierarchicalDepthMipMapRTV[MipLevel]);
            }

            if (m_IsSupportedShaderSubresourceView)
            {
                TextureViewDesc ViewDesc;
                ViewDesc.ViewType        = TEXTURE_VIEW_SHADER_RESOURCE;
                ViewDesc.MostDetailedMip = MipLevel;
                ViewDesc.NumMipLevels    = 1;
                pTexture->CreateView(ViewDesc, &m_HierarchicalDepthMipMapSRV[MipLevel]);
            }
        }
    }

    if (!m_IsSupportedShaderSubresourceView)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthHierarchyIntermediate";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.MipLevels = std::min(ComputeMipLevelsCount(BackBufferWidth, BackBufferHeight), DepthHierarchyMipCount);
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE] = Device.CreateTexture(Desc);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::Roughness";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT; // R8_UNORM is not enough to store alpha roughness
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS] = Device.CreateTexture(Desc);
    }

    {
        m_DepthStencilMaskDSVReadOnly.Release();

        TEXTURE_FORMAT DepthStencilFormat = TEX_FORMAT_D32_FLOAT_S8X24_UINT;

        TextureFormatInfoExt FormatInfo = pDevice->GetTextureFormatInfoExt(TEX_FORMAT_D24_UNORM_S8_UINT);
        if (FormatInfo.Supported && FormatInfo.BindFlags & BIND_DEPTH_STENCIL)
            DepthStencilFormat = TEX_FORMAT_D24_UNORM_S8_UINT;

        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthStencilMask";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = DepthStencilFormat;
        Desc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

        RefCntAutoPtr<ITexture> pTexture = Device.CreateTexture(Desc);

        m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK] = pTexture;

        TextureViewDesc ViewDesc;
        ViewDesc.ViewType = TEXTURE_VIEW_READ_ONLY_DEPTH_STENCIL;
        pTexture->CreateView(ViewDesc, &m_DepthStencilMaskDSVReadOnly);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::Radiance";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_RADIANCE] = Device.CreateTexture(Desc);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::RayDirectionPDF";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF] = Device.CreateTexture(Desc);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::ResolvedRadiance";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_RESOLVED_RADIANCE] = Device.CreateTexture(Desc);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::ResolvedVariance";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_RESOLVED_VARIANCE] = Device.CreateTexture(Desc);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::ResolvedDepth";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_RESOLVED_DEPTH] = Device.CreateTexture(Desc);
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_RADIANCE_HISTORY0; TextureIdx <= RESOURCE_IDENTIFIER_RADIANCE_HISTORY1; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::RadianceHistory";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[TextureIdx] = Device.CreateTexture(Desc);
    }

    for (Uint32 TextureIdx = RESOURCE_IDENTIFIER_VARIANCE_HISTORY0; TextureIdx <= RESOURCE_IDENTIFIER_VARIANCE_HISTORY1; TextureIdx++)
    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::VarianceHistory";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[TextureIdx] = Device.CreateTexture(Desc);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::DepthHistory";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_R32_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_DEPTH_HISTORY] = Device.CreateTexture(Desc);
    }

    {
        TextureDesc Desc;
        Desc.Name      = "ScreenSpaceReflection::Output";
        Desc.Type      = RESOURCE_DIM_TEX_2D;
        Desc.Width     = BackBufferWidth;
        Desc.Height    = BackBufferHeight;
        Desc.Format    = TEX_FORMAT_RGBA16_FLOAT;
        Desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;

        m_Resources[RESOURCE_IDENTIFIER_OUTPUT] = Device.CreateTexture(Desc);
    }
}

void ScreenSpaceReflection::Execute(const RenderAttributes& RenderAttribs)
{
    m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR]               = RenderAttribs.pColorBufferSRV->GetTexture();
    m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH]               = RenderAttribs.pDepthBufferSRV->GetTexture();
    m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL]              = RenderAttribs.pNormalBufferSRV->GetTexture();
    m_Resources[RESOURCE_IDENTIFIER_INPUT_MATERIAL_PARAMETERS] = RenderAttribs.pMaterialBufferSRV->GetTexture();
    m_Resources[RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS]      = RenderAttribs.pMotionVectorsSRV->GetTexture();

    ScopedDebugGroup DebugGroupGlobal{RenderAttribs.pDeviceContext, "ScreenSpaceReflection"};
    {
        MapHelper<ScreenSpaceReflectionAttribs> SSRAttibs{RenderAttribs.pDeviceContext, StaticCast<IBuffer*>(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]), MAP_WRITE, MAP_FLAG_DISCARD};
        *SSRAttibs = RenderAttribs.SSRAttribs;
    }

    ComputeHierarchicalDepthBuffer(RenderAttribs);
    ComputeBlueNoiseTexture(RenderAttribs);
    ComputeStencilMaskAndExtractRoughness(RenderAttribs);
    ComputeIntersection(RenderAttribs);
    ComputeSpatialReconstruction(RenderAttribs);
    ComputeTemporalAccumulation(RenderAttribs);
    ComputeBilateralCleanup(RenderAttribs);
}

ITextureView* ScreenSpaceReflection::GetSSRRadianceSRV() const
{
    return StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_OUTPUT])->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

void ScreenSpaceReflection::CopyTextureDepth(const RenderAttributes& RenderAttribs, ITextureView* pSRV, ITextureView* pRTV)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COPY_DEPTH];
    if (!RenderTech.IsInitialized())
    {
        DILIGENT_CONSTEXPR const char* CopyDepthMip0PS = R"(
            Texture2D<float> g_TextureDepth;
            
            float CopyDepthPS(float4 Position : SV_Position) : SV_Target0
            {
                return g_TextureDepth.Load(int3(Position.xy, 0));
            }
        )";

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromSource(RenderAttribs.pDevice, RenderAttribs.pStateCache, CopyDepthMip0PS, "CopyDepthPS", SHADER_TYPE_PIXEL);

        ShaderResourceVariableDesc VariableDescs[] = {
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables    = VariableDescs;
        ResourceLayout.NumVariables = _countof(VariableDescs);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 nullptr, "ScreenSpaceReflection::CopyDepth",
                                 VS, PS, ResourceLayout,
                                 {
                                     pRTV->GetTexture()->GetDesc().Format,
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB);
    }

    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureDepth")->Set(pSRV);

    RenderAttribs.pDeviceContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}


void ScreenSpaceReflection::ComputeHierarchicalDepthBuffer(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_HIERARCHICAL_DEPTH_BUFFER];
    if (!RenderTech.IsInitialized())
    {
        ShaderMacroHelper Macros;
        Macros.Add("SUPPORTED_SHADER_SRV", static_cast<Int32>(m_IsSupportedShaderSubresourceView));

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeHierarchicalDepthBuffer.fx", "ComputeHierarchicalDepthBufferPS", SHADER_TYPE_PIXEL, Macros);

        std::vector<ShaderResourceVariableDesc> VariableDescs;
        if (m_IsSupportedShaderSubresourceView)
        {
            VariableDescs.emplace_back(SHADER_TYPE_PIXEL, "g_TextureLastMip", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
        }
        else
        {
            VariableDescs.emplace_back(SHADER_TYPE_PIXEL, "cbTextureMipAtrrib", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
            VariableDescs.emplace_back(SHADER_TYPE_PIXEL, "g_TextureMips", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
        }

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables    = VariableDescs.data();
        ResourceLayout.NumVariables = static_cast<Uint32>(VariableDescs.size());

        // Immutable samplers are required for WebGL to work properly
        ImmutableSamplerDesc SamplerDescs[] = {
            ImmutableSamplerDesc{SHADER_TYPE_PIXEL, "g_TextureMips", Sam_PointWrap},
        };

        if (!m_IsSupportedShaderSubresourceView)
        {
            ResourceLayout.ImmutableSamplers    = SamplerDescs;
            ResourceLayout.NumImmutableSamplers = _countof(SamplerDescs);
        }

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeHierarchicalDepthBuffer",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]),
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);

        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeHierarchicalDepthBuffer"};


    if (m_IsSupportCopyDepthToColor)
    {
        CopyTextureAttribs CopyAttribs;
        CopyAttribs.pSrcTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH]);
        CopyAttribs.pDstTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]);
        CopyAttribs.SrcMipLevel              = 0;
        CopyAttribs.DstMipLevel              = 0;
        CopyAttribs.SrcSlice                 = 0;
        CopyAttribs.DstSlice                 = 0;
        CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribs);
    }
    else
    {
        CopyTextureDepth(RenderAttribs,
                         StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH])->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                         m_HierarchicalDepthMipMapRTV[0]);

        if (!m_IsSupportedShaderSubresourceView)
        {
            CopyTextureAttribs CopyMipAttribs;
            CopyMipAttribs.pSrcTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]);
            CopyMipAttribs.pDstTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE]);
            CopyMipAttribs.SrcMipLevel              = 0;
            CopyMipAttribs.DstMipLevel              = 0;
            CopyMipAttribs.SrcSlice                 = 0;
            CopyMipAttribs.DstSlice                 = 0;
            CopyMipAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            CopyMipAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            RenderAttribs.pDeviceContext->CopyTexture(CopyMipAttribs);
        }
    }


    if (m_IsSupportTransitionSubresources)
    {
        StateTransitionDesc TransitionDescW2W[] = {
            StateTransitionDesc{StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]),
                                RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_RENDER_TARGET,
                                STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2W), TransitionDescW2W);

        for (Uint32 MipLevel = 1; MipLevel < m_HierarchicalDepthMipMapRTV.size(); MipLevel++)
        {
            StateTransitionDesc TranslationW2R[] = {
                StateTransitionDesc{StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]),
                                    RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                    MipLevel - 1, 1, 0, REMAINING_ARRAY_SLICES,
                                    STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_NONE},
            };

            RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TranslationW2R), TranslationW2R);

            RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureLastMip")->Set(m_HierarchicalDepthMipMapSRV[MipLevel - 1]);

            ITextureView* pRTVs[] = {
                m_HierarchicalDepthMipMapRTV[MipLevel],
            };

            RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
            RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
            RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
        }

        StateTransitionDesc TransitionDescW2R[] = {
            StateTransitionDesc{StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]),
                                RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_SHADER_RESOURCE,
                                static_cast<Uint32>(m_HierarchicalDepthMipMapRTV.size() - 1), 1, 0, REMAINING_ARRAY_SLICES,
                                STATE_TRANSITION_TYPE_IMMEDIATE, STATE_TRANSITION_FLAG_UPDATE_STATE},
        };
        RenderAttribs.pDeviceContext->TransitionResourceStates(_countof(TransitionDescW2R), TransitionDescW2R);
    }
    else
    {
        if (m_IsSupportedShaderSubresourceView)
        {
            for (Uint32 MipLevel = 1; MipLevel < m_HierarchicalDepthMipMapRTV.size(); MipLevel++)
            {
                RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureLastMip")->Set(m_HierarchicalDepthMipMapSRV[MipLevel - 1]);

                ITextureView* pRTVs[] = {
                    m_HierarchicalDepthMipMapRTV[MipLevel],
                };

                RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_NONE);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
            }
        }
        else
        {
            RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbTextureMipAtrrib")->Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE]);
            RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureMips")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE]));

            for (Uint32 MipLevel = 1; MipLevel < m_HierarchicalDepthMipMapRTV.size(); MipLevel++)
            {
                {
                    MapHelper<Uint32> TextureMipAttribs{RenderAttribs.pDeviceContext, StaticCast<IBuffer*>(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER_INTERMEDIATE]), MAP_WRITE, MAP_FLAG_DISCARD};
                    *TextureMipAttribs = MipLevel - 1;
                }

                ITextureView* pRTVs[] = {
                    m_HierarchicalDepthMipMapRTV[MipLevel],
                };

                RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
                RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
                RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

                CopyTextureAttribs CopyMipAttribs;
                CopyMipAttribs.pSrcTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]);
                CopyMipAttribs.pDstTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE]);
                CopyMipAttribs.SrcMipLevel              = MipLevel;
                CopyMipAttribs.DstMipLevel              = MipLevel;
                CopyMipAttribs.SrcSlice                 = 0;
                CopyMipAttribs.DstSlice                 = 0;
                CopyMipAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                CopyMipAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
                RenderAttribs.pDeviceContext->CopyTexture(CopyMipAttribs);
            }
        }
    }
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeBlueNoiseTexture(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_BLUE_NOISE_TEXTURE];
    if (!RenderTech.IsInitialized())
    {
        ShaderResourceVariableDesc VariableDescs[] = {
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_SobolBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_ScramblingTileBuffer", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
        };

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables    = VariableDescs;
        ResourceLayout.NumVariables = _countof(VariableDescs);

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeBlueNoiseTexture.fx", "ComputeBlueNoiseTexturePS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeBlueNoiseTexture",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE]),
                                 },
                                 TEX_FORMAT_UNKNOWN,
                                 DSS_DisableDepth, BS_Default, false);
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs")->Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_SobolBuffer")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_SOBOL_BUFFER]));
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "g_ScramblingTileBuffer")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_SCRAMBLING_TILE_BUFFER]));
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBlueNoiseTexture"};

    ITextureView* pRTVs[] = {
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE]),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeStencilMaskAndExtractRoughness(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_STENCIL_MASK_AND_EXTRACT_ROUGHNESS];
    if (!RenderTech.IsInitialized())
    {
        ShaderResourceVariableDesc VariableDescs[] = {
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureMaterialParameters", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables    = VariableDescs;
        ResourceLayout.NumVariables = _countof(VariableDescs);

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeStencilMaskAndExtractRoughness.fx", "ComputeStencilMaskAndExtractRoughnessPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeStencilMaskAndExtractRoughness",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS]),
                                 },
                                 GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK]),
                                 DSS_StencilWrite, BS_Default, false);
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs")->Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureDepth")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureMaterialParameters")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_MATERIAL_PARAMETERS]));

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeStencilMaskAndExtractRoughness"};

    ITextureView* pRTVs[] = {
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS]),
    };

    ITextureView* pDSV = GetInternalResourceDSV(m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK]);

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearDepthStencil(pDSV, CLEAR_STENCIL_FLAG, 1.0, 0x00, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeIntersection(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_INTERSECTION];
    if (!RenderTech.IsInitialized())
    {
        ShaderResourceVariableDesc VariableDescs[] = {
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureBlueNoise", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureDepthHierarchy", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables    = VariableDescs;
        ResourceLayout.NumVariables = _countof(VariableDescs);

        // Immutable samplers are required for WebGL to work properly
        ImmutableSamplerDesc SamplerDescs[] = {
            ImmutableSamplerDesc{SHADER_TYPE_PIXEL, "g_TextureDepthHierarchy", Sam_PointClamp},
        };

        if (!m_IsSupportedShaderSubresourceView)
        {
            ResourceLayout.ImmutableSamplers    = SamplerDescs;
            ResourceLayout.NumImmutableSamplers = _countof(SamplerDescs);
        }

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeIntersection.fx", "ComputeIntersectionPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeIntersection",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_RADIANCE]),
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF]),
                                 },
                                 GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK]),
                                 DSS_StencilReadComparisonEqual, BS_Default);
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs")->Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRadiance")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_COLOR]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRoughness")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureBlueNoise")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureDepthHierarchy")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HIERARCHY]));

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeIntersection"};

    ITextureView* pRTVs[] = {
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_RADIANCE]),
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF]),
    };

    constexpr float4 RTVClearColor = float4(0.0, 0.0, 0.0, 0.0);

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[0], RTVClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[1], RTVClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeSpatialReconstruction(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_SPATIAL_RECONSTRUCTION];
    if (!RenderTech.IsInitialized())
    {
        ShaderResourceVariableDesc VariableDescs[] = {
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRayDirectionPDF", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureIntersectSpecular", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRayLength", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables    = VariableDescs;
        ResourceLayout.NumVariables = _countof(VariableDescs);

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeSpatialReconstruction.fx", "ComputeSpatialReconstructionPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeSpatialReconstruction",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_RADIANCE]),
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_VARIANCE]),
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_DEPTH]),
                                 },
                                 GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK]),
                                 DSS_StencilReadComparisonEqual, BS_Default);
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs")->Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRoughness")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureDepth")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRayDirectionPDF")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureIntersectSpecular")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_RADIANCE]));

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "SpatialReconstruction"};

    ITextureView* pRTVs[] = {
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_RADIANCE]),
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_VARIANCE]),
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_DEPTH]),
    };

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

void ScreenSpaceReflection::ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION];
    if (!RenderTech.IsInitialized())
    {
        ShaderResourceVariableDesc VariableDescs[] = {
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureMotion", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureCurrRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureCurrDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureCurrVariance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TexturePrevRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TexturePrevDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TexturePrevVariance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureHitDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };

        ImmutableSamplerDesc SamplerDescs[] = {
            ImmutableSamplerDesc{SHADER_TYPE_PIXEL, "g_TexturePrevDepth", Sam_LinearClamp},
            ImmutableSamplerDesc{SHADER_TYPE_PIXEL, "g_TexturePrevRadiance", Sam_LinearClamp},
            ImmutableSamplerDesc{SHADER_TYPE_PIXEL, "g_TexturePrevVariance", Sam_LinearClamp},
        };

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables            = VariableDescs;
        ResourceLayout.NumVariables         = _countof(VariableDescs);
        ResourceLayout.ImmutableSamplers    = SamplerDescs;
        ResourceLayout.NumImmutableSamplers = _countof(SamplerDescs);

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeTemporalAccumulation.fx", "ComputeTemporalAccumulationPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeTemporalAccumulation",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0]),
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0]),
                                 },
                                 GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK]),
                                 DSS_StencilReadComparisonEqual, BS_Default);
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs")->Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    const Uint32 CurrFrameIdx = RenderAttribs.SSRAttribs.FrameIndex & 1;
    const Uint32 PrevFrameIdx = (RenderAttribs.SSRAttribs.FrameIndex - 1) & 1;

    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureMotion")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRoughness")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureHitDepth")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_DEPTH]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureCurrDepth")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureCurrRadiance")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_RADIANCE]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureCurrVariance")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_RESOLVED_VARIANCE]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TexturePrevDepth")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HISTORY]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TexturePrevRadiance")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0 + PrevFrameIdx]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TexturePrevVariance")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0 + PrevFrameIdx]));

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeTemporalAccumulation"};

    ITextureView* pRTVs[] = {
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0 + CurrFrameIdx]),
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0 + CurrFrameIdx]),
    };

    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);

    if (m_IsSupportCopyDepthToColor)
    {
        CopyTextureAttribs CopyAttribs;
        CopyAttribs.pSrcTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH]);
        CopyAttribs.pDstTexture              = StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HISTORY]);
        CopyAttribs.SrcMipLevel              = 0;
        CopyAttribs.DstMipLevel              = 0;
        CopyAttribs.SrcSlice                 = 0;
        CopyAttribs.DstSlice                 = 0;
        CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        RenderAttribs.pDeviceContext->CopyTexture(CopyAttribs);
    }
    else
    {
        CopyTextureDepth(RenderAttribs,
                         StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH])->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                         StaticCast<ITexture*>(m_Resources[RESOURCE_IDENTIFIER_DEPTH_HISTORY])->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET));
    }
}

void ScreenSpaceReflection::ComputeBilateralCleanup(const RenderAttributes& RenderAttribs)
{
    auto& RenderTech = m_RenderTech[RENDER_TECH_COMPUTE_BILATERAL_CLEANUP];

    if (!RenderTech.IsInitialized())
    {
        ShaderResourceVariableDesc VariableDescs[] = {
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs", SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureDepth", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureNormal", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRoughness", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureRadiance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
            ShaderResourceVariableDesc{SHADER_TYPE_PIXEL, "g_TextureVariance", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC},
        };

        PipelineResourceLayoutDesc ResourceLayout;
        ResourceLayout.Variables    = VariableDescs;
        ResourceLayout.NumVariables = _countof(VariableDescs);

        const auto VS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "FullScreenTriangleVS.fx", "FullScreenTriangleVSExt", SHADER_TYPE_VERTEX);
        const auto PS = CreateShaderFromFile(RenderAttribs.pDevice, RenderAttribs.pStateCache, "ComputeBilateralCleanup.fx", "ComputeBilateralCleanupPS", SHADER_TYPE_PIXEL);

        RenderTech.InitializePSO(RenderAttribs.pDevice,
                                 RenderAttribs.pStateCache, "ScreenSpaceReflection::ComputeBilateralCleanup",
                                 VS, PS, ResourceLayout,
                                 {
                                     GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_OUTPUT]),
                                 },
                                 GetInternalResourceFormat(m_Resources[RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK]),
                                 DSS_StencilReadComparisonEqual, BS_Default);
        RenderTech.PSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbScreenSpaceReflectionAttribs")->Set(m_Resources[RESOURCE_IDENTIFIER_CONSTANT_BUFFER]);
        RenderTech.PSO->CreateShaderResourceBinding(&RenderTech.SRB, true);
    }

    const Uint32 CurrFrameIdx = RenderAttribs.SSRAttribs.FrameIndex & 1;

    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRadiance")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_RADIANCE_HISTORY0 + CurrFrameIdx]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureVariance")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_VARIANCE_HISTORY0 + CurrFrameIdx]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureRoughness")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_ROUGHNESS]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureNormal")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_NORMAL]));
    RenderTech.SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_TextureDepth")->Set(GetInternalResourceSRV(m_Resources[RESOURCE_IDENTIFIER_INPUT_DEPTH]));

    ScopedDebugGroup DebugGroup{RenderAttribs.pDeviceContext, "ComputeBilateralCleanup"};

    ITextureView* pRTVs[] = {
        GetInternalResourceRTV(m_Resources[RESOURCE_IDENTIFIER_OUTPUT]),
    };

    constexpr float4 RTVClearColor = float4(0.0, 0.0, 0.0, 0.0);

    RenderAttribs.pDeviceContext->SetRenderTargets(_countof(pRTVs), pRTVs, m_DepthStencilMaskDSVReadOnly, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->ClearRenderTarget(pRTVs[0], RTVClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->SetStencilRef(0xFF);
    RenderAttribs.pDeviceContext->SetPipelineState(RenderTech.PSO);
    RenderAttribs.pDeviceContext->CommitShaderResources(RenderTech.SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RenderAttribs.pDeviceContext->Draw({3, DRAW_FLAG_VERIFY_ALL, 1});
    RenderAttribs.pDeviceContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
}

} // namespace Diligent
