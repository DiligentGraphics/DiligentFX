/*
 *  Copyright 2023 Diligent Graphics LLC
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
#include <array>

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

#include "Shaders/PostProcess/ScreenSpaceReflection/public/ScreenSpaceReflectionStructures.fxh"

// TODO: Implement SPD for depth buffer

class ScreenSpaceReflection
{
public:
    struct RenderAttributes
    {
        /// Render device that may be used to create new objects needed for this frame, if any.
        IRenderDevice* pDevice = nullptr;

        /// Optional render state cache to optimize state loading.
        IRenderStateCache* pStateCache = nullptr;

        /// Device context that will record the rendering commands.
        IDeviceContext* pDeviceContext = nullptr;

        /// Shader resource view of the source color
        ITextureView* pColorBufferSRV = nullptr;

        /// Shader resource view of the source depth.
        ITextureView* pDepthBufferSRV = nullptr;

        /// Shader resource view of the source normal buffer
        ITextureView* pNormalBufferSRV = nullptr;

        /// Shader resource view of the source roughness buffer
        ITextureView* pMaterialBufferSRV = nullptr;

        /// Shader resource view of the motion vectors
        ITextureView* pMotionVectorsSRV = nullptr;

        ScreenSpaceReflectionAttribs SSRAttribs = {};
    };

public:
    ScreenSpaceReflection(IRenderDevice* pDevice);

    ~ScreenSpaceReflection();

    void SetBackBufferSize(IRenderDevice* pDevice, IDeviceContext* pDeviceContext, Uint32 BackBufferWidth, Uint32 BackBufferHeight);

    void Execute(const RenderAttributes& RenderAttribs);

    ITextureView* GetSSRRadianceSRV() const;

private:
    void ComputeHierarchicalDepthBuffer(const RenderAttributes& RenderAttribs);

    void ComputeBlueNoiseTexture(const RenderAttributes& RenderAttribs);

    void ComputeStencilMaskAndExtractRoughness(const RenderAttributes& RenderAttribs);

    void ComputeIntersection(const RenderAttributes& RenderAttribs);

    void ComputeSpatialReconstruction(const RenderAttributes& RenderAttribs);

    void ComputeTemporalAccumulation(const RenderAttributes& RenderAttribs);

    void ComputeBilateralCleanup(const RenderAttributes& RenderAttribs);

private:
    struct RenderTechnique
    {
        void InitializePSO(IRenderDevice*                    pDevice,
                           IRenderStateCache*                pStateCache,
                           const char*                       PSOName,
                           IShader*                          VertexShader,
                           IShader*                          PixelShader,
                           const PipelineResourceLayoutDesc& ResourceLayout,
                           std::vector<TEXTURE_FORMAT>       RTVFmts,
                           TEXTURE_FORMAT                    DSVFmt,
                           const DepthStencilStateDesc&      DSSDesc,
                           const BlendStateDesc&             BSDesc,
                           bool                              IsDSVReadOnly = true);

        bool IsInitialized() const;

        RefCntAutoPtr<IPipelineState>         PSO;
        RefCntAutoPtr<IShaderResourceBinding> SRB;
    };

    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_HIERARCHICAL_DEPTH_BUFFER = 0,
        RENDER_TECH_COMPUTE_BLUE_NOISE_TEXTURE,
        RENDER_TECH_COMPUTE_STENCIL_MASK_AND_EXTRACT_ROUGHNESS,
        RENDER_TECH_COMPUTE_INTERSECTION,
        RENDER_TECH_COMPUTE_SPATIAL_RECONSTRUCTION,
        RENDER_TECH_COMPUTE_TEMPORAL_ACCUMULATION,
        RENDER_TECH_COMPUTE_BILATERAL_CLEANUP,
        RENDER_TECH_COUNT
    };

    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_INPUT_COLOR = 0,
        RESOURCE_IDENTIFIER_INPUT_DEPTH,
        RESOURCE_IDENTIFIER_INPUT_NORMAL,
        RESOURCE_IDENTIFIER_INPUT_MATERIAL_PARAMETERS,
        RESOURCE_IDENTIFIER_INPUT_MOTION_VECTORS,
        RESOURCE_IDENTIFIER_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_DEPTH_HIERARCHY,
        RESOURCE_IDENTIFIER_DEPTH_STENCIL_MASK,
        RESOURCE_IDENTIFIER_ROUGHNESS,
        RESOURCE_IDENTIFIER_SOBOL_BUFFER,
        RESOURCE_IDENTIFIER_RANKING_TILE_BUFFER,
        RESOURCE_IDENTIFIER_SCRAMBLING_TILE_BUFFER,
        RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE,
        RESOURCE_IDENTIFIER_RADIANCE,
        RESOURCE_IDENTIFIER_RAY_DIRECTION_PDF,
        RESOURCE_IDENTIFIER_RESOLVED_RADIANCE,
        RESOURCE_IDENTIFIER_RESOLVED_VARIANCE,
        RESOURCE_IDENTIFIER_RESOLVED_DEPTH,
        RESOURCE_IDENTIFIER_RADIANCE_HISTORY,
        RESOURCE_IDENTIFIER_VARIANCE_HISTORY,
        RESOURCE_IDENTIFIER_DEPTH_HISTORY,
        RESOURCE_IDENTIFIER_OUTPUT,
        RESOURCE_IDENTIFIER_COUNT
    };

    std::array<RenderTechnique, RENDER_TECH_COUNT>          m_RenderTech{};
    std::array<ResourceInternal, RESOURCE_IDENTIFIER_COUNT> m_Resources{};

    std::vector<RefCntAutoPtr<ITextureView>> m_HierarchicalDepthMipMapDSV;
    std::vector<RefCntAutoPtr<ITextureView>> m_HierarchicalDepthMipMapSRV;

    std::array<RefCntAutoPtr<ITextureView>, 2> m_RadianceHistoryRTV{};
    std::array<RefCntAutoPtr<ITextureView>, 2> m_RadianceHistorySRV{};

    std::array<RefCntAutoPtr<ITextureView>, 2> m_VarianceHistoryRTV{};
    std::array<RefCntAutoPtr<ITextureView>, 2> m_VarianceHistorySRV{};

    RefCntAutoPtr<ITextureView> m_DepthStencilMaskDSVReadOnly;

    bool m_IsSupportTransitionSubresources = false;

    Uint32 m_BackBufferWidth  = 0;
    Uint32 m_BackBufferHeight = 0;
};

} // namespace Diligent
