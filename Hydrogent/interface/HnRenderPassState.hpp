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

#pragma once

#include <array>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RasterizerState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/BlendState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DepthStencilState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"

#include "HnTypes.hpp"

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/renderPassState.h"

namespace Diligent
{

namespace USD
{

struct HnFramebufferTargets
{
    enum GBUFFER_TARGET : Uint32
    {
        GBUFFER_TARGET_SCENE_COLOR,
        GBUFFER_TARGET_MESH_ID,
        GBUFFER_TARGET_MOTION_VECTOR,
        GBUFFER_TARGET_NORMAL,
        GBUFFER_TARGET_BASE_COLOR,
        GBUFFER_TARGET_MATERIAL,
        GBUFFER_TARGET_IBL,
        GBUFFER_TARGET_COUNT
    };

    ITextureView* FinalColorRTV = nullptr;

    std::array<ITextureView*, GBUFFER_TARGET_COUNT> GBufferRTVs = {};
    std::array<ITextureView*, GBUFFER_TARGET_COUNT> GBufferSRVs = {};

    ITextureView* SelectionDepthDSV = nullptr;
    ITextureView* DepthDSV          = nullptr;
    ITextureView* PrevDepthDSV      = nullptr;
    ITextureView* PrevMotionRTV     = nullptr;

    std::array<ITextureView*, 2> ClosestSelectedLocationRTV = {};

    ITextureView* JitteredFinalColorRTV = nullptr;

    explicit operator bool() const
    {
        for (ITextureView* RTV : GBufferRTVs)
        {
            if (RTV == nullptr)
                return false;
        }

        // clang-format off
        return FinalColorRTV                 != nullptr &&
               SelectionDepthDSV             != nullptr &&
               DepthDSV                      != nullptr &&
               PrevDepthDSV                  != nullptr &&
               ClosestSelectedLocationRTV[0] != nullptr &&
               ClosestSelectedLocationRTV[1] != nullptr &&
               JitteredFinalColorRTV         != nullptr &&
               PrevMotionRTV                 != nullptr;
        // clang-format on
    }

    static const char* GetTargetName(GBUFFER_TARGET Id);
};

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

    void SetFramebufferTargets(const HnFramebufferTargets& Targets)
    {
        m_FramebufferTargets = Targets;
    }
    const HnFramebufferTargets& GetFramebufferTargets() const
    {
        return m_FramebufferTargets;
    }

    void SetClearColor(const float3& ClearColor)
    {
        m_ClearColor = ClearColor;
    }
    const float3& GetClearColor() const
    {
        return m_ClearColor;
    }

    void SetClearDepth(float ClearDepth)
    {
        m_ClearDepth = ClearDepth;
    }
    float GetClearDepth() const
    {
        return m_ClearDepth;
    }

private:
    Uint32                                         m_NumRenderTargets = 0;
    std::array<TEXTURE_FORMAT, MAX_RENDER_TARGETS> m_RTVFormats       = {};
    TEXTURE_FORMAT                                 m_DepthFormat      = TEX_FORMAT_UNKNOWN;

    bool m_FrontFaceCCW = false;

    HnFramebufferTargets m_FramebufferTargets;

    float3 m_ClearColor = {0, 0, 0};
    float  m_ClearDepth = 1.f;
};

} // namespace USD

} // namespace Diligent
