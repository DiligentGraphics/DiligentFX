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
/// Defines Diligent::TemporalAntiAliasing class implementing temporal anti-aliasing post-process effect.

#include <unordered_map>
#include <memory>

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/ResourceRegistry.hpp"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

#include "PostProcess/Common/interface/PostFXContext.hpp"
#include "PostProcess/Common/interface/PostFXRenderTechnique.hpp"

namespace Diligent
{


namespace HLSL
{
#include "../../../Shaders/Common/public/ShaderDefinitions.fxh"
#include "../../../Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"
} // namespace HLSL


/// Implements [temporal anti-aliasing](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/TemporalAntiAliasing).

/// \include{doc} DiligentFX/PostProcess/TemporalAntiAliasing/README.md
class TemporalAntiAliasing
{
public:
    /// Feature flags that control the behavior of the effect.
    enum FEATURE_FLAGS : Uint32
    {
        FEATURE_FLAG_NONE = 0u,

        // Use Gaussian weighting in the variance clipping step.
        FEATURE_FLAG_GAUSSIAN_WEIGHTING = 1u << 0u,

        // Use Catmull-Rom filter to sample the history buffer.
        FEATURE_FLAG_BICUBIC_FILTER = 1u << 1u,

        // Use YCoCg color space for color clipping.
        FEATURE_FLAG_YCOCG_COLOR_SPACE = 1u << 2u
    };

    /// Render attributes that are passed to the effect.
    struct RenderAttributes
    {
        /// Render device that may be used to create new objects needed for this frame, if any.
        IRenderDevice* pDevice = nullptr;

        /// Optional render state cache to optimize state loading.
        IRenderStateCache* pStateCache = nullptr;

        /// Device context that will record the rendering commands.
        IDeviceContext* pDeviceContext = nullptr;

        /// PostFX context.
        PostFXContext* pPostFXContext = nullptr;

        /// Shader resource view of the source color.
        ITextureView* pColorBufferSRV = nullptr;

        /// TAA settings.
        const HLSL::TemporalAntiAliasingAttribs* pTAAAttribs = nullptr;

        /// Accumulation buffer index.
        Uint32 AccumulationBufferIdx = 0;
    };

    /// Create info.
    struct CreateInfo
    {
        /// Whether to enable asynchronous shader and pipeline state creation.

        /// If enabled, the shaders and pipeline state objects will be created using
        /// the engine's asynchronous creation mechanism. While shaders are being
        /// compiled, the effect will do nothing and return the input color.
        bool EnableAsyncCreation = false;
    };

public:
    /// Creates a new instance of the effect.
    TemporalAntiAliasing(IRenderDevice* pDevice, const CreateInfo& CI);

    ~TemporalAntiAliasing();

    /// Returns the jitter offset for the specified accumulation buffer index.
    float2 GetJitterOffset(Uint32 AccumulationBufferIdx = 0) const;

    /// Prepares the effect for rendering.
    void PrepareResources(IRenderDevice*  pDevice,
                          IDeviceContext* pDeviceContext,
                          PostFXContext*  pPostFXContext,
                          FEATURE_FLAGS   FeatureFlag,
                          Uint32          AccumulationBufferIdx = 0);

    /// Executes the effect.
    void Execute(const RenderAttributes& RenderAttribs);

    /// Adds the ImGui controls to the UI.
    static bool UpdateUI(HLSL::TemporalAntiAliasingAttribs& TAAAttribs, FEATURE_FLAGS& FeatureFlags);

    /// Returns the shader resource view of the accumulated frame.
    ITextureView* GetAccumulatedFrameSRV(bool IsPrevFrame = false, Uint32 AccumulationBufferIdx = 0) const;

    /// Computes the jittered projection matrix.
    static inline float4x4 GetJitteredProjMatrix(float4x4 Proj, const float2& Jitter)
    {
        if (Proj.m33 == 0.f)
        {
            // Perspective projection.
            // Make jitter proportional to z so that it is constant in screen space.
            Proj.m20 += Jitter.x;
            Proj.m21 += Jitter.y;
        }
        else
        {
            // Orthographic projection.
            // Apply offsets directly.
            Proj.m30 += Jitter.x;
            Proj.m31 += Jitter.y;
        }
        return Proj;
    }

private:
    using RenderTechnique  = PostFXRenderTechnique;
    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;
    struct AccumulationBufferInfo;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION = 0,
        RENDER_TECH_COUNT
    };

    void PrepareShadersAndPSO(const RenderAttributes& RenderAttribs, FEATURE_FLAGS FeatureFlags, TEXTURE_FORMAT TextureFormat);

    void ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs, AccumulationBufferInfo& AccBuff);

    void ComputePlaceholderTexture(const RenderAttributes& RenderAttribs, AccumulationBufferInfo& AccBuff);

private:
    struct RenderTechniqueKey
    {
        const RENDER_TECH   RenderTech;
        const FEATURE_FLAGS FeatureFlags;

        RenderTechniqueKey(RENDER_TECH _RenderTech, FEATURE_FLAGS _FeatureFlags) :
            RenderTech{_RenderTech},
            FeatureFlags{_FeatureFlags}
        {}

        constexpr bool operator==(const RenderTechniqueKey& RHS) const
        {
            return RenderTech == RHS.RenderTech && FeatureFlags == RHS.FeatureFlags;
        }

        struct Hasher
        {
            size_t operator()(const RenderTechniqueKey& Key) const
            {
                return ComputeHash(Key.FeatureFlags, Key.FeatureFlags);
            }
        };
    };

    struct AccumulationBufferInfo
    {
        enum RESOURCE_ID : Uint32
        {
            RESOURCE_ID_CONSTANT_BUFFER,
            RESOURCE_ID_ACCUMULATED_BUFFER0,
            RESOURCE_ID_ACCUMULATED_BUFFER1,
            RESOURCE_ID_COUNT
        };

        ResourceRegistry Resources{RESOURCE_ID_COUNT};

        Uint32        Width           = 0;
        Uint32        Height          = 0;
        Uint32        CurrentFrameIdx = 0;
        Uint32        LastFrameIdx    = ~0u;
        FEATURE_FLAGS FeatureFlags    = FEATURE_FLAG_NONE;

        HLSL::TemporalAntiAliasingAttribs ShaderAttribs;

        RefCntAutoPtr<IShaderResourceBinding> SRB;

        void Prepare(PostFXContext* pPostFXContext, IRenderDevice* pDevice, IDeviceContext* pCtx, Uint32 Width, Uint32 Height, Uint32 CurrFrameIdx, FEATURE_FLAGS FeatureFlags);

        void UpdateConstantBuffer(IDeviceContext* pDeviceContext, const HLSL::TemporalAntiAliasingAttribs& Attribs);
    };

    std::unordered_map<RenderTechniqueKey, RenderTechnique, RenderTechniqueKey::Hasher> m_RenderTech;
    std::unordered_map<Uint32, AccumulationBufferInfo>                                  m_AccumulationBuffers;

    bool       m_AllPSOsReady = false;
    CreateInfo m_Settings;
};

DEFINE_FLAG_ENUM_OPERATORS(TemporalAntiAliasing::FEATURE_FLAGS)

} // namespace Diligent
