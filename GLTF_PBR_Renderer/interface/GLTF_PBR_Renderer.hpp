/*
 *  Copyright 2019-2022 Diligent Graphics LLC
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

#include <unordered_map>
#include <functional>
#include <mutex>
#include <vector>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Common/interface/HashUtils.hpp"
#include "../../../DiligentTools/AssetLoader/interface/GLTFLoader.hpp"

namespace Diligent
{

#include "Shaders/GLTF_PBR/public/GLTF_PBR_Structures.fxh"

/// Implementation of a GLTF PBR renderer
class GLTF_PBR_Renderer
{
public:
    /// Renderer create info
    struct CreateInfo
    {
        /// Render target format.
        TEXTURE_FORMAT RTVFmt = TEX_FORMAT_UNKNOWN;

        /// Depth-buffer format.

        /// \note   If both RTV and DSV formats are TEX_FORMAT_UNKNOWN,
        ///         the renderer will not initialize PSO, uniform buffers and other
        ///         resources. It is expected that an application will use custom
        ///         render callback function.
        TEXTURE_FORMAT DSVFmt = TEX_FORMAT_UNKNOWN;

        /// Indicates if front face is CCW.
        bool FrontCCW = false;

        /// Indicates if the renderer should allow debug views.
        /// Rendering with debug views disabled is more efficient.
        bool AllowDebugView = false;

        /// Indicates whether to use IBL.
        bool UseIBL = false;

        /// Whether to use ambient occlusion texture.
        bool UseAO = true;

        /// Whether to use emissive texture.
        bool UseEmissive = true;

        /// When set to true, pipeline state will be compiled with immutable samplers.
        /// When set to false, samplers from the texture views will be used.
        bool UseImmutableSamplers = true;

        /// Whether to use texture atlas (e.g. apply UV transforms when sampling textures).
        bool UseTextureAtlas = false;

        static const SamplerDesc DefaultSampler;

        /// Immutable sampler for color map texture.
        SamplerDesc ColorMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for physical description map texture.
        SamplerDesc PhysDescMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for normal map texture.
        SamplerDesc NormalMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for AO texture.
        SamplerDesc AOMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for emissive map texture.
        SamplerDesc EmissiveMapImmutableSampler = DefaultSampler;

        /// Maximum number of joints
        Uint32 MaxJointCount = 64;

        /// Number of samples for BRDF LUT creation
        Uint32 NumBRDFSamples = 512;
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

        /// Debug view type
        enum class DebugViewType : int
        {
            None            = 0,
            BaseColor       = 1,
            Transparency    = 2,
            NormalMap       = 3,
            Occlusion       = 4,
            Emissive        = 5,
            Metallic        = 6,
            Roughness       = 7,
            DiffuseColor    = 8,
            SpecularColor   = 9,
            Reflectance90   = 10,
            MeshNormal      = 11,
            PerturbedNormal = 12,
            NdotV           = 13,
            DiffuseIBL      = 14,
            SpecularIBL     = 15,
            NumDebugViews
        };
        DebugViewType DebugView = DebugViewType::None;

        /// Ambient occlusion strength
        float OcclusionStrength = 1;

        /// Emission scale
        float EmissionScale = 1;

        /// IBL scale
        float IBLScale = 1;

        /// Average log luminance used by tone mapping
        float AverageLogLum = 0.3f;

        /// Middle gray value used by tone mapping
        float MiddleGray = 0.18f;

        /// White point value used by tone mapping
        float WhitePoint = 3.f;
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
    /// \param [in] RenderParams   - Render parameters.
    /// \param [in] pModelBindings - The model's shader resource binding information.
    /// \param [in] pCacheBindings - Shader resource cache binding information, if the
    ///                              model has been created using the cache.
    void Render(IDeviceContext*              pCtx,
                const GLTF::Model&           GLTFModel,
                const GLTF::ModelTransforms& Transforms,
                const RenderInfo&            RenderParams,
                ModelResourceBindings*       pModelBindings,
                ResourceCacheBindings*       pCacheBindings = nullptr);

    /// Creates resource bindings for a given GLTF model
    ModelResourceBindings CreateResourceBindings(GLTF::Model& GLTFModel,
                                                 IBuffer*     pCameraAttribs,
                                                 IBuffer*     pLightAttribs);

    /// Precompute cubemaps used by IBL.
    void PrecomputeCubemaps(IRenderDevice*     pDevice,
                            IRenderStateCache* pStateCache,
                            IDeviceContext*    pCtx,
                            ITextureView*      pEnvironmentMap,
                            Uint32             NumPhiSamples   = 64,
                            Uint32             NumThetaSamples = 32,
                            bool               OptimizeSamples = true);

    // clang-format off
    ITextureView* GetIrradianceCubeSRV()    { return m_pIrradianceCubeSRV; }
    ITextureView* GetPrefilteredEnvMapSRV() { return m_pPrefilteredEnvMapSRV; }
    ITextureView* GetBRDFLUTSRV()           { return m_pBRDF_LUT_SRV; }
    ITextureView* GetWhiteTexSRV()          { return m_pWhiteTexSRV; }
    ITextureView* GetBlackTexSRV()          { return m_pBlackTexSRV; }
    ITextureView* GetDefaultNormalMapSRV()  { return m_pDefaultNormalMapSRV; }
    // clang-format on

    /// Initializes a shader resource binding for the given material.

    /// \param [in] Model          - GLTF model that keeps material textures.
    /// \param [in] Material       - GLTF material to create SRB for.
    /// \param [in] pCameraAttribs - Camera attributes constant buffer to set in the SRB.
    /// \param [in] pLightAttribs  - Light attributes constant buffer to set in the SRB.
    /// \param [in] pMaterialSRB   - A pointer to the SRB object to initialize.
    void InitMaterialSRB(GLTF::Model&            Model,
                         GLTF::Material&         Material,
                         IBuffer*                pCameraAttribs,
                         IBuffer*                pLightAttribs,
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
    };

    /// Creates a shader resource binding for a GTLF resource cache.

    /// \param [in] pDevice        - Render device that may be needed by the resource cache to create
    ///                              internal objects.
    /// \param [in] pCtx           - Device context that may be needed by the resource cache to initialize
    ///                              internal objects.
    /// \param [in] CacheUseInfo   - GLTF resource cache usage information.
    /// \param [in] pCameraAttribs - Camera attributes constant buffer to set in the SRB.
    /// \param [in] pLightAttribs  - Light attributes constant buffer to set in the SRB.
    /// \param [in] pPSO           - Optional PSO object to use to create the SRB instead of the
    ///                              default PSO. Can be null
    /// \param [out] ppCacheSRB    - Pointer to memory location where the pointer to the SRB object
    ///                              will be written.
    void CreateResourceCacheSRB(IRenderDevice*           pDevice,
                                IDeviceContext*          pCtx,
                                ResourceCacheUseInfo&    CacheUseInfo,
                                IBuffer*                 pCameraAttribs,
                                IBuffer*                 pLightAttribs,
                                IPipelineState*          pPSO,
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
               IBuffer*               pCameraAttribs,
               IBuffer*               pLightAttribs,
               IPipelineState*        pPSO = nullptr);

private:
    void PrecomputeBRDF(IRenderDevice*     pDevice,
                        IRenderStateCache* pStateCache,
                        IDeviceContext*    pCtx,
                        Uint32             NumBRDFSamples = 512);

    void CreatePSO(IRenderDevice* pDevice, IRenderStateCache* pStateCache);

    void InitCommonSRBVars(IShaderResourceBinding* pSRB,
                           IBuffer*                pCameraAttribs,
                           IBuffer*                pLightAttribs);

    struct PSOKey
    {
        PSOKey() noexcept {};
        PSOKey(GLTF::Material::ALPHA_MODE _AlphaMode, bool _DoubleSided) :
            AlphaMode{_AlphaMode},
            DoubleSided{_DoubleSided}
        {}

        bool operator==(const PSOKey& rhs) const noexcept
        {
            return AlphaMode == rhs.AlphaMode && DoubleSided == rhs.DoubleSided;
        }
        bool operator!=(const PSOKey& rhs) const noexcept
        {
            return AlphaMode != rhs.AlphaMode || DoubleSided != rhs.DoubleSided;
        }

        GLTF::Material::ALPHA_MODE AlphaMode   = GLTF::Material::ALPHA_MODE_OPAQUE;
        bool                       DoubleSided = false;
    };

    static size_t GetPSOIdx(const PSOKey& Key)
    {
        size_t PSOIdx;

        PSOIdx = Key.AlphaMode == GLTF::Material::ALPHA_MODE_BLEND ? 1 : 0;
        PSOIdx = PSOIdx * 2 + (Key.DoubleSided ? 1 : 0);
        return PSOIdx;
    }

    void AddPSO(const PSOKey& Key, RefCntAutoPtr<IPipelineState> pPSO)
    {
        auto Idx = GetPSOIdx(Key);
        if (Idx >= m_PSOCache.size())
            m_PSOCache.resize(Idx + 1);
        VERIFY_EXPR(!m_PSOCache[Idx]);
        m_PSOCache[Idx] = std::move(pPSO);
    }

    IPipelineState* GetPSO(const PSOKey& Key)
    {
        auto Idx = GetPSOIdx(Key);
        VERIFY_EXPR(Idx < m_PSOCache.size());
        return Idx < m_PSOCache.size() ? m_PSOCache[Idx].RawPtr() : nullptr;
    }

    const CreateInfo m_Settings;

    static constexpr Uint32     BRDF_LUT_Dim = 512;
    RefCntAutoPtr<ITextureView> m_pBRDF_LUT_SRV;

    std::vector<RefCntAutoPtr<IPipelineState>> m_PSOCache;

    RefCntAutoPtr<ITextureView> m_pWhiteTexSRV;
    RefCntAutoPtr<ITextureView> m_pBlackTexSRV;
    RefCntAutoPtr<ITextureView> m_pDefaultNormalMapSRV;
    RefCntAutoPtr<ITextureView> m_pDefaultPhysDescSRV;


    static constexpr TEXTURE_FORMAT IrradianceCubeFmt    = TEX_FORMAT_RGBA32_FLOAT;
    static constexpr TEXTURE_FORMAT PrefilteredEnvMapFmt = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr Uint32         IrradianceCubeDim    = 64;
    static constexpr Uint32         PrefilteredEnvMapDim = 256;

    RefCntAutoPtr<ITextureView>           m_pIrradianceCubeSRV;
    RefCntAutoPtr<ITextureView>           m_pPrefilteredEnvMapSRV;
    RefCntAutoPtr<IPipelineState>         m_pPrecomputeIrradianceCubePSO;
    RefCntAutoPtr<IPipelineState>         m_pPrefilterEnvMapPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pPrecomputeIrradianceCubeSRB;
    RefCntAutoPtr<IShaderResourceBinding> m_pPrefilterEnvMapSRB;

    RenderInfo m_RenderParams;

    RefCntAutoPtr<IBuffer> m_TransformsCB;
    RefCntAutoPtr<IBuffer> m_GLTFAttribsCB;
    RefCntAutoPtr<IBuffer> m_PrecomputeEnvMapAttribsCB;
    RefCntAutoPtr<IBuffer> m_JointsBuffer;
};

DEFINE_FLAG_ENUM_OPERATORS(GLTF_PBR_Renderer::RenderInfo::ALPHA_MODE_FLAGS)

} // namespace Diligent
