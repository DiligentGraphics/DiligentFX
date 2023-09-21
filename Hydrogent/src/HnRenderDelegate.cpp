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

#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"
#include "DebugUtilities.hpp"

#include "pxr/imaging/hd/material.h"

namespace Diligent
{

namespace USD
{

std::unique_ptr<HnRenderDelegate> HnRenderDelegate::Create(const CreateInfo& CI)
{
    return std::make_unique<HnRenderDelegate>(CI);
}

// clang-format off
const pxr::TfTokenVector HnRenderDelegate::SupportedRPrimTypes =
{
    pxr::HdPrimTypeTokens->mesh,
    pxr::HdPrimTypeTokens->points,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedSPrimTypes =
{
    pxr::HdPrimTypeTokens->material,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedBPrimTypes =
{
};
// clang-format on

HnRenderDelegate::HnRenderDelegate(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pContext{CI.pContext},
    m_CameraAttribsCB{CI.pCameraAttribs},
    m_LightAttribsCB{CI.pLightAttribs},
    m_PBRRenderer{CI.PBRRenderer},
    m_TextureRegistry{CI.pDevice}
{
}

HnRenderDelegate::~HnRenderDelegate()
{
}

const pxr::TfTokenVector& HnRenderDelegate::GetSupportedRprimTypes() const
{
    return SupportedRPrimTypes;
}

const pxr::TfTokenVector& HnRenderDelegate::GetSupportedSprimTypes() const
{
    return SupportedSPrimTypes;
}


const pxr::TfTokenVector& HnRenderDelegate::GetSupportedBprimTypes() const
{
    return SupportedBPrimTypes;
}

pxr::HdResourceRegistrySharedPtr HnRenderDelegate::GetResourceRegistry() const
{
    return {};
}

pxr::HdRenderPassSharedPtr HnRenderDelegate::CreateRenderPass(pxr::HdRenderIndex*           index,
                                                              pxr::HdRprimCollection const& collection)
{
    return {};
}


pxr::HdInstancer* HnRenderDelegate::CreateInstancer(pxr::HdSceneDelegate* delegate,
                                                    pxr::SdfPath const&   id)
{
    return nullptr;
}

void HnRenderDelegate::DestroyInstancer(pxr::HdInstancer* instancer)
{
}

pxr::HdRprim* HnRenderDelegate::CreateRprim(pxr::TfToken const& typeId,
                                            pxr::SdfPath const& rprimId)
{
    auto it = m_Meshes.emplace(rprimId.GetString(), HnMesh::Create(typeId, rprimId, m_MeshUIDCounter.fetch_add(1)));

    m_MeshUIDToPrimId[it.first->second->GetUID()] = rprimId.GetString();
    return it.first->second.get();
}

void HnRenderDelegate::DestroyRprim(pxr::HdRprim* rPrim)
{
    m_Meshes.erase(rPrim->GetId().GetString());
}

pxr::HdSprim* HnRenderDelegate::CreateSprim(pxr::TfToken const& typeId,
                                            pxr::SdfPath const& sprimId)
{
    if (typeId == pxr::HdPrimTypeTokens->material)
    {
        auto it = m_Materials.emplace(sprimId.GetString(), HnMaterial::Create(sprimId));
        return it.first->second.get();
    }
    else
    {
        UNEXPECTED("Unexpected Sprim Type: ", typeId.GetText());
    }
    return nullptr;
}

pxr::HdSprim* HnRenderDelegate::CreateFallbackSprim(pxr::TfToken const& typeId)
{
    return nullptr;
}

void HnRenderDelegate::DestroySprim(pxr::HdSprim* sprim)
{
    if (auto* material = dynamic_cast<pxr::HdMaterial*>(sprim))
    {
        m_Materials.erase(material->GetId().GetString());
    }
    else if (sprim != nullptr)
    {
        UNEXPECTED("Unexpected Sprim Type: ", sprim->GetId().GetString());
    }
}

pxr::HdBprim* HnRenderDelegate::CreateBprim(pxr::TfToken const& typeId,
                                            pxr::SdfPath const& bprimId)
{
    return nullptr;
}

pxr::HdBprim* HnRenderDelegate::CreateFallbackBprim(pxr::TfToken const& typeId)
{
    return nullptr;
}

void HnRenderDelegate::DestroyBprim(pxr::HdBprim* bprim)
{
}

void HnRenderDelegate::CommitResources(pxr::HdChangeTracker* tracker)
{
    m_TextureRegistry.Commit(m_pContext);
    for (auto mat_it : m_Materials)
    {
        mat_it.second->UpdateSRB(m_pDevice, *m_PBRRenderer, m_CameraAttribsCB, m_LightAttribsCB);
    }
    for (auto mesh_it : m_Meshes)
    {
        mesh_it.second->CommitGPUResources(m_pDevice);
    }
}

const HnMaterial* HnRenderDelegate::GetMaterial(const char* Id) const
{
    auto it = m_Materials.find(Id);
    return it != m_Materials.end() ? it->second.get() : nullptr;
}

const char* HnRenderDelegate::GetMeshPrimId(Uint32 UID) const
{
    auto it = m_MeshUIDToPrimId.find(UID);
    return it != m_MeshUIDToPrimId.end() ? it->second.c_str() : nullptr;
}

} // namespace USD

} // namespace Diligent
