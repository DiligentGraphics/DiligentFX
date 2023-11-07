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

#include "HnTask.hpp"

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

namespace USD
{

class HnRenderPassState;

struct HnRenderAxesTaskParams
{
    float4x4 Transform = float4x4::Identity();

    float4 PositiveXColor{1.0f, 0.0f, 0.0f, 1.0f};
    float4 PositiveYColor{0.0f, 1.0f, 0.0f, 1.0f};
    float4 PositiveZColor{0.0f, 0.0f, 1.0f, 1.0f};
    float4 NegativeXColor{0.5f, 0.3f, 0.3f, 1.0f};
    float4 NegativeYColor{0.3f, 0.5f, 0.3f, 1.0f};
    float4 NegativeZColor{0.3f, 0.3f, 0.5f, 1.0f};

    constexpr bool operator==(const HnRenderAxesTaskParams& rhs) const
    {

        return Transform == rhs.Transform &&
            PositiveXColor == rhs.PositiveXColor &&
            PositiveYColor == rhs.PositiveYColor &&
            PositiveZColor == rhs.PositiveZColor &&
            NegativeXColor == rhs.NegativeXColor &&
            NegativeYColor == rhs.NegativeYColor &&
            NegativeZColor == rhs.NegativeZColor;
    }

    constexpr bool operator!=(const HnRenderAxesTaskParams& rhs) const
    {
        return !(*this == rhs);
    }
};

/// Renders coordinate axes.
class HnRenderAxesTask final : public HnTask
{
public:
    HnRenderAxesTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnRenderAxesTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

private:
    void PreparePSO(const HnRenderPassState& RPState);

private:
    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    HnRenderAxesTaskParams m_Params;

    bool m_ParamsAreDirty = true;

    RefCntAutoPtr<IPipelineState>         m_PSO;
    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    RefCntAutoPtr<IBuffer>                m_ConstantsCB;
};

} // namespace USD

} // namespace Diligent
