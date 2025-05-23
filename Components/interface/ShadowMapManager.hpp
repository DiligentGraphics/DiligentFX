/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
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
/// Defines ShadowMapManager class

#include <vector>
#include <array>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

#include "Shaders/Common/public/BasicStructures.fxh"

/// Shadow map manager.
class ShadowMapManager
{
public:
    ShadowMapManager();

    // clang-format off

    /// Shadow map manager initialization info
    struct InitInfo
    {
        /// Shadow map format. This parameter must not be Diligent::TEX_FORMAT_UNKNOWN.
        TEXTURE_FORMAT Format                      = TEX_FORMAT_UNKNOWN;

        /// Shadow map resolution, must not be 0.
        Uint32         Resolution                  = 0;

        /// Number of shadow cascades, must not be 0.
        Uint32         NumCascades                 = 0;

        /// Shadow mode (see SHADOW_MODE_* defines in BasicStructures.fxh), must not be 0.
        int            ShadowMode                  = 0;

        /// Whether to use 32-bit or 16-bit filterable textures
        bool           Is32BitFilterableFmt        = false;

        /// Optional comparison sampler to be set in the shadow map resource view
        ISampler*      pComparisonSampler          = nullptr;

        /// Optional sampler to be set in the filterable shadow map representation
        ISampler*      pFilterableShadowMapSampler = nullptr;
    };
    /// Initializes the shadow map manager.
    void Initialize(IRenderDevice* pDevice, IRenderStateCache* pStateCache, const InitInfo& initInfo);

    /// Returns the shadow map SRV.
    ITextureView* GetSRV()                     { return m_pShadowMapSRV;           }

    /// Returns the shadow map DSV for the specified cascade.
    ITextureView* GetCascadeDSV(Uint32 Cascade){ return m_pShadowMapDSVs[Cascade]; }

    /// Returns the filterable shadow map SRV.
    ITextureView* GetFilterableSRV()           { return m_pFilterableShadowMapSRV; }

    /// Shadow cascade distribution info
    struct DistributeCascadeInfo
    {
        /// Pointer to camera view matrix, must not be null.
        const float4x4*    pCameraView  = nullptr;

        /// Pointer to camera world matrix.
        const float4x4*    pCameraWorld = nullptr;

        /// Pointer to camera projection matrix, must not be null.
        const float4x4*    pCameraProj  = nullptr;

        /// Pointer to light direction, must not be null.
        const float3*      pLightDir    = nullptr;

        /// Whether to snap cascades to texels in light view space
        bool               SnapCascades       = true;

        /// Whether to stabilize cascade extents in light view space,
        /// so that they do not change when camera rotates.
        bool               StabilizeExtents   = true;

        /// Whether to use same extents for X and Y axis. Enabled automatically if StabilizeExtents == true
        bool               EqualizeExtents    = true;

        /// Cascade partitioning factor that defines the ratio between fully linear (0.0) and 
        /// fully logarithmic (1.0) partitioning.
        float              fPartitioningFactor = 0.95f;

        /// Whether to use right-handed or left-handed light view transform matrix
        bool               UseRightHandedLightViewTransform = true;

        /// Whether to pack matrices in row-major order.
        bool               PackMatrixRowMajor = false;

        /// Callback that allows the application to adjust z range of every cascade.

        /// The callback is also called with cascade value -1 to adjust that entire camera range.
        ///
        /// \param [in]  CascadeIdx - Cascade index, or -1 for the entire camera range.
        /// \param [out] MinZ       - Minimum z value of the cascade range.
        /// \param [out] MaxZ       - Maximum z value of the cascade range.
        std::function<void(int CascadeIdx, float& MinZ, float& MaxZ)> AdjustCascadeRange;

        /// Callback that allows the application to adjust cascade center.

        /// \param [in]    Cascade                   - Cascade index.
        /// \param [in]    WorldToLightViewSpaceMatr - World to light view space transform matrix.
        /// \param [in]    TexelXSize                - Shadow map texel size along X axis.
        /// \param [in]    TexelYSize                - Shadow map texel size along Y axis.
        /// \param [inout] CascadeCenterX            - Cascade center X coordinate.
        /// \param [inout] CascadeCenterY            - Cascade center Y coordinate.
        ///
        /// The main use case for this callback is to adjust the cascade center to
        /// to snap it to texels in light view space in scenarios where global
        /// origin is dynamic (such as terrain rendering). 
        std::function<void(int Cascade, const float4x4& WorldToLightViewSpaceMatr, float TexelXSize, float TexelYSize, float& CascadeCenterX, float& CascadeCenterY)> AdjustCascadeCenter;
    };

    // clang-format on

    /// Cascade transforms.
    struct CascadeTransforms
    {
        /// Projection matrix
        float4x4 Proj;

        /// World to light projection space matrix
        float4x4 WorldToLightProjSpace;
    };

    /// Distributes shadow cascades.
    void DistributeCascades(const DistributeCascadeInfo& Info,
                            ShadowMapAttribs&            shadowMapAttribs);

    /// Converts the shadow map to filterable format.
    void ConvertToFilterable(IDeviceContext* pCtx, const ShadowMapAttribs& ShadowAttribs);

    /// Returns the cascade transforms for the specified cascade.
    const CascadeTransforms& GetCascadeTransform(Uint32 Cascade) const { return m_CascadeTransforms[Cascade]; }

private:
    void InitializeConversionTechniques(TEXTURE_FORMAT FilterableShadowMapFmt);
    void InitializeResourceBindings();

    int                                      m_ShadowMode = 0;
    RefCntAutoPtr<IRenderDevice>             m_pDevice;
    RefCntAutoPtr<IRenderStateCache>         m_pStateCache;
    RefCntAutoPtr<ITextureView>              m_pShadowMapSRV;
    std::vector<RefCntAutoPtr<ITextureView>> m_pShadowMapDSVs;
    RefCntAutoPtr<ITextureView>              m_pFilterableShadowMapSRV;
    std::vector<RefCntAutoPtr<ITextureView>> m_pFilterableShadowMapRTVs;
    RefCntAutoPtr<ITextureView>              m_pIntermediateSRV;
    RefCntAutoPtr<ITextureView>              m_pIntermediateRTV;
    RefCntAutoPtr<IBuffer>                   m_pConversionAttribsBuffer;
    std::vector<CascadeTransforms>           m_CascadeTransforms;
    struct ShadowConversionTechnique
    {
        RefCntAutoPtr<IPipelineState>         PSO;
        RefCntAutoPtr<IShaderResourceBinding> SRB;
    };
    std::array<ShadowConversionTechnique, SHADOW_MODE_EVSM4 + 1> m_ConversionTech;
    ShadowConversionTechnique                                    m_BlurVertTech;
};

} // namespace Diligent
