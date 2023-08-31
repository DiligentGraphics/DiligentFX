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

#include "HnMaterial.hpp"
#include "HnRenderDelegate.hpp"

#include "pxr/imaging/hd/sceneDelegate.h"

#include "DebugUtilities.hpp"

namespace Diligent
{

namespace USD
{

std::shared_ptr<HnMaterial> HnMaterial::Create(const pxr::SdfPath& id)
{
    return std::shared_ptr<HnMaterial>(new HnMaterial{id});
}

HnMaterial::HnMaterial(const pxr::SdfPath& id) :
    pxr::HdMaterial{id}
{
}

HnMaterial::~HnMaterial()
{
}

void HnMaterial::Sync(pxr::HdSceneDelegate* SceneDelegate,
                      pxr::HdRenderParam*   RenderParam,
                      pxr::HdDirtyBits*     DirtyBits)
{
    if (*DirtyBits == pxr::HdMaterial::Clean)
        return;

    pxr::VtValue vtMat = SceneDelegate->GetMaterialResource(GetId());
    if (vtMat.IsHolding<pxr::HdMaterialNetworkMap>())
    {
        const pxr::HdMaterialNetworkMap& hdNetworkMap = vtMat.UncheckedGet<pxr::HdMaterialNetworkMap>();
        if (!hdNetworkMap.terminals.empty() && !hdNetworkMap.map.empty())
        {
            try
            {
                m_Network = HnMaterialNetwork{GetId(), hdNetworkMap};
            }
            catch (const std::runtime_error& err)
            {
                LOG_ERROR_MESSAGE("Failed to create material network for material ", GetId(), ": ", err.what());
                m_Network = {};
            }
            catch (...)
            {
                LOG_ERROR_MESSAGE("Failed to create material network for material ", GetId(), ": unknown error");
                m_Network = {};
            }
        }

        HnTextureRegistry& TexRegistry = static_cast<HnRenderDelegate*>(SceneDelegate->GetRenderIndex().GetRenderDelegate())->GetTextureRegistry();
        AllocateTextures(TexRegistry);
    }

    *DirtyBits = HdMaterial::Clean;
}

void HnMaterial::AllocateTextures(HnTextureRegistry& TexRegistry)
{
    for (const HnMaterialNetwork::TextureDescriptor& TexDescriptor : m_Network.GetTextures())
    {
        if (auto pTex = TexRegistry.Allocate(TexDescriptor.TextureId))
        {
            m_Textures[TexDescriptor.Name] = pTex;
        }
    }
}

const HnTextureRegistry::TextureHandle* HnMaterial::GetTexture(const pxr::TfToken& Name) const
{
    auto tex_it = m_Textures.find(Name);
    return tex_it != m_Textures.end() ? tex_it->second.get() : nullptr;
}

pxr::HdDirtyBits HnMaterial::GetInitialDirtyBitsMask() const
{
    return pxr::HdMaterial::AllDirty;
}

} // namespace USD

} // namespace Diligent
