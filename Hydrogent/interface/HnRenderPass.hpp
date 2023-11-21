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

#include <unordered_set>

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/renderPass.h"

#include "../../PBR/interface/USD_Renderer.hpp"

#include "HnTypes.hpp"

namespace Diligent
{

namespace USD
{

class HnDrawItem;
class HnRenderPassState;
class HnMaterial;

struct HnMeshRenderParams
{
    HN_RENDER_MODE RenderMode = HN_RENDER_MODE_SOLID;

    int DebugViewMode = 0;

    float4x4 Transform = float4x4::Identity();

    pxr::SdfPath SelectedPrimId;
};

struct HnRenderPassParams
{
    enum class SelectionType
    {
        All,
        Unselected,
        Selected
    };
    SelectionType Selection = SelectionType::All;

    USD_Renderer::USD_PSO_FLAGS UsdPsoFlags = USD_Renderer::USD_PSO_FLAG_NONE;

    constexpr bool operator==(const HnRenderPassParams& rhs) const
    {
        return Selection == rhs.Selection && UsdPsoFlags == rhs.UsdPsoFlags;
    }
};

/// Hydra render pass implementation in Hydrogent.
class HnRenderPass final : public pxr::HdRenderPass
{
public:
    static pxr::HdRenderPassSharedPtr Create(pxr::HdRenderIndex*           pIndex,
                                             const pxr::HdRprimCollection& Collection);

    HnRenderPass(pxr::HdRenderIndex*           pIndex,
                 const pxr::HdRprimCollection& Collection);

    void SetParams(const HnRenderPassParams& Params);
    void SetMeshRenderParams(const HnMeshRenderParams& Params);

    using SupportedVertexInputsSetType = std::unordered_set<pxr::TfToken, pxr::TfToken::HashFunctor>;
    static SupportedVertexInputsSetType GetSupportedVertexInputs(const HnMaterial* Material);

    enum DRAW_ITEM_GPU_RES_DIRTY_FLAGS : Uint32
    {
        DRAW_ITEM_GPU_RES_DIRTY_FLAG_NONE     = 0u,
        DRAW_ITEM_GPU_RES_DIRTY_FLAG_GEOMETRY = 1 << 0u,
        DRAW_ITEM_GPU_RES_DIRTY_FLAG_PSO      = 1 << 1u,
        DRAW_ITEM_GPU_RES_DIRTY_FLAG_LAST     = DRAW_ITEM_GPU_RES_DIRTY_FLAG_PSO,
        DRAW_ITEM_GPU_RES_DIRTY_FLAG_ALL      = DRAW_ITEM_GPU_RES_DIRTY_FLAG_LAST * 2 - 1
    };

protected:
    // Virtual API: Execute the buckets corresponding to renderTags;
    // renderTags.empty() implies execute everything.
    virtual void _Execute(const pxr::HdRenderPassStateSharedPtr& RPState,
                          const pxr::TfTokenVector&              Tags) override final;

    virtual void _MarkCollectionDirty() override final;

private:
    void UpdateDrawItems(const pxr::TfTokenVector& RenderTags);
    void UpdateDrawItemsGPUResources(const HnRenderPassState& RPState);

    struct RenderState;
    void RenderPendingDrawItems(RenderState& State);

private:
    HnRenderPassParams m_Params;
    HnMeshRenderParams m_RenderParams;

    std::vector<HnDrawItem>        m_DrawItems;
    std::vector<const HnDrawItem*> m_PendingDrawItems;

    unsigned int m_CollectionVersion     = ~0u;
    unsigned int m_RprimRenderTagVersion = ~0u;
    unsigned int m_TaskRenderTagsVersion = ~0u;

    DRAW_ITEM_GPU_RES_DIRTY_FLAGS m_DrawItemsGPUResDirtyFlags = DRAW_ITEM_GPU_RES_DIRTY_FLAG_ALL;

    pxr::TfTokenVector m_RenderTags;
    pxr::TfToken       m_MaterialTag;
};
DEFINE_FLAG_ENUM_OPERATORS(HnRenderPass::DRAW_ITEM_GPU_RES_DIRTY_FLAGS)

} // namespace USD

} // namespace Diligent
