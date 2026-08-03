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

#include <utility>

namespace Diligent
{

RADIENT_STATUS RadientFrameRenderTargets::Prepare(IRenderDevice* pDevice, IRadientRenderTarget& Target)
{
    const RadientRenderTargetDesc& Desc = Target.GetDesc();
    if (Desc.Size.Width == 0 || Desc.Size.Height == 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    m_pOutputColorRTV = Target.GetColorRTV();
    m_pDepthDSV       = Target.GetDepthDSV();

    if (pDevice != nullptr && m_pOutputColorRTV == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const bool RecreateSceneColor =
        m_pSceneColor == nullptr ||
        m_pSceneColor->GetDesc().Width != Desc.Size.Width ||
        m_pSceneColor->GetDesc().Height != Desc.Size.Height;

    if (pDevice != nullptr && RecreateSceneColor)
    {
        TextureDesc SceneColorDesc;
        SceneColorDesc.Name      = "Radient HDR scene color";
        SceneColorDesc.Type      = RESOURCE_DIM_TEX_2D;
        SceneColorDesc.Width     = Desc.Size.Width;
        SceneColorDesc.Height    = Desc.Size.Height;
        SceneColorDesc.Format    = SceneColorFormat;
        SceneColorDesc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;

        RefCntAutoPtr<ITexture> pSceneColor;
        pDevice->CreateTexture(SceneColorDesc, nullptr, pSceneColor.GetAddressOfEmpty());
        if (pSceneColor == nullptr ||
            pSceneColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) == nullptr ||
            pSceneColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) == nullptr)
        {
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        m_pSceneColor = std::move(pSceneColor);
        ++m_Version;
    }
    else if (pDevice == nullptr && m_Size != Desc.Size)
    {
        ++m_Version;
    }

    m_Size = Desc.Size;

    return RADIENT_STATUS_OK;
}

void RadientFrameRenderTargets::ClearSceneColor(IDeviceContext* pContext) const
{
    ITextureView* pSceneColorRTV = GetSceneColorRTV();
    if (pContext == nullptr || pSceneColorRTV == nullptr)
        return;

    pContext->SetRenderTargets(1, &pSceneColorRTV, m_pDepthDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    constexpr float ClearColor[] = {0.f, 0.f, 0.f, 1.f};
    pContext->ClearRenderTarget(pSceneColorRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
}

const RadientExtent2D& RadientFrameRenderTargets::GetSize() const
{
    return m_Size;
}

Uint32 RadientFrameRenderTargets::GetVersion() const
{
    return m_Version;
}

ITextureView* RadientFrameRenderTargets::GetSceneColorRTV() const
{
    return m_pSceneColor != nullptr ? m_pSceneColor->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr;
}

ITextureView* RadientFrameRenderTargets::GetSceneColorSRV() const
{
    return m_pSceneColor != nullptr ? m_pSceneColor->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

ITextureView* RadientFrameRenderTargets::GetOutputColorRTV() const
{
    return m_pOutputColorRTV;
}

ITextureView* RadientFrameRenderTargets::GetDepthDSV() const
{
    return m_pDepthDSV;
}

} // namespace Diligent
