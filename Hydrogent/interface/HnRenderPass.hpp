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

#include <unordered_set>
#include <vector>
#include <array>

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/renderPass.h"

#include "../../PBR/interface/USD_Renderer.hpp"

#include "HnTypes.hpp"
#include "HnMesh.hpp"

namespace Diligent
{

namespace USD
{

class HnDrawItem;
class HnRenderPassState;
class HnMaterial;

struct HnRenderPassParams
{
    // Render pass name used to get the render pass state.
    pxr::TfToken Name;

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

    static const char* GetSelectionTypeString(SelectionType Type);
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

    const pxr::TfToken& GetName() const
    {
        return m_Params.Name;
    }

    using SupportedVertexInputsSetType = std::unordered_set<pxr::TfToken, pxr::TfToken::HashFunctor>;
    static SupportedVertexInputsSetType GetSupportedVertexInputs(const HnMaterial* Material);
    static PBR_Renderer::PSO_FLAGS      GetMaterialPSOFlags(const HnMaterial& Material);

    enum DRAW_LIST_ITEM_DIRTY_FLAGS : Uint32
    {
        DRAW_LIST_ITEM_DIRTY_FLAG_NONE      = 0u,
        DRAW_LIST_ITEM_DIRTY_FLAG_PSO       = 1u << 0u,
        DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA = 1u << 1u,
        DRAW_LIST_ITEM_DIRTY_FLAG_LAST      = DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA,
        DRAW_LIST_ITEM_DIRTY_FLAG_ALL       = DRAW_LIST_ITEM_DIRTY_FLAG_LAST * 2 - 1
    };

    void Execute(HnRenderPassState& RPState, const pxr::TfTokenVector& Tags);

protected:
    // Virtual API: Execute the buckets corresponding to renderTags;
    // renderTags.empty() implies execute everything.
    virtual void _Execute(const pxr::HdRenderPassStateSharedPtr& RPState,
                          const pxr::TfTokenVector&              Tags) override final;

    virtual void _MarkCollectionDirty() override final;

private:
    struct RenderState;

    struct DrawListItem
    {
        // NB: the order of members is optimized to match the order in which they
        //     are accessed in the _Execute method for better cache locality.

        const HnDrawItem& DrawItem;
        const HnMesh&     Mesh;
        const HnMaterial& Material;

        IPipelineState* pPSO = nullptr;

        const entt::entity MeshEntity;
        const float        MeshUID;

        // Unique ID that identifies the combination of render states used to render the draw item
        // (PSO, SRB, vertex and index buffers). It is used to batch draw calls into a multi-draw command.
        Uint32 RenderStateID : 28;
        Uint32 NumVertexBuffers : 4;

        // Mesh Geometry + Mesh Material version
        Uint32 Version = 0;

        Uint32 NumVertices = 0;
        Uint32 StartIndex  = 0;

        PBR_Renderer::PSO_FLAGS PSOFlags = PBR_Renderer::PSO_FLAG_NONE;

        float4x4 PrevTransform = float4x4::Identity();

        // Primitive attributes shader data size computed from the value of PSOFlags.
        // Note: unshaded (aka wireframe/point) rendering modes don't use any textures, so the shader data
        //       is smaller than that for the shaded mode.
        Uint32 ShaderAttribsDataSize = 0;

        // Primitive attributes buffer range used to set the cbPrimitiveAttribs buffer
        // in the material's SRB.
        Uint32 ShaderAttribsBufferRange = 0;

        IBuffer* IndexBuffer = nullptr;

        std::array<IBuffer*, 4> VertexBuffers = {};

        explicit DrawListItem(HnRenderDelegate& RenderDelegate, const HnDrawItem& Item) noexcept;

        operator bool() const noexcept
        {
            return pPSO != nullptr && NumVertices > 0;
        }
    };

    void UpdateDrawList(const pxr::TfTokenVector& RenderTags);
    void UpdateDrawListGPUResources(RenderState& State);
    void UpdateDrawListItemGPUResources(DrawListItem& ListItem, RenderState& State, DRAW_LIST_ITEM_DIRTY_FLAGS DirtyFlags);

    void RenderPendingDrawItems(RenderState& State);

    GraphicsPipelineDesc GetGraphicsDesc(const HnRenderPassState& RPState) const;

private:
    HnRenderPassParams m_Params;

    HN_RENDER_MODE              m_RenderMode = HN_RENDER_MODE_SOLID;
    PBR_Renderer::DebugViewType m_DebugView  = PBR_Renderer::DebugViewType::None;
    bool                        m_UseShadows = false;

    // All draw items in the collection returned by pRenderIndex->GetDrawItems().
    pxr::HdRenderIndex::HdDrawItemPtrVector m_DrawItems;

    // Only selected/unselected items from m_DrawItems.
    std::vector<DrawListItem> m_DrawList;

    struct PendingDrawItem
    {
        const DrawListItem& ListItem;
        const Uint32        BufferOffset;
        Uint32              DrawCount = 1;
    };

    // Draw list items to be rendered in the current batch.
    std::vector<PendingDrawItem> m_PendingDrawItems;
    // Rendering order of the draw list items sorted by the PSO.
    std::vector<Uint32> m_RenderOrder;

    // Scratch space prepare data for the primitive attributes buffer.
    std::vector<Uint8> m_PrimitiveAttribsData;

    // Scratch space for the MultiDraw/MultiDrawIndexed command items.
    std::vector<Uint8> m_ScratchSpace;

    pxr::SdfPath m_SelectedPrimId = {};
    struct GlobalAttribVersions
    {
        uint32_t Collection          = ~0u;
        uint32_t RprimRenderTag      = ~0u;
        uint32_t TaskRenderTags      = ~0u;
        uint32_t GeomSubsetDrawItems = ~0u;
        uint32_t MeshGeometry        = ~0u;
        uint32_t MeshMaterial        = ~0u;
        uint32_t Material            = ~0u;
    } m_GlobalAttribVersions;

    DRAW_LIST_ITEM_DIRTY_FLAGS m_DrawListItemsDirtyFlags = DRAW_LIST_ITEM_DIRTY_FLAG_ALL;

    pxr::TfTokenVector m_RenderTags;
    pxr::TfToken       m_MaterialTag;
};
DEFINE_FLAG_ENUM_OPERATORS(HnRenderPass::DRAW_LIST_ITEM_DIRTY_FLAGS)

} // namespace USD

} // namespace Diligent
