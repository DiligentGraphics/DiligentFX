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
#include "HnRenderParam.hpp"
#include "DebugUtilities.hpp"
#include "GraphicsUtilities.h"
#include "HnRenderBuffer.hpp"
#include "Align.hpp"
#include "GLTFResourceManager.hpp"

#include "pxr/imaging/hd/material.h"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"

} // namespace HLSL

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
    pxr::HdPrimTypeTokens->light,
    pxr::HdPrimTypeTokens->camera,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedBPrimTypes =
{
    pxr::HdPrimTypeTokens->renderBuffer
};
// clang-format on


static RefCntAutoPtr<IBuffer> CreateFrameAttribsCB(IRenderDevice* pDevice)
{
    RefCntAutoPtr<IBuffer> FrameAttribsCB;
    CreateUniformBuffer(
        pDevice,
        sizeof(HLSL::PBRFrameAttribs),
        "PBR frame attribs CB",
        &FrameAttribsCB,
        USAGE_DEFAULT);
    return FrameAttribsCB;
}

static RefCntAutoPtr<IBuffer> CreatePrimitiveAttribsCB(IRenderDevice* pDevice)
{
    // Allocate a large buffer to batch primitive draw calls
    RefCntAutoPtr<IBuffer> PrimitiveAttribsCB;
    CreateUniformBuffer(
        pDevice,
        65536,
        "PBR frame attribs CB",
        &PrimitiveAttribsCB,
        USAGE_DYNAMIC);
    return PrimitiveAttribsCB;
}

static std::shared_ptr<USD_Renderer> CreateUSDRenderer(IRenderDevice*     pDevice,
                                                       IRenderStateCache* pRenderStateCache,
                                                       IDeviceContext*    pContext,
                                                       IBuffer*           pPrimitiveAttribsCB,
                                                       bool               UseImmutableSamplers)
{
    USD_Renderer::CreateInfo USDRendererCI;

    // Use samplers from texture views
    USDRendererCI.UseImmutableSamplers = UseImmutableSamplers;
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

    USDRendererCI.pPrimitiveAttribsCB = pPrimitiveAttribsCB;

    return std::make_shared<USD_Renderer>(pDevice, pRenderStateCache, pContext, USDRendererCI);
}

static RefCntAutoPtr<GLTF::ResourceManager> CreateResourceManager(IRenderDevice* pDevice)
{
    // Initial vertex and index counts are not important as the
    // real number of vertices and indices will be determined after
    // all meshes are synced for the first time.
    static constexpr Uint32 InitialVertexCount = 1024;
    static constexpr Uint32 InitialIndexCount  = 1024;

    GLTF::ResourceManager::CreateInfo ResMgrCI;

    ResMgrCI.IndexAllocatorCI.Desc        = {"Hydrogent index pool", sizeof(Uint32) * InitialIndexCount, BIND_INDEX_BUFFER, USAGE_DEFAULT};
    ResMgrCI.IndexAllocatorCI.VirtualSize = Uint64{1024} << Uint64{20};

    ResMgrCI.DefaultPoolDesc.VertexCount = InitialVertexCount;
    ResMgrCI.DefaultPoolDesc.Usage       = USAGE_DEFAULT;

    ResMgrCI.DefaultAtlasDesc.Desc.Name      = "Hydrogent texture atlas";
    ResMgrCI.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    ResMgrCI.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
    ResMgrCI.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
    ResMgrCI.DefaultAtlasDesc.Desc.Width     = 4096;
    ResMgrCI.DefaultAtlasDesc.Desc.Height    = 4096;
    ResMgrCI.DefaultAtlasDesc.Desc.MipLevels = 6;
    // Double the number of slices when resizing the atlas
    ResMgrCI.DefaultAtlasDesc.ExtraSliceCount = 0;

    return GLTF::ResourceManager::Create(pDevice, ResMgrCI);
}

HnRenderDelegate::HnRenderDelegate(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pContext{CI.pContext},
    m_pRenderStateCache{CI.pRenderStateCache},
    m_ResourceMgr{CreateResourceManager(CI.pDevice)},
    m_FrameAttribsCB{CreateFrameAttribsCB(CI.pDevice)},
    m_PrimitiveAttribsCB{CreatePrimitiveAttribsCB(CI.pDevice)},
    m_USDRenderer{CreateUSDRenderer(CI.pDevice, CI.pRenderStateCache, CI.pContext, m_PrimitiveAttribsCB, CI.UseTextureAtlas)},
    m_PrimitiveAttribsAlignedOffset{AlignUp(Uint32{sizeof(HLSL::PBRPrimitiveAttribs)}, CI.pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment)},
    m_TextureRegistry{CI.pDevice, CI.UseTextureAtlas ? m_ResourceMgr : nullptr},
    m_RenderParam{std::make_unique<HnRenderParam>(CI.UseVertexPool, CI.UseIndexPool, CI.UseTextureAtlas)}
{
}

HnRenderDelegate::~HnRenderDelegate()
{
}

pxr::HdRenderParam* HnRenderDelegate::GetRenderParam() const
{
    return m_RenderParam.get();
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
        HnLight* Light = HnLight::Create(SPrimId);
        {
            std::lock_guard<std::mutex> Guard{m_LightsMtx};
            m_Lights.emplace(Light);
        }
        SPrim = Light;
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
    if (dynamic_cast<HnMaterial*>(SPrim) != nullptr)
    {
        std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
        m_Materials.erase(static_cast<HnMaterial*>(SPrim));
    }
    else if (dynamic_cast<HnLight*>(SPrim) != nullptr)
    {
        std::lock_guard<std::mutex> Guard{m_LightsMtx};
        m_Lights.erase(static_cast<HnLight*>(SPrim));
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
    m_ResourceMgr->UpdateAllResources(m_pDevice, m_pContext);

    m_TextureRegistry.Commit(m_pContext);

    {
        std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
        for (auto* pMat : m_Materials)
        {
            pMat->UpdateSRB(m_pDevice, *m_USDRenderer, m_FrameAttribsCB);
        }
    }

    {
        std::lock_guard<std::mutex> Guard{m_MeshesMtx};
        for (auto* pMesh : m_Meshes)
        {
            pMesh->CommitGPUResources(*this);
        }
    }
}

const pxr::SdfPath* HnRenderDelegate::GetRPrimId(Uint32 UID) const
{
    std::lock_guard<std::mutex> Guard{m_RPrimUIDToSdfPathMtx};

    auto it = m_RPrimUIDToSdfPath.find(UID);
    return it != m_RPrimUIDToSdfPath.end() ? &it->second : nullptr;
}

HnRenderDelegateMemoryStats HnRenderDelegate::GetMemoryStats() const
{
    HnRenderDelegateMemoryStats MemoryStats;

    const BufferSuballocatorUsageStats  IndexUsage  = m_ResourceMgr->GetIndexBufferUsageStats();
    const VertexPoolUsageStats          VertexUsage = m_ResourceMgr->GetVertexPoolUsageStats();
    const DynamicTextureAtlasUsageStats AtlasUsage  = m_ResourceMgr->GetAtlasUsageStats();

    MemoryStats.IndexPool.CommittedSize   = IndexUsage.CommittedSize;
    MemoryStats.IndexPool.UsedSize        = IndexUsage.UsedSize;
    MemoryStats.IndexPool.AllocationCount = IndexUsage.AllocationCount;

    MemoryStats.VertexPool.CommittedSize        = VertexUsage.CommittedMemorySize;
    MemoryStats.VertexPool.UsedSize             = VertexUsage.UsedMemorySize;
    MemoryStats.VertexPool.AllocationCount      = VertexUsage.AllocationCount;
    MemoryStats.VertexPool.AllocatedVertexCount = VertexUsage.AllocatedVertexCount;

    MemoryStats.Atlas.CommittedSize   = AtlasUsage.CommittedSize;
    MemoryStats.Atlas.AllocationCount = AtlasUsage.AllocationCount;
    MemoryStats.Atlas.TotalTexels     = AtlasUsage.TotalArea;
    MemoryStats.Atlas.AllocatedTexels = AtlasUsage.AllocatedArea;

    return MemoryStats;
}

} // namespace USD

} // namespace Diligent
