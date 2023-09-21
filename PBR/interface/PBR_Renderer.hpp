/*
 *  Copyright 2019-2023 Diligent Graphics LLC
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

#include <vector>
#include <array>

#include "../../../DiligentCore/Platforms/Basic/interface/DebugUtilities.hpp"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/GraphicsTypesX.hpp"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/ShaderMacroHelper.hpp"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"

namespace Diligent
{

class PBR_Renderer
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

        /// Whether to use separate textures for metallic and roughness
        /// instead of a combined physical description texture.
        bool UseSeparateMetallicRoughnessTextures = false;

        /// Manually convert shader output to sRGB color space.
        bool ConvertOutputToSRGB = false;

        /// Whether to enable rendering mesh Ids to a separate render target.
        bool EnableMeshIdRendering = false;

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

        /// The maximum number of joints.
        ///
        /// If set to 0, the animation will be disabled.
        Uint32 MaxJointCount = 64;

        /// The number of samples for BRDF LUT creation
        Uint32 NumBRDFSamples = 512;

        /// Optional input layout description.
        InputLayoutDesc InputLayout;

        /// Conversion mode applied to diffuse, specular and emissive textures.
        ///
        /// \note   Normal map, ambient occlusion and physical description textures are
        ///         always assumed to be in linear color space.
        enum TEX_COLOR_CONVERSION_MODE
        {
            /// Sampled texture colors are used as is.
            ///
            /// \remarks    This mode should be used if the textures are in linear color space,
            ///             or if the texture is in sRGB color space and the texture view is
            ///             also in sRGB color space (which ensures that sRGB->linear conversion
            ///             is performed by the GPU).
            TEX_COLOR_CONVERSION_MODE_NONE = 0,

            /// Manually convert texture colors from sRGB to linear color space.
            ///
            /// \remarks    This mode should be used if the textures are in sRGB color space,
            ///             but the texture views are in linear color space.
            TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR,
        };
        TEX_COLOR_CONVERSION_MODE TexColorConversionMode = TEX_COLOR_CONVERSION_MODE_SRGB_TO_LINEAR;
    };

    enum ALPHA_MODE
    {
        ALPHA_MODE_OPAQUE = 0,
        ALPHA_MODE_MASK,
        ALPHA_MODE_BLEND,
        ALPHA_MODE_NUM_MODES
    };

    enum PBR_WORKFLOW
    {
        PBR_WORKFLOW_METALL_ROUGH = 0,
        PBR_WORKFLOW_SPEC_GLOSS
    };

    /// Debug view type
    enum class DebugViewType : int
    {
        None,
        Texcoord0,
        BaseColor,
        Transparency,
        NormalMap,
        Occlusion,
        Emissive,
        Metallic,
        Roughness,
        DiffuseColor,
        SpecularColor,
        Reflectance90,
        MeshNormal,
        PerturbedNormal,
        NdotV,
        DirectLighting,
        DiffuseIBL,
        SpecularIBL,
        NumDebugViews
    };

    /// Initializes the renderer
    PBR_Renderer(IRenderDevice*     pDevice,
                 IRenderStateCache* pStateCache,
                 IDeviceContext*    pCtx,
                 const CreateInfo&  CI);

    ~PBR_Renderer();

    // clang-format off
    ITextureView* GetIrradianceCubeSRV() const    { return m_pIrradianceCubeSRV; }
    ITextureView* GetPrefilteredEnvMapSRV() const { return m_pPrefilteredEnvMapSRV; }
    ITextureView* GetBRDFLUTSRV() const           { return m_pBRDF_LUT_SRV; }
    ITextureView* GetWhiteTexSRV() const          { return m_pWhiteTexSRV; }
    ITextureView* GetBlackTexSRV() const          { return m_pBlackTexSRV; }
    ITextureView* GetDefaultNormalMapSRV() const  { return m_pDefaultNormalMapSRV; }
    IBuffer*      GetPBRAttribsCB() const         {return m_PBRAttribsCB;}
    // clang-format on

    /// Precompute cubemaps used by IBL.
    void PrecomputeCubemaps(IRenderDevice*     pDevice,
                            IRenderStateCache* pStateCache,
                            IDeviceContext*    pCtx,
                            ITextureView*      pEnvironmentMap,
                            Uint32             NumPhiSamples   = 64,
                            Uint32             NumThetaSamples = 32,
                            bool               OptimizeSamples = true);

    void CreateResourceBinding(IShaderResourceBinding** ppSRB);

    struct PSOKey
    {
        PSOKey() noexcept {};
        PSOKey(ALPHA_MODE _AlphaMode, bool _DoubleSided) :
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

        ALPHA_MODE AlphaMode   = ALPHA_MODE_OPAQUE;
        bool       DoubleSided = false;
    };

    IPipelineState* GetPSO(const PSOKey& Key) const
    {
        auto Idx = GetPSOIdx(Key);
        VERIFY_EXPR(Idx < m_PSOCache.size());
        return Idx < m_PSOCache.size() ? m_PSOCache[Idx].RawPtr() : nullptr;
    }

    IPipelineState* GetMeshIdPSO(bool DoubleSided) const
    {
        return m_MeshIdPSO[DoubleSided ? 1 : 0];
    }

    void InitCommonSRBVars(IShaderResourceBinding* pSRB,
                           IBuffer*                pCameraAttribs,
                           IBuffer*                pLightAttribs);

protected:
    ShaderMacroHelper DefineMacros() const;
    InputLayoutDescX  GetInputLayout() const;

    static size_t GetPSOIdx(const PSOKey& Key)
    {
        size_t PSOIdx;

        PSOIdx = Key.AlphaMode == ALPHA_MODE_BLEND ? 1 : 0;
        PSOIdx = PSOIdx * 2 + (Key.DoubleSided ? 1 : 0);
        return PSOIdx;
    }

    void AddPSO(const PSOKey& Key, RefCntAutoPtr<IPipelineState> pPSO);

private:
    void PrecomputeBRDF(IRenderDevice*     pDevice,
                        IRenderStateCache* pStateCache,
                        IDeviceContext*    pCtx,
                        Uint32             NumBRDFSamples = 512);

    void CreatePSO(IRenderDevice* pDevice, IRenderStateCache* pStateCache);
    void CreateSignature(IRenderDevice* pDevice, IRenderStateCache* pStateCache);

protected:
    const CreateInfo m_Settings;

    static constexpr Uint32     BRDF_LUT_Dim = 512;
    RefCntAutoPtr<ITextureView> m_pBRDF_LUT_SRV;

    RefCntAutoPtr<ITextureView> m_pWhiteTexSRV;
    RefCntAutoPtr<ITextureView> m_pBlackTexSRV;
    RefCntAutoPtr<ITextureView> m_pDefaultNormalMapSRV;
    RefCntAutoPtr<ITextureView> m_pDefaultPhysDescSRV;


    static constexpr TEXTURE_FORMAT IrradianceCubeFmt    = TEX_FORMAT_RGBA32_FLOAT;
    static constexpr TEXTURE_FORMAT PrefilteredEnvMapFmt = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT MeshIdFmt            = TEX_FORMAT_RGBA8_UINT;
    static constexpr Uint32         IrradianceCubeDim    = 64;
    static constexpr Uint32         PrefilteredEnvMapDim = 256;

    RefCntAutoPtr<ITextureView>           m_pIrradianceCubeSRV;
    RefCntAutoPtr<ITextureView>           m_pPrefilteredEnvMapSRV;
    RefCntAutoPtr<IPipelineState>         m_pPrecomputeIrradianceCubePSO;
    RefCntAutoPtr<IPipelineState>         m_pPrefilterEnvMapPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pPrecomputeIrradianceCubeSRB;
    RefCntAutoPtr<IShaderResourceBinding> m_pPrefilterEnvMapSRB;

    RefCntAutoPtr<IBuffer> m_PBRAttribsCB;
    RefCntAutoPtr<IBuffer> m_PrecomputeEnvMapAttribsCB;
    RefCntAutoPtr<IBuffer> m_JointsBuffer;

    RefCntAutoPtr<IPipelineResourceSignature>    m_ResourceSignature;
    std::vector<RefCntAutoPtr<IPipelineState>>   m_PSOCache;
    std::array<RefCntAutoPtr<IPipelineState>, 2> m_MeshIdPSO;
};

} // namespace Diligent
