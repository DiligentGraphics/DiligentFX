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

#include <memory>
#include <array>
#include <vector>
#include <unordered_map>

#include "HnTask.hpp"
#include "../interface/HnRenderPassState.hpp"
#include "../interface/HnTypes.hpp"
#include "../interface/HnFrameRenderTargets.hpp"

#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../../DiligentCore/Common/interface/Timer.hpp"

namespace Diligent
{

struct ITextureView;

namespace USD
{

class HnCamera;

struct HnBeginFrameTaskParams
{
    struct RenderTargetFormats
    {
        std::array<TEXTURE_FORMAT, HnFrameRenderTargets::GBUFFER_TARGET_COUNT> GBuffer = {};

        TEXTURE_FORMAT Depth                   = TEX_FORMAT_D32_FLOAT;
        TEXTURE_FORMAT ClosestSelectedLocation = TEX_FORMAT_RG16_UNORM;
        TEXTURE_FORMAT JitteredColor           = TEX_FORMAT_RGBA16_FLOAT;

        bool operator==(const RenderTargetFormats& rhs) const
        {
            // clang-format off
            return GBuffer                 == rhs.GBuffer &&
                   Depth                   == rhs.Depth &&
                   ClosestSelectedLocation == rhs.ClosestSelectedLocation &&
                   JitteredColor           == rhs.JitteredColor;
            // clang-format on
        }

        RenderTargetFormats() noexcept;
    };
    RenderTargetFormats Formats;

    struct RenderState
    {
        bool FrontFaceCCW = false;

        float                  DepthBias            = 0;
        float                  SlopeScaledDepthBias = 0;
        pxr::HdCompareFunction DepthFunc            = pxr::HdCmpFuncLess;
        bool                   DepthBiasEnabled     = false;
        bool                   DepthTestEnabled     = true;
        bool                   DepthClampEnabled    = false;

        pxr::HdCullStyle CullStyle = pxr::HdCullStyleBack;

        pxr::HdCompareFunction StencilFunc    = pxr::HdCmpFuncAlways;
        int                    StencilRef     = 0;
        int                    StencilMask    = 0xFF;
        pxr::HdStencilOp       StencilFailOp  = pxr::HdStencilOpKeep;
        pxr::HdStencilOp       StencilZFailOp = pxr::HdStencilOpKeep;
        pxr::HdStencilOp       StencilZPassOp = pxr::HdStencilOpKeep;
        bool                   StencilEnabled = false;

        constexpr bool operator==(const RenderState& rhs) const
        {
            // clang-format off
            return FrontFaceCCW         == rhs.FrontFaceCCW &&
                   DepthBias            == rhs.DepthBias &&
                   SlopeScaledDepthBias == rhs.SlopeScaledDepthBias &&
                   DepthFunc            == rhs.DepthFunc &&
                   DepthBiasEnabled     == rhs.DepthBiasEnabled &&
                   DepthTestEnabled     == rhs.DepthTestEnabled &&
                   DepthClampEnabled    == rhs.DepthClampEnabled &&
                   CullStyle            == rhs.CullStyle &&
                   StencilFunc          == rhs.StencilFunc &&
                   StencilRef           == rhs.StencilRef &&
                   StencilMask          == rhs.StencilMask &&
                   StencilFailOp        == rhs.StencilFailOp &&
                   StencilZFailOp       == rhs.StencilZFailOp &&
                   StencilZPassOp       == rhs.StencilZPassOp &&
                   StencilEnabled       == rhs.StencilEnabled;
            // clang-format on
        }
    };
    RenderState State;

    float4 ClearColor = {0, 0, 0, 0};
    float  ClearDepth = 1.f;

    pxr::SdfPath FinalColorTargetId;
    pxr::SdfPath CameraId;

    struct RendererParams
    {
        float OcclusionStrength = 1;
        float EmissionScale     = 1;
        float IBLScale          = 1;

        float4 UnshadedColor = {1, 1, 1, 1};
        float  PointSize     = 1;

        constexpr bool operator==(const RendererParams& rhs) const
        {
            // clang-format off
            return OcclusionStrength == rhs.OcclusionStrength &&
                   EmissionScale     == rhs.EmissionScale &&
                   IBLScale          == rhs.IBLScale &&
                   UnshadedColor     == rhs.UnshadedColor &&
                   PointSize         == rhs.PointSize;
            // clang-format on
        }
    };
    RendererParams Renderer;

    bool operator==(const HnBeginFrameTaskParams& rhs) const
    {
        // clang-format off
        return Formats              == rhs.Formats &&
               ClearColor           == rhs.ClearColor &&
               ClearDepth           == rhs.ClearDepth &&
               State                == rhs.State &&
               FinalColorTargetId   == rhs.FinalColorTargetId &&
               CameraId             == rhs.CameraId &&
               Renderer             == rhs.Renderer;
        // clang-format on
    }
    bool operator!=(const HnBeginFrameTaskParams& rhs) const
    {
        return !(*this == rhs);
    }
};

/// Sets up rendering state for subsequent tasks:
/// - Prepares the render targets and depth buffer
///   - Retrieves final color Bprim from the render index using the FinalColorTargetId
///   - (Re)creates the render targets if necessary
///   - Inserts them into the render index as Bprims
///   - Passes Bprim Id to subsequent tasks via the task context
/// - Updates the render pass states
/// - Updates the task context with the render pass states so that subsequent tasks can use it
class HnBeginFrameTask final : public HnTask
{
public:
    HnBeginFrameTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnBeginFrameTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

private:
    void PrepareRenderTargets(pxr::HdRenderIndex* RenderIndex, pxr::HdTaskContext* TaskCtx, ITextureView* pFinalColorRTV);
    void UpdateFrameConstants(IDeviceContext* pCtx, IBuffer* pFrameAttrbisCB, bool UseTAA, const float2& Jitter, bool& CameraTransformDirty);

private:
    std::unordered_map<pxr::TfToken, HnRenderPassState, pxr::TfToken::HashFunctor> m_RenderPassStates;

    HnFrameRenderTargets m_FrameRenderTargets;

    pxr::SdfPath m_JitteredFinalColorTargetId;

    std::array<pxr::SdfPath, HnFrameRenderTargets::GBUFFER_TARGET_COUNT> m_GBufferTargetIds;

    pxr::SdfPath m_SelectionDepthBufferId;

    // Ping-pong buffers for the last two frames
    std::array<pxr::SdfPath, 2> m_DepthBufferId;

    // Ping-pong buffers for jump-flood algorithm
    std::array<pxr::SdfPath, 2> m_ClosestSelLocnTargetId;

    const HnCamera*     m_pCamera     = nullptr;
    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    HnBeginFrameTaskParams m_Params;

    std::vector<Uint8> m_FrameAttribsData;

    Uint32 m_FrameBufferWidth  = 0;
    Uint32 m_FrameBufferHeight = 0;

    Timer m_FrameTimer;
};

} // namespace USD

} // namespace Diligent
