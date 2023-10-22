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

#include <memory>

#include "HnTask.hpp"
#include "../interface/HnTypes.hpp"

namespace Diligent
{

namespace USD
{

class HnRenderPassState;

struct HnSetupRenderingTaskParams
{
    constexpr bool operator==(const HnSetupRenderingTaskParams& rhs) const
    {
        // clang-format off
        return ColorFormat          == rhs.ColorFormat &&
               MeshIdFormat         == rhs.MeshIdFormat &&
               DepthFormat          == rhs.DepthFormat &&
               RenderMode           == rhs.RenderMode &&
               FrontFaceCCW         == rhs.FrontFaceCCW &&
               DebugView            == rhs.DebugView &&
               OcclusionStrength    == rhs.OcclusionStrength &&
               EmissionScale        == rhs.EmissionScale &&
               IBLScale             == rhs.IBLScale &&
               Transform            == rhs.Transform &&
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
    constexpr bool operator!=(const HnSetupRenderingTaskParams& rhs) const
    {
        return !(*this == rhs);
    }

    TEXTURE_FORMAT ColorFormat  = TEX_FORMAT_UNKNOWN;
    TEXTURE_FORMAT MeshIdFormat = TEX_FORMAT_UNKNOWN;
    TEXTURE_FORMAT DepthFormat  = TEX_FORMAT_UNKNOWN;

    HN_RENDER_MODE RenderMode = HN_RENDER_MODE_SOLID;

    bool FrontFaceCCW = false;

    int   DebugView         = 0;
    float OcclusionStrength = 1;
    float EmissionScale     = 1;
    float IBLScale          = 1;

    float4x4 Transform = float4x4::Identity();

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
};

/// Post processing task implementation in Hydrogent.
class HnSetupRenderingTask final : public HnTask
{
public:
    HnSetupRenderingTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnSetupRenderingTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* PostProcessIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

private:
    void UpdateRenderPassState(const HnSetupRenderingTaskParams& Params);

private:
    std::shared_ptr<HnRenderPassState> m_RenderPassState;
};

} // namespace USD

} // namespace Diligent
