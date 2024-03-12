/*
 *  Copyright 2019-2024 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
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

#include "PBR_Renderer.hpp"

#include <vector>
#include <array>

#include "../../../DiligentTools/AssetLoader/interface/GLTFLoader.hpp"

namespace Diligent
{

namespace HLSL
{
struct PBRMaterialBasicAttribs;
struct PBRLightAttribs;
} // namespace HLSL

/// Implementation of a GLTF PBR renderer
class GLTF_PBR_Renderer : public PBR_Renderer
{
public:
    struct CreateInfo : public PBR_Renderer::CreateInfo
    {
        /// The number of render targets.
        Uint8 NumRenderTargets = 0;

        /// Render target formats.
        TEXTURE_FORMAT RTVFormats[8] = {};

        /// Depth-stencil format.
        TEXTURE_FORMAT DSVFormat = TEX_FORMAT_UNKNOWN;

        bool FrontCounterClockwise = false;
    };

    /// Initializes the renderer
    GLTF_PBR_Renderer(IRenderDevice*     pDevice,
                      IRenderStateCache* pStateCache,
                      IDeviceContext*    pCtx,
                      const CreateInfo&  CI);

    /// Rendering information
    struct RenderInfo
    {
        /// Index of the scene to render
        Uint32 SceneIndex = 0;

        /// Model transform matrix
        float4x4 ModelTransform = float4x4::Identity();

        /// Alpha mode flags
        enum ALPHA_MODE_FLAGS : Uint32
        {
            /// Render nothing
            ALPHA_MODE_FLAG_NONE = 0,

            /// Render opaque matetrials
            ALPHA_MODE_FLAG_OPAQUE = 1 << GLTF::Material::ALPHA_MODE_OPAQUE,

            /// Render alpha-masked matetrials
            ALPHA_MODE_FLAG_MASK = 1 << GLTF::Material::ALPHA_MODE_MASK,

            /// Render alpha-blended matetrials
            ALPHA_MODE_FLAG_BLEND = 1 << GLTF::Material::ALPHA_MODE_BLEND,

            /// Render all materials
            ALPHA_MODE_FLAG_ALL = ALPHA_MODE_FLAG_OPAQUE | ALPHA_MODE_FLAG_MASK | ALPHA_MODE_FLAG_BLEND
        };
        /// Flag indicating which alpha modes to render
        ALPHA_MODE_FLAGS AlphaModes = ALPHA_MODE_FLAG_ALL;

        DebugViewType DebugView = DebugViewType::None;

        PSO_FLAGS Flags = PSO_FLAG_DEFAULT;

        bool Wireframe = false;
    };

    /// GLTF Model shader resource binding information
    struct ModelResourceBindings
    {
        void Clear()
        {
            MaterialSRB.clear();
        }
        /// Shader resource binding for every material
        std::vector<RefCntAutoPtr<IShaderResourceBinding>> MaterialSRB;
    };

    /// GLTF resource cache shader resource binding information
    struct ResourceCacheBindings
    {
        /// Resource version
        Uint32 Version = ~0u;

        RefCntAutoPtr<IShaderResourceBinding> pSRB;
    };

    /// Renders a GLTF model.

    /// \param [in] pCtx           - Device context to record rendering commands to.
    /// \param [in] GLTFModel      - GLTF model to render.
    /// \param [in] Transforms     - The model transforms.
    /// \param [in] PrevTransforms - The model transforms from the previous frame.
    ///                              This parameter should not be null if motion vectors
    ///                              are enabled.
    /// \param [in] RenderParams   - Render parameters.
    /// \param [in] pModelBindings - The model's shader resource binding information.
    /// \param [in] pCacheBindings - Shader resource cache binding information, if the
    ///                              model has been created using the cache.
    void Render(IDeviceContext*              pCtx,
                const GLTF::Model&           GLTFModel,
                const GLTF::ModelTransforms& Transforms,
                const GLTF::ModelTransforms* PrevTransforms,
                const RenderInfo&            RenderParams,
                ModelResourceBindings*       pModelBindings,
                ResourceCacheBindings*       pCacheBindings = nullptr);

    /// Creates resource bindings for a given GLTF model
    ModelResourceBindings CreateResourceBindings(GLTF::Model& GLTFModel,
                                                 IBuffer*     pFrameAttribs);


    /// Initializes a shader resource binding for the given material.

    /// \param [in] Model         - GLTF model that keeps material textures.
    /// \param [in] Material      - GLTF material to create SRB for.
    /// \param [in] pFrameAttribs - Frame attributes constant buffer to set in the SRB.
    /// \param [in] pMaterialSRB  - A pointer to the SRB object to initialize.
    void InitMaterialSRB(GLTF::Model&            Model,
                         GLTF::Material&         Material,
                         IBuffer*                pFrameAttribs,
                         IShaderResourceBinding* pMaterialSRB);

    /// GLTF resource cache use information.
    struct ResourceCacheUseInfo
    {
        /// A pointer to the resource manager.
        GLTF::ResourceManager* pResourceMgr = nullptr;

        /// Vertex layout key.
        GLTF::ResourceManager::VertexLayoutKey VtxLayoutKey;

        /// Base color texture format.
        TEXTURE_FORMAT BaseColorFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Base color texture format for alpha-cut and alpha-blend materials.
        TEXTURE_FORMAT BaseColorAlphaFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Physical descriptor texture format.
        TEXTURE_FORMAT PhysicalDescFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Normal map format.
        TEXTURE_FORMAT NormalFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Occlusion texture format.
        TEXTURE_FORMAT OcclusionFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Emissive texture format.
        TEXTURE_FORMAT EmissiveFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Clear coat texture format.
        TEXTURE_FORMAT ClearCoatFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Clear coat roughness texture format.
        TEXTURE_FORMAT ClearCoatRoughnessFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Clear coat normal texture format.
        TEXTURE_FORMAT ClearCoatNormalFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Sheen color texture format.
        TEXTURE_FORMAT SheenColorFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Sheen roughness texture format.
        TEXTURE_FORMAT SheenRoughnessFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Anisotropy texture format.
        TEXTURE_FORMAT AnisotropyFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Iridescence texture format.
        TEXTURE_FORMAT IridescenceFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Iridescence thickness texture format.
        TEXTURE_FORMAT IridescenceThicknessFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Transmission texture format.
        TEXTURE_FORMAT TransmissionFormat = TEX_FORMAT_RGBA8_UNORM;

        /// Thickness texture format.
        TEXTURE_FORMAT ThicknessFormat = TEX_FORMAT_RGBA8_UNORM;
    };

    /// Creates a shader resource binding for a GTLF resource cache.

    /// \param [in] pDevice       - Render device that may be needed by the resource cache to create
    ///                             internal objects.
    /// \param [in] pCtx          - Device context that may be needed by the resource cache to initialize
    ///                             internal objects.
    /// \param [in] CacheUseInfo  - GLTF resource cache usage information.
    /// \param [in] pFrameAttribs - Frame attributes constant buffer to set in the SRB.
    /// \param [out] ppCacheSRB   - Pointer to memory location where the pointer to the SRB object
    ///                             will be written.
    void CreateResourceCacheSRB(IRenderDevice*           pDevice,
                                IDeviceContext*          pCtx,
                                ResourceCacheUseInfo&    CacheUseInfo,
                                IBuffer*                 pFrameAttribs,
                                IShaderResourceBinding** ppCacheSRB);

    /// Prepares the renderer for rendering objects.
    /// This method must be called at least once per frame.
    void Begin(IDeviceContext* pCtx);


    /// Prepares the renderer for rendering objects from the resource cache.
    /// This method must be called at least once per frame before the first object
    /// from the cache is rendered.
    void Begin(IRenderDevice*         pDevice,
               IDeviceContext*        pCtx,
               ResourceCacheUseInfo&  CacheUseInfo,
               ResourceCacheBindings& Bindings,
               IBuffer*               pFrameAttribs);

    struct PBRPrimitiveShaderAttribsData
    {
        PSO_FLAGS       PSOFlags       = PSO_FLAG_NONE;
        const float4x4* NodeMatrix     = nullptr;
        const float4x4* PrevNodeMatrix = nullptr;
        const Uint32    JointCount     = 0;
        const void*     CustomData     = nullptr;
        size_t          CustomDataSize = 0;

        HLSL::PBRMaterialBasicAttribs** pMaterialBasicAttribsDstPtr = nullptr;
    };
    static void* WritePBRPrimitiveShaderAttribs(void*                                           pDstShaderAttribs,
                                                const PBRPrimitiveShaderAttribsData&            AttribsData,
                                                const std::array<int, TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
                                                const GLTF::Material&                           Material);

    struct PBRLightShaderAttribsData
    {
        const GLTF::Light* Light     = nullptr;
        const float3*      Position  = nullptr;
        const float3*      Direction = nullptr;
        // Distance scaling factor.
        // This value is used to scale the point and spot light's range (by s) and intensity (by s^2).
        float DistanceScale  = 1.f;
        int   ShadowMapIndex = -1;
    };
    static void WritePBRLightShaderAttribs(const PBRLightShaderAttribsData& AttribsData,
                                           HLSL::PBRLightAttribs*           pShaderAttribs);

    PSO_FLAGS GetMaterialPSOFlags(const GLTF::Material& Mat) const;

private:
    static ALPHA_MODE GltfAlphaModeToAlphaMode(GLTF::Material::ALPHA_MODE GltfAlphaMode);

private:
    RenderInfo m_RenderParams;

    struct PrimitiveRenderInfo
    {
        const GLTF::Primitive& Primitive;
        const GLTF::Node&      Node;

        PrimitiveRenderInfo(const GLTF::Primitive& _Primitive,
                            const GLTF::Node&      _Node) noexcept :
            Primitive{_Primitive},
            Node{_Node}
        {}
    };
    std::array<std::vector<PrimitiveRenderInfo>, GLTF::Material::ALPHA_MODE_NUM_MODES> m_RenderLists;

    PsoCacheAccessor m_PbrPSOCache;
    PsoCacheAccessor m_WireframePSOCache;
};

DEFINE_FLAG_ENUM_OPERATORS(GLTF_PBR_Renderer::RenderInfo::ALPHA_MODE_FLAGS)

} // namespace Diligent
