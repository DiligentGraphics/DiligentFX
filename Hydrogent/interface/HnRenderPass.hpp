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
class HnMesh;
class HnMaterial;

struct HnMeshRenderParams
{
    float4x4 Transform = float4x4::Identity();
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
    void SetMeshRenderParams(const HnMeshRenderParams& Params);

    using SupportedVertexInputsSetType = std::unordered_set<pxr::TfToken, pxr::TfToken::HashFunctor>;
    static SupportedVertexInputsSetType GetSupportedVertexInputs(const HnMaterial* Material);
    static PBR_Renderer::PSO_FLAGS      GetMaterialPSOFlags(const HnMaterial& Material);

    enum DRAW_LIST_ITEM_DIRTY_FLAGS : Uint32
    {
        DRAW_LIST_ITEM_DIRTY_FLAG_NONE      = 0u,
        DRAW_LIST_ITEM_DIRTY_FLAG_PSO       = 1 << 0u,
        DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA = 1 << 0u,
        DRAW_LIST_ITEM_DIRTY_FLAG_LAST      = DRAW_LIST_ITEM_DIRTY_FLAG_MESH_DATA,
        DRAW_LIST_ITEM_DIRTY_FLAG_ALL       = DRAW_LIST_ITEM_DIRTY_FLAG_LAST * 2 - 1
    };

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
        const HnDrawItem& DrawItem;
        const HnMesh&     Mesh;
        const HnMaterial& Material;

        IPipelineState* pPSO = nullptr;

        // Unique ID that identifies the combination of render states used to render the draw item
        // (PSO, SRB, vertex and index buffers)
        // NB: this member should go after the pPSO member for better cache locality.
        Uint32 RenderStateID = 0;

        IBuffer* IndexBuffer = nullptr;
        Uint32   StartIndex  = 0;
        Uint32   NumVertices = 0;

        std::array<IBuffer*, 4> VertexBuffers    = {};
        Uint32                  NumVertexBuffers = 0;

        PBR_Renderer::PSO_FLAGS PSOFlags = PBR_Renderer::PSO_FLAG_NONE;

        // Primitive attributes shader data size computed from the used PSO flags.
        // Note: unshaded (aka wireframe/point) rendering modes don't use any textures, so the shader data
        //       is smaller than that for the shaded mode.
        Uint32 ShaderAttribsDataSize = 0;

        // Primitive attributes buffer range computed from all material PSO flags.
        // Note: it is always greater than or equal to ShaderAttribsDataAlignedSize.
        Uint32 ShaderAttribsBufferRange = 0;

        Uint32 Version = 0;

        float4x4 PrevTransform = float4x4::Identity();

        explicit DrawListItem(const HnDrawItem& Item) noexcept;

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
    HnMeshRenderParams m_RenderParams;

    HN_RENDER_MODE              m_RenderMode = HN_RENDER_MODE_SOLID;
    PBR_Renderer::DebugViewType m_DebugView  = PBR_Renderer::DebugViewType::None;

    // All draw items in the collection returned by the render index.
    pxr::HdRenderIndex::HdDrawItemPtrVector m_DrawItems;
    // Only selected/unselected draw items in the collection.
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

    std::vector<Uint8> m_PrimitiveAttribsData;
    std::vector<Uint8> m_ScratchSpace;

    pxr::SdfPath m_SelectedPrimId             = {};
    unsigned int m_CollectionVersion          = ~0u;
    unsigned int m_RprimRenderTagVersion      = ~0u;
    unsigned int m_TaskRenderTagsVersion      = ~0u;
    unsigned int m_GeomSubsetDrawItemsVersion = ~0u;
    unsigned int m_MeshVersion                = ~0u;

    DRAW_LIST_ITEM_DIRTY_FLAGS m_DrawListItemsDirtyFlags = DRAW_LIST_ITEM_DIRTY_FLAG_ALL;

    pxr::TfTokenVector m_RenderTags;
    pxr::TfToken       m_MaterialTag;
};
DEFINE_FLAG_ENUM_OPERATORS(HnRenderPass::DRAW_LIST_ITEM_DIRTY_FLAGS)

} // namespace USD

} // namespace Diligent
