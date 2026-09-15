/*
 *  Copyright 2026 Diligent Graphics LLC
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

#include "PBR_Renderer.hpp"

#include "GraphicsAccessories.hpp"
#include "ShaderSourceFactoryUtils.hpp"
#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"

namespace Diligent
{

void PBR_Renderer::PrecomputeSheenSampling(IDeviceContext* pCtx)
{
    // Gauss-Legendre nodes and weights on [0, 1]. Uniform NdotV integration
    // averages views uniformly in solid angle. Float4 matches the CB array stride.
    static constexpr float4 SheenViewQuadrature[] = {
        {1.3680690753e-3f, 3.5093050047e-3f, 0, 0},
        {7.1942442274e-3f, 8.1371973655e-3f, 0, 0},
        {1.7618872206e-2f, 1.2696032655e-2f, 0, 0},
        {3.2546962031e-2f, 1.7136931457e-2f, 0, 0},
        {5.1839422117e-2f, 2.1417949011e-2f, 0, 0},
        {7.5316193134e-2f, 2.5499029631e-2f, 0, 0},
        {1.0275810202e-1f, 2.9342046739e-2f, 0, 0},
        {1.3390894063e-1f, 3.2911111388e-2f, 0, 0},
        {1.6847786653e-1f, 3.6172897054e-2f, 0, 0},
        {2.0614212138e-1f, 3.9096947894e-2f, 0, 0},
        {2.4655004553e-1f, 4.1655962113e-2f, 0, 0},
        {2.8932436193e-1f, 4.3826046502e-2f, 0, 0},
        {3.3406569886e-1f, 4.5586939348e-2f, 0, 0},
        {3.8035631887e-1f, 4.6922199540e-2f, 0, 0},
        {4.2776401921e-1f, 4.7819360040e-2f, 0, 0},
        {4.7584616716e-1f, 4.8270044257e-2f, 0, 0},
        {5.2415383284e-1f, 4.8270044257e-2f, 0, 0},
        {5.7223598079e-1f, 4.7819360040e-2f, 0, 0},
        {6.1964368113e-1f, 4.6922199540e-2f, 0, 0},
        {6.6593430114e-1f, 4.5586939348e-2f, 0, 0},
        {7.1067563807e-1f, 4.3826046502e-2f, 0, 0},
        {7.5344995447e-1f, 4.1655962113e-2f, 0, 0},
        {7.9385787862e-1f, 3.9096947894e-2f, 0, 0},
        {8.3152213347e-1f, 3.6172897054e-2f, 0, 0},
        {8.6609105937e-1f, 3.2911111388e-2f, 0, 0},
        {8.9724189798e-1f, 2.9342046739e-2f, 0, 0},
        {9.2468380687e-1f, 2.5499029631e-2f, 0, 0},
        {9.4816057788e-1f, 2.1417949011e-2f, 0, 0},
        {9.6745303797e-1f, 1.7136931457e-2f, 0, 0},
        {9.8238112779e-1f, 1.2696032655e-2f, 0, 0},
        {9.9280575577e-1f, 8.1371973655e-3f, 0, 0},
        {9.9863193092e-1f, 3.5093050047e-3f, 0, 0},
    };
    static_assert(sizeof(float4) == 16, "Sheen quadrature entries must match the constant-buffer array stride");

    const BufferDesc       QuadratureDesc{"Sheen view quadrature", sizeof(SheenViewQuadrature), BIND_UNIFORM_BUFFER, USAGE_IMMUTABLE};
    RefCntAutoPtr<IBuffer> pQuadrature = m_Device.CreateBuffer(QuadratureDesc, BufferData{SheenViewQuadrature, sizeof(SheenViewQuadrature)});
    if (pQuadrature == nullptr)
        LOG_ERROR_AND_THROW("Failed to create sheen view quadrature constant buffer");

    // Independent of environment dimensions and sample count. Roughness rows
    // include both endpoints; the zero row represents the grazing-circle limit.
    constexpr Uint32 ViewCount           = _countof(SheenViewQuadrature);
    constexpr Uint32 KernelResolution    = 256;
    constexpr Uint32 QuantileResolution  = 256;
    constexpr Uint32 RoughnessResolution = 64;

    ShaderMacroHelper Macros;
    Macros.Add("SHEEN_SAMPLING_VIEW_COUNT", static_cast<int>(ViewCount))
        .Add("SHEEN_SAMPLING_KERNEL_RESOLUTION", static_cast<int>(KernelResolution))
        .Add("SHEEN_SAMPLING_QUANTILE_RESOLUTION", static_cast<int>(QuantileResolution))
        .Add("SHEEN_SAMPLING_ROUGHNESS_RESOLUTION", static_cast<int>(RoughnessResolution));

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
    ShaderCI.Desc                       = {"Sheen sampling full screen VS", SHADER_TYPE_VERTEX, true};
    ShaderCI.EntryPoint                 = "FullScreenTriangleVS";
    ShaderCI.FilePath                   = "FullScreenTriangleVS.fx";
    RefCntAutoPtr<IShader> pVS          = m_Device.CreateShader(ShaderCI);
    if (pVS == nullptr)
        LOG_ERROR_AND_THROW("Failed to create sheen sampling vertex shader");

    // Each pass consumes the previous FP32 result without CPU readback. Only
    // the final inverse CDF remains alive after renderer initialization.
    const auto RunPass = [&](const char* Name, const char* EntryPoint, Uint32 Width,
                             const char* InputName, ITextureView* pInput) {
        TextureDesc TexDesc;
        TexDesc.Name                     = Name;
        TexDesc.Type                     = RESOURCE_DIM_TEX_2D;
        TexDesc.Width                    = Width;
        TexDesc.Height                   = RoughnessResolution + 1;
        TexDesc.Format                   = TEX_FORMAT_R32_FLOAT;
        TexDesc.MipLevels                = 1;
        TexDesc.BindFlags                = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
        RefCntAutoPtr<ITexture> pTexture = m_Device.CreateTexture(TexDesc);
        if (pTexture == nullptr)
            LOG_ERROR_AND_THROW("Failed to create ", Name, " texture");

        ShaderCI.Desc              = {Name, SHADER_TYPE_PIXEL, true};
        ShaderCI.EntryPoint        = EntryPoint;
        ShaderCI.FilePath          = "PrecomputeSheenSampling.psh";
        ShaderCI.Macros            = Macros;
        RefCntAutoPtr<IShader> pPS = m_Device.CreateShader(ShaderCI);
        if (pPS == nullptr)
            LOG_ERROR_AND_THROW("Failed to create ", Name, " shader");

        GraphicsPipelineStateCreateInfo PSOCI;
        PSOCI.PSODesc.Name                    = Name;
        PSOCI.PSODesc.PipelineType            = PIPELINE_TYPE_GRAPHICS;
        PSOCI.pVS                             = pVS;
        PSOCI.pPS                             = pPS;
        auto& Graphics                        = PSOCI.GraphicsPipeline;
        Graphics.NumRenderTargets             = 1;
        Graphics.RTVFormats[0]                = TexDesc.Format;
        Graphics.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        Graphics.RasterizerDesc.CullMode      = CULL_MODE_NONE;
        Graphics.DepthStencilDesc.DepthEnable = False;

        RefCntAutoPtr<IPipelineState> pPSO = m_Device.CreateGraphicsPipelineState(PSOCI);
        if (pPSO == nullptr)
            LOG_ERROR_AND_THROW("Failed to create ", Name, " pipeline state");
        // The inverse-CDF pass does not use the viewing-angle quadrature.
        if (IShaderResourceVariable* pVar = pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "cbSheenQuadrature"))
            pVar->Set(pQuadrature);
        if (pInput != nullptr)
            pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, InputName)->Set(pInput);
        RefCntAutoPtr<IShaderResourceBinding> pSRB;
        pPSO->CreateShaderResourceBinding(&pSRB, true);

        ITextureView* pRTV = pTexture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        pCtx->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->SetPipelineState(pPSO);
        pCtx->CommitShaderResources(pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        pCtx->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
        pCtx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        StateTransitionDesc Barrier{pTexture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
        pCtx->TransitionResourceStates(1, &Barrier);
        return pTexture;
    };

    RefCntAutoPtr<ITexture> pViewAlbedo = RunPass(
        "Sheen view normalization", "PrecomputeSheenViewAlbedo_PS", ViewCount, nullptr, nullptr);
    RefCntAutoPtr<ITexture> pKernel = RunPass(
        "View-averaged sheen kernel", "PrecomputeSheenKernel_PS", KernelResolution,
        "g_SheenViewAlbedo", pViewAlbedo->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    RefCntAutoPtr<ITexture> pSampling = RunPass(
        "Sheen inverse CDF", "PrecomputeSheenInverseCDF_PS", QuantileResolution + 1,
        "g_SheenKernel", pKernel->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    m_pSheenSampling_SRV = pSampling->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

} // namespace Diligent
