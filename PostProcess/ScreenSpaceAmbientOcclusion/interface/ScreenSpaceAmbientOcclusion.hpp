/*
 *  Copyright 2024-2025 Diligent Graphics LLC
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
/// Defines Diligent::ScreenSpaceAmbientOcclusion class implementing screen-space ambient occlusion post-process effect.

#include <unordered_map>
#include <vector>
#include <memory>

#include "../../../../DiligentCore/Common/interface/Timer.hpp"
#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

#include "PostProcess/Common/interface/PostFXRenderTechnique.hpp"
#include "PostProcess/Common/interface/PostFXContext.hpp"

namespace Diligent
{

namespace HLSL
{
struct ScreenSpaceAmbientOcclusionAttribs;
}

/// Implements [screen-space ambient occlusion post-process effect](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/ScreenSpaceAmbientOcclusion).

/// \include{doc} DiligentFX/PostProcess/ScreenSpaceAmbientOcclusion/README.md
class ScreenSpaceAmbientOcclusion
{
public:
    /// Feature flags that control the behavior of the effect.
    enum FEATURE_FLAGS : Uint32
    {
        /// No feature flags are set.
        FEATURE_FLAG_NONE = 0u,

        /// Use half-precision depth buffer.
        FEATURE_FLAG_HALF_PRECISION_DEPTH = 1u << 0u,

        /// Compute the effect in half resolution.
        FEATURE_FLAG_HALF_RESOLUTION = 1u << 1u,

        /// Use uniform weighting for the occlusion.
        FEATURE_FLAG_UNIFORM_WEIGHTING = 1u << 2u
    };

    /// Render attributes.
    struct RenderAttributes
    {
        /// Render device that may be used to create new objects needed for this frame, if any.
        IRenderDevice* pDevice = nullptr;

        /// Optional render state cache to optimize state loading.
        IRenderStateCache* pStateCache = nullptr;

        /// Device context that will record the rendering commands.
        IDeviceContext* pDeviceContext = nullptr;

        /// PostFX context
        PostFXContext* pPostFXContext = nullptr;

        /// Shader resource view of the source depth.
        ITextureView* pDepthBufferSRV = nullptr;

        /// Shader resource view of the source normal buffer
        ITextureView* pNormalBufferSRV = nullptr;

        /// SSAO settings
        const HLSL::ScreenSpaceAmbientOcclusionAttribs* pSSAOAttribs = nullptr;
    };

    /// Create info.
    struct CreateInfo
    {
        /// Whether to enable asynchronous shader and pipeline state creation.

        /// If enabled, the shaders and pipeline state objects will be created using
        /// the engine's asynchronous creation mechanism. While shaders are being
        /// compiled, the effect will do nothing.
        bool EnableAsyncCreation = false;
    };

public:
    /// Creates a new instance of the ScreenSpaceAmbientOcclusion class.
    ScreenSpaceAmbientOcclusion(IRenderDevice* pDevice, const CreateInfo& CI);

    ~ScreenSpaceAmbientOcclusion();

    /// Prepares the effect for rendering.
    void PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags);

    /// Executes the effect.
    void Execute(const RenderAttributes& RenderAttribs);

    /// Adds the ImGui controls to the UI.
    static bool UpdateUI(HLSL::ScreenSpaceAmbientOcclusionAttribs& SSRAttribs, FEATURE_FLAGS& FeatureFlags);

    /// Returns the shader resource view of the occlusion texture.
    ITextureView* GetAmbientOcclusionSRV() const;

private:
    using RenderTechnique  = PostFXRenderTechnique;
    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_DOWNSAMPLED_DEPTH_BUFFER = 0,
        RENDER_TECH_COMPUTE_PREFILTERED_DEPTH_BUFFER,
        RENDER_TECH_COMPUTE_AMBIENT_OCCLUSION,
        RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION,
        RENDER_TECH_COMPUTE_CONVOLUTED_DEPTH_HISTORY,
        RENDER_TECH_COMPUTE_RESAMPLED_HISTORY,
        RENDER_TECH_COMPUTE_SPATIAL_RECONSTRUCTION,
        RENDER_TECH_COMPUTE_BILATERAL_UPSAMPLING,
        RENDER_TECH_COUNT
    };

    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_INPUT_DEPTH = 0,
        RESOURCE_IDENTIFIER_INPUT_NORMAL,
        RESOURCE_IDENTIFIER_INPUT_LAST = RESOURCE_IDENTIFIER_INPUT_NORMAL,
        RESOURCE_IDENTIFIER_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_DEPTH_CHECKERBOARD_HALF_RES,
        RESOURCE_IDENTIFIER_DEPTH_PREFILTERED,
        RESOURCE_IDENTIFIER_DEPTH_PREFILTERED_INTERMEDIATE,
        RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED,
        RESOURCE_IDENTIFIER_DEPTH_CONVOLUTED_INTERMEDIATE,
        RESOURCE_IDENTIFIER_OCCLUSION,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY0,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY1,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH0,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_LENGTH1,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_CONVOLUTED_INTERMEDIATE,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESAMPLED,
        RESOURCE_IDENTIFIER_OCCLUSION_HISTORY_RESOLVED,
        RESOURCE_IDENTIFIER_OCCLUSION_UPSAMPLED,
        RESOURCE_IDENTIFIER_COUNT
    };

    bool PrepareShadersAndPSO(const RenderAttributes& RenderAttribs);

    void UpdateConstantBuffer(const RenderAttributes& RenderAttribs, bool ResetTimer);

    void ComputeDepthCheckerboard(const RenderAttributes& RenderAttribs);

    void ComputePrefilteredDepth(const RenderAttributes& RenderAttribs);

    void ComputeAmbientOcclusion(const RenderAttributes& RenderAttribs);

    void ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs);

    void ComputeConvolutedDepthHistory(const RenderAttributes& RenderAttribs);

    void ComputeResampledHistory(const RenderAttributes& RenderAttribs);

    void ComputeSpatialReconstruction(const RenderAttributes& RenderAttribs);

    void ComputeBilateralUpsampling(const RenderAttributes& RenderAttribs);

    void ComputePlaceholderTexture(const RenderAttributes& RenderAttribs);

    RenderTechnique& GetRenderTechnique(RENDER_TECH RenderTech);

private:
    struct RenderTechniqueKey
    {
        const RENDER_TECH   RenderTech;
        const FEATURE_FLAGS FeatureFlags;
        const bool          UseReverseDepth;

        RenderTechniqueKey(RENDER_TECH _RenderTech, FEATURE_FLAGS _FeatureFlags, bool _UseReverseDepth) :
            RenderTech{_RenderTech},
            FeatureFlags{_FeatureFlags},
            UseReverseDepth{_UseReverseDepth}
        {}

        constexpr bool operator==(const RenderTechniqueKey& RHS) const
        {
            return RenderTech == RHS.RenderTech &&
                FeatureFlags == RHS.FeatureFlags &&
                UseReverseDepth == RHS.UseReverseDepth;
        }

        struct Hasher
        {
            size_t operator()(const RenderTechniqueKey& Key) const
            {
                return ComputeHash(Key.FeatureFlags, Key.FeatureFlags, Key.UseReverseDepth);
            }
        };
    };

    std::unordered_map<RenderTechniqueKey, RenderTechnique, RenderTechniqueKey::Hasher> m_RenderTech;

    ResourceRegistry m_Resources{RESOURCE_IDENTIFIER_COUNT};

    std::unique_ptr<HLSL::ScreenSpaceAmbientOcclusionAttribs> m_SSAOAttribs;

    std::vector<RefCntAutoPtr<ITextureView>> m_ConvolutedHistoryMipMapRTV;
    std::vector<RefCntAutoPtr<ITextureView>> m_ConvolutedHistoryMipMapSRV;

    std::vector<RefCntAutoPtr<ITextureView>> m_ConvolutedDepthMipMapRTV;
    std::vector<RefCntAutoPtr<ITextureView>> m_ConvolutedDepthMipMapSRV;

    std::vector<RefCntAutoPtr<ITextureView>> m_PrefilteredDepthMipMapRTV;
    std::vector<RefCntAutoPtr<ITextureView>> m_PrefilteredDepthMipMapSRV;

    struct
    {
        TEXTURE_FORMAT PrefileteredDepth = TEX_FORMAT_R32_FLOAT;
        TEXTURE_FORMAT CheckerBoardDepth = TEX_FORMAT_R32_FLOAT;
        TEXTURE_FORMAT ConvolutionDepth  = TEX_FORMAT_R32_FLOAT;
        TEXTURE_FORMAT Occlusion         = TEX_FORMAT_R8_UNORM;
        TEXTURE_FORMAT HistoryLength     = TEX_FORMAT_R16_FLOAT;

    } m_BackBufferFormats;

    Uint32 m_BackBufferWidth  = 0;
    Uint32 m_BackBufferHeight = 0;
    Uint32 m_CurrentFrameIdx  = 0;
    Uint32 m_LastFrameIdx     = ~0u;

    FEATURE_FLAGS m_FeatureFlags    = FEATURE_FLAG_NONE;
    bool          m_UseReverseDepth = false;
    CreateInfo    m_Settings;

    Timer m_FrameTimer;
};

DEFINE_FLAG_ENUM_OPERATORS(ScreenSpaceAmbientOcclusion::FEATURE_FLAGS)

} // namespace Diligent
