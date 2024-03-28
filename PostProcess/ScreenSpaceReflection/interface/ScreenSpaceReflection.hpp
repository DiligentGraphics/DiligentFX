/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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
struct ScreenSpaceReflectionAttribs;
}


// TODO: Implement SPD for depth buffer

class ScreenSpaceReflection
{
public:
    enum FEATURE_FLAGS : Uint32
    {
        FEATURE_FLAG_NONE           = 0,
        FEATURE_FLAG_REVERSED_DEPTH = 1 << 0, // Not implemented
        FEATURE_FLAG_PACKED_NORMAL  = 1 << 1, // Nor implemented

        // When using this flag, you only need to pass the color buffer of the previous frame.
        // We find the intersection using the depth buffer of the current frame, and when an intersection is found,
        // we make the corresponding offset by the velocity vector at the intersection point, for sampling from the color buffer.
        FEATURE_FLAG_PREVIOUS_FRAME = 1 << 2,

        // When this flag is used, ray tracing step is executed at half resolution
        FEATURE_FLAG_HALF_RESOLUTION = 1 << 3,

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

        /// Shader resource view of the source color
        ITextureView* pColorBufferSRV = nullptr;

        /// Shader resource view of the source depth.
        ITextureView* pDepthBufferSRV = nullptr;

        /// Shader resource view of the source normal buffer
        ITextureView* pNormalBufferSRV = nullptr;

        /// Shader resource view of the source roughness buffer
        ITextureView* pMaterialBufferSRV = nullptr;

        /// Shader resource view of the source motion buffer
        ITextureView* pMotionVectorsSRV = nullptr;

        /// SSR settings
        const HLSL::ScreenSpaceReflectionAttribs* pSSRAttribs = nullptr;
    };

public:
    ScreenSpaceReflection(IRenderDevice* pDevice);

    ~ScreenSpaceReflection();

    void PrepareResources(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, PostFXContext* pPostFXContext, FEATURE_FLAGS FeatureFlags);

    void Execute(const RenderAttributes& RenderAttribs);

    static bool UpdateUI(HLSL::ScreenSpaceReflectionAttribs& SSRAttribs, FEATURE_FLAGS& FeatureFlags, Uint32& DisplayMode);

    ITextureView* GetSSRRadianceSRV() const;

private:
    using RenderTechnique  = PostFXRenderTechnique;
    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_HIERARCHICAL_DEPTH_BUFFER = 0,
        RENDER_TECH_COMPUTE_STENCIL_MASK_AND_EXTRACT_ROUGHNESS,
        RENDER_TECH_COMPUTE_DOWNSAMPLED_STENCIL_MASK,
        RENDER_TECH_COMPUTE_INTERSECTION,
        RENDER_TECH_COMPUTE_SPATIAL_RECONSTRUCTION,
        RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION,
        RENDER_TECH_COMPUTE_BILATERAL_CLEANUP,
        RENDER_TECH_COPY_DEPTH,
        RENDER_TECH_COUNT
    };

    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_INPUT_COLOR = 0,
        RESOURCE_IDENTIFIER_INPUT_DEPTH,
        RESOURCE_IDENTIFIER_INPUT_NORMAL,
        RESOURCE_IDENTIFIER_INPUT_MATERIAL_PARAMETERS,
        RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS,
        RESOURCE_IDENTIFIER_INPUT_LAST = RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS,
        RESOURCE_IDENTIFIER_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_DEPTH_HIERARCHY,
        RESOURCE_IDENTIFIER_DEPTH_HIERARCHY_INTERMEDIATE,
        RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK,
        RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK_HALF_RES,
        RESOURCE_IDENTIFIER_ROUGHNESS,
        RESOURCE_IDENTIFIER_RADIANCE,
        RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF,
        RESOURCE_IDENTIFIER_RESOLVED_RADIANCE,
        RESOURCE_IDENTIFIER_RESOLVED_VARIANCE,
        RESOURCE_IDENTIFIER_RESOLVED_DEPTH,
        RESOURCE_IDENTIFIER_RADIANCE_HISTORY0,
        RESOURCE_IDENTIFIER_RADIANCE_HISTORY1,
        RESOURCE_IDENTIFIER_VARIANCE_HISTORY0,
        RESOURCE_IDENTIFIER_VARIANCE_HISTORY1,
        RESOURCE_IDENTIFIER_OUTPUT,
        RESOURCE_IDENTIFIER_COUNT
    };

    void CopyTextureDepth(const RenderAttributes& RenderAttribs, ITextureView* pSRV, ITextureView* pRTV);

    void ComputeHierarchicalDepthBuffer(const RenderAttributes& RenderAttribs);

    void ComputeStencilMaskAndExtractRoughness(const RenderAttributes& RenderAttribs);

    void ComputeDownsampledStencilMask(const RenderAttributes& RenderAttribs);

    void ComputeIntersection(const RenderAttributes& RenderAttribs);

    void ComputeSpatialReconstruction(const RenderAttributes& RenderAttribs);

    void ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs);

    void ComputeBilateralCleanup(const RenderAttributes& RenderAttribs);

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

    std::unique_ptr<HLSL::ScreenSpaceReflectionAttribs> m_SSRAttribs;

    ResourceRegistry m_Resources{RESOURCE_IDENTIFIER_COUNT};

    std::vector<RefCntAutoPtr<ITextureView>> m_HierarchicalDepthMipMapRTV;
    std::vector<RefCntAutoPtr<ITextureView>> m_HierarchicalDepthMipMapSRV;
    RefCntAutoPtr<ITextureView>              m_DepthStencilMaskDSVReadOnly;
    RefCntAutoPtr<ITextureView>              m_DepthStencilMaskDSVReadOnlyHalfRes;

    Uint32 m_BackBufferWidth  = 0;
    Uint32 m_BackBufferHeight = 0;

    FEATURE_FLAGS m_FeatureFlags = FEATURE_FLAG_NONE;
};

DEFINE_FLAG_ENUM_OPERATORS(ScreenSpaceReflection::FEATURE_FLAGS)

} // namespace Diligent
