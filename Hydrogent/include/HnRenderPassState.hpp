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

    void SetRenderTargetFormat(Uint32 RT, TEXTURE_FORMAT Fmt)
    {
        m_RTVFormats[RT] = Fmt;
    }
    void SetDepthStencilFormat(TEXTURE_FORMAT DepthFormat)
    {
        m_DepthFormat = DepthFormat;
    }
    void SetNumRenderTargets(Uint32 NumRTs)
    {
        m_NumRenderTargets = NumRTs;
    }

    void SetRenderMode(HN_RENDER_MODE RenderMode)
    {
        m_RenderMode = RenderMode;
    }
    HN_RENDER_MODE GetRenderMode() const
    {
        return m_RenderMode;
    }

    void SetFrontFaceCCW(bool FrontFaceCCW)
    {
        m_FrontFaceCCW = FrontFaceCCW;
    }
    bool GetFrontFaceCCW() const
    {
        return m_FrontFaceCCW;
    }

    void SetDebugView(int DebugView)
    {
        m_DebugView = DebugView;
    }
    int GetDebugView() const
    {
        return m_DebugView;
    }

    void SetOcclusionStrength(float OcclusionStrength)
    {
        m_OcclusionStrength = OcclusionStrength;
    }
    float GetOcclusionStrength() const
    {
        return m_OcclusionStrength;
    }

    void SetEmissionScale(float EmissionScale)
    {
        m_EmissionScale = EmissionScale;
    }
    float GetEmissionScale() const
    {
        return m_EmissionScale;
    }

    void SetIBLScale(float IBLScale)
    {
        m_IBLScale = IBLScale;
    }
    float GetIBLScale() const
    {
        return m_IBLScale;
    }

    void SetTransform(const float4x4& Transform)
    {
        m_Transform = Transform;
    }
    const float4x4& GetTransform() const
    {
        return m_Transform;
    }

    RasterizerStateDesc   GetRasterizerState() const;
    DepthStencilStateDesc GetDepthStencilState() const;
    BlendStateDesc        GetBlendState() const;
    GraphicsPipelineDesc  GetGraphicsPipelineDesc() const;

private:
    Uint32                                         m_NumRenderTargets = 0;
    std::array<TEXTURE_FORMAT, MAX_RENDER_TARGETS> m_RTVFormats       = {};
    TEXTURE_FORMAT                                 m_DepthFormat      = TEX_FORMAT_UNKNOWN;

    HN_RENDER_MODE m_RenderMode = HN_RENDER_MODE_SOLID;

    bool m_FrontFaceCCW = false;

    int   m_DebugView         = 0;
    float m_OcclusionStrength = 1;
    float m_EmissionScale     = 1;
    float m_IBLScale          = 1;

    float4x4 m_Transform = float4x4::Identity();
};

} // namespace USD

} // namespace Diligent
