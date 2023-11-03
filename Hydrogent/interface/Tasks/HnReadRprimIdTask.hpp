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

#include <memory>

#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/GPUCompletionAwaitQueue.hpp"

namespace Diligent
{

namespace USD
{

struct HnReadRprimIdTaskParams
{
    bool   IsEnabled = false;
    Uint32 LocationX = 0;
    Uint32 LocationY = 0;

    constexpr bool operator==(const HnReadRprimIdTaskParams& rhs) const
    {
        // clang-format off
        return IsEnabled == rhs.IsEnabled &&
               LocationX == rhs.LocationX &&
               LocationY == rhs.LocationY;
        // clang-format on
    }

    constexpr bool operator!=(const HnReadRprimIdTaskParams& rhs) const
    {
        return !(*this == rhs);
    }
};

/// Reads the RPrim index from the mesh id target.
class HnReadRprimIdTask final : public HnTask
{
public:
    HnReadRprimIdTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnReadRprimIdTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

    static constexpr Uint32 InvalidMeshIndex = ~0u;

    /// Returns the mesh index that was read from the mesh id target last time the task was executed.
    /// If Mesh Id is not available, returns InvalidMeshIndex (~0u).
    Uint32 GetMeshIndex() const { return m_MeshIndex; }

private:
    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    using MeshIdReadBackQueueType = GPUCompletionAwaitQueue<RefCntAutoPtr<ITexture>>;
    std::unique_ptr<MeshIdReadBackQueueType> m_MeshIdReadBackQueue;

    HnReadRprimIdTaskParams m_Params;

    Uint32 m_MeshIndex = InvalidMeshIndex;
};

} // namespace USD

} // namespace Diligent
