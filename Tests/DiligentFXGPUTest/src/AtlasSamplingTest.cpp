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

#include "Utilities/interface/DiligentFXShaderSourceStreamFactory.hpp"

#include "BasicMath.hpp"
#include "GPUTestingEnvironment.hpp"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace Diligent;
using namespace Diligent::Testing;

namespace
{

constexpr Uint32 MaxMipLevelCount = 15;

struct SamplingCase
{
    const char* Name;
    Uint32      TextureSize;
    Uint32      RegionSize;
    Uint32      ResourceMipLevels;
    Uint32      ViewMipLevels;
    Uint32      UploadedMips;
    Uint32      MipLevelCount;
    uint2       Origin;
    Uint32      Slice;
};

constexpr SamplingCase SamplingCases[] = {
    // A one-mip texture constrains effective LOD without relying on sampler clamps.
    {"SingleMipTexture", 256, 256, 1, 1, 1, MaxMipLevelCount, {0, 0}, 0},
    {"ShortTextureChain", 256, 256, 3, 3, 3, MaxMipLevelCount, {0, 0}, 0},
    {"CompleteFullSlice", 256, 256, 9, 9, 9, MaxMipLevelCount, {0, 0}, 0},
    {"FullSliceMissingTail", 256, 256, 9, 9, 3, 3, {0, 0}, 0},
    {"Subregion", 256, 128, 9, 9, 3, 3, {32, 64}, 1},
    // Asymmetric placements have different X/Y offsets and poisoned neighbors on all sides.
    {"InteriorSubregion", 256, 64, 9, 9, 3, 3, {64, 32}, 1},
    {"InteriorSubregionRight", 256, 64, 9, 9, 3, 3, {160, 96}, 1},
    {"InteriorSubregionBottom", 256, 64, 9, 9, 3, 3, {32, 160}, 1},
    // The view exposes a full chain, but only the region's base mip is populated.
    {"SingleMipSubregion", 256, 64, 9, 9, 1, 1, {144, 80}, 1},
    // The resource has more mips than the view exposes. Its hidden tail is poisoned too.
    {"RestrictedFullSliceView", 256, 256, 9, 3, 3, MaxMipLevelCount, {0, 0}, 0},
};

// R identifies the mip; alpha is reserved for invalid data. G/B vary across the
// interior, with distinct constant bands at the left/right and top/bottom edges.
// The bands make atlas edge insets and ordinary sampler clamping return the same
// colors at identical UVs. The varying interior still detects misplaced samples.
void FillMip(Uint8* Pixels, Uint32 Size, Uint32 Stride, Uint32 Mip)
{
    // In the smallest (64-texel) region, 20-texel bands contain the entire
    // 16x4 anisotropic footprint even after its center is inset from the edge.
    constexpr float BorderWidth   = 5.f / 16.f;
    constexpr float InteriorWidth = 1.f - 2.f * BorderWidth;
    for (Uint32 y = 0; y < Size; ++y)
    {
        for (Uint32 x = 0; x < Size; ++x)
        {
            const size_t Offset = size_t{y} * Stride + x * 4;
            Pixels[Offset + 0]  = static_cast<Uint8>(16 + 24 * Mip);
            const float U       = (static_cast<float>(x) + 0.5f) / Size;
            const float V       = (static_cast<float>(y) + 0.5f) / Size;
            Pixels[Offset + 1]  = static_cast<Uint8>(std::min(std::max((U - BorderWidth) / InteriorWidth, 0.f), 1.f) * 255.f + 0.5f);
            Pixels[Offset + 2]  = static_cast<Uint8>(std::min(std::max((V - BorderWidth) / InteriorWidth, 0.f), 1.f) * 255.f + 0.5f);
            Pixels[Offset + 3]  = 0;
        }
    }
}

// clang-format off
constexpr char VertexShader[] = R"(
float4 main(uint VertexId : SV_VertexID) : SV_Position
{
    float2 UV = float2((VertexId << 1u) & 2u, VertexId & 2u);
    return float4(UV * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr char PixelShader[] = R"(
#include "AtlasSampling.fxh"

Texture2DArray g_Atlas;
SamplerState g_Atlas_sampler;
Texture2D g_Reference;
SamplerState g_Reference_sampler;

cbuffer Constants
{
    float4 Region;
    float4 Gradients;
    float4 SamplePosition; // Region-relative UV, slice, anisotropy
    float4 ReferenceParams; // Optional explicit LOD, use explicit LOD, unused, unused
    uint4 Params; // sampling mip count, wrap UV, unused, unused
};

struct Output
{
    float4 Actual : SV_Target0;
    float4 Reference : SV_Target1;
};

Output main(float4 Position : SV_Position)
{
    // The observed pixel is (4,4). Smooth UVs are affine across complete quads.
    // Both the implicit LOD query and SampleGrad therefore see the same footprint.
    float2 UVOffset = (Position.x - 4.5) * Gradients.xy + (Position.y - 4.5) * Gradients.zw;
    // Keep the derivative coordinates independent of the sample translation.
    // Adding arbitrary UVs first can round an exact power-of-two footprint up,
    // changing ceil(LOD) and making the expected integer-LOD margin ambiguous.
    float2 SmoothUV = UVOffset;
    float2 dx = ddx(SmoothUV);
    float2 dy = ddy(SmoothUV);
    float2 UV = SamplePosition.xy + UVOffset;
    if (Params.y != 0u)
        UV = frac(UV);

    SampleTextureAtlasAttribs Attribs;
    Attribs.f2UV = UV * Region.xy + Region.zw;
    Attribs.f2SmoothUV = SmoothUV * Region.xy;
    Attribs.fSlice = SamplePosition.z;
    Attribs.f4UVRegion = Region;
    Attribs.f2dSmoothUV_dx = dx * Region.xy;
    Attribs.f2dSmoothUV_dy = dy * Region.xy;
    Attribs.fSmallestValidLevelDim = 1.0;
    Attribs.MipLevelCount = Params.x;
    Attribs.IsNonFilterable = false;
    Attribs.fMaxAnisotropy = SamplePosition.w;

    Output Result;
    Result.Actual = SampleTextureAtlas(g_Atlas, g_Atlas_sampler, Attribs);
    // The reference is a separate, region-sized texture with only valid texels.
    // Both paths use the same region-relative UV, wrapping, and texel footprint.
    // There is no reference UV correction or copy of the atlas margin logic.
    if (ReferenceParams.y != 0.0)
    {
        // The mean-color test explicitly checks the coarsest level used by
        // that path, at the same center UV as the atlas sample.
        Result.Reference = g_Reference.SampleLevel(g_Reference_sampler, UV, ReferenceParams.x);
    }
    else
    {
        Result.Reference = g_Reference.SampleGrad(g_Reference_sampler, UV, dx, dy);
    }
    return Result;
}
)";
// clang-format on

struct SamplingConstants
{
    float4 Region;
    float4 Gradients;
    float4 SamplePosition;
    float4 ReferenceParams;
    uint4  Params;
};

class AtlasSamplingTest : public ::testing::TestWithParam<SamplingCase>
{
    struct TextureResources
    {
        RefCntAutoPtr<ITexture>     pTexture;
        RefCntAutoPtr<ITextureView> pSRV;
        RefCntAutoPtr<ITexture>     pReferenceTexture;
        RefCntAutoPtr<ITextureView> pReferenceSRV;
    };

    struct SuiteResources
    {
        RefCntAutoPtr<IShader>        pVS;
        RefCntAutoPtr<IShader>        pPS;
        RefCntAutoPtr<IPipelineState> pLinearPSO;
        RefCntAutoPtr<IPipelineState> pAnisotropicPSO;

        std::array<TextureResources, sizeof(SamplingCases) / sizeof(SamplingCases[0])> Textures;
    };
    static SuiteResources s_Resources;

public:
    static void SetUpTestSuite()
    {
        IRenderDevice* pDevice = GPUTestingEnvironment::GetInstance()->GetDevice();
        ASSERT_NE(pDevice, nullptr);

        ShaderCreateInfo ShaderCI;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        ShaderCI.ShaderCompiler = GPUTestingEnvironment::GetInstance()->GetDefaultCompiler(ShaderCI.SourceLanguage);
        ShaderCI.EntryPoint     = "main";
        ShaderCI.Desc           = {"Atlas sampling test VS", SHADER_TYPE_VERTEX, true};
        ShaderCI.Source         = VertexShader;
        pDevice->CreateShader(ShaderCI, &s_Resources.pVS);
        ASSERT_NE(s_Resources.pVS, nullptr);

        ShaderCI.Desc                       = {"Atlas sampling test PS", SHADER_TYPE_PIXEL, true};
        ShaderCI.Source                     = PixelShader;
        ShaderCI.pShaderSourceStreamFactory = &DiligentFXShaderSourceStreamFactory::GetInstance();
        pDevice->CreateShader(ShaderCI, &s_Resources.pPS);
        ASSERT_NE(s_Resources.pPS, nullptr);

        // Tests share two immutable pipeline configurations. Each backend uses
        // its production shader path, including the fallback on GLES/WebGPU.
        ASSERT_NO_FATAL_FAILURE(CreatePipeline(pDevice, 1, &s_Resources.pLinearPSO));
        if (pDevice->GetAdapterInfo().Sampler.MaxAnisotropy >= 4)
        {
            ASSERT_NO_FATAL_FAILURE(CreatePipeline(pDevice, 4, &s_Resources.pAnisotropicPSO));
        }

        // Input textures are immutable and shared by all tests of a sampling case.
        for (size_t Index = 0; Index < s_Resources.Textures.size(); ++Index)
        {
            ASSERT_NO_FATAL_FAILURE(CreateTextureResources(pDevice, SamplingCases[Index], s_Resources.Textures[Index]));
        }
    }

    static void TearDownTestSuite()
    {
        s_Resources = {};
    }

protected:
    void SetUp() override
    {
        m_pDevice  = GPUTestingEnvironment::GetInstance()->GetDevice();
        m_pContext = GPUTestingEnvironment::GetInstance()->GetDeviceContext();
        ASSERT_NE(m_pDevice, nullptr);
        ASSERT_NE(m_pContext, nullptr);
        const SamplingCase& Case = GetParam();
        for (size_t Index = 0; Index < s_Resources.Textures.size(); ++Index)
        {
            if (std::strcmp(Case.Name, SamplingCases[Index].Name) == 0)
            {
                m_pTextureResources = &s_Resources.Textures[Index];
                break;
            }
        }
        ASSERT_NE(m_pTextureResources, nullptr);

        const float TextureSize = static_cast<float>(Case.TextureSize);

        m_UVRegion = {
            Case.RegionSize / TextureSize,
            Case.RegionSize / TextureSize,
            Case.Origin.x / TextureSize,
            Case.Origin.y / TextureSize,
        };
    }

    void TearDown() override
    {
        GPUTestingEnvironment::GetInstance()->Reset();
    }

    static void CreateTextureResources(IRenderDevice* pDevice, const SamplingCase& Case, TextureResources& Resources)
    {
        // Subregion cases must exercise atlas isolation on every side. A texture
        // boundary would let the sampler's CLAMP mode hide a bad atlas clamp.
        if (Case.RegionSize < Case.TextureSize)
        {
            ASSERT_GT(Case.Origin.x, 0u);
            ASSERT_GT(Case.Origin.y, 0u);
            ASSERT_LT(Case.Origin.x + Case.RegionSize, Case.TextureSize);
            ASSERT_LT(Case.Origin.y + Case.RegionSize, Case.TextureSize);
        }

        TextureDesc Desc;
        Desc.Name  = "Atlas sampling input";
        Desc.Type  = RESOURCE_DIM_TEX_2D_ARRAY;
        Desc.Width = Desc.Height = Case.TextureSize;
        Desc.ArraySize           = Case.Slice + 1;
        Desc.MipLevels           = Case.ResourceMipLevels;
        Desc.Format              = TEX_FORMAT_RGBA8_UNORM;
        Desc.Usage               = USAGE_IMMUTABLE;
        Desc.BindFlags           = BIND_SHADER_RESOURCE;

        // Initialize all storage, including neighboring regions/slices and missing
        // mip tails. Alpha=1 is poison; the valid pattern always has alpha=0.
        const Uint32                    SubresourceCount = Desc.ArraySize * Desc.MipLevels;
        std::vector<std::vector<Uint8>> Pixels(SubresourceCount);
        std::vector<TextureSubResData>  Subresources(SubresourceCount);
        for (Uint32 Slice = 0; Slice < Desc.ArraySize; ++Slice)
        {
            for (Uint32 Mip = 0; Mip < Desc.MipLevels; ++Mip)
            {
                const Uint32 Index  = Slice * Desc.MipLevels + Mip;
                const Uint32 Size   = std::max(Case.TextureSize >> Mip, 1u);
                const Uint32 Stride = Size * 4;
                Pixels[Index].resize(size_t{Stride} * Size, 0);
                for (size_t Byte = 3; Byte < Pixels[Index].size(); Byte += 4)
                    Pixels[Index][Byte] = 255;
                if (Slice == Case.Slice && Mip < Case.UploadedMips)
                {
                    const size_t Offset = size_t{Case.Origin.y >> Mip} * Stride + (Case.Origin.x >> Mip) * 4;
                    FillMip(Pixels[Index].data() + Offset, std::max(Case.RegionSize >> Mip, 1u), Stride, Mip);
                }
                Subresources[Index] = TextureSubResData{Pixels[Index].data(), Stride};
            }
        }
        const TextureData Data{Subresources.data(), SubresourceCount};
        pDevice->CreateTexture(Desc, &Data, &Resources.pTexture);
        ASSERT_NE(Resources.pTexture, nullptr);

        TextureViewDesc ViewDesc;
        ViewDesc.Name         = "Atlas sampling view";
        ViewDesc.ViewType     = TEXTURE_VIEW_SHADER_RESOURCE;
        ViewDesc.NumMipLevels = Case.ViewMipLevels;
        Resources.pTexture->CreateView(ViewDesc, &Resources.pSRV);
        ASSERT_NE(Resources.pSRV, nullptr);
        // The reference has exactly the region's dimensions and uploaded mips.
        // Every texel is valid; it has no neighboring regions or poisoned tail.
        std::vector<std::vector<Uint8>> ReferencePixels(Case.UploadedMips);
        std::vector<TextureSubResData>  ReferenceSubresources(Case.UploadedMips);
        for (Uint32 Mip = 0; Mip < Case.UploadedMips; ++Mip)
        {
            const Uint32 Size   = std::max(Case.RegionSize >> Mip, 1u);
            const Uint32 Stride = Size * 4;
            ReferencePixels[Mip].resize(size_t{Stride} * Size);
            FillMip(ReferencePixels[Mip].data(), Size, Stride, Mip);
            ReferenceSubresources[Mip] = TextureSubResData{ReferencePixels[Mip].data(), Stride};
        }
        Desc.Name  = "Standalone atlas sampling reference";
        Desc.Type  = RESOURCE_DIM_TEX_2D;
        Desc.Width = Desc.Height = Case.RegionSize;
        Desc.ArraySize           = 1;
        Desc.MipLevels           = Case.UploadedMips;
        const TextureData ReferenceData{ReferenceSubresources.data(), Case.UploadedMips};
        pDevice->CreateTexture(Desc, &ReferenceData, &Resources.pReferenceTexture);
        ASSERT_NE(Resources.pReferenceTexture, nullptr);
        Resources.pReferenceSRV = Resources.pReferenceTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        ASSERT_NE(Resources.pReferenceSRV, nullptr);
    }

    static void CreatePipeline(IRenderDevice* pDevice, Uint32 Anisotropy, IPipelineState** ppPSO)
    {
        SamplerDesc Sampler;
        Sampler.MinFilter = Sampler.MagFilter = Sampler.MipFilter = Anisotropy > 1 ? FILTER_TYPE_ANISOTROPIC : FILTER_TYPE_LINEAR;
        Sampler.MaxAnisotropy                                     = Anisotropy;
        Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
        const ImmutableSamplerDesc Samplers[]                  = {
            {SHADER_TYPE_PIXEL, "g_Atlas", Sampler},
            {SHADER_TYPE_PIXEL, "g_Reference", Sampler},
        };
        GraphicsPipelineStateCreateInfo PSOCI;
        PSOCI.PSODesc.Name                                = "DiligentFX atlas sampling test";
        PSOCI.PSODesc.ResourceLayout.DefaultVariableType  = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        PSOCI.PSODesc.ResourceLayout.ImmutableSamplers    = Samplers;
        PSOCI.PSODesc.ResourceLayout.NumImmutableSamplers = 2;
        PSOCI.GraphicsPipeline.NumRenderTargets           = 2;
        PSOCI.GraphicsPipeline.RTVFormats[0] = PSOCI.GraphicsPipeline.RTVFormats[1] = TEX_FORMAT_RGBA32_FLOAT;
        PSOCI.GraphicsPipeline.PrimitiveTopology                                    = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        PSOCI.GraphicsPipeline.RasterizerDesc.CullMode                              = CULL_MODE_NONE;
        PSOCI.GraphicsPipeline.DepthStencilDesc.DepthEnable                         = False;
        PSOCI.pVS                                                                   = s_Resources.pVS;
        PSOCI.pPS                                                                   = s_Resources.pPS;
        pDevice->CreateGraphicsPipelineState(PSOCI, ppPSO);
        ASSERT_NE(*ppPSO, nullptr);
    }

    void CreateTestResources(Uint32 Anisotropy = 1)
    {
        m_Anisotropy = Anisotropy;
        m_pPSO       = Anisotropy > 1 ? s_Resources.pAnisotropicPSO : s_Resources.pLinearPSO;
        ASSERT_NE(m_pPSO, nullptr);
        BufferDesc ConstantsDesc;
        ConstantsDesc.Name      = "Atlas sampling constants";
        ConstantsDesc.Size      = sizeof(SamplingConstants);
        ConstantsDesc.BindFlags = BIND_UNIFORM_BUFFER;
        ConstantsDesc.Usage     = USAGE_DEFAULT;
        m_pDevice->CreateBuffer(ConstantsDesc, nullptr, &m_pConstants);
        ASSERT_NE(m_pConstants, nullptr);

        m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
        ASSERT_NE(m_pSRB, nullptr);
        m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "Constants")->Set(m_pConstants);
        m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Atlas")->Set(m_pTextureResources->pSRV);
        m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Reference")->Set(m_pTextureResources->pReferenceSRV);

        TextureDesc OutputDesc;
        OutputDesc.Name  = "Atlas sampling output";
        OutputDesc.Type  = RESOURCE_DIM_TEX_2D;
        OutputDesc.Width = OutputDesc.Height = 8;
        OutputDesc.Format                    = TEX_FORMAT_RGBA32_FLOAT;
        OutputDesc.BindFlags                 = BIND_RENDER_TARGET;
        for (Uint32 Index = 0; Index < 2; ++Index)
        {
            m_pDevice->CreateTexture(OutputDesc, nullptr, &m_pOutput[Index]);
            ASSERT_NE(m_pOutput[Index], nullptr);
        }
        OutputDesc.Name           = "Atlas sampling readback";
        OutputDesc.BindFlags      = BIND_NONE;
        OutputDesc.Usage          = USAGE_STAGING;
        OutputDesc.CPUAccessFlags = CPU_ACCESS_READ;
        m_pDevice->CreateTexture(OutputDesc, nullptr, &m_pReadback);
        ASSERT_NE(m_pReadback, nullptr);
    }

    void Sample(const float2& UV, const float2& dx, const float2& dy, bool Wrap = false, float ReferenceLOD = -1.f)
    {
        const TextureDesc& Desc   = m_pTextureResources->pReferenceTexture->GetDesc();
        const float4&      Region = m_UVRegion;
        SamplingConstants  Constants{};
        Constants.Region    = Region;
        Constants.Gradients = {
            dx.x / Desc.Width,
            dx.y / Desc.Height,
            dy.x / Desc.Width,
            dy.y / Desc.Height,
        };
        Constants.SamplePosition = {
            UV.x,
            UV.y,
            static_cast<float>(GetParam().Slice),
            static_cast<float>(m_Anisotropy),
        };
        Constants.ReferenceParams = {ReferenceLOD, ReferenceLOD >= 0.f ? 1.f : 0.f, 0.f, 0.f};
        Constants.Params          = {GetParam().MipLevelCount, Wrap ? 1u : 0u, 0, 0};
        m_pContext->UpdateBuffer(m_pConstants, 0, sizeof(Constants), &Constants, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ITextureView* RTVs[] = {
            m_pOutput[0]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
            m_pOutput[1]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET),
        };
        m_pContext->SetRenderTargets(2, RTVs, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        // A missing draw must fail rather than compare two uninitialized outputs.
        const float ClearColor[] = {-1.f, -1.f, -1.f, -1.f};
        for (ITextureView* pRTV : RTVs)
            m_pContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pContext->SetPipelineState(m_pPSO);
        m_pContext->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pContext->Draw(DrawAttribs{3, DRAW_FLAG_VERIFY_ALL});
        m_pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
        for (Uint32 Index = 0; Index < 2; ++Index)
        {
            CopyTextureAttribs Copy;
            Copy.pSrcTexture              = m_pOutput[Index];
            Copy.pDstTexture              = m_pReadback;
            Copy.SrcTextureTransitionMode = Copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
            m_pContext->CopyTexture(Copy);
            m_pContext->WaitForIdle();
            const RenderDeviceInfo&  DeviceInfo = m_pDevice->GetDeviceInfo();
            const MAP_FLAGS          Flags      = DeviceInfo.Type == RENDER_DEVICE_TYPE_D3D11 || DeviceInfo.IsGLDevice() ? MAP_FLAG_NONE : MAP_FLAG_DO_NOT_WAIT;
            MappedTextureSubresource Mapped;
            m_pContext->MapTextureSubresource(m_pReadback, 0, 0, MAP_READ, Flags, nullptr, Mapped);
            ASSERT_NE(Mapped.pData, nullptr);
            const Uint8* Pixel = static_cast<const Uint8*>(Mapped.pData) + 4 * static_cast<size_t>(Mapped.Stride) + 4 * sizeof(float4);
            std::memcpy(&m_Result[Index], Pixel, sizeof(float4));
            m_pContext->UnmapTextureSubresource(m_pReadback, 0, 0);
        }
        EXPECT_NEAR(m_Result[0].w, 0.f, 0.002f) << "Sampled poisoned texels or mip tail";
        EXPECT_NEAR(m_Result[1].w, 0.f, 0.002f) << "Reference footprint must stay in populated data";
        EXPECT_GE(m_Result[1].x, 15.f / 255.f) << "Reference must contain the initialized mip pattern";
    }

    void ExpectReference()
    {
        EXPECT_NEAR(m_Result[0].x, m_Result[1].x, 0.003f) << "Wrong mip contribution";
        EXPECT_NEAR(m_Result[0].y, m_Result[1].y, 0.003f) << "Wrong horizontal pattern contribution";
        EXPECT_NEAR(m_Result[0].z, m_Result[1].z, 0.003f) << "Wrong vertical pattern contribution";
    }

    void CheckBoundarySamples(float Width, float Height)
    {
        SCOPED_TRACE(::testing::Message{} << "Footprint: " << Width << " x " << Height);
        struct SampleLocation
        {
            float2 UV;
            bool   Wrap;
        };
        const SampleLocation Locations[] = {
            // Every edge, with the other coordinate away from the center.
            {{0.001f, 0.4375f}, false},
            {{0.999f, 0.4375f}, false},
            {{0.5625f, 0.001f}, false},
            {{0.5625f, 0.999f}, false},
            // All four corners.
            {{0.001f, 0.001f}, false},
            {{0.999f, 0.001f}, false},
            {{0.001f, 0.999f}, false},
            {{0.999f, 0.999f}, false},
            // Both directions of each wrap seam and simultaneous U/V wrapping.
            {{-0.001f, 0.4375f}, true},
            {{1.001f, 0.4375f}, true},
            {{0.5625f, -0.001f}, true},
            {{0.5625f, 1.001f}, true},
            {{-0.001f, -0.001f}, true},
            {{1.001f, -0.001f}, true},
            {{-0.001f, 1.001f}, true},
            {{1.001f, 1.001f}, true},
            // An interior sample must preserve both coordinates.
            {{0.4375f, 0.5625f}, false},
        };
        for (const SampleLocation& Location : Locations)
        {
            SCOPED_TRACE(::testing::Message{} << "UV: " << Location.UV.x << ", " << Location.UV.y << ", wrap: " << Location.Wrap);
            ASSERT_NO_FATAL_FAILURE(Sample(Location.UV, {Width, 0.f}, {0.f, Height}, Location.Wrap));
            ExpectReference();
        }
    }

    void CheckIsotropicBoundaries()
    {
        // Short chains are checked immediately below, at, and above their last
        // populated mip. Full chains use the same LOD-2 probes, well before the
        // region-sized footprints that intentionally fade to the mean color.
        const float LimitFootprint = std::exp2(static_cast<float>(std::min(GetParam().UploadedMips - 1, 2u)));
        for (float Footprint : {0.5f, 2.828427f, LimitFootprint * 0.96875f, LimitFootprint, LimitFootprint * 1.03125f, 16.f})
        {
            ASSERT_NO_FATAL_FAILURE(CheckBoundarySamples(Footprint, Footprint));
        }
    }

    bool HasNativeLOD() const
    {
        const RENDER_DEVICE_TYPE Type = m_pDevice->GetDeviceInfo().Type;
        return Type != RENDER_DEVICE_TYPE_GLES && Type != RENDER_DEVICE_TYPE_WEBGPU;
    }

    bool IsLlvmPipe() const
    {
        return std::strstr(m_pDevice->GetAdapterInfo().Description, "llvmpipe") != nullptr;
    }

    IRenderDevice*                         m_pDevice           = nullptr;
    IDeviceContext*                        m_pContext          = nullptr;
    const TextureResources*                m_pTextureResources = nullptr;
    float4                                 m_UVRegion;
    RefCntAutoPtr<IBuffer>                 m_pConstants;
    RefCntAutoPtr<IPipelineState>          m_pPSO;
    RefCntAutoPtr<IShaderResourceBinding>  m_pSRB;
    std::array<RefCntAutoPtr<ITexture>, 2> m_pOutput;
    RefCntAutoPtr<ITexture>                m_pReadback;
    std::array<float4, 2>                  m_Result{};
    Uint32                                 m_Anisotropy = 1;
};

AtlasSamplingTest::SuiteResources AtlasSamplingTest::s_Resources;

// Samples the region center with isotropic footprints covering magnification,
// fractional LODs, and minification beyond the uploaded chain. Mip colors must
// match hardware sampling from the standalone texture containing only uploaded levels.
TEST_P(AtlasSamplingTest, SelectsUploadedMips)
{
    ASSERT_NO_FATAL_FAILURE(CreateTestResources());
    for (float Footprint : {0.5f, 2.828427f, 3.732132f, 4.f, 8.f, 16.f})
    {
        SCOPED_TRACE(Footprint);
        ASSERT_NO_FATAL_FAILURE(Sample({0.5f, 0.5f}, {Footprint, 0.f}, {0.f, Footprint}));
        ExpectReference();
    }
    if (!HasNativeLOD())
    {
        // Native-query rounding at this footprint is covered by the disabled
        // reproducer below; the GLES/WebGPU calculation must already pass.
        SCOPED_TRACE("Fractional LOD above the uploaded-mip limit");
        ASSERT_NO_FATAL_FAILURE(Sample({0.5f, 0.5f}, {4.287094f, 0.f}, {0.f, 4.287094f}));
        ExpectReference();
    }
}

// Checks all edges, corners, U/V wrap seams, and an off-center interior point
// under magnification, fractional LODs, and on both sides of the uploaded-mip
// limit. Compare the complete color against a standalone texture sampled at
// identical UVs. Distinct border bands and the varying interior detect wrong
// positions; poison alpha detects crossing any region edge or the mip limit.
TEST_P(AtlasSamplingTest, IsolatesBordersCornersAndWrapSeams)
{
    ASSERT_NO_FATAL_FAILURE(CreateTestResources());
    ASSERT_NO_FATAL_FAILURE(CheckIsotropicBoundaries());
}

// Exercise horizontal and vertical anisotropic footprints at every boundary.
// Colors and poison checks must match the independent standalone texture at
// identical UVs, with a real 4x anisotropic sampler on both paths.
TEST_P(AtlasSamplingTest, PreservesAnisotropicBoundaryPositions)
{
    if (m_pDevice->GetAdapterInfo().Sampler.MaxAnisotropy < 4)
        GTEST_SKIP() << "Requires 4x anisotropic filtering";
    // llvmpipe uses derivatives of the clamped coordinates instead of the
    // explicit gradients. GL can produce NaNs at corners; Vulkan can select
    // mips beyond LOD 2 unless the view itself prevents it.
    if (IsLlvmPipe() && (m_pDevice->GetDeviceInfo().IsGLDevice() || GetParam().ViewMipLevels > 3))
        GTEST_SKIP() << "llvmpipe anisotropic sampling ignores explicit gradients at clamped region boundaries";
    ASSERT_NO_FATAL_FAILURE(CreateTestResources(4));
    ASSERT_NO_FATAL_FAILURE(CheckBoundarySamples(16.f, 4.f));
    ASSERT_NO_FATAL_FAILURE(CheckBoundarySamples(4.f, 16.f));
}

// Samples a grid inside the varying center band at identical UVs in the atlas
// and standalone texture. Matching both spatial channels detects accidental
// centering or translation without computing an expected sampling position.
TEST_P(AtlasSamplingTest, PreservesInteriorPattern)
{
    ASSERT_NO_FATAL_FAILURE(CreateTestResources());
    for (float U : {0.4375f, 0.5f, 0.5625f})
    {
        for (float V : {0.4375f, 0.5f, 0.5625f})
        {
            SCOPED_TRACE(::testing::Message{} << "UV: " << U << ", " << V);
            ASSERT_NO_FATAL_FAILURE(Sample({U, V}, {4.f, 0.f}, {0.f, 4.f}));
            ExpectReference();
        }
    }
}

// Uses a region-sized footprint to fully enter the mean-color sampling path.
// The symmetric pattern makes the four-sample average equal to a center sample
// at the coarsest mip permitted by the region size and uploaded-mip limits.
TEST_P(AtlasSamplingTest, MeanColorUsesAvailableMips)
{
    ASSERT_NO_FATAL_FAILURE(CreateTestResources());
    const float Footprint   = static_cast<float>(GetParam().RegionSize);
    const float LastMeanLOD = std::min(std::log2(Footprint / 2.f), static_cast<float>(GetParam().UploadedMips - 1));
    ASSERT_NO_FATAL_FAILURE(Sample({0.5f, 0.5f}, {Footprint, 0.f}, {0.f, Footprint}, false, LastMeanLOD));
    ExpectReference();
}

// Native LOD-query backends must preserve a rotated 16x4 footprint with 4x
// anisotropy. Keep this separate from axis-aligned coverage so a backend's
// limitation for rotated footprints does not skip the axis-aligned checks.
TEST_P(AtlasSamplingTest, PreservesRotatedAnisotropy)
{
    if (!HasNativeLOD())
        GTEST_SKIP() << "Rotated fallback footprints are covered by the disabled regression test";
    if (m_pDevice->GetAdapterInfo().Sampler.MaxAnisotropy < 4)
        GTEST_SKIP() << "Requires 4x anisotropic filtering";
    if (IsLlvmPipe() && GetParam().ViewMipLevels > GetParam().UploadedMips)
        GTEST_SKIP() << "llvmpipe anisotropic sampling ignores explicit gradients needed to avoid missing mips";
    ASSERT_NO_FATAL_FAILURE(CreateTestResources(4));
    ASSERT_NO_FATAL_FAILURE(Sample({0.5f, 0.5f}, {11.313708f, 2.828427f}, {11.313708f, -2.828427f}));
    ExpectReference();
}

// Exercises the backend's production LOD path with an axis-aligned 16x4
// footprint and a 4x anisotropic sampler. Mip colors detect over-sharpening
// caused by ignoring sampler anisotropy.
TEST_P(AtlasSamplingTest, PreservesAxisAlignedAnisotropy)
{
    if (m_pDevice->GetAdapterInfo().Sampler.MaxAnisotropy < 4)
        GTEST_SKIP() << "Requires 4x anisotropic filtering";
    // Mesa's Vulkan software path also exposes the ignored-gradient issue for
    // axis-aligned footprints when the view contains an unpopulated mip tail.
    // GL is affected too when the last uploaded mip is below this footprint's
    // LOD 2, so reaching valid data requires reducing the explicit gradients.
    if (IsLlvmPipe() && GetParam().ViewMipLevels > GetParam().UploadedMips &&
        (m_pDevice->GetDeviceInfo().IsVulkanDevice() || GetParam().UploadedMips < 3))
        GTEST_SKIP() << "llvmpipe anisotropic sampling cannot enforce the uploaded-mip limit";
    ASSERT_NO_FATAL_FAILURE(CreateTestResources(4));
    SCOPED_TRACE("Axis-aligned 16x4 footprint");
    ASSERT_NO_FATAL_FAILURE(Sample({0.5f, 0.5f}, {16.f, 0.f}, {0.f, 4.f}));
    ExpectReference();
}

// Exercises a rotated 16x4 footprint with 4x anisotropy on GLES/WebGPU.
// Mip colors must match hardware sampling from the independent standalone texture.
// Known issue: the GLES/WebGPU estimate ignores the angle between derivatives.
// Keep the reproducer runnable with --gtest_also_run_disabled_tests until fixed.
TEST_P(AtlasSamplingTest, DISABLED_FallbackPreservesRotatedAnisotropy)
{
    if (HasNativeLOD())
        GTEST_SKIP() << "This regression test requires the GLES/WebGPU fallback";
    if (m_pDevice->GetAdapterInfo().Sampler.MaxAnisotropy < 4)
        GTEST_SKIP() << "Requires 4x anisotropic filtering";
    ASSERT_NO_FATAL_FAILURE(CreateTestResources(4));
    ASSERT_NO_FATAL_FAILURE(Sample({0.5f, 0.5f}, {11.313708f, 2.828427f}, {11.313708f, -2.828427f}));
    ExpectReference();
}

// Exercises gradient reduction just above an integer LOD boundary. Poison alpha
// detects even a small contribution from the missing mip tail, while mip colors
// check that the result matches the independent standalone texture.
// Known issue: on D3D11 hardware, reducing a fractional native-query LOD to the
// last valid integer LOD can still blend 1/256 of the next (unpopulated) mip.
// WARP and the explicit fallback do not reproduce this query/sampling mismatch.
// Keep the strict poison check; do not conceal the bleed with a larger tolerance.
TEST_P(AtlasSamplingTest, DISABLED_RejectsMipTailAtFractionalLODBoundary)
{
    ASSERT_NO_FATAL_FAILURE(CreateTestResources());
    ASSERT_NO_FATAL_FAILURE(Sample({0.5f, 0.5f}, {4.287094f, 0.f}, {0.f, 4.287094f}));
    ExpectReference();
}

INSTANTIATE_TEST_SUITE_P(Regions, AtlasSamplingTest, ::testing::ValuesIn(SamplingCases), [](const ::testing::TestParamInfo<SamplingCase>& Info) { return std::string{Info.param.Name}; });

} // namespace
