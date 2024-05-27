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
#include <vector>
#include <memory>

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
struct SuperResolutionAttribs;
}

class SuperResolution
{
public:
    enum FEATURE_FLAGS : Uint32
    {
        FEATURE_FLAG_NONE           = 0u,
        FEATURE_FLAG_ASYNC_CREATION = 1u << 0u
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

        /// Super resolution settings
        const HLSL::SuperResolutionAttribs* pFSRAttribs = nullptr;
    };

public:
    SuperResolution(IRenderDevice* pDevice);

    ~SuperResolution();

    void PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags);

    void Execute(const RenderAttributes& RenderAttribs);

    static bool UpdateUI(HLSL::SuperResolutionAttribs& Attribs, FEATURE_FLAGS& FeatureFlags);

    ITextureView* GetUpsampledTextureSRV() const;

private:
    using RenderTechnique  = PostFXRenderTechnique;
    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_EDGE_ADAPTIVE_UPSAMPLING = 0,
        RENDER_TECH_COMPUTE_CONTRAST_ADAPTIVE_SHARPENING,
        RENDER_TECH_COUNT
    };

    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_INPUT_COLOR = 0,
        RESOURCE_IDENTIFIER_INPUT_LAST  = RESOURCE_IDENTIFIER_INPUT_COLOR,
        RESOURCE_IDENTIFIER_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_EAU,
        RESOURCE_IDENTIFIER_CAS,
        RESOURCE_IDENTIFIER_COUNT
    };

    RenderTechnique& GetRenderTechnique(RENDER_TECH RenderTech, FEATURE_FLAGS FeatureFlags);

    bool PrepareShadersAndPSO(const RenderAttributes& RenderAttribs, FEATURE_FLAGS FeatureFlags);

    void UpdateConstantBuffer(const RenderAttributes& RenderAttribs);

    void ComputeEdgeAdaptiveUpsampling(const RenderAttributes& RenderAttribs);

    void ComputeContrastAdaptiveSharpening(const RenderAttributes& RenderAttribs);

    void ComputePlaceholderTexture(const RenderAttributes& RenderAttribs);

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

    std::unique_ptr<HLSL::SuperResolutionAttribs> m_SuperResolutionAttribs;

    Uint32 m_BackBufferWidth  = 0;
    Uint32 m_BackBufferHeight = 0;
    Uint32 m_CurrentFrameIdx  = 0;

    FEATURE_FLAGS m_FeatureFlags = FEATURE_FLAG_NONE;
};

DEFINE_FLAG_ENUM_OPERATORS(SuperResolution::FEATURE_FLAGS)

} // namespace Diligent
