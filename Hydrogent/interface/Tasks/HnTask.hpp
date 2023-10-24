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

#include "pxr/imaging/hd/task.h"
#include "pxr/imaging/hd/renderIndex.h"

#include "../../../../DiligentCore/Platforms/Basic/interface/DebugUtilities.hpp"

namespace Diligent
{

struct ITextureView;

namespace USD
{

class HnRenderPassState;

/// Hydra task implementation in Hydrogent.
class HnTask : public pxr::HdTask
{
public:
    HnTask(const pxr::SdfPath& Id);

protected:
    std::shared_ptr<HnRenderPassState> GetRenderPassState(pxr::HdTaskContext* TaskCtx) const;

    static ITextureView* GetRenderBufferTarget(pxr::HdRenderIndex& RenderIndex, const pxr::SdfPath& RenderBufferId);
    ITextureView*        GetRenderBufferTarget(pxr::HdRenderIndex& RenderIndex, pxr::HdTaskContext* TaskCtx, const pxr::TfToken& Name) const;

    template <typename ParamType>
    bool GetTaskContextData(pxr::HdTaskContext* TaskCtx, const pxr::TfToken& Name, ParamType& Param) const
    {
        if (TaskCtx == nullptr)
        {
            UNEXPECTED("Task context is null");
            return false;
        }

        auto param_it = TaskCtx->find(Name);
        if (param_it == TaskCtx->end())
        {
            UNEXPECTED("Parameter '", Name, "' is not set in the task context");
            return false;
        }

        if (!param_it->second.IsHolding<ParamType>())
        {
            UNEXPECTED("Type ", param_it->second.GetTypeName(), " is not expected for parameter ", Name);
            return false;
        }

        Param = param_it->second.UncheckedGet<ParamType>();
        return true;
    }

    template <typename ParamType>
    bool GetTaskParams(pxr::HdSceneDelegate* Delegate, ParamType& Param) const
    {
        pxr::VtValue ParamsValue = Delegate->Get(GetId(), pxr::HdTokens->params);
        if (ParamsValue.IsHolding<ParamType>())
        {
            Param = ParamsValue.UncheckedGet<ParamType>();
            return true;
        }
        else
        {
            UNEXPECTED("Parameters type ", ParamsValue.GetTypeName(), " is not recognized by task ", GetId());
            return false;
        }
    }
};

} // namespace USD

} // namespace Diligent
