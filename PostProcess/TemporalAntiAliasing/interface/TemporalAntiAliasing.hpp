/*
 *  Copyright 2024 Diligent Graphics LLC
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
struct TemporalAntiAliasingAttribs;
}

class TemporalAntiAliasing
{
public:
    enum FEATURE_FLAGS : Uint32
    {
        FEATURE_FLAG_NONE = 0,

        // Indicates that the application uses a reversed depth buffer.
        FEATURE_FLAG_REVERSED_DEPTH = 1u << 0u,

        // Use Gaussian weighting in the variance clipping step.
        FEATURE_FLAG_GAUSSIAN_WEIGHTING = 1u << 1u,

        // Use Catmull-Rom filter to sample the history buffer.
        FEATURE_FLAG_BICUBIC_FILTER = 1u << 2u,

        // Use depth buffer from the previous frame to calculate disocclusion.
        // The corresponding texture should be provided through RenderAttributes::pPrevDepthBufferSRV.
        FEATURE_FLAG_DEPTH_DISOCCLUSION = 1u << 3u,

        // Use motion buffer from the previous frame to calculate disocclusion,
        // The corresponding texture should be provided through RenderAttributes::pPrevMotionVectorsSRV.
        FEATURE_FLAG_MOTION_DISOCCLUSION = 1u << 4u
    };

    struct ResourceAttribs
    {
        TEXTURE_FORMAT AccumulatedBufferFormat = TEX_FORMAT_UNKNOWN;
    };

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

        /// Shader resource view of the source depth.
        ITextureView* pDepthBufferSRV = nullptr;

        /// Shader resource view of the motion vectors.
        ITextureView* pMotionVectorsSRV = nullptr;

        /// Shader resource view of the source depth from previous frame.
        ITextureView* pPrevDepthBufferSRV = nullptr;

        /// Shader resource view of the motion vectors from previous frame.
        ITextureView* pPrevMotionVectorsSRV = nullptr;

        /// Render features.
        FEATURE_FLAGS FeatureFlag = FEATURE_FLAG_NONE;

        /// TAA settings.
        const HLSL::TemporalAntiAliasingAttribs* pTAAAttribs = nullptr;
    };

public:
    TemporalAntiAliasing(IRenderDevice* pDevice);

    ~TemporalAntiAliasing();

    float2 GetJitterOffset() const;

    void PrepareResources(IRenderDevice* pDevice, PostFXContext* pPostFXContext, const ResourceAttribs& Attribs);

    void Execute(const RenderAttributes& RenderAttribs);

    bool UpdateUI(HLSL::TemporalAntiAliasingAttribs& TAAAttribs);

    ITextureView* GetAccumulatedFrameSRV() const;

private:
    using RenderTechnique  = PostFXRenderTechnique;
    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION = 0,
        RENDER_TECH_COUNT
    };

    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_INPUT_COLOR = 0,
        RESOURCE_IDENTIFIER_INPUT_DEPTH,
        RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS,
        RESOURCE_IDENTIFIER_INPUT_PREV_DEPTH,
        RESOURCE_IDENTIFIER_INPUT_PREV_MOTION_VECTORS,
        RESOURCE_IDENTIFIER_INPUT_LAST = RESOURCE_IDENTIFIER_INPUT_PREV_MOTION_VECTORS,

        RESOURCE_IDENTIFIER_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER0,
        RESOURCE_IDENTIFIER_ACCUMULATED_BUFFER1,
        RESOURCE_IDENTIFIER_COUNT
    };

    void ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs);

    RenderTechnique& GetRenderTechnique(RENDER_TECH RenderTech, FEATURE_FLAGS FeatureFlags);

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

    std::unordered_map<RenderTechniqueKey, RenderTechnique, RenderTechniqueKey::Hasher> m_RenderTech;

    ResourceRegistry m_Resources{RESOURCE_IDENTIFIER_COUNT};

    Uint32 m_BackBufferWidth  = 0;
    Uint32 m_BackBufferHeight = 0;
    Uint32 m_CurrentFrameIdx  = 0;
    Uint32 m_LastFrameIdx     = ~0u;

    std::unique_ptr<HLSL::TemporalAntiAliasingAttribs> m_ShaderAttribs;
};

DEFINE_FLAG_ENUM_OPERATORS(TemporalAntiAliasing::FEATURE_FLAGS)

} // namespace Diligent
