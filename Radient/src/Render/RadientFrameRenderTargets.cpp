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

#include "Render/RadientFrameRenderTargets.hpp"

#include "Errors.hpp"

#include <array>
#include <utility>

namespace Diligent
{

TEXTURE_FORMAT RadientFrameRenderTargets::GetGBufferFormat(GBufferTarget Target) noexcept
{
    switch (Target)
    {
        case GBUFFER_TARGET_SCENE_COLOR: return TEX_FORMAT_RGBA16_FLOAT;
        case GBUFFER_TARGET_MOTION_VECTOR: return TEX_FORMAT_RG16_FLOAT;
        case GBUFFER_TARGET_NORMAL: return TEX_FORMAT_RGBA16_FLOAT;
        case GBUFFER_TARGET_BASE_COLOR: return TEX_FORMAT_RGBA8_UNORM;
        case GBUFFER_TARGET_MATERIAL: return TEX_FORMAT_RG8_UNORM;
        case GBUFFER_TARGET_IBL: return TEX_FORMAT_RGBA16_FLOAT;

        default:
            UNEXPECTED("Unexpected Radient G-buffer target");
            return TEX_FORMAT_UNKNOWN;
    }
}

const Char* RadientFrameRenderTargets::GetGBufferName(GBufferTarget Target) noexcept
{
    switch (Target)
    {
        case GBUFFER_TARGET_SCENE_COLOR: return "Radient HDR scene color";
        case GBUFFER_TARGET_MOTION_VECTOR: return "Radient motion vectors";
        case GBUFFER_TARGET_NORMAL: return "Radient normal";
        case GBUFFER_TARGET_BASE_COLOR: return "Radient base color";
        case GBUFFER_TARGET_MATERIAL: return "Radient material data";
        case GBUFFER_TARGET_IBL: return "Radient specular IBL";

        default:
            UNEXPECTED("Unexpected Radient G-buffer target");
            return "Radient G-buffer";
    }
}

RADIENT_STATUS RadientFrameRenderTargets::Prepare(IRenderDevice* pDevice, IRadientRenderTarget& Target)
{
    const RadientRenderTargetDesc& Desc = Target.GetDesc();
    if (Desc.Size.Width == 0 || Desc.Size.Height == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    m_pOutputColorRTV = Target.GetColorRTV();
    m_UseReverseDepth = pDevice != nullptr && pDevice->GetDeviceInfo().NDC.MinZ == 0.f;

    // Tessera renders depth into its own shader-readable ping-pong textures so
    // post effects can consume both the current and previous frame depths.

    if (pDevice != nullptr && m_pOutputColorRTV == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    bool RecreateTargets = false;
    for (Uint32 TargetIndex = 0; TargetIndex < GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        const GBufferTarget GBufferTargetId = static_cast<GBufferTarget>(TargetIndex);
        ITexture* const     pTexture        = m_GBuffer[TargetIndex];
        RecreateTargets |=
            pTexture == nullptr ||
            pTexture->GetDesc().Width != Desc.Size.Width ||
            pTexture->GetDesc().Height != Desc.Size.Height ||
            pTexture->GetDesc().Format != GetGBufferFormat(GBufferTargetId);
    }
    for (ITexture* pTexture : m_Depth)
    {
        RecreateTargets |=
            pTexture == nullptr ||
            pTexture->GetDesc().Width != Desc.Size.Width ||
            pTexture->GetDesc().Height != Desc.Size.Height ||
            pTexture->GetDesc().Format != DepthFormat;
    }

    if (pDevice != nullptr && RecreateTargets)
    {
        std::array<RefCntAutoPtr<ITexture>, GBUFFER_TARGET_COUNT> NewGBuffer;
        for (Uint32 TargetIndex = 0; TargetIndex < GBUFFER_TARGET_COUNT; ++TargetIndex)
        {
            const GBufferTarget GBufferTargetId = static_cast<GBufferTarget>(TargetIndex);

            TextureDesc Texture;
            Texture.Name      = GetGBufferName(GBufferTargetId);
            Texture.Type      = RESOURCE_DIM_TEX_2D;
            Texture.Width     = Desc.Size.Width;
            Texture.Height    = Desc.Size.Height;
            Texture.Format    = GetGBufferFormat(GBufferTargetId);
            Texture.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

            pDevice->CreateTexture(Texture, nullptr, NewGBuffer[TargetIndex].GetAddressOfEmpty());
            ITexture* const pTexture = NewGBuffer[TargetIndex];
            if (pTexture == nullptr ||
                pTexture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) == nullptr ||
                pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) == nullptr)
            {
                return RADIENT_STATUS_INVALID_OPERATION;
            }
        }

        std::array<RefCntAutoPtr<ITexture>, 2> NewDepth;
        for (Uint32 DepthIndex = 0; DepthIndex < NewDepth.size(); ++DepthIndex)
        {
            TextureDesc Texture;
            Texture.Name      = DepthIndex == 0 ? "Radient depth 0" : "Radient depth 1";
            Texture.Type      = RESOURCE_DIM_TEX_2D;
            Texture.Width     = Desc.Size.Width;
            Texture.Height    = Desc.Size.Height;
            Texture.Format    = DepthFormat;
            Texture.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;

            pDevice->CreateTexture(Texture, nullptr, NewDepth[DepthIndex].GetAddressOfEmpty());
            ITexture* const pTexture = NewDepth[DepthIndex];
            if (pTexture == nullptr ||
                pTexture->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) == nullptr ||
                pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) == nullptr)
            {
                return RADIENT_STATUS_INVALID_OPERATION;
            }
        }

        m_GBuffer           = std::move(NewGBuffer);
        m_Depth             = std::move(NewDepth);
        m_CurrentDepth      = 0;
        m_DepthHistoryValid = false;
        ++m_Version;
    }
    else if (pDevice == nullptr && m_Size != Desc.Size)
    {
        ++m_Version;
    }

    m_Size = Desc.Size;

    return RADIENT_STATUS_OK;
}

void RadientFrameRenderTargets::ClearGBuffer(IDeviceContext* pContext, const RadientFloat4& ClearColor) const
{
    if (pContext == nullptr)
        return;

    std::array<ITextureView*, GBUFFER_TARGET_COUNT> GBufferRTVs{};
    for (Uint32 TargetIndex = 0; TargetIndex < GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        GBufferRTVs[TargetIndex] = GetGBufferRTV(static_cast<GBufferTarget>(TargetIndex));
        if (GBufferRTVs[TargetIndex] == nullptr)
            return;
    }

    ITextureView* const pDepthDSV = GetDepthDSV();
    if (pDepthDSV == nullptr)
        return;

    pContext->SetRenderTargets(GBUFFER_TARGET_COUNT,
                               GBufferRTVs.data(),
                               pDepthDSV,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Scene-color alpha accumulates opacity and is used to attenuate
    // screen-space effects over the background.
    const float     SceneColorClear[] = {ClearColor.x, ClearColor.y, ClearColor.z, ClearColor.w};
    constexpr float GBufferClear[]    = {0.f, 0.f, 0.f, 0.f};
    for (Uint32 TargetIndex = 0; TargetIndex < GBUFFER_TARGET_COUNT; ++TargetIndex)
    {
        pContext->ClearRenderTarget(
            GBufferRTVs[TargetIndex],
            TargetIndex == GBUFFER_TARGET_SCENE_COLOR ? SceneColorClear : GBufferClear,
            RESOURCE_STATE_TRANSITION_MODE_VERIFY);
    }
    pContext->ClearDepthStencil(pDepthDSV,
                                CLEAR_DEPTH_FLAG,
                                m_UseReverseDepth ? 0.f : 1.f,
                                0,
                                RESOURCE_STATE_TRANSITION_MODE_VERIFY);
}

void RadientFrameRenderTargets::CommitFrame() noexcept
{
    m_DepthHistoryValid = true;
    m_CurrentDepth ^= 1u;
}

const RadientExtent2D& RadientFrameRenderTargets::GetSize() const
{
    return m_Size;
}

Uint32 RadientFrameRenderTargets::GetVersion() const
{
    return m_Version;
}

ITextureView* RadientFrameRenderTargets::GetGBufferRTV(GBufferTarget Target) const
{
    const Uint32 TargetIndex = static_cast<Uint32>(Target);
    if (TargetIndex >= GBUFFER_TARGET_COUNT || m_GBuffer[TargetIndex] == nullptr)
        return nullptr;

    return m_GBuffer[TargetIndex]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
}

ITextureView* RadientFrameRenderTargets::GetGBufferSRV(GBufferTarget Target) const
{
    const Uint32 TargetIndex = static_cast<Uint32>(Target);
    if (TargetIndex >= GBUFFER_TARGET_COUNT || m_GBuffer[TargetIndex] == nullptr)
        return nullptr;

    return m_GBuffer[TargetIndex]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
}

ITextureView* RadientFrameRenderTargets::GetSceneColorRTV() const
{
    return GetGBufferRTV(GBUFFER_TARGET_SCENE_COLOR);
}

ITextureView* RadientFrameRenderTargets::GetSceneColorSRV() const
{
    return GetGBufferSRV(GBUFFER_TARGET_SCENE_COLOR);
}

ITextureView* RadientFrameRenderTargets::GetOutputColorRTV() const
{
    return m_pOutputColorRTV;
}

ITextureView* RadientFrameRenderTargets::GetDepthDSV() const
{
    return m_Depth[m_CurrentDepth] != nullptr ?
        m_Depth[m_CurrentDepth]->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) :
        nullptr;
}

ITextureView* RadientFrameRenderTargets::GetDepthSRV() const
{
    return m_Depth[m_CurrentDepth] != nullptr ?
        m_Depth[m_CurrentDepth]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) :
        nullptr;
}

ITextureView* RadientFrameRenderTargets::GetPreviousDepthSRV() const
{
    const Uint32 PreviousDepth = m_DepthHistoryValid ? m_CurrentDepth ^ 1u : m_CurrentDepth;
    return m_Depth[PreviousDepth] != nullptr ?
        m_Depth[PreviousDepth]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) :
        nullptr;
}

} // namespace Diligent
