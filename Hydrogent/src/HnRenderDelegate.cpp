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
#include "HnCamera.hpp"
#include "HnLight.hpp"
#include "HnRenderPass.hpp"
#include "DebugUtilities.hpp"
#include "GraphicsUtilities.h"
#include "HnRenderBuffer.hpp"

#include "pxr/imaging/hd/material.h"

namespace Diligent
{

namespace USD
{


namespace HLSL
{

namespace
{
#include "Shaders/Common/public/BasicStructures.fxh"
} // namespace

} // namespace HLSL

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
    pxr::HdPrimTypeTokens->light,
    pxr::HdPrimTypeTokens->camera,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedBPrimTypes =
{
    pxr::HdPrimTypeTokens->renderBuffer
};
// clang-format on


HnRenderDelegate::HnRenderDelegate(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pContext{CI.pContext},
    m_pRenderStateCache{CI.pRenderStateCache},
    m_CameraAttribsCB{
        [](IRenderDevice* pDevice) {
            RefCntAutoPtr<IBuffer> CameraAttribsCB;
            CreateUniformBuffer(
                pDevice,
                sizeof(HLSL::CameraAttribs),
                "Camera Attribs CB",
                &CameraAttribsCB,
                USAGE_DEFAULT);
            return CameraAttribsCB;
        }(CI.pDevice),
    },
    m_LightAttribsCB{CI.pLightAttribs},
    m_USDRenderer{
        std::make_shared<USD_Renderer>(
            CI.pDevice,
            CI.pRenderStateCache,
            CI.pContext,
            []() {
                USD_Renderer::CreateInfo USDRendererCI;

                // Use samplers from texture views
                USDRendererCI.UseImmutableSamplers = false;
                // Disable animation
                USDRendererCI.MaxJointCount = 0;
                // Use separate textures for metallic and roughness
                USDRendererCI.UseSeparateMetallicRoughnessTextures = true;

                static constexpr LayoutElement Inputs[] =
                    {
                        {0, 0, 3, VT_FLOAT32}, //float3 Pos     : ATTRIB0;
                        {1, 1, 3, VT_FLOAT32}, //float3 Normal  : ATTRIB1;
                        {2, 2, 2, VT_FLOAT32}, //float2 UV0     : ATTRIB2;
                        {3, 3, 2, VT_FLOAT32}, //float2 UV1     : ATTRIB3;
                    };

                USDRendererCI.InputLayout.LayoutElements = Inputs;
                USDRendererCI.InputLayout.NumElements    = _countof(Inputs);

                return USDRendererCI;
            }()),
    },
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

pxr::HdRenderPassSharedPtr HnRenderDelegate::CreateRenderPass(pxr::HdRenderIndex*           Index,
                                                              const pxr::HdRprimCollection& Collection)
{
    return HnRenderPass::Create(Index, Collection);
}


pxr::HdInstancer* HnRenderDelegate::CreateInstancer(pxr::HdSceneDelegate* Delegate,
                                                    const pxr::SdfPath&   Id)
{
    return nullptr;
}

void HnRenderDelegate::DestroyInstancer(pxr::HdInstancer* Instancer)
{
}

pxr::HdRprim* HnRenderDelegate::CreateRprim(const pxr::TfToken& TypeId,
                                            const pxr::SdfPath& RPrimId)
{
    pxr::HdRprim* RPrim    = nullptr;
    const Uint32  RPrimUID = m_RPrimNextUID.fetch_add(1);
    if (TypeId == pxr::HdPrimTypeTokens->mesh)
    {
        HnMesh* Mesh = HnMesh::Create(TypeId, RPrimId, RPrimUID);
        {
            std::lock_guard<std::mutex> Guard{m_RPrimUIDToSdfPathMtx};
            m_RPrimUIDToSdfPath[RPrimUID] = RPrimId;
        }
        {
            std::lock_guard<std::mutex> Guard{m_MeshesMtx};
            m_Meshes.emplace(Mesh);
        }
        RPrim = Mesh;
    }
    else
    {
        UNEXPECTED("Unexpected Rprim Type: ", TypeId.GetText());
    }

    return RPrim;
}

void HnRenderDelegate::DestroyRprim(pxr::HdRprim* rPrim)
{
    {
        std::lock_guard<std::mutex> Guard{m_MeshesMtx};
        m_Meshes.erase(static_cast<HnMesh*>(rPrim));
    }
    delete rPrim;
}

pxr::HdSprim* HnRenderDelegate::CreateSprim(const pxr::TfToken& TypeId,
                                            const pxr::SdfPath& SPrimId)
{
    pxr::HdSprim* SPrim = nullptr;
    if (TypeId == pxr::HdPrimTypeTokens->material)
    {
        HnMaterial* Mat = HnMaterial::Create(SPrimId);
        {
            std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
            m_Materials.emplace(Mat);
        }
        SPrim = Mat;
    }
    else if (TypeId == pxr::HdPrimTypeTokens->camera)
    {
        SPrim = HnCamera::Create(SPrimId);
    }
    else if (TypeId == pxr::HdPrimTypeTokens->light)
    {
        SPrim = HnLight::Create(SPrimId);
    }
    else
    {
        UNEXPECTED("Unexpected Sprim Type: ", TypeId.GetText());
    }
    return SPrim;
}

pxr::HdSprim* HnRenderDelegate::CreateFallbackSprim(const pxr::TfToken& TypeId)
{
    pxr::HdSprim* SPrim = nullptr;
    if (TypeId == pxr::HdPrimTypeTokens->material)
    {
        SPrim = CreateSprim(TypeId, pxr::SdfPath{});
    }
    else if (TypeId == pxr::HdPrimTypeTokens->camera ||
             TypeId == pxr::HdPrimTypeTokens->light)
    {
        SPrim = nullptr;
    }
    else
    {
        UNEXPECTED("Unexpected Sprim Type: ", TypeId.GetText());
    }
    return SPrim;
}

void HnRenderDelegate::DestroySprim(pxr::HdSprim* SPrim)
{
    {
        std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
        m_Materials.erase(static_cast<HnMaterial*>(SPrim));
    }
    delete SPrim;
}

pxr::HdBprim* HnRenderDelegate::CreateBprim(const pxr::TfToken& TypeId,
                                            const pxr::SdfPath& BPrimId)
{
    pxr::HdBprim* BPrim = nullptr;
    if (TypeId == pxr::HdPrimTypeTokens->renderBuffer)
    {
        BPrim = new HnRenderBuffer{BPrimId};
    }
    else
    {
        UNEXPECTED("Unexpected Bprim Type: ", TypeId.GetText());
    }
    return BPrim;
}

pxr::HdBprim* HnRenderDelegate::CreateFallbackBprim(pxr::TfToken const& typeId)
{
    return nullptr;
}

void HnRenderDelegate::DestroyBprim(pxr::HdBprim* BPrim)
{
    delete BPrim;
}

void HnRenderDelegate::CommitResources(pxr::HdChangeTracker* tracker)
{
    m_TextureRegistry.Commit(m_pContext);

    {
        std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
        for (auto* pMat : m_Materials)
        {
            pMat->UpdateSRB(m_pDevice, *m_USDRenderer, m_CameraAttribsCB, m_LightAttribsCB);
        }
    }

    {
        std::lock_guard<std::mutex> Guard{m_MeshesMtx};
        for (auto* pMesh : m_Meshes)
        {
            pMesh->CommitGPUResources(m_pDevice);
        }
    }
}

const pxr::SdfPath* HnRenderDelegate::GetRPrimId(Uint32 UID) const
{
    std::lock_guard<std::mutex> Guard{m_RPrimUIDToSdfPathMtx};

    auto it = m_RPrimUIDToSdfPath.find(UID);
    return it != m_RPrimUIDToSdfPath.end() ? &it->second : nullptr;
}

} // namespace USD

} // namespace Diligent
