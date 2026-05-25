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

#pragma once

#include "RadientRenderer.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <memory>
#include <string>

namespace Diligent
{

class RadientRenderPipeline;
class RadientAssetManagerImpl;

class RadientRenderTargetImpl final : public ObjectBase<IRadientRenderTarget>
{
public:
    using TBase = ObjectBase<IRadientRenderTarget>;

    RadientRenderTargetImpl(IReferenceCounters* pRefCounters, const RadientRenderTargetDesc& Desc);
    ~RadientRenderTargetImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientRenderTarget, TBase)

    static RefCntAutoPtr<IRadientRenderTarget> Create(const RadientRenderTargetDesc& Desc);

    virtual const RadientRenderTargetDesc& DILIGENT_CALL_TYPE GetDesc() const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Resize(Uint32 Width,
                                                     Uint32 Height) override final;

    virtual ITextureView* DILIGENT_CALL_TYPE GetColorRTV() override final;

    virtual ITextureView* DILIGENT_CALL_TYPE GetDepthDSV() override final;

    virtual ISwapChain* DILIGENT_CALL_TYPE GetSwapChain() override final;

private:
    std::string m_Name;

    RadientRenderTargetDesc m_Desc;

    RefCntAutoPtr<ISwapChain>   m_pSwapChain;
    RefCntAutoPtr<ITextureView> m_pColorRTV;
    RefCntAutoPtr<ITextureView> m_pDepthDSV;
};


class RadientRendererImpl final : public ObjectBase<IRadientRenderer>
{
public:
    using TBase = ObjectBase<IRadientRenderer>;

    struct CreateInfo
    {
        RadientRendererDesc      Desc;
        IRadientBackend*         pBackend      = nullptr;
        RadientAssetManagerImpl* pAssetManager = nullptr;
    };

    RadientRendererImpl(IReferenceCounters* pRefCounters,
                        const CreateInfo&   CI);
    ~RadientRendererImpl();

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientRenderer, TBase)

    static RefCntAutoPtr<IRadientRenderer> Create(const CreateInfo& CI);

    virtual const RadientRendererDesc& DILIGENT_CALL_TYPE GetDesc() const override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateRenderTarget(const RadientRenderTargetDesc& Desc,
                                                                 IRadientRenderTarget**         ppTarget) override final;

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Render(const RadientRenderAttribs& Attribs) override final;

private:
    std::string m_Name;

    RadientRendererDesc m_Desc;

    RefCntAutoPtr<IRadientBackend>         m_pBackend;
    std::unique_ptr<RadientRenderPipeline> m_RenderPipeline;
};

} // namespace Diligent
