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

#pragma once

#include <array>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RasterizerState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/BlendState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DepthStencilState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"

#include "../interface/HnTypes.hpp"

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/renderPassState.h"

namespace Diligent
{

namespace USD
{

/// Hydra render pass state implementation in Hydrogent.
class HnRenderPassState final : public pxr::HdRenderPassState
{
public:
    static pxr::HdRenderPassStateSharedPtr Create();

    HnRenderPassState();

    void Begin(IDeviceContext* pContext);

    void SetRenderTargetFormat(Uint32 rt, TEXTURE_FORMAT Fmt)
    {
        m_RTVFormats[rt] = Fmt;
    }
    void SetDepthStencilFormat(TEXTURE_FORMAT DepthFormat)
    {
        m_DepthFormat = DepthFormat;
    }
    void SetNumRenderTargets(Uint32 NumRTs)
    {
        m_NumRenderTargets = NumRTs;
    }

    void SetFrontFaceCCW(bool FrontFaceCCW)
    {
        m_FrontFaceCCW = FrontFaceCCW;
    }
    bool GetFrontFaceCCW() const
    {
        return m_FrontFaceCCW;
    }

    Uint32 GetNumRenderTargets() const
    {
        return m_NumRenderTargets;
    }
    TEXTURE_FORMAT GetRenderTargetFormat(Uint32 rt) const
    {
        return m_RTVFormats[rt];
    }
    TEXTURE_FORMAT GetDepthStencilFormat() const
    {
        return m_DepthFormat;
    }

    RasterizerStateDesc   GetRasterizerState() const;
    DepthStencilStateDesc GetDepthStencilState() const;
    BlendStateDesc        GetBlendState() const;
    GraphicsPipelineDesc  GetGraphicsPipelineDesc() const;

    struct FramebufferTargets
    {
        ITextureView* FinalColorRTV     = nullptr;
        ITextureView* OffscreenColorRTV = nullptr;
        ITextureView* MeshIdRTV         = nullptr;
        ITextureView* SelectionDepthDSV = nullptr;
        ITextureView* DepthDSV          = nullptr;

        constexpr explicit operator bool() const
        {
            // clang-format off
            return FinalColorRTV     != nullptr &&
                   OffscreenColorRTV != nullptr &&
                   MeshIdRTV         != nullptr &&
                   SelectionDepthDSV != nullptr &&
                   DepthDSV          != nullptr;
            // clang-format on
        }
    };
    void SetFramebufferTargets(const FramebufferTargets& Targets)
    {
        m_FramebufferTargets = Targets;
    }
    const FramebufferTargets& GetFramebufferTargets() const
    {
        return m_FramebufferTargets;
    }

private:
    Uint32                                         m_NumRenderTargets = 0;
    std::array<TEXTURE_FORMAT, MAX_RENDER_TARGETS> m_RTVFormats       = {};
    TEXTURE_FORMAT                                 m_DepthFormat      = TEX_FORMAT_UNKNOWN;

    bool m_FrontFaceCCW = false;

    FramebufferTargets m_FramebufferTargets;
};

} // namespace USD

} // namespace Diligent
