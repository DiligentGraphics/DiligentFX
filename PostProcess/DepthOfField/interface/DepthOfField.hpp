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
struct DepthOfFieldAttribs;
}

class DepthOfField
{
public:
    enum FEATURE_FLAGS : Uint32
    {
        FEATURE_FLAG_NONE                      = 0u,
        FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING = 1u << 0u,
        FEATURE_FLAG_ENABLE_KARIS_INVERSE      = 1u << 1u,
        FEATURE_FLAG_ASYNC_CREATION            = 1u << 2u
    };

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

        /// Shader resource view of the source color.
        ITextureView* pColorBufferSRV = nullptr;

        /// Shader resource view of the source depth.
        ITextureView* pDepthBufferSRV = nullptr;

        /// Bloom settings
        const HLSL::DepthOfFieldAttribs* pDOFAttribs = nullptr;
    };

public:
    DepthOfField(IRenderDevice* pDevice);

    ~DepthOfField();

    void PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags);

    void Execute(const RenderAttributes& RenderAttribs);

    static bool UpdateUI(HLSL::DepthOfFieldAttribs& Attribs, FEATURE_FLAGS& FeatureFlags);

    ITextureView* GetDepthOfFieldTextureSRV() const;

private:
    using RenderTechnique  = PostFXRenderTechnique;
    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION,
        RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_TEMPORAL,
        RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_DILATION,
        RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_SEPARATED,
        RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_BLUR_X,
        RENDER_TECH_COMPUTE_CIRCLE_OF_CONFUSION_BLUR_Y,
        RENDER_TECH_COMPUTE_PREFILTERED_TEXTURE,
        RENDER_TECH_COMPUTE_BOKEH_FIRST_PASS,
        RENDER_TECH_COMPUTE_BOKEH_SECOND_PASS,
        RENDER_TECH_COMPUTE_POST_FILTERED_TEXTURE,
        RENDER_TECH_COMPUTE_COMBINED_TEXTURE,
        RENDER_TECH_COUNT
    };

    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_INPUT_COLOR = 0,
        RESOURCE_IDENTIFIER_INPUT_DEPTH,
        RESOURCE_IDENTIFIER_INPUT_LAST = RESOURCE_IDENTIFIER_INPUT_DEPTH,
        RESOURCE_IDENTIFIER_GAUSS_KERNEL_TEXTURE,
        RESOURCE_IDENTIFIER_BOKEH_LARGE_KERNEL_TEXTURE,
        RESOURCE_IDENTIFIER_BOKEH_SMALL_KERNEL_TEXTURE,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEXTURE,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP0, // We don't use mip levels, because WebGL doesn't support concurrent read/write to separate mip levels
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP1,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP2,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP3,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_LAST_MIP = RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_MIP3,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_DILATION_TEXTURE_INTERMEDIATE,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE0,
        RESOURCE_IDENTIFIER_CIRCLE_OF_CONFUSION_TEMPORAL_TEXTURE1,
        RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE0, // Reuse texture for bokeh second pass
        RESOURCE_IDENTIFIER_PREFILTERED_TEXTURE1, // Reuse texture for bokeh second pass
        RESOURCE_IDENTIFIER_BOKEH_TEXTURE0,       // Reuse texture for post-filtered texture
        RESOURCE_IDENTIFIER_BOKEH_TEXTURE1,       // Reuse texture for post-filtered texture
        RESOURCE_IDENTIFIER_COMBINED_TEXTURE,
        RESOURCE_IDENTIFIER_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_COUNT
    };

    bool PrepareShadersAndPSO(const RenderAttributes& RenderAttribs, FEATURE_FLAGS FeatureFlags);

    void UpdateConstantBuffers(const RenderAttributes& RenderAttribs, bool ResetTimer);

    void ComputeCircleOfConfusion(const RenderAttributes& RenderAttribs);

    void ComputeTemporalCircleOfConfusion(const RenderAttributes& RenderAttribs);

    void ComputeSeparatedCircleOfConfusion(const RenderAttributes& RenderAttribs);

    void ComputeDilationCircleOfConfusion(const RenderAttributes& RenderAttribs);

    void ComputeCircleOfConfusionBlurX(const RenderAttributes& RenderAttribs);

    void ComputeCircleOfConfusionBlurY(const RenderAttributes& RenderAttribs);

    void ComputePrefilteredTexture(const RenderAttributes& RenderAttribs);

    void ComputeBokehFirstPass(const RenderAttributes& RenderAttribs);

    void ComputeBokehSecondPass(const RenderAttributes& RenderAttribs);

    void ComputePostFilteredTexture(const RenderAttributes& RenderAttribs);

    void ComputeCombinedTexture(const RenderAttributes& RenderAttribs);

    void ComputePlaceholderTexture(const RenderAttributes& RenderAttribs);

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
            return RenderTech == RHS.RenderTech &&
                FeatureFlags == RHS.FeatureFlags;
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

    std::unique_ptr<HLSL::DepthOfFieldAttribs> m_pDOFAttribs;

    Uint32 m_BackBufferWidth  = 0;
    Uint32 m_BackBufferHeight = 0;
    Uint32 m_CurrentFrameIdx  = 0;

    FEATURE_FLAGS m_FeatureFlags = FEATURE_FLAG_NONE;

    Timer m_FrameTimer;
};

DEFINE_FLAG_ENUM_OPERATORS(DepthOfField::FEATURE_FLAGS)

} // namespace Diligent
