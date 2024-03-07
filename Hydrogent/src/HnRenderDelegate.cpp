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

#include "HnRenderDelegate.hpp"
#include "HnMesh.hpp"
#include "HnMaterial.hpp"
#include "HnCamera.hpp"
#include "HnLight.hpp"
#include "HnRenderPass.hpp"
#include "HnRenderParam.hpp"
#include "HnRenderPassState.hpp"
#include "DebugUtilities.hpp"
#include "GraphicsUtilities.h"
#include "HnRenderBuffer.hpp"
#include "Align.hpp"
#include "PlatformMisc.hpp"
#include "GLTFResourceManager.hpp"

#include "pxr/imaging/hd/material.h"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"

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


static RefCntAutoPtr<IBuffer> CreateFrameAttribsCB(IRenderDevice* pDevice, Uint32 Size)
{
    RefCntAutoPtr<IBuffer> FrameAttribsCB;
    CreateUniformBuffer(
        pDevice,
        Size,
        "PBR frame attribs CB",
        &FrameAttribsCB,
        USAGE_DEFAULT);
    return FrameAttribsCB;
}

static RefCntAutoPtr<IBuffer> CreatePrimitiveAttribsCB(IRenderDevice* pDevice)
{
    Uint64 Size  = 65536;
    USAGE  Usage = USAGE_DYNAMIC;
    if (pDevice->GetDeviceInfo().IsGLDevice())
    {
        // On OpenGL, use large USAGE_DEFAULT buffer and update it
        // with UpdateBuffer() method.
#ifndef DILIGENT_DEBUG
        Size = Uint64{512} << 10u;
#endif
        Usage = USAGE_DEFAULT;
    }
    // Allocate a large buffer to batch primitive draw calls
    RefCntAutoPtr<IBuffer> PrimitiveAttribsCB;
    CreateUniformBuffer(pDevice, Size, "PBR frame attribs CB", &PrimitiveAttribsCB, Usage);
    return PrimitiveAttribsCB;
}

static std::shared_ptr<USD_Renderer> CreateUSDRenderer(const HnRenderDelegate::CreateInfo& RenderDelegateCI,
                                                       IBuffer*                            pPrimitiveAttribsCB,
                                                       IObject*                            MaterialSRBCache)
{
    USD_Renderer::CreateInfo USDRendererCI;

    // Disable animation
    USDRendererCI.MaxJointCount = 0;
    // Use separate textures for metallic and roughness
    USDRendererCI.UseSeparateMetallicRoughnessTextures = true;
    // Default textures will be provided by the texture registry
    USDRendererCI.CreateDefaultTextures = false;
    // Enable clear coat support
    USDRendererCI.EnableClearCoat = true;

    USDRendererCI.MaxLightCount = RenderDelegateCI.MaxLightCount;

    USDRendererCI.ColorTargetIndex        = HnFramebufferTargets::GBUFFER_TARGET_SCENE_COLOR;
    USDRendererCI.MeshIdTargetIndex       = HnFramebufferTargets::GBUFFER_TARGET_MESH_ID;
    USDRendererCI.MotionVectorTargetIndex = HnFramebufferTargets::GBUFFER_TARGET_MOTION_VECTOR;
    USDRendererCI.NormalTargetIndex       = HnFramebufferTargets::GBUFFER_TARGET_NORMAL;
    USDRendererCI.BaseColorTargetIndex    = HnFramebufferTargets::GBUFFER_TARGET_BASE_COLOR;
    USDRendererCI.MaterialDataTargetIndex = HnFramebufferTargets::GBUFFER_TARGET_MATERIAL;
    USDRendererCI.IBLTargetIndex          = HnFramebufferTargets::GBUFFER_TARGET_IBL;
    static_assert(HnFramebufferTargets::GBUFFER_TARGET_COUNT == 7, "Unexpected number of G-buffer targets");

    HN_MATERIAL_TEXTURES_BINDING_MODE TextureBindingMode = RenderDelegateCI.TextureBindingMode;
    Uint32                            TexturesArraySize  = RenderDelegateCI.TexturesArraySize;
    if (TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC &&
        !RenderDelegateCI.pDevice->GetDeviceInfo().Features.BindlessResources)
    {
        LOG_WARNING_MESSAGE("This device does not support bindless resources. Switching to texture atlas mode");
        TextureBindingMode = HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS;
        TexturesArraySize  = 0;
    }

    if (TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC)
    {
        if (TexturesArraySize == 0)
            TexturesArraySize = 256;

        USDRendererCI.ShaderTexturesArrayMode   = USD_Renderer::SHADER_TEXTURE_ARRAY_MODE_DYNAMIC;
        USDRendererCI.MaterialTexturesArraySize = TexturesArraySize;
    }
    else if (TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
    {
        if (TexturesArraySize == 0)
            TexturesArraySize = 8;

        USDRendererCI.ShaderTexturesArrayMode   = USD_Renderer::SHADER_TEXTURE_ARRAY_MODE_STATIC;
        USDRendererCI.MaterialTexturesArraySize = TexturesArraySize;
        VERIFY_EXPR(MaterialSRBCache != nullptr);
        USDRendererCI.GetStaticShaderTextureIds = [MaterialSRBCache](const USD_Renderer::PSOKey& Key) {
            // User value in the PSO key is the shader indexing ID that identifies the static texture
            // indexing in the SRB cache.
            return HnMaterial::GetStaticShaderTextureIds(MaterialSRBCache, Key.GetUserValue());
        };
    }
    else
    {
        VERIFY_EXPR(RenderDelegateCI.TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_LEGACY);
        USDRendererCI.ShaderTexturesArrayMode = USD_Renderer::SHADER_TEXTURE_ARRAY_MODE_NONE;
    }

    static constexpr LayoutElement Inputs[] =
        {
            {0, 0, 3, VT_FLOAT32}, //float3 Pos     : ATTRIB0;
            {1, 1, 3, VT_FLOAT32}, //float3 Normal  : ATTRIB1;
            {2, 2, 2, VT_FLOAT32}, //float2 UV0     : ATTRIB2;
            {3, 3, 2, VT_FLOAT32}, //float2 UV1     : ATTRIB3;
        };

    const auto& DeviceInfo = RenderDelegateCI.pDevice->GetDeviceInfo();
    if (DeviceInfo.Features.NativeMultiDraw && (DeviceInfo.IsVulkanDevice() || DeviceInfo.IsGLDevice()))
        USDRendererCI.PrimitiveArraySize = RenderDelegateCI.MultiDrawBatchSize;

    USDRendererCI.InputLayout.LayoutElements = Inputs;
    USDRendererCI.InputLayout.NumElements    = _countof(Inputs);

    USDRendererCI.pPrimitiveAttribsCB = pPrimitiveAttribsCB;

    return std::make_shared<USD_Renderer>(RenderDelegateCI.pDevice, RenderDelegateCI.pRenderStateCache, RenderDelegateCI.pContext, USDRendererCI);
}

static RefCntAutoPtr<GLTF::ResourceManager> CreateResourceManager(const HnRenderDelegate::CreateInfo& CI)
{
    // Initial vertex and index counts are not important as the
    // real number of vertices and indices will be determined after
    // all meshes are synced for the first time.
    static constexpr Uint32 InitialVertexCount = 1024;
    static constexpr Uint32 InitialIndexCount  = 1024;

    GLTF::ResourceManager::CreateInfo ResMgrCI;

    ResMgrCI.IndexAllocatorCI.Desc    = {"Hydrogent index pool", sizeof(Uint32) * InitialIndexCount, BIND_INDEX_BUFFER, USAGE_DEFAULT};
    ResMgrCI.IndexAllocatorCI.MaxSize = Uint64{1024} << Uint64{20};

    ResMgrCI.DefaultPoolDesc.VertexCount = InitialVertexCount;
    ResMgrCI.DefaultPoolDesc.Usage       = USAGE_DEFAULT;

    if (CI.TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS)
    {
        Uint32 TextureAtlasDim = CI.TextureAtlasDim;

        if (TextureAtlasDim != 0)
        {
            constexpr Uint32 MinAtlasDim = 512;
            constexpr Uint32 MaxAtlasDim = 16384;
            if (TextureAtlasDim < MinAtlasDim)
            {
                LOG_ERROR_MESSAGE("Texture atlas dimension (", TextureAtlasDim, ") must be at least ", MinAtlasDim);
                TextureAtlasDim = MinAtlasDim;
            }
            else if (TextureAtlasDim > MaxAtlasDim)
            {
                LOG_ERROR_MESSAGE("Texture atlas dimension (", TextureAtlasDim, ") must be at most ", MaxAtlasDim);
                TextureAtlasDim = MaxAtlasDim;
            }
            if (!IsPowerOfTwo(TextureAtlasDim))
            {
                LOG_ERROR_MESSAGE("Texture atlas dimension (", TextureAtlasDim, ") must be a power of two");
                const auto MSB  = PlatformMisc::GetMSB(TextureAtlasDim);
                TextureAtlasDim = (MSB >= 13) ? 16384 : (1 << (MSB + 1));
            }
        }
        else
        {
            TextureAtlasDim = 2048;
        }

        ResMgrCI.DefaultAtlasDesc.Desc.Name      = "Hydrogent texture atlas";
        ResMgrCI.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
        ResMgrCI.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
        ResMgrCI.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
        ResMgrCI.DefaultAtlasDesc.Desc.Width     = TextureAtlasDim;
        ResMgrCI.DefaultAtlasDesc.Desc.Height    = TextureAtlasDim;
        ResMgrCI.DefaultAtlasDesc.Desc.MipLevels = 6;
        // Since texture atlas is resized only once, we can add one
        // slice at a time to avoid wasting memory.
        ResMgrCI.DefaultAtlasDesc.ExtraSliceCount = 1;

        ResMgrCI.DefaultAtlasDesc.MinAlignment = 64;
    }

    return GLTF::ResourceManager::Create(CI.pDevice, ResMgrCI);
}

HnRenderDelegate::HnRenderDelegate(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pContext{CI.pContext},
    m_pRenderStateCache{CI.pRenderStateCache},
    m_ResourceMgr{CreateResourceManager(CI)},
    m_PrimitiveAttribsCB{CreatePrimitiveAttribsCB(CI.pDevice)},
    m_MaterialSRBCache{HnMaterial::CreateSRBCache()},
    m_USDRenderer{CreateUSDRenderer(CI, m_PrimitiveAttribsCB, m_MaterialSRBCache)},
    m_FrameAttribsCB{CreateFrameAttribsCB(CI.pDevice, m_USDRenderer->GetPRBFrameAttribsSize())},
    m_TextureRegistry{CI.pDevice, CI.TextureAtlasDim != 0 ? m_ResourceMgr : RefCntAutoPtr<GLTF::ResourceManager>{}},
    m_RenderParam{std::make_unique<HnRenderParam>(CI.UseVertexPool, CI.UseIndexPool, CI.TextureBindingMode)},
    m_MeshAttribsAllocator{GetRawAllocator(), sizeof(HnMesh::Attributes), 64}
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
        HnMesh* Mesh = HnMesh::Create(TypeId, RPrimId, *this, RPrimUID);
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
        HnMaterial* Mat = HnMaterial::CreateFallback(m_TextureRegistry, *m_USDRenderer);
        {
            std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
            m_Materials.emplace(Mat);
        }
        SPrim = Mat;
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
    m_ResourceMgr->UpdateVertexBuffers(m_pDevice, m_pContext);
    m_ResourceMgr->UpdateIndexBuffer(m_pDevice, m_pContext);

    m_TextureRegistry.Commit(m_pContext);

    {
        const auto MaterialVersion = m_RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::Material);
        if (m_MaterialResourcesVersion != MaterialVersion)
        {
            std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
            for (auto* pMat : m_Materials)
            {
                pMat->UpdateSRB(*this);
            }
            for (auto* pMat : m_Materials)
            {
                pMat->BindPrimitiveAttribsBuffer(*this);
            }

            m_MaterialResourcesVersion = MaterialVersion;
        }
    }

    {
        const auto MeshVersion = m_RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshGeometry);
        if (m_MeshResourcesVersion != MeshVersion)
        {
            std::lock_guard<std::mutex> Guard{m_MeshesMtx};
            for (auto* pMesh : m_Meshes)
            {
                pMesh->CommitGPUResources(*this);
            }
            m_MeshResourcesVersion = MeshVersion;
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

void HnRenderDelegate::SetDebugView(PBR_Renderer::DebugViewType DebugView)
{
    m_RenderParam->SetDebugView(DebugView);
}

void HnRenderDelegate::SetRenderMode(HN_RENDER_MODE RenderMode)
{
    m_RenderParam->SetRenderMode(RenderMode);
}

void HnRenderDelegate::SetSelectedRPrimId(const pxr::SdfPath& RPrimID)
{
    m_RenderParam->SetSelectedPrimId(RPrimID);
}

} // namespace USD

} // namespace Diligent
