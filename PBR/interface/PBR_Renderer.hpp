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

namespace HLSL
{
struct PBRRendererShaderParameters;
struct PBRMaterialBasicAttribs;
struct PBRMaterialTextureAttribs;
} // namespace HLSL

class PBR_Renderer
{
public:
    enum PSO_FLAGS : Uint32;

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

        /// Whether to create default textures.
        ///
        /// \remarks If set to true, the following textures will be created:
        ///             - White texture
        ///             - Black texture
        ///             - Default normal map
        ///             - Default physical description map
        bool CreateDefaultTextures = true;

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

        /// An optional user-provided callback function that is used to generate the pixel
        /// shader's main function source code for the specified PSO flags. If null, the renderer
        /// will use the default implementation.
        std::function<std::string(PSO_FLAGS PsoFlags)> GetPSMainSource = nullptr;

        /// A pointer to the user-provided primitive attribs buffer.
        /// If null, the renderer will allocate the buffer.
        IBuffer* pPrimitiveAttribsCB = nullptr;

        /// Texture attribute index info.
        struct ShaderTextureAttribIndex
        {
            /// Texture attribute index name (e.g. "BaseColorTextureAttribId").
            const char* Name = nullptr;

            /// Texture attribute index value.
            Uint32 Index = 0;
        };
        /// An array of NumShaderTextureAttribIndices texture attribute index info.
        const ShaderTextureAttribIndex* pShaderTextureAttribIndices = nullptr;

        /// The number of texture attributes in pShaderTextureAttribIndices array.
        Uint32 NumShaderTextureAttribIndices = 0;
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
    enum class DebugViewType : Uint8
    {
        None,
        Texcoord0,
        Texcoord1,
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
    IRenderDevice* GetDevice() const               { return m_Device; }
    ITextureView* GetIrradianceCubeSRV() const     { return m_pIrradianceCubeSRV; }
    ITextureView* GetPrefilteredEnvMapSRV() const  { return m_pPrefilteredEnvMapSRV; }
    ITextureView* GetBRDFLUTSRV() const            { return m_pBRDF_LUT_SRV; }
    ITextureView* GetWhiteTexSRV() const           { return m_pWhiteTexSRV; }
    ITextureView* GetBlackTexSRV() const           { return m_pBlackTexSRV; }
    ITextureView* GetDefaultNormalMapSRV() const   { return m_pDefaultNormalMapSRV; }
    IBuffer*      GetPBRPrimitiveAttribsCB() const {return m_PBRPrimitiveAttribsCB;}
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

        PSO_FLAG_USE_TEXTURE_ATLAS         = 1u << 13u,
        PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM = 1u << 14u,
        PSO_FLAG_CONVERT_OUTPUT_TO_SRGB    = 1u << 15u,
        PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT = 1u << 16u,
        PSO_FLAG_ENABLE_TONE_MAPPING       = 1u << 17u,
        PSO_FLAG_UNSHADED                  = 1u << 18u,

        PSO_FLAG_LAST = PSO_FLAG_UNSHADED,

        PSO_FLAG_FIRST_USER_DEFINED = PSO_FLAG_LAST << 1u,

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
            PSO_FLAG_USE_IBL |
            PSO_FLAG_ENABLE_TONE_MAPPING,

        PSO_FLAG_ALL = PSO_FLAG_LAST * 2u - 1u,

        PSO_FLAG_ALL_USER_DEFINED = ~(PSO_FLAG_FIRST_USER_DEFINED - 1u)
    };

    class PSOKey
    {
    public:
        constexpr PSOKey() noexcept {};
        PSOKey(PSO_FLAGS _Flags, ALPHA_MODE _AlphaMode, bool _DoubleSided, DebugViewType DebugView) noexcept;
        PSOKey(PSO_FLAGS _Flags, bool _DoubleSided, DebugViewType DebugView) noexcept :
            PSOKey{_Flags, ALPHA_MODE_OPAQUE, _DoubleSided, DebugView}
        {}

        constexpr bool operator==(const PSOKey& rhs) const noexcept
        {
            // clang-format off
            return Hash        == rhs.Hash        &&
                   Flags       == rhs.Flags       &&
                   DoubleSided == rhs.DoubleSided &&
                   AlphaMode   == rhs.AlphaMode   &&
                   DebugView   == rhs.DebugView;
            // clang-format on
        }
        constexpr bool operator!=(const PSOKey& rhs) const noexcept
        {
            return !(*this == rhs);
        }

        struct Hasher
        {
            size_t operator()(const PSOKey& Key) const noexcept
            {
                return Key.Hash;
            }
        };

        constexpr PSO_FLAGS     GetFlags() const noexcept { return Flags; }
        constexpr bool          IsDoubleSided() const noexcept { return DoubleSided; }
        constexpr ALPHA_MODE    GetAlphaMode() const noexcept { return AlphaMode; }
        constexpr DebugViewType GetDebugView() const noexcept { return DebugView; }

    private:
        PSO_FLAGS     Flags       = PSO_FLAG_NONE;
        ALPHA_MODE    AlphaMode   = ALPHA_MODE_OPAQUE;
        bool          DoubleSided = false;
        DebugViewType DebugView   = DebugViewType::None;
        size_t        Hash        = 0;
    };

    using PsoHashMapType = std::unordered_map<PSOKey, RefCntAutoPtr<IPipelineState>, PSOKey::Hasher>;

    class PsoCacheAccessor
    {
    public:
        PsoCacheAccessor() noexcept
        {}

        // clang-format off
        PsoCacheAccessor(const PsoCacheAccessor&)            = default;
        PsoCacheAccessor(PsoCacheAccessor&&)                 = default;
        PsoCacheAccessor& operator=(const PsoCacheAccessor&) = default;
        PsoCacheAccessor& operator=(PsoCacheAccessor&&)      = default;
        // clang-format on

        explicit operator bool() const noexcept
        {
            return m_pRenderer != nullptr && m_pPsoHashMap != nullptr && m_pGraphicsDesc != nullptr;
        }

        IPipelineState* Get(const PSOKey& Key, bool CreateIfNull) const
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

    PsoCacheAccessor GetPsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc);

    void InitCommonSRBVars(IShaderResourceBinding* pSRB, IBuffer* pFrameAttribs);

    /// Initializes internal renderer parameters.
    ///
    /// \remarks    The function initializes the following parameters:
    ///             - PrefilteredCubeMipLevels
    void SetInternalShaderParameters(HLSL::PBRRendererShaderParameters& Renderer);

    struct PBRPrimitiveShaderAttribsData
    {
        const float4x4*                        NodeMatrix        = nullptr;
        const Uint32                           JointCount        = 0;
        const HLSL::PBRMaterialBasicAttribs*   BasicAttribs      = nullptr;
        const HLSL::PBRMaterialTextureAttribs* TextureAttribs    = nullptr;
        Uint32                                 NumTextureAttribs = 0;
        const void*                            CustomData        = nullptr;
        size_t                                 CustomDataSize    = 0;
    };

    void* WritePBRPrimitiveShaderAttribs(void* pDstShaderAttribs, const PBRPrimitiveShaderAttribsData& AttribsData);

    Uint32 GetNumShaderTextureAttribs() const { return m_NumShaderTextureAttribs; }

    Uint32 GetPBRPrimitiveAttribsSize() const;

protected:
    ShaderMacroHelper DefineMacros(PSO_FLAGS PSOFlags, DebugViewType DebugView) const;

    void GetVSInputStructAndLayout(PSO_FLAGS PSOFlags, std::string& VSInputStruct, InputLayoutDescX& InputLayout) const;

    IPipelineState* GetPSO(PsoHashMapType&             PsoHashMap,
                           const GraphicsPipelineDesc& GraphicsDesc,
                           const PSOKey&               Key,
                           bool                        CreateIfNull);

    static std::string GetVSOutputStruct(PSO_FLAGS PSOFlags, bool UseVkPointSize);
    static std::string GetPSOutputStruct(PSO_FLAGS PSOFlags);

private:
    void PrecomputeBRDF(IDeviceContext* pCtx,
                        Uint32          NumBRDFSamples = 512);

    void CreatePSO(PsoHashMapType& PsoHashMap, const GraphicsPipelineDesc& GraphicsDesc, const PSOKey& Key);
    void CreateSignature();

protected:
    const InputLayoutDescX m_InputLayout;

    const std::vector<std::string>                          m_ShaderTextureAttribIndexNames;
    const std::vector<CreateInfo::ShaderTextureAttribIndex> m_ShaderTextureAttribIndices;

    const CreateInfo m_Settings;

    // The number of texture attributes in PBRMaterialShaderInfo.Textures array
    // (aka PBR_NUM_TEXTURE_ATTRIBUTES).
    //
    // \remarks    This value is equal to the maximum index in m_ShaderTextureAttribIndices array.
    //             Typically, it will be the same as m_Settings.NumShaderTextureAttribIndices,
    //             but it may be greater if texture attribute indices are not consecutive.
    const Uint32 m_NumShaderTextureAttribs;

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

    RefCntAutoPtr<IBuffer> m_PBRPrimitiveAttribsCB;
    RefCntAutoPtr<IBuffer> m_PrecomputeEnvMapAttribsCB;
    RefCntAutoPtr<IBuffer> m_JointsBuffer;

    RefCntAutoPtr<IPipelineResourceSignature> m_ResourceSignature;

    std::unordered_map<GraphicsPipelineDesc, PsoHashMapType> m_PSOs;
};

DEFINE_FLAG_ENUM_OPERATORS(PBR_Renderer::PSO_FLAGS)

} // namespace Diligent
