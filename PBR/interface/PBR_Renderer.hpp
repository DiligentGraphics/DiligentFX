/*
 *  Copyright 2019-2025 Diligent Graphics LLC
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
#include <unordered_set>
#include <functional>
#include <array>

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
} // namespace HLSL

class PBR_Renderer
{
public:
    enum PSO_FLAGS : Uint64;

    enum VERTEX_ATTRIB_ID : Uint32
    {
        VERTEX_ATTRIB_ID_POSITION = 0,
        VERTEX_ATTRIB_ID_NORMAL,
        VERTEX_ATTRIB_ID_TEXCOORD0,
        VERTEX_ATTRIB_ID_TEXCOORD1,
        VERTEX_ATTRIB_ID_JOINTS,
        VERTEX_ATTRIB_ID_WEIGHTS,
        VERTEX_ATTRIB_ID_COLOR,
        VERTEX_ATTRIB_ID_TANGENT,
        VERTEX_ATTRIB_ID_COUNT
    };

    enum TEXTURE_ATTRIB_ID : Uint32
    {
        TEXTURE_ATTRIB_ID_BASE_COLOR = 0,
        TEXTURE_ATTRIB_ID_NORMAL,
        TEXTURE_ATTRIB_ID_PHYS_DESC,
        TEXTURE_ATTRIB_ID_METALLIC,
        TEXTURE_ATTRIB_ID_ROUGHNESS,
        TEXTURE_ATTRIB_ID_OCCLUSION,
        TEXTURE_ATTRIB_ID_EMISSIVE,
        TEXTURE_ATTRIB_ID_CLEAR_COAT,
        TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS,
        TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL,
        TEXTURE_ATTRIB_ID_SHEEN_COLOR,
        TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS,
        TEXTURE_ATTRIB_ID_ANISOTROPY,
        TEXTURE_ATTRIB_ID_IRIDESCENCE,
        TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS,
        TEXTURE_ATTRIB_ID_TRANSMISSION,
        TEXTURE_ATTRIB_ID_THICKNESS,
        TEXTURE_ATTRIB_ID_COUNT
    };

    static constexpr Uint16 InvalidMaterialTextureId = 0xFFFFu;

    /// Static indices assigned to each material texture at shader compile time
    /// (e.g. BaseColorTextureId, NormalTextureId, etc.).
    using StaticShaderTextureIdsArrayType = std::array<Uint16, TEXTURE_ATTRIB_ID_COUNT>;

    class PSOKey;

    /// Material texture array indexing mode.
    enum SHADER_TEXTURE_ARRAY_MODE : Uint8
    {
        /// Shader texture array is not used and textures are accessed by name:
        ///     - g_BaseColorMap
        ///     - g_PhysicalDescriptorMap
        ///     - g_NormalMap
        ///     - ...
        SHADER_TEXTURE_ARRAY_MODE_NONE = 0,

        /// Shader textures array is used and the indices are defined at shader compile time.
        ///     - g_MaterialTextures[BaseColorTextureId]
        ///     - g_MaterialTextures[PhysicalDescriptorTextureId]
        ///     - g_MaterialTextures[NormalTextureId]
        ///     - ...
        SHADER_TEXTURE_ARRAY_MODE_STATIC,

        /// Shader textures array is used and the indices are provided dynamically at run time
        /// through the TextureSlice field of the corresponding texture attribute.
        SHADER_TEXTURE_ARRAY_MODE_DYNAMIC
    };

    /// Skinning joints buffer mode.
    enum JOINTS_BUFFER_MODE : Uint8
    {
        /// Joints are stored in a uniform buffer.
        JOINTS_BUFFER_MODE_UNIFORM = 0,

        /// Joints are stored in a structured buffer.
        JOINTS_BUFFER_MODE_STRUCTURED,
    };

    /// Vertex position packing mode.
    enum VERTEX_POS_PACK_MODE : Uint8
    {
        /// Vertex positions are not packed and are stored as float3.
        VERTEX_POS_PACK_MODE_NONE = 0,

        /// Vertex positions are packed into two 32-bit uints using
        /// 21 bits for normalized x, y, z coordinates, see PackVertxPos64().
        VERTEX_POS_PACK_MODE_64_BIT,
    };

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

        /// Whether to enable clear coat.
        /// A pipeline state can use clear coat only if this flag is set to true.
        bool EnableClearCoat = false;

        /// Whether to enable sheen.
        /// A pipeline state can use sheen only if this flag is set to true.
        bool EnableSheen = false;

        /// Whether to enable anisotropy.
        /// A pipeline state can use anisotropy only if this flag is set to true.
        bool EnableAnisotropy = false;

        /// Whether to enable iridescence.
        /// A pipeline state can use iridescence only if this flag is set to true.
        bool EnableIridescence = false;

        /// Whether to enable transmission.
        /// A pipeline state can use transmission only if this flag is set to true.
        bool EnableTransmission = false;

        /// Whether to enable volume.
        /// A pipeline state can use volume only if this flag is set to true.
        bool EnableVolume = false;

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

        /// Whether to enable shadows.
        /// A pipeline state can use shadows only if this flag is set to true.
        bool EnableShadows = false;

        /// Whether to allow hot shader reload.
        ///
        /// \remarks    When hot shader reload is enabled, the renderer will need
        ///             to keep copies of generated shaders in the shader source factory.
        ///             This adds some overhead and should only be used in development mode.
        bool AllowHotShaderReload = false;

        /// Whether shader matrices are laid out in row-major order in GPU memory.
        ///
        /// \remarks    By default, shader matrices are laid out in column-major order
        ///             in GPU memory. If this option is set to true, shaders will be compiled
        ///             with the SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR flag and
        ///             use the row-major layout.
        bool PackMatrixRowMajor = false;

        /// Whether to use skin pre-transform before applying joint transformations.
        bool UseSkinPreTransform = false;

        /// Whether vertex normals are packed into a single 32-bit uint, see PackVertexNormal().
        bool PackVertexNormals = false;

        /// Whether vertex colors are packed into a single 32-bit uint.
        bool PackVertexColors = false;

        /// Vertex position packing mode, see VERTEX_POS_PACK_MODE.
        VERTEX_POS_PACK_MODE VertexPosPackMode = VERTEX_POS_PACK_MODE_NONE;

        /// PCF shadow kernel size.
        /// Allowed values are 2, 3, 5, 7.
        Uint32 PCFKernelSize = 3;

        /// Shader textures array access mode, see SHADER_TEXTURE_ARRAY_MODE.
        SHADER_TEXTURE_ARRAY_MODE ShaderTexturesArrayMode = SHADER_TEXTURE_ARRAY_MODE_NONE;

        /// User-provided material textures array size.
        ///
        /// \remarks    This parameter is ignored if ShaderTexturesArrayMode is SHADER_TEXTURE_ARRAY_MODE_NONE.
        ///             If this parameter is set to 0, the renderer will define the array size.
        ///             If it is not zero, the client should provide the GetStaticShaderTextureIds
        ///             callback function to define texture indices.
        Uint32 MaterialTexturesArraySize = 0;

        /// The size of the shader primitive array.
        /// When 0, single primitive will be used.
        Uint32 PrimitiveArraySize = 0;

        /// The maximum number of lights.
        Uint32 MaxLightCount = 16;

        /// The maximum number of shadow-casting lights.
        Uint32 MaxShadowCastingLightCount = 8;

        /// The number of OIT layers.
        /// If set to 0, OIT will be disabled.
        Uint32 OITLayerCount = 0;

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

        /// Immutable sampler for clear coat map texture.
        SamplerDesc ClearCoatMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for sheen texture.
        SamplerDesc SheenMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for anisotropy texture.
        SamplerDesc AnisotropyMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for iridescence texture.
        SamplerDesc IridescenceMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for transmission texture.
        SamplerDesc TransmissionMapImmutableSampler = DefaultSampler;

        /// Immutable sampler for thickness texture.
        SamplerDesc ThicknessMapImmutableSampler = DefaultSampler;

        /// The maximum number of joints.
        ///
        /// If set to 0, the animation will be disabled.
        Uint32 MaxJointCount = 64;

        /// Joints buffer mode.
        JOINTS_BUFFER_MODE JointsBufferMode = JOINTS_BUFFER_MODE_UNIFORM;

        /// The number of samples for BRDF LUT creation.
        Uint32 NumBRDFSamples = 512;

        /// If Sheen is enabled, this parameter specifies the path to the sheen look-up table.
        const char* SheenAlbedoScalingLUTPath = nullptr;

        /// If IBL and Sheen are enabled, this parameter specifies the path to the
        /// preintegrated Charlie BRDF look-up table.
        const char* PreintegratedCharlieBRDFPath = nullptr;

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
        ///                     float3 Tangent : ATTRIB7; // If PSO_FLAG_USE_VERTEX_TANGENTS is set
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
        TEX_COLOR_CONVERSION_MODE TexColorConversionMode = TEX_COLOR_CONVERSION_MODE_NONE;

        struct PSMainSourceInfo
        {
            std::string OutputStruct;
            std::string Footer;
        };
        /// An optional user-provided callback function that is used to generate the pixel
        /// shader's main function source code for the specified PSO flags. If null, the renderer
        /// will use the default implementation.
        std::function<PSMainSourceInfo(PSO_FLAGS PsoFlags)> GetPSMainSource = nullptr;

        /// An optional user-provided callback function that returns static material texture indices
        /// for the specified PSO key. If null, the renderer will assign the indices automatically.
        ///
        /// \remarks    This function is called only if ShaderTexturesArrayMode is set SHADER_TEXTURE_ARRAY_MODE_STATIC.
        ///
        ///             The main usage scenario for this function is to implement "static" bindless
        ///             mode, where texture indices are assigned at shader compile time and hard-coded
        ///             into PSO. The client can use the Key.UserValue to identify the shader indices.
        std::function<StaticShaderTextureIdsArrayType(const PSOKey& Key)> GetStaticShaderTextureIds = nullptr;

        /// A pointer to the user-provided primitive attribs buffer.
        /// If null, the renderer will allocate the buffer.
        IBuffer* pPrimitiveAttribsCB = nullptr;

        /// A pointer to the user-provided material attribs buffer.
        /// If null, the renderer will allocate the buffer.
        IBuffer* pMaterialAttribsCB = nullptr;

        /// A pointer to the user-provided joints buffer.
        /// If null, the renderer will allocate the buffer.
        IBuffer* pJointsBuffer = nullptr;

        /// Texture attribute index info
        std::array<int, TEXTURE_ATTRIB_ID_COUNT> TextureAttribIndices{};

        CreateInfo() noexcept
        {
            TextureAttribIndices.fill(-1);
        }
    };

    enum ALPHA_MODE : Uint8
    {
        ALPHA_MODE_OPAQUE = 0,
        ALPHA_MODE_MASK,
        ALPHA_MODE_BLEND,
        ALPHA_MODE_NUM_MODES
    };

    enum PBR_WORKFLOW : Uint8
    {
        PBR_WORKFLOW_METALL_ROUGH = 0,
        PBR_WORKFLOW_SPEC_GLOSS,
        PBR_WORKFLOW_UNLIT
    };

    enum LIGHT_TYPE : Uint8
    {
        LIGHT_TYPE_UNKNOWN = 0,
        LIGHT_TYPE_DIRECTIONAL,
        LIGHT_TYPE_POINT,
        LIGHT_TYPE_SPOT
    };

    /// Debug view type
    enum class DebugViewType : Uint8
    {
        None,
        Texcoord0,
        Texcoord1,
        BaseColor,
        Transparency,
        Occlusion,
        Emissive,
        Metallic,
        Roughness,
        DiffuseColor,
        SpecularColor,
        Reflectance90,
        MeshNormal,
        ShadingNormal,
        MotionVectors,
        NdotV,
        PunctualLighting,
        DiffuseIBL,
        SpecularIBL,
        WhiteBaseColor,
        ClearCoat,
        ClearCoatFactor,
        ClearCoatRoughness,
        ClearCoatNormal,
        Sheen,
        SheenColor,
        SheenRoughness,
        AnisotropyStrength,
        AnisotropyDirection,
        Iridescence,
        IridescenceFactor,
        IridescenceThickness,
        Transmission,
        Thickness,
        SceneDepth,
        NumDebugViews
    };

    enum class LoadingAnimationMode : Uint8
    {
        None,
        Always,
        Transitioning,
        Count
    };

    enum class RenderPassType : Uint8
    {
        Main,
        Shadow,
        OITLayers,
        Count
    };

    /// Initializes the renderer
    PBR_Renderer(IRenderDevice*     pDevice,
                 IRenderStateCache* pStateCache,
                 IDeviceContext*    pCtx,
                 const CreateInfo&  CI,
                 bool               InitSignature = true);

    virtual ~PBR_Renderer();

    // clang-format off
    IRenderDevice* GetDevice() const               { return m_Device; }
    ITextureView* GetIrradianceCubeSRV() const     { return m_pIrradianceCubeSRV; }
    ITextureView* GetPrefilteredEnvMapSRV() const  { return m_pPrefilteredEnvMapSRV; }
    ITextureView* GetPreintegratedGGX_SRV() const  { return m_pPreintegratedGGX_SRV; }
    ITextureView* GetWhiteTexSRV() const           { return m_pWhiteTexSRV; }
    ITextureView* GetBlackTexSRV() const           { return m_pBlackTexSRV; }
    ITextureView* GetDefaultNormalMapSRV() const   { return m_pDefaultNormalMapSRV; }
    IBuffer*      GetPBRPrimitiveAttribsCB() const {return m_PBRPrimitiveAttribsCB;}
    IBuffer*      GetPBRMaterialAttribsCB() const  {return m_PBRMaterialAttribsCB;}
    IBuffer*      GetJointsBuffer() const          {return m_JointsBuffer;}
    // clang-format on

    /// Precompute cubemaps used by IBL.
    ///
    /// \remarks If NumDiffuseSamples or NumSpecularSamples is 0,
    ///          the renderer will choose the optimal number of samples.
    void PrecomputeCubemaps(IDeviceContext* pCtx,
                            ITextureView*   pEnvironmentMap,
                            Uint32          NumDiffuseSamples  = 0,
                            Uint32          NumSpecularSamples = 0,
                            bool            OptimizeSamples    = true);

    void CreateResourceBinding(IShaderResourceBinding** ppSRB, Uint32 Idx = 0) const;

#define PSO_FLAG_BIT(Bit) (Uint64{1} << Uint64{Bit})
    enum PSO_FLAGS : Uint64
    {
        PSO_FLAG_NONE = 0u,

        PSO_FLAG_USE_COLOR_MAP                 = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_BASE_COLOR),
        PSO_FLAG_USE_NORMAL_MAP                = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_NORMAL),
        PSO_FLAG_USE_PHYS_DESC_MAP             = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_PHYS_DESC),
        PSO_FLAG_USE_METALLIC_MAP              = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_METALLIC),
        PSO_FLAG_USE_ROUGHNESS_MAP             = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_ROUGHNESS),
        PSO_FLAG_USE_AO_MAP                    = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_OCCLUSION),
        PSO_FLAG_USE_EMISSIVE_MAP              = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_EMISSIVE),
        PSO_FLAG_USE_CLEAR_COAT_MAP            = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_CLEAR_COAT),
        PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP  = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS),
        PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP     = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL),
        PSO_FLAG_USE_SHEEN_COLOR_MAP           = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_SHEEN_COLOR),
        PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP       = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS),
        PSO_FLAG_USE_ANISOTROPY_MAP            = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_ANISOTROPY),
        PSO_FLAG_USE_IRIDESCENCE_MAP           = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_IRIDESCENCE),
        PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS),
        PSO_FLAG_USE_TRANSMISSION_MAP          = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_TRANSMISSION),
        PSO_FLAG_USE_THICKNESS_MAP             = PSO_FLAG_BIT(TEXTURE_ATTRIB_ID_THICKNESS),

        PSO_FLAG_LAST_TEXTURE = PSO_FLAG_USE_THICKNESS_MAP,
        PSO_FLAG_ALL_TEXTURES = PSO_FLAG_LAST_TEXTURE * 2ull - 1ull,

        PSO_FLAG_USE_VERTEX_COLORS   = PSO_FLAG_BIT(17),
        PSO_FLAG_USE_VERTEX_NORMALS  = PSO_FLAG_BIT(18),
        PSO_FLAG_USE_VERTEX_TANGENTS = PSO_FLAG_BIT(19),
        PSO_FLAG_USE_TEXCOORD0       = PSO_FLAG_BIT(20),
        PSO_FLAG_USE_TEXCOORD1       = PSO_FLAG_BIT(21),
        PSO_FLAG_USE_JOINTS          = PSO_FLAG_BIT(22),
        PSO_FLAG_ENABLE_CLEAR_COAT   = PSO_FLAG_BIT(23),
        PSO_FLAG_ENABLE_SHEEN        = PSO_FLAG_BIT(24),
        PSO_FLAG_ENABLE_ANISOTROPY   = PSO_FLAG_BIT(25),
        PSO_FLAG_ENABLE_IRIDESCENCE  = PSO_FLAG_BIT(26),
        PSO_FLAG_ENABLE_TRANSMISSION = PSO_FLAG_BIT(27),
        PSO_FLAG_ENABLE_VOLUME       = PSO_FLAG_BIT(28),

        PSO_FLAG_USE_IBL                   = PSO_FLAG_BIT(29),
        PSO_FLAG_USE_LIGHTS                = PSO_FLAG_BIT(30),
        PSO_FLAG_USE_TEXTURE_ATLAS         = PSO_FLAG_BIT(31),
        PSO_FLAG_ENABLE_TEXCOORD_TRANSFORM = PSO_FLAG_BIT(32),
        PSO_FLAG_CONVERT_OUTPUT_TO_SRGB    = PSO_FLAG_BIT(33),
        PSO_FLAG_ENABLE_CUSTOM_DATA_OUTPUT = PSO_FLAG_BIT(34),
        PSO_FLAG_ENABLE_TONE_MAPPING       = PSO_FLAG_BIT(35),
        PSO_FLAG_UNSHADED                  = PSO_FLAG_BIT(36),
        PSO_FLAG_COMPUTE_MOTION_VECTORS    = PSO_FLAG_BIT(37),
        PSO_FLAG_ENABLE_SHADOWS            = PSO_FLAG_BIT(38),

        PSO_FLAG_LAST = PSO_FLAG_ENABLE_SHADOWS,

        PSO_FLAG_FIRST_USER_DEFINED = PSO_FLAG_LAST << 1ull,

        PSO_FLAG_VERTEX_ATTRIBS =
            PSO_FLAG_USE_VERTEX_COLORS |
            PSO_FLAG_USE_VERTEX_NORMALS |
            PSO_FLAG_USE_VERTEX_TANGENTS |
            PSO_FLAG_USE_TEXCOORD0 |
            PSO_FLAG_USE_TEXCOORD1 |
            PSO_FLAG_USE_JOINTS,

        PSO_FLAG_DEFAULT_TEXTURES =
            PSO_FLAG_USE_COLOR_MAP |
            PSO_FLAG_USE_NORMAL_MAP |
            PSO_FLAG_USE_PHYS_DESC_MAP |
            PSO_FLAG_USE_AO_MAP |
            PSO_FLAG_USE_EMISSIVE_MAP,

        PSO_FLAG_DEFAULT =
            PSO_FLAG_VERTEX_ATTRIBS |
            PSO_FLAG_DEFAULT_TEXTURES |
            PSO_FLAG_USE_IBL |
            PSO_FLAG_USE_LIGHTS |
            PSO_FLAG_ENABLE_TONE_MAPPING,

        PSO_FLAG_ALL = PSO_FLAG_LAST * 2ull - 1ull,

        PSO_FLAG_ALL_USER_DEFINED = ~(PSO_FLAG_FIRST_USER_DEFINED - 1ull)
    };

    static std::string GetPSOFlagsString(PSO_FLAGS Flags);
    static const char* GetAlphaModeString(ALPHA_MODE AlphaMode);
    static const char* GetRenderPassTypeString(RenderPassType Type);

    class PSOKey
    {
    public:
        constexpr PSOKey() noexcept {};

        PSOKey(RenderPassType       _Type,
               PSO_FLAGS            _Flags,
               ALPHA_MODE           _AlphaMode,
               CULL_MODE            _CullMode,
               DebugViewType        _DebugView        = DebugViewType::None,
               LoadingAnimationMode _LoadingAnimation = LoadingAnimationMode::None,
               Uint64               _UserValue        = 0) noexcept;

        PSOKey(RenderPassType       _Type,
               PSO_FLAGS            _Flags,
               CULL_MODE            _CullMode,
               DebugViewType        _DebugView        = DebugViewType::None,
               LoadingAnimationMode _LoadingAnimation = LoadingAnimationMode::None,
               Uint64               _UserValue        = 0) noexcept :
            PSOKey{_Type, _Flags, ALPHA_MODE_OPAQUE, _CullMode, _DebugView, _LoadingAnimation, _UserValue}
        {}

        PSOKey(PSO_FLAGS     _Flags,
               ALPHA_MODE    _AlphaMode,
               CULL_MODE     _CullMode,
               const PSOKey& Other) noexcept :
            PSOKey{Other.GetType(), _Flags, _AlphaMode, _CullMode, Other.GetDebugView(), Other.GetLoadingAnimation(), Other.GetUserValue()}
        {}

        PSOKey(PSO_FLAGS     _Flags,
               const PSOKey& Other) noexcept :
            PSOKey{_Flags, Other.GetAlphaMode(), Other.GetCullMode(), Other}
        {}

        constexpr bool operator==(const PSOKey& rhs) const noexcept
        {
            // clang-format off
            return Hash             == rhs.Hash      &&
                   Type             == rhs.Type      &&
                   Flags            == rhs.Flags     &&
                   CullMode         == rhs.CullMode  &&
                   AlphaMode        == rhs.AlphaMode &&
                   DebugView        == rhs.DebugView &&
                   LoadingAnimation == rhs.LoadingAnimation &&
                   UserValue        == rhs.UserValue;
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

        constexpr RenderPassType       GetType() const noexcept { return Type; }
        constexpr PSO_FLAGS            GetFlags() const noexcept { return Flags; }
        constexpr CULL_MODE            GetCullMode() const noexcept { return CullMode; }
        constexpr ALPHA_MODE           GetAlphaMode() const noexcept { return AlphaMode; }
        constexpr DebugViewType        GetDebugView() const noexcept { return DebugView; }
        constexpr LoadingAnimationMode GetLoadingAnimation() const noexcept { return LoadingAnimation; }
        constexpr Uint64               GetUserValue() const noexcept { return UserValue; }

    private:
        RenderPassType       Type             = RenderPassType::Main;
        PSO_FLAGS            Flags            = PSO_FLAG_NONE;
        ALPHA_MODE           AlphaMode        = ALPHA_MODE_OPAQUE;
        CULL_MODE            CullMode         = CULL_MODE_BACK;
        DebugViewType        DebugView        = DebugViewType::None;
        LoadingAnimationMode LoadingAnimation = LoadingAnimationMode::None;
        Uint64               UserValue        = 0;
        size_t               Hash             = 0;
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

        enum GET_FLAGS : Uint32
        {
            GET_FLAG_NONE           = 0u,
            GET_FLAG_CREATE_IF_NULL = 1u << 0u,
            GET_FLAG_ASYNC_COMPILE  = 1u << 1u
        };
        IPipelineState* Get(const PSOKey& Key, GET_FLAGS Flags = GET_FLAG_NONE) const
        {
            if (!*this)
            {
                UNEXPECTED("Accessor is not initialized");
                return nullptr;
            }
            return m_pRenderer->GetPSO(*m_pPsoHashMap, *m_pGraphicsDesc, Key, Flags);
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

    struct OITResources
    {
        RefCntAutoPtr<IBuffer>  Layers;
        RefCntAutoPtr<ITexture> Tail;

        explicit operator bool() const { return Layers && Tail; }
    };

    PsoCacheAccessor GetPsoCacheAccessor(const GraphicsPipelineDesc& GraphicsDesc);

    void InitCommonSRBVars(IShaderResourceBinding* pSRB,
                           IBuffer*                pFrameAttribs,
                           bool                    BindPrimitiveAttribsBuffer = true,
                           bool                    BindMaterialAttribsBuffer  = true,
                           ITextureView*           pShadowMap                 = nullptr) const;

    void SetMaterialTexture(IShaderResourceBinding* pSRB, ITextureView* pTexSRV, TEXTURE_ATTRIB_ID TextureId) const;

    void SetOITResources(IShaderResourceBinding* pSRB, const OITResources& OITResources) const;

    /// Initializes internal renderer parameters.
    ///
    /// \remarks    The function initializes the following parameters:
    ///             - PrefilteredCubeLastMip
    void SetInternalShaderParameters(HLSL::PBRRendererShaderParameters& Renderer);

    /// Returns the PBR primitive attributes shader data size for the given PSO flags.
    Uint32 GetPBRPrimitiveAttribsSize(PSO_FLAGS Flags, Uint32 CustomDataSize = sizeof(float4)) const;

    /// Returns the PBR material attributes shader data size for the given PSO flags.
    Uint32 GetPBRMaterialAttribsSize(PSO_FLAGS Flags) const;

    /// Returns the PBR Frame attributes shader data size for the given light count.
    static Uint32 GetPRBFrameAttribsSize(Uint32 LightCount, Uint32 ShadowCastingLightCount);

    /// Returns the PBR Frame attributes shader data size.
    Uint32 GetPRBFrameAttribsSize() const;

    const CreateInfo& GetSettings() const { return m_Settings; }

    inline static constexpr PSO_FLAGS GetTextureAttribPSOFlag(TEXTURE_ATTRIB_ID AttribId);

    /// Processes enabled texture attributes with the given handler.
    template <typename HandlerType>
    inline static void ProcessTexturAttribs(PSO_FLAGS PSOFlags, HandlerType&& Handler);

    struct WriteSkinningDataAttribs
    {
        PSO_FLAGS    PSOFlags   = PSO_FLAG_NONE;
        const Uint32 JointCount = 0;

        const float4x4* JointMatrices     = nullptr;
        const float4x4* PrevJointMatrices = nullptr; // If PSO_FLAG_COMPUTE_MOTION_VECTORS
    };
    /// Writes skinning data to the cbJointTransforms buffer as expected by the shader.
    static void* WriteSkinningData(void* pDst, const WriteSkinningDataAttribs& Attribs, bool PackMatrixRowMajor, Uint32 MaxJointCount);
    void*        WriteSkinningData(void* pDst, const WriteSkinningDataAttribs& Attribs);

    static Uint32 GetJointsDataSize(Uint32 MaxJointCount, bool UsePrevFrameTransforms);
    Uint32        GetJointsDataSize(Uint32 JointCount, PSO_FLAGS PSOFlags) const;
    Uint32        GetJointsBufferSize() const;
    const char*   GetJointTransformsVarName() const;

    /// Packs normal into a single 32-bit uint.
    ///
    /// \remarks    The function assumes that the input vector is normalized.
    static inline Uint32 PackVertexNormal(const float3& Normal);

    /// Packs vertex position into two 32-bit uints.
    ///
    /// \remarks    Bias and Scale are used to map the vertex position to the [0, 1] range as follows:
    ///                 NormPos = (Pos + Bias) * Scale
    ///             Typically, Bias is set to the negated minimum vertex position and Scale is set to
    ///             one over the maximum vertex position minus the minimum vertex position.
    static inline void PackVertexPos64(const float3& Pos, const float3& Bias, const float3& Scale, Uint32& U0, Uint32& U1);

    static const char* GetDebugViewTypeString(DebugViewType DebugView);

    static constexpr TEXTURE_FORMAT OITTailFmt = TEX_FORMAT_RGBA8_UNORM;

    static OITResources CreateOITResources(IRenderDevice* pDevice, Uint32 Width, Uint32 Height, Uint32 LayerCount);
    OITResources        CreateOITResources(Uint32 Width, Uint32 Height) const;
    void                CreateClearOITLayersSRB(IBuffer* pFrameAttribs, IBuffer* OITLayers, IShaderResourceBinding** ppSRB) const;
    void                CreateRWOITLayersSRB(IBuffer* OITLayers, ITextureView* pDepthSRV, IShaderResourceBinding** ppSRB) const;
    void                ClearOITLayers(IDeviceContext* pCtx, IShaderResourceBinding* pSRB, Uint32 Width, Uint32 Height) const;
    void                CreateApplyOITAttenuationPSO(const TEXTURE_FORMAT* RTVFormats,
                                                     Uint32                NumRenderTargets,
                                                     Uint32                RenderTargetMask,
                                                     TEXTURE_FORMAT        DepthFormat,
                                                     IPipelineState**      ppPSO) const;
    void                CreateApplyOITAttenuationSRB(IBuffer*                 pFrameAttribs,
                                                     IBuffer*                 OITLayers,
                                                     ITexture*                OITTail,
                                                     IShaderResourceBinding** ppSRB) const;
    void                ApplyOITAttenuation(IDeviceContext* pCtx, IPipelineState* pPSO, IShaderResourceBinding* pSRB) const;

protected:
    ShaderMacroHelper DefineMacros(const PSOKey& Key) const;

    void GetVSInputStructAndLayout(PSO_FLAGS PSOFlags, std::string& VSInputStruct, InputLayoutDescX& InputLayout) const;

    IPipelineState* GetPSO(PsoHashMapType&             PsoHashMap,
                           const GraphicsPipelineDesc& GraphicsDesc,
                           const PSOKey&               Key,
                           PsoCacheAccessor::GET_FLAGS GetFlags);

    static std::string GetVSOutputStruct(PSO_FLAGS PSOFlags, bool UseVkPointSize, bool UsePrimitiveId);
    static std::string GetPSOutputStruct(PSO_FLAGS PSOFlags);

    void         CreateSignature();
    virtual void CreateCustomSignature(PipelineResourceSignatureDescX&& SignatureDesc);

private:
    void PrecomputeBRDF(IDeviceContext* pCtx,
                        Uint32          NumBRDFSamples = 512);

    void CreatePSO(PsoHashMapType&             PsoHashMap,
                   const GraphicsPipelineDesc& GraphicsDesc,
                   const PSOKey&               Key,
                   bool                        AsyncCompile);

    void CreateClearOITLayersPSO();

protected:
    enum IBL_FEATURE_FLAGS : Uint32
    {
        IBL_FEATURE_FLAG_NONE             = 0,
        IBL_FEATURE_FLAG_OPTIMIZE_SAMPLES = 1u << 0u,
    };
    DECLARE_FRIEND_FLAG_ENUM_OPERATORS(IBL_FEATURE_FLAGS)

    struct IBL_PSOKey
    {
        enum PSO_TYPE : Uint8
        {
            PSO_TYPE_IRRADIANCE_CUBE = 0,
            PSO_TYPE_PREFILTERED_ENV_MAP,
        };
        enum ENV_MAP_TYPE : Uint8
        {
            ENV_MAP_TYPE_CUBE = 0,
            ENV_MAP_TYPE_SPHERE,
            ENV_MAP_TYPE_NUM_TYPES
        };

        const PSO_TYPE          PSOType;
        const ENV_MAP_TYPE      EnvMapType;
        const IBL_FEATURE_FLAGS FeatureFlags;
        const TEXTURE_FORMAT    RTVFormat;

        IBL_PSOKey(PSO_TYPE          _PSOType,
                   ENV_MAP_TYPE      _EnvMapType,
                   IBL_FEATURE_FLAGS _FeatureFlags,
                   TEXTURE_FORMAT    _Format) :
            PSOType{_PSOType},
            EnvMapType{_EnvMapType},
            FeatureFlags{_FeatureFlags},
            RTVFormat{_Format}
        {}

        constexpr bool operator==(const IBL_PSOKey& rhs) const
        {
            return (PSOType == rhs.PSOType &&
                    EnvMapType == rhs.EnvMapType &&
                    FeatureFlags == rhs.FeatureFlags &&
                    RTVFormat == rhs.RTVFormat);
        }

        struct Hasher
        {
            size_t operator()(const IBL_PSOKey& Key) const
            {
                return ComputeHash(Key.PSOType, Key.EnvMapType, Key.FeatureFlags, Key.RTVFormat);
            }
        };
    };

    struct IBL_RenderTechnique
    {
        RefCntAutoPtr<IPipelineState>         PSO{};
        RefCntAutoPtr<IShaderResourceBinding> SRB{};

        bool IsInitialized() const
        {
            return PSO != nullptr && SRB != nullptr;
        }
    };

    using IBL_PipelineStateObjectCache = std::unordered_map<IBL_PSOKey, IBL_RenderTechnique, IBL_PSOKey::Hasher>;

    const InputLayoutDescX m_InputLayout;

    CreateInfo m_Settings;

    RenderDeviceWithCache_N m_Device;

    static constexpr Uint32     BRDF_LUT_Dim = 512;
    RefCntAutoPtr<ITextureView> m_pPreintegratedGGX_SRV;
    RefCntAutoPtr<ITextureView> m_pPreintegratedCharlie_SRV;
    RefCntAutoPtr<ITextureView> m_pSheenAlbedoScaling_LUT_SRV;

    RefCntAutoPtr<ITextureView> m_pWhiteTexSRV;
    RefCntAutoPtr<ITextureView> m_pBlackTexSRV;
    RefCntAutoPtr<ITextureView> m_pDefaultNormalMapSRV;
    RefCntAutoPtr<ITextureView> m_pDefaultPhysDescSRV;

    static constexpr TEXTURE_FORMAT PrefilteredEnvMapFmt = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr TEXTURE_FORMAT IrradianceCubeFmt    = TEX_FORMAT_RGBA16_FLOAT;
    static constexpr Uint32         IrradianceCubeDim    = 64;
    static constexpr Uint32         PrefilteredEnvMapDim = 256;

    RefCntAutoPtr<ITextureView> m_pIrradianceCubeSRV;
    RefCntAutoPtr<ITextureView> m_pPrefilteredEnvMapSRV;

    IBL_PipelineStateObjectCache m_IBL_PSOCache;

    RefCntAutoPtr<IBuffer> m_PBRPrimitiveAttribsCB;
    RefCntAutoPtr<IBuffer> m_PBRMaterialAttribsCB;
    RefCntAutoPtr<IBuffer> m_PrecomputeEnvMapAttribsCB;
    RefCntAutoPtr<IBuffer> m_JointsBuffer;

    std::unordered_set<std::string> m_GeneratedIncludes;

    std::vector<RefCntAutoPtr<IPipelineResourceSignature>> m_ResourceSignatures;

    std::unordered_map<PSOKey, RefCntAutoPtr<IShader>, PSOKey::Hasher> m_VertexShaders;
    std::unordered_map<PSOKey, RefCntAutoPtr<IShader>, PSOKey::Hasher> m_PixelShaders;

    std::unordered_map<GraphicsPipelineDesc, PsoHashMapType> m_PSOs;

    static constexpr Uint32                   ClearOITLayersThreadGroupSize = 16;
    RefCntAutoPtr<IPipelineState>             m_ClearOITLayersPSO;
    RefCntAutoPtr<IPipelineResourceSignature> m_RWOITLayersSignature;
    RefCntAutoPtr<IPipelineResourceSignature> m_OITAttenuationSignature;

    std::unique_ptr<StaticShaderTextureIdsArrayType> m_StaticShaderTextureIds;
};

DEFINE_FLAG_ENUM_OPERATORS(PBR_Renderer::PSO_FLAGS)
DEFINE_FLAG_ENUM_OPERATORS(PBR_Renderer::IBL_FEATURE_FLAGS)
DEFINE_FLAG_ENUM_OPERATORS(PBR_Renderer::PsoCacheAccessor::GET_FLAGS)

inline constexpr PBR_Renderer::PSO_FLAGS PBR_Renderer::GetTextureAttribPSOFlag(PBR_Renderer::TEXTURE_ATTRIB_ID AttribId)
{
    // clang-format off
    static_assert(PSO_FLAG_USE_COLOR_MAP                 == 1u << TEXTURE_ATTRIB_ID_BASE_COLOR,            "PSO_FLAG_USE_COLOR_MAP must be 1 << TEXTURE_ATTRIB_ID_BASE_COLOR");
    static_assert(PSO_FLAG_USE_NORMAL_MAP                == 1u << TEXTURE_ATTRIB_ID_NORMAL,                "PSO_FLAG_USE_NORMAL_MAP must be 1 << TEXTURE_ATTRIB_ID_NORMAL");
    static_assert(PSO_FLAG_USE_PHYS_DESC_MAP             == 1u << TEXTURE_ATTRIB_ID_PHYS_DESC,             "PSO_FLAG_USE_PHYS_DESC_MAP must be 1 << TEXTURE_ATTRIB_ID_PHYS_DESC");
    static_assert(PSO_FLAG_USE_METALLIC_MAP              == 1u << TEXTURE_ATTRIB_ID_METALLIC,              "PSO_FLAG_USE_METALLIC_MAP must be 1 << TEXTURE_ATTRIB_ID_METALLIC");
    static_assert(PSO_FLAG_USE_ROUGHNESS_MAP             == 1u << TEXTURE_ATTRIB_ID_ROUGHNESS,             "PSO_FLAG_USE_ROUGHNESS_MAP must be 1 << TEXTURE_ATTRIB_ID_ROUGHNESS");
    static_assert(PSO_FLAG_USE_AO_MAP                    == 1u << TEXTURE_ATTRIB_ID_OCCLUSION,             "PSO_FLAG_USE_AO_MAP must be 1 << TEXTURE_ATTRIB_ID_OCCLUSION");
    static_assert(PSO_FLAG_USE_EMISSIVE_MAP              == 1u << TEXTURE_ATTRIB_ID_EMISSIVE,              "PSO_FLAG_USE_EMISSIVE_MAP must be 1 << TEXTURE_ATTRIB_ID_EMISSIVE");
    static_assert(PSO_FLAG_USE_CLEAR_COAT_MAP            == 1u << TEXTURE_ATTRIB_ID_CLEAR_COAT,            "PSO_FLAG_USE_CLEAR_COAT_MAP must be 1 << TEXTURE_ATTRIB_ID_CLEAR_COAT");
    static_assert(PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP  == 1u << TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS,  "PSO_FLAG_USE_CLEAR_COAT_ROUGHNESS_MAP must be 1 << TEXTURE_ATTRIB_ID_CLEAR_COAT_ROUGHNESS");
    static_assert(PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP     == 1u << TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL,     "PSO_FLAG_USE_CLEAR_COAT_NORMAL_MAP must be 1 << TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL");
    static_assert(PSO_FLAG_USE_SHEEN_COLOR_MAP           == 1u << TEXTURE_ATTRIB_ID_SHEEN_COLOR,           "PSO_FLAG_USE_SHEEN_COLOR_MAP must be 1 << TEXTURE_ATTRIB_ID_SHEEN_COLOR");
    static_assert(PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP       == 1u << TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS,       "PSO_FLAG_USE_SHEEN_ROUGHNESS_MAP must be 1 << TEXTURE_ATTRIB_ID_SHEEN_ROUGHNESS");
    static_assert(PSO_FLAG_USE_ANISOTROPY_MAP            == 1u << TEXTURE_ATTRIB_ID_ANISOTROPY,            "PSO_FLAG_USE_ANISOTROPY_MAP must be 1 << TEXTURE_ATTRIB_ID_ANISOTROPY");
    static_assert(PSO_FLAG_USE_IRIDESCENCE_MAP           == 1u << TEXTURE_ATTRIB_ID_IRIDESCENCE,           "PSO_FLAG_USE_IRIDESCENCE_MAP must be 1 << TEXTURE_ATTRIB_ID_IRIDESCENCE");
    static_assert(PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP == 1u << TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS, "PSO_FLAG_USE_IRIDESCENCE_THICKNESS_MAP must be 1 << TEXTURE_ATTRIB_ID_IRIDESCENCE_THICKNESS");
    static_assert(PSO_FLAG_USE_TRANSMISSION_MAP          == 1u << TEXTURE_ATTRIB_ID_TRANSMISSION,          "PSO_FLAG_USE_TRANSMISSION_MAP must be 1 << TEXTURE_ATTRIB_ID_TRANSMISSION");
    static_assert(PSO_FLAG_USE_THICKNESS_MAP             == 1u << TEXTURE_ATTRIB_ID_THICKNESS,             "PSO_FLAG_USE_THICKNESS_MAP must be 1 << TEXTURE_ATTRIB_ID_THICKNESS");
    // clang-format on
    static_assert(PBR_Renderer::PSO_FLAG_LAST_TEXTURE == 1u << 16u, "Did you add new texture flag? You may need to handle it here.");

    return static_cast<PBR_Renderer::PSO_FLAGS>(Uint64{1} << AttribId);
}

template <typename HandlerType>
inline void PBR_Renderer::ProcessTexturAttribs(PBR_Renderer::PSO_FLAGS PSOFlags, HandlerType&& Handler)
{
    PSOFlags &= PBR_Renderer::PSO_FLAG_ALL_TEXTURES;
    int AttribIndex = 0;
    while (PSOFlags != 0)
    {
        const TEXTURE_ATTRIB_ID AttribId = static_cast<TEXTURE_ATTRIB_ID>(PlatformMisc::GetLSB(static_cast<Uint64>(PSOFlags)));
        Handler(AttribIndex++, AttribId);
        PSOFlags &= ~GetTextureAttribPSOFlag(AttribId);
    }
}

inline Uint32 PBR_Renderer::PackVertexNormal(const float3& Normal)
{
    if (Normal == float3{})
        return ~0u;

    Uint32 x = static_cast<Uint32>(clamp((Normal.x + 1.f) * 32767.f, 0.f, 65535.f));
    Uint32 y = static_cast<Uint32>(clamp((Normal.y + 1.f) * 16383.f, 0.f, 32767.f));
    Uint32 z = Normal.z >= 0 ? 0 : 1;
    return x | (y << 16) | (z << 31);
}

inline void PBR_Renderer::PackVertexPos64(const float3& Pos, const float3& Bias, const float3& Scale, Uint32& U0, Uint32& U1)
{
    //      X           Y             Y		     Z
    // | 0 ... 20 | 21 ... 31|   | 0 ... 9 | 10 ... 30 |
    //      21         11             10         21
    constexpr float3 U21Scale{static_cast<float>((1 << 21) - 1)};
    const float3     NormPos = (Pos + Bias) * Scale;
    const uint3      U21Pos  = clamp(NormPos * U21Scale, float3{0}, U21Scale).Recast<uint>();

    U0 = U21Pos.x | (U21Pos.y << 21u);
    U1 = (U21Pos.y >> 11u) | (U21Pos.z << 10u);
}

} // namespace Diligent
