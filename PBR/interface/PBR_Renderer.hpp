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

#include <unordered_map>
#include <functional>

#include "../../../DiligentCore/Platforms/Basic/interface/DebugUtilities.hpp"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/GraphicsTypesX.hpp"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.hpp"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/ShaderMacroHelper.hpp"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/HashUtils.hpp"

namespace Diligent
{

class PBR_Renderer
{
public:
    /// Renderer create info
    struct CreateInfo
    {
        /// Indicates whether to enable IBL.
        /// A pipeline state can use IBL only if this flag is set to true.
        bool EnableIBL = true;

        /// Whether to use enable ambient occlusion.
        /// A pipeline state can use AO only if this flag is set to true.
        bool EnableAO = true;

        /// Whether to enable emissive texture.
        /// A pipeline state can use emissive texture only if this flag is set to true.
        bool EnableEmissive = true;

        /// When set to true, pipeline state will be compiled with immutable samplers.
        /// When set to false, samplers from the texture views will be used.
        bool UseImmutableSamplers = true;

        /// Whether to use separate textures for metallic and roughness
        /// instead of a combined physical description texture.
        bool UseSeparateMetallicRoughnessTextures = false;

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

        /// Input layout description.
        ///
        /// \remarks    The renderer uses the following input layout:
        ///
        ///                 struct VSInput
        ///                 {
        ///                     float3 Pos     : ATTRIB0;
        ///                     float3 Normal  : ATTRIB1; // If PSO_FLAG_USE_VERTEX_NORMALS is set
        ///                     float2 UV0     : ATTRIB2; // If PSO_FLAG_USE_TEXCOORD0 is set
        ///                     float2 UV1     : ATTRIB3; // If PSO_FLAG_USE_TEXCOORD1 is set
        ///                     float4 Joint0  : ATTRIB4; // If PSO_FLAG_USE_JOINTS is set
        ///                     float4 Weight0 : ATTRIB5; // If PSO_FLAG_USE_JOINTS is set
        ///                     float4 Color   : ATTRIB6; // If PSO_FLAG_USE_VERTEX_COLORS is set
        ///                 };
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
    void PrecomputeCubemaps(IDeviceContext* pCtx,
                            ITextureView*   pEnvironmentMap,
                            Uint32          NumPhiSamples   = 64,
                            Uint32          NumThetaSamples = 32,
                            bool            OptimizeSamples = true);

    void CreateResourceBinding(IShaderResourceBinding** ppSRB);

    enum PSO_FLAGS : Uint32
    {
        PSO_FLAG_NONE = 0u,

        PSO_FLAG_USE_VERTEX_COLORS  = 1u << 0u,
        PSO_FLAG_USE_VERTEX_NORMALS = 1u << 1u,
        PSO_FLAG_USE_TEXCOORD0      = 1u << 2u,
        PSO_FLAG_USE_TEXCOORD1      = 1u << 3u,
        PSO_FLAG_USE_JOINTS         = 1u << 4u,

        PSO_FLAG_USE_COLOR_MAP     = 1u << 5u,
        PSO_FLAG_USE_NORMAL_MAP    = 1u << 6u,
        PSO_FLAG_USE_METALLIC_MAP  = 1u << 7u,
        PSO_FLAG_USE_ROUGHNESS_MAP = 1u << 8u,
        PSO_FLAG_USE_PHYS_DESC_MAP = 1u << 9u,
        PSO_FLAG_USE_AO_MAP        = 1u << 10u,
        PSO_FLAG_USE_EMISSIVE_MAP  = 1u << 11u,
        PSO_FLAG_USE_IBL           = 1u << 12u,

        PSO_FLAG_ENABLE_DEBUG_VIEW         = 1u << 13u,
        PSO_FLAG_USE_TEXTURE_ATLAS         = 1u << 14u,
        PSO_FLAG_CONVERT_OUTPUT_TO_SRGB    = 1u << 15u,
        PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT = 1u << 16u,

        PSO_FLAG_LAST = PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT,

        PSO_FLAG_VERTEX_ATTRIBS =
            PSO_FLAG_USE_VERTEX_COLORS |
            PSO_FLAG_USE_VERTEX_NORMALS |
            PSO_FLAG_USE_TEXCOORD0 |
            PSO_FLAG_USE_TEXCOORD1 |
            PSO_FLAG_USE_JOINTS,

        PSO_FLAG_DEFAULT =
            PSO_FLAG_VERTEX_ATTRIBS |
            PSO_FLAG_USE_COLOR_MAP |
            PSO_FLAG_USE_NORMAL_MAP |
            PSO_FLAG_USE_PHYS_DESC_MAP |
            PSO_FLAG_USE_AO_MAP |
            PSO_FLAG_USE_EMISSIVE_MAP |
            PSO_FLAG_USE_IBL,

        PSO_FLAG_ALL = PSO_FLAG_LAST * 2u - 1u,
    };

    struct PSOKey
    {
        PSO_FLAGS Flags       = PSO_FLAG_NONE;
        bool      DoubleSided = false;

        constexpr PSOKey() noexcept {};
        constexpr PSOKey(PSO_FLAGS _Flags, bool _DoubleSided) noexcept :
            Flags{_Flags}, DoubleSided{_DoubleSided}
        {}

        constexpr bool operator==(const PSOKey& rhs) const noexcept
        {
            return Flags == rhs.Flags && DoubleSided == rhs.DoubleSided;
        }
        constexpr bool operator!=(const PSOKey& rhs) const noexcept
        {
            return Flags != rhs.Flags || DoubleSided != rhs.DoubleSided;
        }

        struct Hasher
        {
            size_t operator()(const PSOKey& Key) const noexcept
            {
                return ComputeHash(Key.Flags, Key.DoubleSided);
            }
        };
    };

    struct PbrPSOKey : PSOKey
    {
        static const PSO_FLAGS SupportedFlags;

        ALPHA_MODE AlphaMode = ALPHA_MODE_OPAQUE;

        PbrPSOKey() noexcept {};
        PbrPSOKey(PSO_FLAGS _Flags, ALPHA_MODE _AlphaMode, bool _DoubleSided) noexcept;

        bool operator==(const PbrPSOKey& rhs) const noexcept
        {
            return PSOKey::operator==(rhs) && AlphaMode == rhs.AlphaMode;
        }
        bool operator!=(const PbrPSOKey& rhs) const noexcept
        {
            return PSOKey::operator!=(rhs) || AlphaMode != rhs.AlphaMode;
        }

        struct Hasher
        {
            size_t operator()(const PbrPSOKey& Key) const noexcept
            {
                size_t Hash = PSOKey::Hasher{}(Key);
                HashCombine(Hash, Key.AlphaMode);
                return Hash;
            }
        };
    };


    struct WireframePSOKey : PSOKey
    {
        static const PSO_FLAGS SupportedFlags;

        WireframePSOKey() noexcept {};
        WireframePSOKey(PSO_FLAGS _Flags, bool _DoubleSided) noexcept;
    };

    template <typename PsoHashMapType, typename PsoKeyType>
    class PsoCacheAccessor
    {
    public:
        PsoCacheAccessor() noexcept
        {}

        PsoCacheAccessor(const PsoCacheAccessor&) = default;
        PsoCacheAccessor(PsoCacheAccessor&&)      = default;
        PsoCacheAccessor& operator=(const PsoCacheAccessor&) = default;
        PsoCacheAccessor& operator=(PsoCacheAccessor&&) = default;

        explicit operator bool() const noexcept
        {
            return m_pRenderer != nullptr && m_pPsoHashMap != nullptr && m_pGraphicsDesc != nullptr;
        }

        IPipelineState* Get(const PsoKeyType& Key, bool CreateIfNull) const
        {
            if (!*this)
            {
                UNEXPECTED("Accessor is not initialized");
                return nullptr;
            }
            return m_pRenderer->GetPSO(*m_pPsoHashMap, *m_pGraphicsDesc, Key, CreateIfNull);
        }

    private:
        friend PBR_Renderer;
        PsoCacheAccessor(PBR_Renderer&               Renderer,
                         PsoHashMapType&             PsoHashMap,
                         const GraphicsPipelineDesc& GraphicsDesc) noexcept :
            m_pRenderer{&Renderer},
            m_pPsoHashMap{&PsoHashMap},
            m_pGraphicsDesc{&GraphicsDesc}
        {}

    private:
        PBR_Renderer*               m_pRenderer     = nullptr;
        PsoHashMapType*             m_pPsoHashMap   = nullptr;
        const GraphicsPipelineDesc* m_pGraphicsDesc = nullptr;
    };

    using PbrPsoHashMapType       = std::unordered_map<PbrPSOKey, RefCntAutoPtr<IPipelineState>, PbrPSOKey::Hasher>;
    using WireframePsoHashMapType = std::unordered_map<WireframePSOKey, RefCntAutoPtr<IPipelineState>, WireframePSOKey::Hasher>;

    using PbrPsoCacheAccessor       = PsoCacheAccessor<PbrPsoHashMapType, PbrPSOKey>;
    using WireframePsoCacheAccessor = PsoCacheAccessor<WireframePsoHashMapType, WireframePSOKey>;

    PbrPsoCacheAccessor       GetPbrPsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc);
    WireframePsoCacheAccessor GetWireframePsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc);

    void InitCommonSRBVars(IShaderResourceBinding* pSRB,
                           IBuffer*                pCameraAttribs,
                           IBuffer*                pLightAttribs);

protected:
    ShaderMacroHelper DefineMacros(PSO_FLAGS PSOFlags) const;

    void GetVSInputStructAndLayout(PSO_FLAGS PSOFlags, std::string& VSInputStruct, InputLayoutDescX& InputLayout) const;

    template <typename KeyType>
    struct GetPSOHelper;

    template <typename KeyType>
    IPipelineState* GetPSO(std::unordered_map<KeyType, RefCntAutoPtr<IPipelineState>, typename KeyType::Hasher>& PSOCache,
                           const GraphicsPipelineDesc&                                                           GraphicsDesc,
                           KeyType                                                                               Key,
                           bool                                                                                  CreateIfNull);

    static std::string GetVSOutputStruct(PSO_FLAGS PSOFlags);
    static std::string GetPSOutputStruct(PSO_FLAGS PSOFlags);

    void CreateShaders(PSO_FLAGS               PSOFlags,
                       const char*             VSPath,
                       const char*             VSName,
                       const char*             PSPath,
                       const char*             PSName,
                       RefCntAutoPtr<IShader>& pVS,
                       RefCntAutoPtr<IShader>& pPS,
                       InputLayoutDescX&       InputLayout);

private:
    void PrecomputeBRDF(IDeviceContext* pCtx,
                        Uint32          NumBRDFSamples = 512);

    void CreatePbrPSO(PbrPsoHashMapType& PbrPSOs, const GraphicsPipelineDesc& GraphicsDesc, const PbrPSOKey& Key);
    void CreateWireframePSO(WireframePsoHashMapType& WireframePSOs, const GraphicsPipelineDesc& GraphicsDesc, const WireframePSOKey& Key);
    void CreateSignature();

protected:
    const InputLayoutDescX m_InputLayout;
    const CreateInfo       m_Settings;

    RenderDeviceWithCache_N m_Device;

    static constexpr Uint32     BRDF_LUT_Dim = 512;
    RefCntAutoPtr<ITextureView> m_pBRDF_LUT_SRV;

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

    RefCntAutoPtr<IBuffer> m_PBRAttribsCB;
    RefCntAutoPtr<IBuffer> m_PrecomputeEnvMapAttribsCB;
    RefCntAutoPtr<IBuffer> m_JointsBuffer;

    RefCntAutoPtr<IPipelineResourceSignature> m_ResourceSignature;

    std::unordered_map<GraphicsPipelineDesc, PbrPsoHashMapType>       m_PbrPSOs;
    std::unordered_map<GraphicsPipelineDesc, WireframePsoHashMapType> m_WireframePSOs;
};

DEFINE_FLAG_ENUM_OPERATORS(PBR_Renderer::PSO_FLAGS)

inline PBR_Renderer::PbrPSOKey::PbrPSOKey(PSO_FLAGS  _Flags,
                                          ALPHA_MODE _AlphaMode,
                                          bool       _DoubleSided) noexcept :
    PSOKey{_Flags & SupportedFlags, _DoubleSided},
    AlphaMode{_AlphaMode}
{}

inline PBR_Renderer::WireframePSOKey::WireframePSOKey(PSO_FLAGS _Flags,
                                                      bool      _DoubleSided) noexcept :
    PSOKey{_Flags & SupportedFlags, _DoubleSided}
{}

template <>
struct PBR_Renderer::GetPSOHelper<PBR_Renderer::PbrPSOKey>
{
    static constexpr decltype(&PBR_Renderer::CreatePbrPSO) CreatePSO = &PBR_Renderer::CreatePbrPSO;
};

template <>
struct PBR_Renderer::GetPSOHelper<PBR_Renderer::WireframePSOKey>
{
    static constexpr decltype(&PBR_Renderer::CreateWireframePSO) CreatePSO = &PBR_Renderer::CreateWireframePSO;
};

template <typename KeyType>
IPipelineState* PBR_Renderer::GetPSO(std::unordered_map<KeyType, RefCntAutoPtr<IPipelineState>, typename KeyType::Hasher>& PSOCache,
                                     const GraphicsPipelineDesc&                                                           GraphicsDesc,
                                     KeyType                                                                               Key,
                                     bool                                                                                  CreateIfNull)
{
    Key.Flags &= KeyType::SupportedFlags;

    if (!m_Settings.EnableIBL)
    {
        Key.Flags &= ~PSO_FLAG_USE_IBL;
    }
    if (!m_Settings.EnableAO)
    {
        Key.Flags &= ~PSO_FLAG_USE_AO_MAP;
    }
    if (!m_Settings.EnableEmissive)
    {
        Key.Flags &= ~PSO_FLAG_USE_EMISSIVE_MAP;
    }
    if (m_Settings.MaxJointCount == 0)
    {
        Key.Flags &= ~PSO_FLAG_USE_JOINTS;
    }
    if (m_Settings.UseSeparateMetallicRoughnessTextures)
    {
        DEV_CHECK_ERR((Key.Flags & PSO_FLAG_USE_PHYS_DESC_MAP) == 0, "Physical descriptor map is not enabled");
    }
    else
    {
        DEV_CHECK_ERR((Key.Flags & (PSO_FLAG_USE_METALLIC_MAP | PSO_FLAG_USE_ROUGHNESS_MAP)) == 0, "Separate metallic and roughness maps are not enaled");
    }

    auto it = PSOCache.find(Key);
    if (it == PSOCache.end())
    {
        if (CreateIfNull)
        {
            (this->*GetPSOHelper<KeyType>::CreatePSO)(PSOCache, GraphicsDesc, Key);
            it = PSOCache.find(Key);
            VERIFY_EXPR(it != PSOCache.end());
        }
    }

    return it != PSOCache.end() ? it->second.RawPtr() : nullptr;
}

} // namespace Diligent
