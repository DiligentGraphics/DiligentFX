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

#include "HnRenderBuffer.hpp"
#include "HnTypeConversions.hpp"
#include "HnRenderDelegate.hpp"

#include "DebugUtilities.hpp"
#include "GraphicsAccessories.hpp"

namespace Diligent
{

namespace USD
{

HnRenderBuffer::HnRenderBuffer(const pxr::SdfPath& Id, const HnRenderDelegate* RnederDelegate) :
    pxr::HdRenderBuffer{Id},
    m_RenderDelegate{RnederDelegate}
{
}

HnRenderBuffer::HnRenderBuffer(const pxr::SdfPath& Id, ITexture* pTexture) :
    pxr::HdRenderBuffer{Id},
    m_pTexture{pTexture}
{
}

HnRenderBuffer::~HnRenderBuffer()
{
}

bool HnRenderBuffer::Allocate(const pxr::GfVec3i& Dimensions,
                              pxr::HdFormat       Format,
                              bool                MultiSampled)
{
    if (m_RenderDelegate == nullptr)
    {
        UNEXPECTED("Texture cannot be allocated without render delegate");
        return false;
    }

    std::string Name = "Render buffer " + GetId().GetString();
    TextureDesc TexDesc;
    TexDesc.Name      = Name.c_str();
    TexDesc.Type      = Dimensions[2] > 1 ? RESOURCE_DIM_TEX_3D : RESOURCE_DIM_TEX_2D;
    TexDesc.Width     = Dimensions[0];
    TexDesc.Height    = Dimensions[1];
    TexDesc.Depth     = Dimensions[2];
    TexDesc.MipLevels = 1;
    TexDesc.Format    = HdFormatToTextureFormat(Format);

    const auto FmtAttribs = GetTextureFormatAttribs(TexDesc.Format);
    if (FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH || FmtAttribs.ComponentType == COMPONENT_TYPE_DEPTH_STENCIL)
        TexDesc.BindFlags = BIND_DEPTH_STENCIL;
    else
        TexDesc.BindFlags = BIND_RENDER_TARGET;
    TexDesc.BindFlags |= BIND_SHADER_RESOURCE;

    TexDesc.Usage       = USAGE_DEFAULT;
    TexDesc.SampleCount = MultiSampled ? 4 : 1;

    if (m_pTexture && m_pTexture->GetDesc() == TexDesc)
        return true;

    m_pTexture.Release();
    auto* pDevice = static_cast<const HnRenderDelegate*>(m_RenderDelegate)->GetDevice();
    pDevice->CreateTexture(TexDesc, nullptr, &m_pTexture);
    VERIFY(m_pTexture, "Failed to create render buffer texture ", Name);

    return m_pTexture != nullptr;
}

unsigned int HnRenderBuffer::GetWidth() const
{
    return m_pTexture ? m_pTexture->GetDesc().GetWidth() : 0;
}

unsigned int HnRenderBuffer::GetHeight() const
{
    return m_pTexture ? m_pTexture->GetDesc().GetHeight() : 0;
}

unsigned int HnRenderBuffer::GetDepth() const
{
    return m_pTexture ? m_pTexture->GetDesc().GetDepth() : 0;
}

pxr::HdFormat HnRenderBuffer::GetFormat() const
{
    return m_pTexture ?
        TextureFormatToHdFormat(m_pTexture->GetDesc().Format) :
        pxr::HdFormatInvalid;
}

bool HnRenderBuffer::IsMultiSampled() const
{
    return m_pTexture ? m_pTexture->GetDesc().SampleCount > 1 : false;
}

void* HnRenderBuffer::Map()
{
    UNSUPPORTED("Mapping is not supported");
    return nullptr;
}

void HnRenderBuffer::Unmap()
{
}

bool HnRenderBuffer::IsMapped() const
{
    return false;
}

void HnRenderBuffer::Resolve()
{
}

bool HnRenderBuffer::IsConverged() const
{
    return true;
}

pxr::VtValue HnRenderBuffer::GetResource(bool MultiSampled) const
{
    return m_pTexture ? pxr::VtValue{m_pTexture.RawPtr()} : pxr::VtValue{};
}

void HnRenderBuffer::_Deallocate()
{
    m_pTexture.Release();
}

} // namespace USD

} // namespace Diligent
