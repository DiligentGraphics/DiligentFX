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

#include "pxr/imaging/hd/types.h"
#include "pxr/imaging/hd/renderPass.h"

#include "HnTypes.hpp"

namespace Diligent
{

namespace USD
{

class HnMesh;
class HnMaterial;

struct HnMeshRenderParams
{
    HN_RENDER_MODE RenderMode = HN_RENDER_MODE_SOLID;

    int   DebugView         = 0;
    float OcclusionStrength = 1;
    float EmissionScale     = 1;
    float IBLScale          = 1;

    float4 WireframeColor = float4(1, 1, 1, 1);

    float4x4 Transform = float4x4::Identity();

    pxr::SdfPath SelectedPrimId;
};

/// Hydra render pass implementation in Hydrogent.
class HnRenderPass final : public pxr::HdRenderPass
{
public:
    static pxr::HdRenderPassSharedPtr Create(pxr::HdRenderIndex*           pIndex,
                                             const pxr::HdRprimCollection& Collection);

    HnRenderPass(pxr::HdRenderIndex*           pIndex,
                 const pxr::HdRprimCollection& Collection);

    void SetMeshRenderParams(const HnMeshRenderParams& Params)
    {
        m_Params = Params;
    }

protected:
    // Virtual API: Execute the buckets corresponding to renderTags;
    // renderTags.empty() implies execute everything.
    virtual void _Execute(const pxr::HdRenderPassStateSharedPtr& RPState,
                          const pxr::TfTokenVector&              Tags) override final;

    virtual void _MarkCollectionDirty() override final;

private:
    void UpdateDrawItems(const pxr::TfTokenVector& RenderTags);

    struct RenderState;
    void RenderMesh(RenderState&      State,
                    const HnMesh&     Mesh,
                    const HnMaterial& Material);

private:
    HnMeshRenderParams m_Params;

    pxr::HdRenderIndex::HdDrawItemPtrVector m_DrawItems;

    unsigned int m_CollectionVersion     = ~0u;
    unsigned int m_RprimRenderTagVersion = ~0u;
    unsigned int m_TaskRenderTagsVersion = ~0u;

    pxr::TfTokenVector m_RenderTags;
    pxr::TfToken       m_MaterialTag;
};

} // namespace USD

} // namespace Diligent
