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

#include <memory>

#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

class BoundBoxRenderer;

namespace USD
{

class HnRenderPassState;

struct HnRenderBoundBoxTaskParams
{
    float4 Color = float4{1.0f, 1.0f, 1.0f, 1.0f};

    /// Pattern length in pixels.
    float PatternLength = 32;

    /// Pattern mask.
    /// Each bit defines whether the corresponding 1/32 section of the pattern is filled or not.
    /// For example, use 0x0000FFFFu to draw a dashed line.
    Uint32 PatternMask = 0xFFFFFFFFu;

    constexpr bool operator==(const HnRenderBoundBoxTaskParams& rhs) const
    {
        return (Color == rhs.Color &&
                PatternLength == rhs.PatternLength &&
                PatternMask == rhs.PatternMask);
    }

    constexpr bool operator!=(const HnRenderBoundBoxTaskParams& rhs) const
    {
        return !(*this == rhs);
    }
};

/// Renders Bounding Box.
class HnRenderBoundBoxTask final : public HnTask
{
public:
    HnRenderBoundBoxTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnRenderBoundBoxTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

private:
    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    pxr::TfToken               m_RenderPassName;
    HnRenderBoundBoxTaskParams m_Params;

    std::unique_ptr<BoundBoxRenderer> m_BoundBoxRenderer;

    bool m_RenderBoundBox = false;
};

} // namespace USD

} // namespace Diligent
