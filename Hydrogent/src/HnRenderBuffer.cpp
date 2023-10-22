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

HnRenderBuffer::HnRenderBuffer(const pxr::SdfPath& Id) :
    pxr::HdRenderBuffer{Id}
{
}

HnRenderBuffer::HnRenderBuffer(const pxr::SdfPath& Id, const HnRenderDelegate* RnederDelegate) :
    pxr::HdRenderBuffer{Id},
    m_RenderDelegate{RnederDelegate}
{
}

HnRenderBuffer::HnRenderBuffer(const pxr::SdfPath& Id, ITextureView* pTarget) :
    pxr::HdRenderBuffer{Id},
    m_pTarget{pTarget}
{
}

HnRenderBuffer::~HnRenderBuffer()
{
}

bool HnRenderBuffer::Allocate(const pxr::GfVec3i& Dimensions,
                              pxr::HdFormat       Format,
                              bool                MultiSampled)
{
    if (!(Dimensions[0] > 0 && Dimensions[1] > 0 && Format != pxr::HdFormatInvalid))
        return false;

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

    const auto IsDepth = GetTextureFormatAttribs(TexDesc.Format).IsDepthStencil();
    TexDesc.BindFlags  = (IsDepth ? BIND_DEPTH_STENCIL : BIND_RENDER_TARGET) | BIND_SHADER_RESOURCE;

    TexDesc.Usage       = USAGE_DEFAULT;
    TexDesc.SampleCount = MultiSampled ? 4 : 1;

    if (m_pTarget && m_pTarget->GetTexture()->GetDesc() == TexDesc)
        return true;

    m_pTarget.Release();
    auto* const pDevice = static_cast<const HnRenderDelegate*>(m_RenderDelegate)->GetDevice();

    RefCntAutoPtr<ITexture> pTexture;
    pDevice->CreateTexture(TexDesc, nullptr, &pTexture);
    if (!pTexture)
    {
        UNEXPECTED("Failed to create render buffer texture ", Name);
        return false;
    }

    m_pTarget = pTexture->GetDefaultView(IsDepth ? TEXTURE_VIEW_DEPTH_STENCIL : TEXTURE_VIEW_RENDER_TARGET);
    VERIFY(m_pTarget, "Failed to get default view for render buffer texture ", Name);

    return m_pTarget != nullptr;
}

unsigned int HnRenderBuffer::GetWidth() const
{
    if (!m_pTarget)
        return 0;

    const auto MipLevelProps = GetMipLevelProperties(m_pTarget->GetTexture()->GetDesc(), m_pTarget->GetDesc().MostDetailedMip);
    return MipLevelProps.LogicalWidth;
}

unsigned int HnRenderBuffer::GetHeight() const
{
    if (!m_pTarget)
        return 0;

    const auto MipLevelProps = GetMipLevelProperties(m_pTarget->GetTexture()->GetDesc(), m_pTarget->GetDesc().MostDetailedMip);
    return MipLevelProps.LogicalHeight;
}

unsigned int HnRenderBuffer::GetDepth() const
{
    if (!m_pTarget)
        return 0;

    const auto MipLevelProps = GetMipLevelProperties(m_pTarget->GetTexture()->GetDesc(), m_pTarget->GetDesc().MostDetailedMip);
    return MipLevelProps.Depth;
}

pxr::HdFormat HnRenderBuffer::GetFormat() const
{
    return m_pTarget ?
        TextureFormatToHdFormat(m_pTarget->GetDesc().Format) :
        pxr::HdFormatInvalid;
}

bool HnRenderBuffer::IsMultiSampled() const
{
    return m_pTarget ? m_pTarget->GetTexture()->GetDesc().SampleCount > 1 : false;
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
    return m_pTarget ? pxr::VtValue{m_pTarget.RawPtr()} : pxr::VtValue{};
}

void HnRenderBuffer::_Deallocate()
{
    m_pTarget.Release();
}

void HnRenderBuffer::SetTarget(ITextureView* pTarget)
{
    m_pTarget = pTarget;
}

void HnRenderBuffer::ReleaseTarget()
{
    m_pTarget.Release();
}

} // namespace USD

} // namespace Diligent
