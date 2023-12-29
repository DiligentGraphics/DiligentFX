/*
 *  Copyright 2023 Diligent Graphics LLC
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

#include "GBuffer.hpp"

#include "GraphicsAccessories.hpp"
#include "PlatformMisc.hpp"

namespace Diligent
{

static std::vector<GBuffer::ElementDesc> GetGBufferElementDescs(const GBuffer::ElementDesc* Elements,
                                                                size_t                      NumElements)
{
    std::vector<GBuffer::ElementDesc> Elems{Elements, Elements + NumElements};
    for (auto& ElemDesc : Elems)
    {
        DEV_CHECK_ERR(ElemDesc.Format != TEX_FORMAT_UNKNOWN, "GBuffer element format is not specified");

        if (ElemDesc.BindFlags == BIND_NONE)
        {
            const auto& FmtAttribs = GetTextureFormatAttribs(ElemDesc.Format);
            if (FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH || FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH_STENCIL)
            {
                ElemDesc.BindFlags = BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE;
            }
            else
            {
                ElemDesc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
            }
        }
    }
    return Elems;
}

GBuffer::GBuffer(const ElementDesc* Elements,
                 size_t             NumElements) :
    m_ElemDesc{GetGBufferElementDescs(Elements, NumElements)}
{
    m_Buffers.reserve(NumElements);
}

GBuffer::GBuffer(const ElementDesc* Elements,
                 size_t             NumElements,
                 IRenderDevice*     pDevice,
                 Uint32             Width,
                 Uint32             Height) :
    GBuffer{Elements, NumElements}
{
    Resize(pDevice, Width, Height);
}

void GBuffer::Resize(IRenderDevice* pDevice, Uint32 Width, Uint32 Height)
{
    if (Width == m_Width && Height == m_Height)
        return;

    VERIFY_EXPR(pDevice != nullptr && Width > 0 && Height > 0);

    m_Width  = Width;
    m_Height = Height;

    m_Buffers.clear();
    if (m_Width == 0 || m_Height == 0)
        return;

    TextureDesc TexDesc;
    TexDesc.Width     = Width;
    TexDesc.Height    = Height;
    TexDesc.MipLevels = 1;
    TexDesc.ArraySize = 1;
    TexDesc.Usage     = USAGE_DEFAULT;
    TexDesc.Type      = RESOURCE_DIM_TEX_2D;

    for (const auto& ElemDesc : m_ElemDesc)
    {
        TexDesc.Format     = ElemDesc.Format;
        TexDesc.BindFlags  = ElemDesc.BindFlags;
        TexDesc.ClearValue = ElemDesc.ClearValue;
        VERIFY_EXPR(TexDesc.BindFlags != BIND_NONE);

        std::string Name = "GBuffer " + std::to_string(m_Buffers.size());
        TexDesc.Name     = Name.c_str();

        RefCntAutoPtr<ITexture> pGBufferTex;
        pDevice->CreateTexture(TexDesc, nullptr, &pGBufferTex);
        VERIFY_EXPR(pGBufferTex);

        m_Buffers.emplace_back(std::move(pGBufferTex));
    }
}

void GBuffer::Bind(IDeviceContext* pContext, Uint32 BuffersMask, ITextureView* pDSV, Uint32 ClearMask)
{
    ITextureView* pRTVs[MAX_RENDER_TARGETS];

    Uint32 NumRTs = 0;
    for (Uint32 i = 0; i < GetBufferCount(); ++i)
    {
        if ((BuffersMask & (1u << i)) == 0)
        {
            pRTVs[i] = nullptr;
            continue;
        }

        const auto& BindFlags = GetElementDesc(i).BindFlags;
        if (BindFlags & BIND_RENDER_TARGET)
        {
            pRTVs[i] = GetBuffer(i)->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            NumRTs   = std::max(NumRTs, i + 1);
        }
        else if (BindFlags & BIND_DEPTH_STENCIL)
        {
            VERIFY(pDSV == nullptr, "Only one depth-stencil buffer is expected");
            pDSV = GetBuffer(i)->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        }
    }

    pContext->SetRenderTargets(NumRTs, pRTVs, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    ClearMask &= (1u << GetBufferCount()) - 1;
    while (ClearMask != 0)
    {
        Uint32 i = PlatformMisc::GetLSB(ClearMask);
        ClearMask &= ~(1u << i);

        const auto& ElemDesc = GetElementDesc(i);
        if (ElemDesc.BindFlags & BIND_RENDER_TARGET)
        {
            if (pRTVs[i] != nullptr)
            {
                pContext->ClearRenderTarget(pRTVs[i], ElemDesc.ClearValue.Color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
        }
        else if (ElemDesc.BindFlags & BIND_DEPTH_STENCIL)
        {
            if (pDSV != nullptr)
            {
                CLEAR_DEPTH_STENCIL_FLAGS   ClearFlags = CLEAR_DEPTH_FLAG_NONE;
                const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(ElemDesc.Format);
                if (FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH)
                    ClearFlags = CLEAR_DEPTH_FLAG;
                else if (FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH_STENCIL)
                    ClearFlags = CLEAR_DEPTH_FLAG | CLEAR_STENCIL_FLAG;
                else
                    UNEXPECTED("Unexpected component type");

                pContext->ClearDepthStencil(pDSV, ClearFlags, ElemDesc.ClearValue.DepthStencil.Depth, ElemDesc.ClearValue.DepthStencil.Stencil, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
        }
    }
}

} // namespace Diligent
