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

#include "HnRenderPass.hpp"
#include "HnRenderDelegate.hpp"

#include "pxr/imaging/hd/renderIndex.h"

namespace Diligent
{

namespace USD
{

pxr::HdRenderPassSharedPtr HnRenderPass::Create(pxr::HdRenderIndex*           pIndex,
                                                const pxr::HdRprimCollection& Collection)
{
    return pxr::HdRenderPassSharedPtr{new HnRenderPass{pIndex, Collection}};
}

HnRenderPass::HnRenderPass(pxr::HdRenderIndex*           pIndex,
                           const pxr::HdRprimCollection& Collection) :
    pxr::HdRenderPass{pIndex, Collection}
{}

void HnRenderPass::_Execute(pxr::HdRenderPassStateSharedPtr const& State,
                            pxr::TfTokenVector const&              Tags)
{
    UpdateDrawItems(Tags);

    pxr::HdRenderIndex* pRenderIndex    = GetRenderIndex();
    HnRenderDelegate*   pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());

    for (const pxr::HdDrawItem* pDrawItem : m_DrawItems)
    {
        if (!pDrawItem->GetVisible())
            continue;

        const pxr::SdfPath& RPrimID = pDrawItem->GetRprimID();
        if (auto& pMesh = pRenderDelegate->GetMesh(RPrimID))
        {
            //LOG_INFO_MESSAGE("RPrimID: ", RPrimID.GetText());
            //pMesh->Draw(State);
        }
    }
}

void HnRenderPass::_MarkCollectionDirty()
{
    // Force any cached data based on collection to be refreshed.
    m_CollectionVersion = ~0u;
}

void HnRenderPass::UpdateDrawItems(const pxr::TfTokenVector& RenderTags)
{
    pxr::HdRenderIndex* pRenderIndex    = GetRenderIndex();
    HnRenderDelegate*   pRenderDelegate = static_cast<HnRenderDelegate*>(pRenderIndex->GetRenderDelegate());

    const pxr::HdRprimCollection& Collection = GetRprimCollection();
    const pxr::HdChangeTracker&   Tracker    = pRenderIndex->GetChangeTracker();

    const unsigned int CollectionVersion     = Tracker.GetCollectionVersion(Collection.GetName());
    const unsigned int RprimRenderTagVersion = Tracker.GetRenderTagVersion();
    const unsigned int TaskRenderTagsVersion = Tracker.GetTaskRenderTagsVersion();
    //const unsigned int MaterialTagsVersion        = GetMaterialTagsVersion(pRenderIndex);
    //const unsigned int GeomSubsetDrawItemsVersion = GetGeomSubsetDrawItemsVersion(pRenderIndex);

    const bool CollectionChanged     = (m_CollectionVersion != CollectionVersion);
    const bool RprimRenderTagChanged = (m_RprimRenderTagVersion != RprimRenderTagVersion);
    //const bool MaterialTagsChanged        = (m_MaterialTagsVersion != MaterialTagsVersion);
    //const bool GeomSubsetDrawItemsChanged = (m_GeomSubsetDrawItemsVersion != GeomSubsetDrawItemsVersion);

    bool TaskRenderTagsChanged = false;
    if (m_TaskRenderTagsVersion != TaskRenderTagsVersion)
    {
        m_TaskRenderTagsVersion = TaskRenderTagsVersion;
        if (m_RenderTags != RenderTags)
        {
            m_RenderTags          = RenderTags;
            TaskRenderTagsChanged = true;
        }
    }

    if (CollectionChanged ||
        RprimRenderTagChanged ||
        //MaterialTagsChanged ||
        //GeomSubsetDrawItemsChanged ||
        TaskRenderTagsChanged)
    {
        //const HnRenderParam* const RenderParam = static_cast<HnRenderParam*>(pRenderIndex->GetRenderDelegate()->GetRenderParam());
        //if (RenderParam->HasMaterialTag(Collection.GetMaterialTag()))
        {
            m_DrawItems = GetRenderIndex()->GetDrawItems(Collection, RenderTags);
        }
        //else
        //{
        //    // There is no prim with the desired material tag.
        //    m_DrawItems.clear()
        //}
    }

    m_CollectionVersion     = CollectionVersion;
    m_RprimRenderTagVersion = RprimRenderTagVersion;
}


} // namespace USD

} // namespace Diligent
