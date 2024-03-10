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

struct HnFrameRenderTargets;

struct HnProcessSelectionTaskParams
{
    float MaximumDistance = 4.0f;

    constexpr bool operator==(const HnProcessSelectionTaskParams& rhs) const
    {
        return MaximumDistance == rhs.MaximumDistance;
    }
};

/// Processes selection depth buffer with the jump-flood algorithm.
// https://blog.demofox.org/2016/02/29/fast-voronoi-diagrams-and-distance-dield-textures-on-the-gpu-with-the-jump-flooding-algorithm/
// https://bgolus.medium.com/the-quest-for-very-wide-outlines-ba82ed442cd9
class HnProcessSelectionTask final : public HnTask
{
public:
    HnProcessSelectionTask(pxr::HdSceneDelegate* ParamsDelegate, const pxr::SdfPath& Id);
    ~HnProcessSelectionTask();

    virtual void Sync(pxr::HdSceneDelegate* Delegate,
                      pxr::HdTaskContext*   TaskCtx,
                      pxr::HdDirtyBits*     DirtyBits) override final;

    virtual void Prepare(pxr::HdTaskContext* TaskCtx,
                         pxr::HdRenderIndex* RenderIndex) override final;


    virtual void Execute(pxr::HdTaskContext* TaskCtx) override final;

private:
    void PrepareTechniques(TEXTURE_FORMAT RTVFormat);
    void PrepareSRBs(const HnFrameRenderTargets& FrameTargets);

private:
    Uint32 m_NumJFIterations = 3;

    pxr::HdRenderIndex* m_RenderIndex = nullptr;

    RefCntAutoPtr<IBuffer> m_ConstantsCB;

    // Processes selected objects depth buffer and for each valid location
    // writes its coordinates into the output buffer.
    struct InitClosestLocationTech
    {
        RefCntAutoPtr<IPipelineState>         PSO;
        RefCntAutoPtr<IShaderResourceBinding> SRB;
        struct ShaderVariables
        {
            IShaderResourceVariable* SelectionDepth = nullptr;

            constexpr explicit operator bool() const
            {
                return SelectionDepth != nullptr;
            }
        } Vars;

        explicit operator bool() const
        {
            return PSO && SRB;
        }

        bool IsDirty = true;
    } m_InitTech;

    // Jump-flood algorithm iteration: samples the previous closest location
    // with the specified offset and writes the updated closest location to the
    // output buffer.
    struct UpdateClosestLocationTech
    {
        RefCntAutoPtr<IPipelineState> PSO;
        struct ShaderResources
        {
            RefCntAutoPtr<IShaderResourceBinding> SRB;
            struct ShaderVariables
            {
                IShaderResourceVariable* SrcClosestLocation = nullptr;

                constexpr explicit operator bool() const
                {
                    return SrcClosestLocation != nullptr;
                }
            } Vars;
        };
        std::array<ShaderResources, 2> Res; // Ping-pong

        explicit operator bool() const
        {
            return PSO && Res[0].SRB && Res[1].SRB;
        }

        bool IsDirty = true;
    } m_UpdateTech;

    pxr::SdfPath m_SelectedPrimId;
};

} // namespace USD

} // namespace Diligent
