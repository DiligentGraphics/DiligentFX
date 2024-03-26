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

#include "GBuffer.hpp"

#include "GraphicsAccessories.hpp"
#include "PlatformMisc.hpp"

#include <array>

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

void GBuffer::Bind(IDeviceContext* pContext,
                   Uint32          BuffersMask,
                   ITextureView*   pDSV,
                   Uint32          ClearMask,
                   const Uint32*   RTIndices)
{
    std::array<ITextureView*, MAX_RENDER_TARGETS> RTVs            = {};
    std::array<const float*, MAX_RENDER_TARGETS>  ClearColors     = {};
    const float*                                  ClearDepthVal   = nullptr;
    const Uint8*                                  ClearStencilVal = nullptr;

    Uint32        NumRTs  = 0;
    const Uint32* RTIndex = RTIndices;
    for (Uint32 i = 0; i < GetBufferCount(); ++i)
    {
        Uint32 BufferBit = 1u << i;
        if ((BuffersMask & BufferBit) == 0)
            continue;

        const auto& ElemDesc  = GetElementDesc(i);
        const auto& BindFlags = ElemDesc.BindFlags;
        if (BindFlags & BIND_RENDER_TARGET)
        {
            const Uint32 RTIdx = RTIndex != nullptr ? *(RTIndex++) : i;

            VERIFY(RTVs[RTIdx] == nullptr, "Render target slot ", RTIdx, " is already used");
            RTVs[RTIdx] = GetBuffer(i)->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
            NumRTs      = std::max(NumRTs, RTIdx + 1);

            if ((ClearMask & BufferBit) != 0)
                ClearColors[RTIdx] = ElemDesc.ClearValue.Color;
        }
        else if (BindFlags & BIND_DEPTH_STENCIL)
        {
            VERIFY(pDSV == nullptr, "Only one depth-stencil buffer is expected");
            pDSV = GetBuffer(i)->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);

            if ((ClearMask & BufferBit) != 0)
            {
                const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(ElemDesc.Format);

                ClearDepthVal = &ElemDesc.ClearValue.DepthStencil.Depth;
                if (FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH_STENCIL)
                    ClearStencilVal = &ElemDesc.ClearValue.DepthStencil.Stencil;
            }
        }
    }

    pContext->SetRenderTargets(NumRTs, RTVs.data(), pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    if ((ClearMask & BuffersMask) != 0)
    {
        for (Uint32 rt = 0; rt < NumRTs; ++rt)
        {
            if (ClearColors[rt] != nullptr)
                pContext->ClearRenderTarget(RTVs[rt], ClearColors[rt], RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        if (ClearDepthVal != nullptr)
        {
            CLEAR_DEPTH_STENCIL_FLAGS ClearFlags = CLEAR_DEPTH_FLAG;
            if (ClearStencilVal != nullptr)
                ClearFlags |= CLEAR_STENCIL_FLAG;
            pContext->ClearDepthStencil(pDSV, ClearFlags, *ClearDepthVal, ClearStencilVal != nullptr ? *ClearStencilVal : 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }
}

} // namespace Diligent
