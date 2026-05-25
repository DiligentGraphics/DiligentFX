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

#include "RadientRendererImpl.hpp"
#include "RadientRenderPipeline.hpp"

namespace Diligent
{

RadientRenderTargetImpl::RadientRenderTargetImpl(IReferenceCounters* pRefCounters, const RadientRenderTargetDesc& Desc) :
    TBase{pRefCounters},
    m_Name{Desc.Name != nullptr ? Desc.Name : ""},
    m_Desc{Desc},
    m_pSwapChain{Desc.pSwapChain},
    m_pColorRTV{Desc.pColorRTV},
    m_pDepthDSV{Desc.pDepthDSV}
{
    m_Desc.Name = m_Name.c_str();
}

RadientRenderTargetImpl::~RadientRenderTargetImpl()
{
}

RefCntAutoPtr<IRadientRenderTarget> RadientRenderTargetImpl::Create(const RadientRenderTargetDesc& Desc)
{
    return RefCntAutoPtr<RadientRenderTargetImpl>{MakeNewRCObj<RadientRenderTargetImpl>()(Desc)};
}

const RadientRenderTargetDesc& RadientRenderTargetImpl::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientRenderTargetImpl::Resize(Uint32 Width, Uint32 Height)
{
    const RadientExtent2D NewSize{Width, Height};
    if (m_Desc.Size == NewSize)
        return RADIENT_STATUS_NO_CHANGE;

    m_Desc.Size = NewSize;
    return RADIENT_STATUS_OK;
}

ITextureView* RadientRenderTargetImpl::GetColorRTV()
{
    return m_pColorRTV;
}

ITextureView* RadientRenderTargetImpl::GetDepthDSV()
{
    return m_pDepthDSV;
}

ISwapChain* RadientRenderTargetImpl::GetSwapChain()
{
    return m_pSwapChain;
}


RadientRendererImpl::RadientRendererImpl(IReferenceCounters* pRefCounters,
                                         const CreateInfo&   CI) :
    TBase{pRefCounters},
    m_Name{CI.Desc.Name != nullptr ? CI.Desc.Name : ""},
    m_Desc{CI.Desc},
    m_pBackend{CI.pBackend},
    m_RenderPipeline{std::make_unique<RadientRenderPipeline>(CI.pBackend, CI.pAssetManager)}
{
    m_Desc.Name = m_Name.c_str();
}

RadientRendererImpl::~RadientRendererImpl()
{
}

RefCntAutoPtr<IRadientRenderer> RadientRendererImpl::Create(const CreateInfo& CI)
{
    return RefCntAutoPtr<RadientRendererImpl>{MakeNewRCObj<RadientRendererImpl>()(CI)};
}

const RadientRendererDesc& RadientRendererImpl::GetDesc() const
{
    return m_Desc;
}

RADIENT_STATUS RadientRendererImpl::CreateRenderTarget(const RadientRenderTargetDesc& Desc, IRadientRenderTarget** ppTarget)
{
    if (ppTarget == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    *ppTarget = nullptr;

    RefCntAutoPtr<IRadientRenderTarget> pTarget = RadientRenderTargetImpl::Create(Desc);
    *ppTarget                                   = pTarget.Detach();
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientRendererImpl::Render(const RadientRenderAttribs& Attribs)
{
    if (m_pBackend == nullptr ||
        m_RenderPipeline == nullptr ||
        Attribs.pScene == nullptr ||
        Attribs.pRenderTarget == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    return m_RenderPipeline->Render(Attribs);
}

} // namespace Diligent
