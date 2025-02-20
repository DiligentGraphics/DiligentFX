/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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
#include "HnExtComputation.hpp"
#include "HnRenderPass.hpp"
#include "HnRenderParam.hpp"
#include "HnFrameRenderTargets.hpp"
#include "HnShadowMapManager.hpp"
#include "HnTextureRegistry.hpp"
#include "HnGeometryPool.hpp"

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

const pxr::TfTokenVector HnRenderDelegate::SupportedRPrimTypes = {
    pxr::HdPrimTypeTokens->mesh,
    pxr::HdPrimTypeTokens->points,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedSPrimTypes = {
    pxr::HdPrimTypeTokens->material,
    pxr::HdPrimTypeTokens->camera,
    //pxr::HdPrimTypeTokens->simpleLight,
    pxr::HdPrimTypeTokens->cylinderLight,
    pxr::HdPrimTypeTokens->diskLight,
    pxr::HdPrimTypeTokens->distantLight,
    pxr::HdPrimTypeTokens->rectLight,
    pxr::HdPrimTypeTokens->sphereLight,
    pxr::HdPrimTypeTokens->domeLight,
    pxr::HdPrimTypeTokens->extComputation,
};

const pxr::TfTokenVector HnRenderDelegate::SupportedBPrimTypes = {
    pxr::HdPrimTypeTokens->renderBuffer,
};

static RefCntAutoPtr<IBuffer> CreatePrimitiveAttribsCB(IRenderDevice* pDevice)
{
    Uint64 Size = 65536;

    // Don't do this as this degrades dynamic memory utilization!
    //Size += 8192; // Add extra space as buffer for the attributes buffer range.

    USAGE Usage = USAGE_DYNAMIC;
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
    CreateUniformBuffer(pDevice, Size, "PBR primitive attribs", &PrimitiveAttribsCB, Usage);
    return PrimitiveAttribsCB;
}

static RefCntAutoPtr<IBuffer> CreateJointsBuffer(IRenderDevice* pDevice, USD_Renderer::JOINTS_BUFFER_MODE Mode, Uint32& MaxJointCount)
{
    BufferDesc Desc;
    Desc.Name = "Joint transforms";
    Desc.Size = 65536;
    if (pDevice->GetDeviceInfo().IsGLDevice())
    {
        // On OpenGL, use USAGE_DEFAULT buffer and update it
        // with UpdateBuffer() method.
        Desc.Usage = USAGE_DEFAULT;
    }
    else
    {
        Desc.Usage          = USAGE_DYNAMIC;
        Desc.CPUAccessFlags = CPU_ACCESS_WRITE;
    }

    auto GetMaxAllowedJointCount = [](Uint64 BufferSize) {
        // MaxJointCount matrices * (this + last frames)
        return static_cast<Uint32>(BufferSize / sizeof(float4x4) / 2);
    };

    if (Mode == USD_Renderer::JOINTS_BUFFER_MODE_UNIFORM)
    {
        Desc.BindFlags = BIND_UNIFORM_BUFFER;

        const Uint32 MaxAllowedJointCount = GetMaxAllowedJointCount(Desc.Size);
        if (MaxJointCount > MaxAllowedJointCount)
        {
            LOG_WARNING_MESSAGE("The maximum number of joints (", MaxJointCount, ") exceeds the limit (", MaxAllowedJointCount, ") for the uniform buffer. Clamping the number of joints.");
            MaxJointCount = MaxAllowedJointCount;
        }
    }
    else if (Mode == USD_Renderer::JOINTS_BUFFER_MODE_STRUCTURED)
    {
        Desc.BindFlags         = BIND_SHADER_RESOURCE;
        Desc.Mode              = BUFFER_MODE_STRUCTURED;
        Desc.ElementByteStride = sizeof(float4x4);

        Desc.Size = std::max<Uint64>(Desc.Size, USD_Renderer::GetJointsDataSize(MaxJointCount, /*UsePrevFrameTransforms = */ true));
        // In structured mode, the entire buffer may be used to store joints for a single mesh.
        MaxJointCount = GetMaxAllowedJointCount(Desc.Size);
    }
    else
    {
        UNEXPECTED("Unexpected buffer mode");
    }

    RefCntAutoPtr<IBuffer> JointsBuffer;
    pDevice->CreateBuffer(Desc, nullptr, &JointsBuffer);
    VERIFY_EXPR(JointsBuffer);
    return JointsBuffer;
}

static std::shared_ptr<USD_Renderer> CreateUSDRenderer(const HnRenderDelegate::CreateInfo& RenderDelegateCI,
                                                       IBuffer*                            pPrimitiveAttribsCB,
                                                       IObject*                            MaterialSRBCache)
{
    USD_Renderer::CreateInfo USDRendererCI;

    // Use separate textures for metallic and roughness
    USDRendererCI.UseSeparateMetallicRoughnessTextures = true;
    // Default textures will be provided by the texture registry
    USDRendererCI.CreateDefaultTextures = false;
    // Enable clear coat support
    USDRendererCI.EnableClearCoat = true;

    USDRendererCI.AllowHotShaderReload = RenderDelegateCI.AllowHotShaderReload;
    USDRendererCI.PackVertexNormals    = RenderDelegateCI.PackVertexNormals;
    USDRendererCI.VertexPosPackMode    = RenderDelegateCI.PackVertexPositions ?
        USD_Renderer::VERTEX_POS_PACK_MODE_64_BIT :
        USD_Renderer::VERTEX_POS_PACK_MODE_NONE;

    USDRendererCI.PackVertexColors = RenderDelegateCI.PackVertexColors;

    const RenderDeviceInfo& DeviceInfo = RenderDelegateCI.pDevice->GetDeviceInfo();
    // There is a major performance degradation when using row-major matrices
    // on Safari/WebGL, so use column-major matrices on OpenGL.
    USDRendererCI.PackMatrixRowMajor = !DeviceInfo.IsGLDevice();

    // We use SRGB textures, so color conversion in the shader is not needed
    USDRendererCI.TexColorConversionMode = PBR_Renderer::CreateInfo::TEX_COLOR_CONVERSION_MODE_NONE;

    USDRendererCI.MaxLightCount              = RenderDelegateCI.MaxLightCount;
    USDRendererCI.EnableShadows              = RenderDelegateCI.EnableShadows;
    USDRendererCI.PCFKernelSize              = RenderDelegateCI.PCFKernelSize;
    USDRendererCI.MaxShadowCastingLightCount = RenderDelegateCI.MaxShadowCastingLightCount;

    RefCntAutoPtr<IBuffer> pJointsCB;
    if (RenderDelegateCI.MaxJointCount > 0)
    {
        USDRendererCI.UseSkinPreTransform = true;
        USDRendererCI.MaxJointCount       = RenderDelegateCI.MaxJointCount;

        USDRendererCI.JointsBufferMode = DeviceInfo.IsGLDevice() ?
            USD_Renderer::JOINTS_BUFFER_MODE_UNIFORM :
            USD_Renderer::JOINTS_BUFFER_MODE_STRUCTURED;

        pJointsCB = CreateJointsBuffer(RenderDelegateCI.pDevice, USDRendererCI.JointsBufferMode, USDRendererCI.MaxJointCount);
    }

    USDRendererCI.ColorTargetIndex        = HnFrameRenderTargets::GBUFFER_TARGET_SCENE_COLOR;
    USDRendererCI.MeshIdTargetIndex       = HnFrameRenderTargets::GBUFFER_TARGET_MESH_ID;
    USDRendererCI.MotionVectorTargetIndex = HnFrameRenderTargets::GBUFFER_TARGET_MOTION_VECTOR;
    USDRendererCI.NormalTargetIndex       = HnFrameRenderTargets::GBUFFER_TARGET_NORMAL;
    USDRendererCI.BaseColorTargetIndex    = HnFrameRenderTargets::GBUFFER_TARGET_BASE_COLOR;
    USDRendererCI.MaterialDataTargetIndex = HnFrameRenderTargets::GBUFFER_TARGET_MATERIAL;
    USDRendererCI.IBLTargetIndex          = HnFrameRenderTargets::GBUFFER_TARGET_IBL;
    static_assert(HnFrameRenderTargets::GBUFFER_TARGET_COUNT == 7, "Unexpected number of G-buffer targets");

    HN_MATERIAL_TEXTURES_BINDING_MODE TextureBindingMode = RenderDelegateCI.TextureBindingMode;
    Uint32                            TexturesArraySize  = RenderDelegateCI.TexturesArraySize;
    if (TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC &&
        !DeviceInfo.Features.BindlessResources)
    {
        LOG_WARNING_MESSAGE("This device does not support bindless resources. Switching to texture atlas mode");
        TextureBindingMode = HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS;
        TexturesArraySize  = 0;
    }

    if (TextureBindingMode == HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC)
    {
        if (TexturesArraySize == 0)
            TexturesArraySize = 256;

#if PLATFORM_APPLE
        TexturesArraySize = std::min(TexturesArraySize, 96u);
#endif

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

    // clang-format off
    constexpr LayoutElement PosInputF3       {USD_Renderer::VERTEX_ATTRIB_ID_POSITION,  HnRenderPass::VERTEX_BUFFER_SLOT_POSITIONS,     3, VT_FLOAT32};       // float3 Pos     : ATTRIB0;
    constexpr LayoutElement PosInputPacked64 {USD_Renderer::VERTEX_ATTRIB_ID_POSITION,  HnRenderPass::VERTEX_BUFFER_SLOT_POSITIONS,     2, VT_UINT32, false}; // uint2  Pos     : ATTRIB0;
    constexpr LayoutElement NormalInputF3    {USD_Renderer::VERTEX_ATTRIB_ID_NORMAL,    HnRenderPass::VERTEX_BUFFER_SLOT_NORMALS,       3, VT_FLOAT32};       // float3 Normal  : ATTRIB1;
    constexpr LayoutElement NormalInputPacked{USD_Renderer::VERTEX_ATTRIB_ID_NORMAL,    HnRenderPass::VERTEX_BUFFER_SLOT_NORMALS,       1, VT_UINT32, false}; // uint   Normal  : ATTRIB1;
    constexpr LayoutElement TexCoord0Input   {USD_Renderer::VERTEX_ATTRIB_ID_TEXCOORD0, HnRenderPass::VERTEX_BUFFER_SLOT_TEX_COORDS0,   2, VT_FLOAT32};       // float2 UV0     : ATTRIB2;
    constexpr LayoutElement TexCoord1Input   {USD_Renderer::VERTEX_ATTRIB_ID_TEXCOORD1, HnRenderPass::VERTEX_BUFFER_SLOT_TEX_COORDS1,   2, VT_FLOAT32};       // float2 UV1     : ATTRIB3;
    constexpr LayoutElement ColorInputF3     {USD_Renderer::VERTEX_ATTRIB_ID_COLOR,     HnRenderPass::VERTEX_BUFFER_SLOT_VERTEX_COLORS, 3, VT_FLOAT32};       // float3 Color   : ATTRIB6;
    constexpr LayoutElement ColorInputPacked {USD_Renderer::VERTEX_ATTRIB_ID_COLOR,     HnRenderPass::VERTEX_BUFFER_SLOT_VERTEX_COLORS, 4, VT_UINT8,  true};  // float4 Color   : ATTRIB6;
    constexpr LayoutElement JointsInput      {USD_Renderer::VERTEX_ATTRIB_ID_JOINTS,    HnRenderPass::VERTEX_BUFFER_SLOT_VERTEX_JOINTS, 4, VT_FLOAT32};       // float4 Joint0  : ATTRIB4;
    constexpr LayoutElement WeightsInput     {USD_Renderer::VERTEX_ATTRIB_ID_WEIGHTS,   HnRenderPass::VERTEX_BUFFER_SLOT_VERTEX_JOINTS, 4, VT_FLOAT32};       // float4 Weight0 : ATTRIB5;
    // clang-format on

    const LayoutElement& PosInput    = USDRendererCI.VertexPosPackMode == USD_Renderer::VERTEX_POS_PACK_MODE_64_BIT ? PosInputPacked64 : PosInputF3;
    const LayoutElement& NormalInput = USDRendererCI.PackVertexNormals ? NormalInputPacked : NormalInputF3;
    const LayoutElement& ColorInput  = USDRendererCI.PackVertexColors ? ColorInputPacked : ColorInputF3;

    const LayoutElement Inputs[] =
        {
            PosInput,
            NormalInput,
            TexCoord0Input,
            TexCoord1Input,
            ColorInput,
            JointsInput,
            WeightsInput,
        };

    if (DeviceInfo.IsVulkanDevice() ||
        DeviceInfo.IsWebGPUDevice() ||
        (DeviceInfo.IsGLDevice() && DeviceInfo.Features.NativeMultiDraw))
    {
        // When native multi-draw is not supported, we use instance id as a primitive id.
        // Direct3D is currently not supported because SV_InstanceID is not offset by base instance.
        USDRendererCI.PrimitiveArraySize = RenderDelegateCI.MultiDrawBatchSize;
    }

    USDRendererCI.InputLayout.LayoutElements = Inputs;
    USDRendererCI.InputLayout.NumElements    = _countof(Inputs);

    USDRendererCI.pPrimitiveAttribsCB = pPrimitiveAttribsCB;
    USDRendererCI.pJointsBuffer       = pJointsCB;

    return std::make_shared<USD_Renderer>(RenderDelegateCI.pDevice, RenderDelegateCI.pRenderStateCache, RenderDelegateCI.pContext, USDRendererCI);
}

static bool CheckSparseTextureSupport(IRenderDevice* pDevice)
{
    const DeviceFeatures& Features = pDevice->GetDeviceInfo().Features;
    if (!Features.SparseResources)
    {
        return false;
    }

    const SparseResourceProperties& SparseRes = pDevice->GetAdapterInfo().SparseResources;
    return (SparseRes.CapFlags & SPARSE_RESOURCE_CAP_FLAG_TEXTURE_2D_ARRAY_MIP_TAIL) != 0;
}

static bool CheckSparseBufferSupport(IRenderDevice* pDevice)
{
    const DeviceFeatures& Features = pDevice->GetDeviceInfo().Features;
    if (!Features.SparseResources)
    {
        return false;
    }

    const SparseResourceProperties& SparseRes = pDevice->GetAdapterInfo().SparseResources;
    return (SparseRes.CapFlags & SPARSE_RESOURCE_CAP_FLAG_BUFFER) != 0;
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

    if (CheckSparseBufferSupport(CI.pDevice))
    {
        if (CI.pDevice->GetDeviceInfo().Type != RENDER_DEVICE_TYPE_D3D11)
        {
            // Direct3D11 does not support sparse index buffers
            ResMgrCI.IndexAllocatorCI.Desc.Usage = USAGE_SPARSE;
            // Initial buffer size defines the page size. Too small page
            // size may be a problem on Vulkan because the number of
            // memory objects is limited by 4096.
            ResMgrCI.IndexAllocatorCI.Desc.Size = 8 << 20;
        }
        ResMgrCI.DefaultPoolDesc.Usage = USAGE_SPARSE;
        // Similar to index buffer, make sure that the initial buffer size
        // is not too small.
        ResMgrCI.DefaultPoolDesc.VertexCount = 256 << 10;
    }

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

        ResMgrCI.DefaultAtlasDesc.Desc.Name = "Hydrogent texture atlas";
        ResMgrCI.DefaultAtlasDesc.Desc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
        if (CheckSparseTextureSupport(CI.pDevice))
        {
            ResMgrCI.DefaultAtlasDesc.Desc.Usage      = USAGE_SPARSE;
            ResMgrCI.DefaultAtlasDesc.ExtraSliceCount = 1;
        }
        else
        {
            ResMgrCI.DefaultAtlasDesc.Desc.Usage   = USAGE_DEFAULT;
            ResMgrCI.DefaultAtlasDesc.GrowthFactor = 1.25f;
        }
        ResMgrCI.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
        ResMgrCI.DefaultAtlasDesc.Desc.Width     = TextureAtlasDim;
        ResMgrCI.DefaultAtlasDesc.Desc.Height    = TextureAtlasDim;
        // Ensure that the atlas will have at least one slice the first time it is updated.
        // Since textures are loaded asynchronously, no texture may be loaded during the first update
        // and RendererDelegate.GetResourceManager().GetTexture(AtlasFmt) in HnMaterial::UpdateSRB() will
        // return null.
        ResMgrCI.DefaultAtlasDesc.Desc.ArraySize = 1;
        ResMgrCI.DefaultAtlasDesc.Desc.MipLevels = 6;
        ResMgrCI.DefaultAtlasDesc.MinAlignment   = 64;
    }

    return GLTF::ResourceManager::Create(CI.pDevice, ResMgrCI);
}

static std::unique_ptr<HnShadowMapManager> CreateShadowMapManager(const HnRenderDelegate::CreateInfo& CI)
{
    if (!CI.EnableShadows)
        return {};

    HnShadowMapManager::CreateInfo ShadowMgrCI;
    return std::make_unique<HnShadowMapManager>(ShadowMgrCI);
}

HnRenderDelegate::HnRenderDelegate(const CreateInfo& CI) :
    m_pDevice{CI.pDevice},
    m_pContext{CI.pContext},
    m_pRenderStateCache{CI.pRenderStateCache},
    m_ResourceMgr{CreateResourceManager(CI)},
    m_PrimitiveAttribsCB{CreatePrimitiveAttribsCB(CI.pDevice)},
    m_MaterialSRBCache{HnMaterial::CreateSRBCache()},
    m_USDRenderer{CreateUSDRenderer(CI, m_PrimitiveAttribsCB, m_MaterialSRBCache)},
    m_TextureRegistry{
        std::make_shared<HnTextureRegistry>(
            HnTextureRegistry::CreateInfo{
                CI.pDevice,
                CI.AsyncTextureLoading ? CI.pThreadPool : nullptr,
                CI.TextureAtlasDim != 0 ? m_ResourceMgr : RefCntAutoPtr<GLTF::ResourceManager>{},
                [](IRenderDevice* pDevice, TEXTURE_LOAD_COMPRESS_MODE CompressMode) {
                    switch (CompressMode)
                    {
                        case TEXTURE_LOAD_COMPRESS_MODE_NONE:
                            return TEXTURE_LOAD_COMPRESS_MODE_NONE;

                        case TEXTURE_LOAD_COMPRESS_MODE_BC:
                        case TEXTURE_LOAD_COMPRESS_MODE_BC_HIGH_QUAL:
                            return pDevice->GetDeviceInfo().Features.TextureCompressionBC ? CompressMode : TEXTURE_LOAD_COMPRESS_MODE_NONE;

                        default:
                            UNEXPECTED("Unexpected compress mode");
                            return TEXTURE_LOAD_COMPRESS_MODE_NONE;
                    }
                }(CI.pDevice, CI.TextureCompressMode),
                CI.TextureLoadBudget,
            }),
    },
    m_GeometryPool{
        std::make_unique<HnGeometryPool>(
            CI.pDevice,
            *m_ResourceMgr,
            CI.UseVertexPool,
            CI.UseIndexPool),
    },
    m_RenderParam{
        std::make_unique<HnRenderParam>(
            HnRenderParam::Configuration{
                CI.UseVertexPool,
                CI.UseIndexPool,
                CI.AsyncShaderCompilation,
                !CI.pDevice->GetDeviceInfo().IsGLDevice(), // UseNativeStartVertex
                CI.TextureBindingMode,
                CI.MetersPerUnit,
                CI.GeometryLoadBudget,
            },
            CI.EnableShadows),
    },
    m_ShadowMapManager{CreateShadowMapManager(CI)}
{
    HnMaterial::InitSRBCache(*this);

    const Uint32 ConstantBufferOffsetAlignment = m_pDevice->GetAdapterInfo().Buffer.ConstantBufferOffsetAlignment;

    m_MainPassFrameAttribsAlignedSize   = AlignUpNonPw2(m_USDRenderer->GetPRBFrameAttribsSize(), ConstantBufferOffsetAlignment);
    m_ShadowPassFrameAttribsAlignedSize = AlignUpNonPw2(USD_Renderer::GetPRBFrameAttribsSize(0, 0), ConstantBufferOffsetAlignment);

    // Reserve space in the buffer for each shadow casting light + space for the main pass
    CreateUniformBuffer(
        m_pDevice,
        m_MainPassFrameAttribsAlignedSize + m_ShadowPassFrameAttribsAlignedSize * (CI.EnableShadows ? CI.MaxShadowCastingLightCount : 0),
        "PBR frame attribs CB",
        &m_FrameAttribsCB,
        USAGE_DEFAULT);
}

HnRenderDelegate::~HnRenderDelegate()
{
    VERIFY(m_Meshes.empty(), "Not all mesh prims have been destroyed. Verify that the imaging delegate is destroyed first, then the render index, and the render delegate is destroyed last.");
    VERIFY(m_Materials.empty(), "Not all material prims have been destroyed. Verify that the imaging delegate is destroyed first, then the render index, and the render delegate is destroyed last.");
    VERIFY(m_Lights.empty(), "Not all light prims have been destroyed. Verify that the imaging delegate is destroyed first, then the render index, and the render delegate is destroyed last.");

    // Wait for all async tasks to complete.
    // Note that this can't be done in the texture registry's destructor because shared pointer is
    // destroyed before the destructor is called.
    m_TextureRegistry->WaitForAsyncTasks();
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
        HnMesh* Mesh = HnMesh::Create(TypeId, RPrimId, *this, RPrimUID, m_EcsRegistry.create());
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
    if (HnMesh* pMesh = dynamic_cast<HnMesh*>(rPrim))
    {
        std::lock_guard<std::mutex> Guard{m_MeshesMtx};
        m_EcsRegistry.destroy(pMesh->GetEntity());
        m_Meshes.erase(pMesh);
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
    else if (TypeId == pxr::HdPrimTypeTokens->simpleLight ||
             TypeId == pxr::HdPrimTypeTokens->cylinderLight ||
             TypeId == pxr::HdPrimTypeTokens->diskLight ||
             TypeId == pxr::HdPrimTypeTokens->distantLight ||
             TypeId == pxr::HdPrimTypeTokens->rectLight ||
             TypeId == pxr::HdPrimTypeTokens->sphereLight ||
             TypeId == pxr::HdPrimTypeTokens->domeLight)
    {
        HnLight* Light = HnLight::Create(SPrimId, TypeId);
        {
            std::lock_guard<std::mutex> Guard{m_LightsMtx};
            m_Lights.emplace(Light);
        }
        SPrim = Light;
    }
    else if (TypeId == pxr::HdPrimTypeTokens->extComputation)
    {
        SPrim = HnExtComputation::Create(SPrimId);
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
        m_FallbackMaterial = HnMaterial::CreateFallback(*this);
        {
            std::lock_guard<std::mutex> Guard{m_MaterialsMtx};
            m_Materials.emplace(m_FallbackMaterial);
        }
        SPrim = m_FallbackMaterial;
    }
    else if (TypeId == pxr::HdPrimTypeTokens->camera ||
             TypeId == pxr::HdPrimTypeTokens->simpleLight ||
             TypeId == pxr::HdPrimTypeTokens->cylinderLight ||
             TypeId == pxr::HdPrimTypeTokens->diskLight ||
             TypeId == pxr::HdPrimTypeTokens->distantLight ||
             TypeId == pxr::HdPrimTypeTokens->rectLight ||
             TypeId == pxr::HdPrimTypeTokens->sphereLight ||
             TypeId == pxr::HdPrimTypeTokens->domeLight ||
             TypeId == pxr::HdPrimTypeTokens->extComputation)
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

    m_TextureRegistry->Commit(m_pContext);

    m_LastPendingIndexDataSize  = m_GeometryPool->GetPendingIndexDataSize();
    m_LastPendingVertexDataSize = m_GeometryPool->GetPendingVertexDataSize();
    m_GeometryPool->Commit(m_pContext);

    if (m_ShadowMapManager)
    {
        m_ShadowMapManager->Commit(m_pDevice, m_pContext);
        const Uint32 ShadowAtlasVersion = m_ShadowMapManager->GetAtlasVersion();
        if (m_ShadowAtlasVersion != ShadowAtlasVersion)
        {
            m_ShadowAtlasVersion = ShadowAtlasVersion;
            m_MainPassFrameAttribsSRB.Release();
        }
    }

    auto CreateFrameAttribsSRB = [this](Uint32 BufferRange, ITextureView* ShadowSRV) {
        RefCntAutoPtr<IShaderResourceBinding> SRB;
        m_USDRenderer->CreateResourceBinding(&SRB, 0);
        if (!SRB)
        {
            UNEXPECTED("Failed to create Frame Atttibs SRB");
            return SRB;
        }

        // Primitive attribs and material attribs buffers are in SRB1
        constexpr bool BindPrimitiveAttribsBuffer = false;
        constexpr bool BindMaterialAttribsBuffer  = false;
        m_USDRenderer->InitCommonSRBVars(SRB,
                                         nullptr, // pFrameAttribs
                                         BindPrimitiveAttribsBuffer,
                                         BindMaterialAttribsBuffer,
                                         ShadowSRV);
        if (auto* pVar = SRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbFrameAttribs"))
        {
            // m_FrameAttribsCB has space for the main pass and all shadow passes.
            pVar->SetBufferRange(m_FrameAttribsCB, 0, BufferRange);
        }
        else
        {
            UNEXPECTED("cbFrameAttribs variable not found in the SRB");
        }

        return SRB;
    };

    // FrameAttribs
    //
    // ||                   Main Pass                  ||        Shadow Pass 1       ||  ...  ||       Shadow Pass N        ||
    // || Camera|PrevCamera|Renderer|Lights|ShadowMaps || Camera|PrevCamera|Renderer ||  ...  || Camera|PrevCamera|Renderer ||
    //  |<-------------------------------------------->||<-------------------------->||
    //      m_USDRenderer->GetPRBFrameAttribsSize()       USD_Renderer::GetPRBFrameAttribsSize(0, 0)
    if (!m_MainPassFrameAttribsSRB)
    {
        m_MainPassFrameAttribsSRB = CreateFrameAttribsSRB(m_USDRenderer->GetPRBFrameAttribsSize(), m_ShadowMapManager ? m_ShadowMapManager->GetShadowSRV() : nullptr);
    }
    if (m_ShadowMapManager && !m_ShadowPassFrameAttribs.SRB)
    {
        constexpr Uint32 LightCount                 = 0;
        constexpr Uint32 ShadowCastingLightCount    = 0;
        const Uint32     ShadowPassFrameAttribsSize = USD_Renderer::GetPRBFrameAttribsSize(LightCount, ShadowCastingLightCount);

        RefCntAutoPtr<ITextureView> pDummyShadowSRV;
        if (m_ShadowMapManager && m_pDevice->GetDeviceInfo().IsWebGPUDevice())
        {
            // Even though the shadow map SRV is not used in the shadow pass, WebGPU errors out
            // if there is a null resource in the bind group. So we have to create a dummy SRV.
            TextureDesc DummyShadowMapDesc = m_ShadowMapManager->GetAtlasDesc();
            DummyShadowMapDesc.Name        = "Dummy shadow map SRV";
            DummyShadowMapDesc.Width       = 16;
            DummyShadowMapDesc.Height      = 16;
            DummyShadowMapDesc.ArraySize   = 1;
            DummyShadowMapDesc.MipLevels   = 1;
            RefCntAutoPtr<ITexture> pDummyShadowMap;
            m_pDevice->CreateTexture(DummyShadowMapDesc, nullptr, &pDummyShadowMap);
            VERIFY_EXPR(pDummyShadowMap);
            pDummyShadowSRV = pDummyShadowMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        }
        m_ShadowPassFrameAttribs.SRB             = CreateFrameAttribsSRB(ShadowPassFrameAttribsSize, pDummyShadowSRV);
        m_ShadowPassFrameAttribs.FrameAttribsVar = m_ShadowPassFrameAttribs.SRB->GetVariableByName(SHADER_TYPE_VERTEX, "cbFrameAttribs");
        VERIFY_EXPR(m_ShadowPassFrameAttribs.FrameAttribsVar != nullptr);
    }

    {
        if (m_MaterialResourcesVersion != m_RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::Material) + HnMaterial::GetResourceCacheVersion(*this))
        {
            std::lock_guard<std::mutex> Guard{m_MaterialsMtx};

            HnMaterial::BeginResourceUpdate(*this);
            bool AllMaterialsUpdated = true;
            for (auto* pMat : m_Materials)
            {
                if (!pMat->UpdateSRB(*this))
                    AllMaterialsUpdated = false;
            }
            HnMaterial::EndResourceUpdate(*this);

            if (AllMaterialsUpdated)
            {
                // Make global material attrib dirty to let the render passes update PSOs
                // Notice that the material resource cache version may have been updated since the time we checked it.
                m_MaterialResourcesVersion = m_RenderParam->MakeAttribDirty(HnRenderParam::GlobalAttrib::Material) + HnMaterial::GetResourceCacheVersion(*this);
            }
        }
    }

    {
        const auto MeshVersion =
            m_RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::MeshGeometry) +
            HnMesh::GetCacheResourceVersion(*this);
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

    {
        const auto LightResourcesVersion = m_RenderParam->GetAttribVersion(HnRenderParam::GlobalAttrib::LightResources);
        if (m_LightResourcesVersion != LightResourcesVersion)
        {
            std::lock_guard<std::mutex> Guard{m_LightsMtx};

            HnLight* DomeLight = nullptr;
            for (HnLight* pLight : m_Lights)
            {
                if (pLight->GetTypeId() == pxr::HdPrimTypeTokens->domeLight)
                {
                    if (DomeLight == nullptr)
                    {
                        pLight->PrecomputeIBLCubemaps(*this);
                        DomeLight = pLight;
                    }
                    else
                    {
                        LOG_WARNING_MESSAGE("Only one dome light is supported. ", pLight->GetId(), " will be ignored");
                    }
                }
            }
            m_LightResourcesVersion = LightResourcesVersion;
        }
    }

    {
        GLTF::ResourceManager::TransitionResourceStatesInfo TRSInfo;
        TRSInfo.VertexBuffers.NewState = RESOURCE_STATE_VERTEX_BUFFER;
        TRSInfo.VertexBuffers.Update   = false;

        TRSInfo.IndexBuffer.NewState = RESOURCE_STATE_INDEX_BUFFER;
        TRSInfo.IndexBuffer.Update   = false;

        TRSInfo.TextureAtlases.NewState = RESOURCE_STATE_SHADER_RESOURCE;
        // It is important to not update texture atlases here. With dynamic texture streaming,
        // new slices may have been added since the last update. If Update is true, new texture
        // atlases may be created and transitioned instead of the ones bound in the material SRBs.
        // Besides, this will intoduce extra memory overhead and extra copies.
        TRSInfo.TextureAtlases.Update = false;
        m_ResourceMgr->TransitionResourceStates(m_pDevice, m_pContext, TRSInfo);
    }
}

bool HnRenderDelegate::IsParallelSyncEnabled(pxr::TfToken primType) const
{
    return (primType == pxr::HdPrimTypeTokens->mesh ||
            primType == pxr::HdPrimTypeTokens->material ||
            primType == pxr::HdPrimTypeTokens->camera ||
            primType == pxr::HdPrimTypeTokens->extComputation);
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

    const BufferSuballocatorUsageStats  IndexUsage       = m_ResourceMgr->GetIndexBufferUsageStats();
    const VertexPoolUsageStats          VertexUsage      = m_ResourceMgr->GetVertexPoolUsageStats();
    const DynamicTextureAtlasUsageStats AtlasUsage       = m_ResourceMgr->GetAtlasUsageStats();
    const HnTextureRegistry::UsageStats TexRegistryStats = m_TextureRegistry->GetUsageStats();

    MemoryStats.IndexPool.CommittedSize   = IndexUsage.CommittedSize;
    MemoryStats.IndexPool.UsedSize        = IndexUsage.UsedSize;
    MemoryStats.IndexPool.AllocationCount = IndexUsage.AllocationCount;
    MemoryStats.IndexPool.PendingDataSize = m_LastPendingIndexDataSize;

    MemoryStats.VertexPool.CommittedSize        = VertexUsage.CommittedMemorySize;
    MemoryStats.VertexPool.UsedSize             = VertexUsage.UsedMemorySize;
    MemoryStats.VertexPool.AllocationCount      = VertexUsage.AllocationCount;
    MemoryStats.VertexPool.AllocatedVertexCount = VertexUsage.AllocatedVertexCount;
    MemoryStats.VertexPool.PendingDataSize      = m_LastPendingVertexDataSize;

    MemoryStats.NumRPrimsLoading = static_cast<Uint32>(m_RenderParam->GetLastDirtyRPrimCount());

    MemoryStats.Atlas.CommittedSize   = AtlasUsage.CommittedSize;
    MemoryStats.Atlas.AllocationCount = AtlasUsage.AllocationCount;
    MemoryStats.Atlas.TotalTexels     = AtlasUsage.TotalArea;
    MemoryStats.Atlas.AllocatedTexels = AtlasUsage.AllocatedArea;

    MemoryStats.TextureRegistry.NumTexturesLoading  = TexRegistryStats.NumTexturesLoading;
    MemoryStats.TextureRegistry.LoadingTexDataSize  = TexRegistryStats.LoadingTexDataSize;
    MemoryStats.TextureRegistry.AtlasDataSize       = TexRegistryStats.AtlasDataSize;
    MemoryStats.TextureRegistry.SeparateTexDataSize = TexRegistryStats.SeparateTexDataSize;

    return MemoryStats;
}

HnRenderDelegateRenderingStats HnRenderDelegate::GetRenderingStats() const
{
    HnRenderDelegateRenderingStats Stats;
    Stats.LoadingAnimationActive = m_RenderParam->GetLoadingAnimationActive();
    return Stats;
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

void HnRenderDelegate::SetUseShadows(bool UseShadows)
{
    if (UseShadows && !m_USDRenderer->GetSettings().EnableShadows)
    {
        LOG_WARNING_MESSAGE("Shadows are not enabled in the renderer settings. Shadows will not be used");
        return;
    }
    m_RenderParam->SetUseShadows(UseShadows);
}

Uint32 HnRenderDelegate::GetShadowPassFrameAttribsOffset(Uint32 LightId) const
{
    return m_MainPassFrameAttribsAlignedSize + m_ShadowPassFrameAttribsAlignedSize * LightId;
}

IShaderResourceBinding* HnRenderDelegate::GetShadowPassFrameAttribsSRB(Uint32 LightId) const
{
    VERIFY_EXPR(m_ShadowPassFrameAttribs.SRB);
    VERIFY_EXPR(m_ShadowPassFrameAttribs.FrameAttribsVar != nullptr);
    m_ShadowPassFrameAttribs.FrameAttribsVar->SetBufferOffset(GetShadowPassFrameAttribsOffset(LightId));
    return m_ShadowPassFrameAttribs.SRB;
}

bool HnRenderDelegate::AllowPrimitiveRestart() const
{
    // WebGL supports primitive restart index, however on MacOS the presence of the restart index
    // in the buffer causes disastrous performance degradation. So we disable it on OpenGL.
    return !m_pDevice->GetDeviceInfo().IsGLDevice();
}

} // namespace USD

} // namespace Diligent
