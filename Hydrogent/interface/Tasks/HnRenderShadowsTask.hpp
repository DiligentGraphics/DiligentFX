/*
 *  Copyright 2024 Diligent Graphics LLC
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

#include "HnTask.hpp"

#include <map>

#include "../interface/HnRenderPassState.hpp"

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/GraphicsTypes.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"

namespace Diligent
{

namespace USD
{

class HnRenderPass;
class HnLight;
class HnRenderDelegate;

struct HnRenderShadowsTaskParams
{
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
                   CullStyle            == rhs.CullStyle;
            // clang-format on
        }
    };
    RenderState State;

    float ClearDepth = 1.f;

    constexpr bool operator==(const HnRenderShadowsTaskParams& rhs) const
    {
        return State == rhs.State && ClearDepth == rhs.ClearDepth;
    }
    constexpr bool operator!=(const HnRenderShadowsTaskParams& rhs) const
    {
        return !(*this == rhs);
    }
};

/// Renders shadow maps for shadow-casting lights
class HnRenderShadowsTask final : public HnTask
{
public:
    HnRenderShadowsTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnRenderShadowsTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;

    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

private:
    void PrepareClearDepthPSO(const HnRenderDelegate& RenderDelegate);
    void PrepareClearDepthVB(const HnRenderDelegate& RenderDelegate);

private:
    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    HnRenderPassState m_RPState;

    pxr::TfTokenVector            m_RenderTags;
    std::shared_ptr<HnRenderPass> m_RenderPass;

    RefCntAutoPtr<IPipelineState> m_ClearDepthPSO;
    RefCntAutoPtr<IBuffer>        m_ClearDepthVB;
    float                         m_ClearDepthValue = 0.f;

    // Combined geometry version (transform, visibility, etc.) last time we rendered shadows
    Uint32 m_LastGeometryVersion = ~0u;

    std::multimap<Uint32, HnLight*> m_LightsByShadowSlice;
};

} // namespace USD

} // namespace Diligent
