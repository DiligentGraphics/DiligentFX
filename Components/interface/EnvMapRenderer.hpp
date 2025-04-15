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

#pragma once

/// \file
/// Defines EnvMapRenderer class

#include <unordered_map>
#include <vector>
#include <memory>
#include <array>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/HashUtils.hpp"
#include "../../../DiligentCore/Primitives/interface/FlagEnum.h"

namespace Diligent
{

namespace HLSL
{
struct ToneMappingAttribs;
} // namespace HLSL


/// Environment map renderer.
class EnvMapRenderer
{
public:
    /// Environment map renderer creation info.
    struct CreateInfo
    {
        /// Render device.
        IRenderDevice* pDevice = nullptr;

        /// An optional render state cache.
        IRenderStateCache* pStateCache = nullptr;

        /// A buffer that contains camera attributes.
        IBuffer* pCameraAttribsCB = nullptr;

        /// The number of render targets.
        Uint8 NumRenderTargets = 1;

        /// Render target formats.
        TEXTURE_FORMAT RTVFormats[DILIGENT_MAX_RENDER_TARGETS] = {TEX_FORMAT_RGBA8_UNORM_SRGB};

        /// Depth-stencil view format.
        TEXTURE_FORMAT DSVFormat = TEX_FORMAT_D32_FLOAT;

        /// A bit mask that defines the render targets to render to.

        /// If bit N is set, the N-th render target's color write mask will be set to
        /// Diligent::COLOR_MASK_ALL. Otherwise, it will be set to Diligent::COLOR_MASK_NONE.
        Uint32 RenderTargetMask = 0x1u;

        const char* PSMainSource = nullptr;

        /// Whether shader matrices are laid out in row-major order in GPU memory.

        /// By default, shader matrices are laid out in column-major order
        /// in GPU memory. If this option is set to true, shaders will be compiled
        /// with the Diligent::SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR flag and
        /// use the row-major layout.
        bool PackMatrixRowMajor = false;
    };
    /// Creates a new environment map renderer.
    EnvMapRenderer(const CreateInfo& CI);

    ~EnvMapRenderer();

    /// Option flags.
    enum OPTION_FLAGS : Uint32
    {
        /// No options.
        OPTION_FLAG_NONE = 0u,

        /// Manually convert shader output to sRGB color space.
        OPTION_FLAG_CONVERT_OUTPUT_TO_SRGB = 1u << 0u,

        /// Compute motion vectors.
        OPTION_FLAG_COMPUTE_MOTION_VECTORS = 1u << 1u,

        /// Use reverse depth (i.e. near plane is at 1.0, far plane is at 0.0).
        OPTION_FLAG_USE_REVERSE_DEPTH = 1u << 2u
    };

    /// Environment map renderer attributes.
    struct RenderAttribs
    {
        /// Environment map cube map or sphere map.
        ITextureView* pEnvMap = nullptr;

        /// Average log luminance for tone mapping.
        float AverageLogLum = 1;

        /// Mip level of the environment map to use.
        float MipLevel = 0;

        /// Alpha value to write to the output render target.
        float Alpha = 1;

        /// Option flags.
        OPTION_FLAGS Options = OPTION_FLAG_NONE;

        /// Scaling factor to apply to the environment map.
        float3 Scale = {1, 1, 1};
    };

    /// Prepares the environment map renderer for rendering.
    void Prepare(IDeviceContext*                 pContext,
                 const RenderAttribs&            Attribs,
                 const HLSL::ToneMappingAttribs& ToneMapping);

    /// Renders the environment map.
    void Render(IDeviceContext* pContext);

private:
    struct PSOKey
    {
        enum ENV_MAP_TYPE : Uint8
        {
            ENV_MAP_TYPE_CUBE = 0,
            ENV_MAP_TYPE_SPHERE,
            ENV_MAP_TYPE_COUNT
        };

        const int          ToneMappingMode;
        const OPTION_FLAGS Flags;
        const ENV_MAP_TYPE EnvMapType;

        PSOKey(int          _ToneMappingMode,
               OPTION_FLAGS _Flags,
               ENV_MAP_TYPE _EnvMapType) :
            ToneMappingMode{_ToneMappingMode},
            Flags{_Flags},
            EnvMapType{_EnvMapType}
        {}

        constexpr bool operator==(const PSOKey& rhs) const
        {
            return (ToneMappingMode == rhs.ToneMappingMode &&
                    Flags == rhs.Flags &&
                    EnvMapType == rhs.EnvMapType);
        }

        struct Hasher
        {
            size_t operator()(const PSOKey& Key) const
            {
                return ComputeHash(Key.ToneMappingMode, Key.Flags, Key.EnvMapType);
            }
        };
    };
    IPipelineState* GetPSO(const PSOKey& Key);

    RefCntAutoPtr<IRenderDevice>     m_pDevice;
    RefCntAutoPtr<IRenderStateCache> m_pStateCache;
    RefCntAutoPtr<IBuffer>           m_pCameraAttribsCB;
    RefCntAutoPtr<IBuffer>           m_RenderAttribsCB;

    const std::vector<TEXTURE_FORMAT> m_RTVFormats;
    const TEXTURE_FORMAT              m_DSVFormat;
    const Uint32                      m_RenderTargetMask;
    const std::string                 m_PSMainSource;
    const bool                        m_PackMatrixRowMajor;

    std::unordered_map<PSOKey, RefCntAutoPtr<IPipelineState>, PSOKey::Hasher> m_PSOs;

    IPipelineState* m_pCurrentPSO = nullptr;

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    IShaderResourceVariable*              m_pEnvMapVar = nullptr;

    struct EnvMapShaderAttribs;
    std::unique_ptr<EnvMapShaderAttribs> m_ShaderAttribs;
};
DEFINE_FLAG_ENUM_OPERATORS(EnvMapRenderer::OPTION_FLAGS);

} // namespace Diligent
