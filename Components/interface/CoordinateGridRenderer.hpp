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
/// Defines CoordinateGridRenderer class

#include <unordered_map>
#include <vector>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/ResourceRegistry.hpp"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/ShaderMacroHelper.hpp"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/HashUtils.hpp"

namespace Diligent
{

namespace HLSL
{
struct CameraAttribs;
struct CoordinateGridAttribs;
} // namespace HLSL


/// Coordinate grid renderer.
class CoordinateGridRenderer
{
public:
    /// Feature flags.
    enum FEATURE_FLAGS : Uint32
    {
        /// No feature flags.
        FEATURE_FLAG_NONE = 0u,

        /// Convert pixel shader output to sRGB.
        FEATURE_FLAG_CONVERT_TO_SRGB = 1u << 0u,

        /// Render grid in YZ plane.
        FEATURE_FLAG_RENDER_PLANE_YZ = 1u << 1u,

        /// Render grid in XZ plane.
        FEATURE_FLAG_RENDER_PLANE_XZ = 1u << 2u,

        /// Render grid in XY plane.
        FEATURE_FLAG_RENDER_PLANE_XY = 1u << 3u,

        /// Render X axis.
        FEATURE_FLAG_RENDER_AXIS_X = 1u << 4u,

        /// Render Y axis.
        FEATURE_FLAG_RENDER_AXIS_Y = 1u << 5u,

        /// Render Z axis.
        FEATURE_FLAG_RENDER_AXIS_Z = 1u << 6u,
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

        /// Render target view to render of grid and axes
        ITextureView* pColorRTV = nullptr;

        /// Shader resource view of the current depth buffer
        ITextureView* pDepthSRV = nullptr;

        /// Current camera settings
        const HLSL::CameraAttribs* pCamera = nullptr;

        /// If this parameter is null, the effect will use its own buffer.
        IBuffer* pCameraAttribsCB = nullptr;

        /// Feature flags
        FEATURE_FLAGS FeatureFlags = FEATURE_FLAG_NONE;

        /// Settings
        const HLSL::CoordinateGridAttribs* pAttribs = nullptr;
    };

    /// Create info.
    struct CreateInfo
    {
        /// Whether shader matrices are laid out in row-major order in GPU memory.

        /// By default, shader matrices are laid out in column-major order
        /// in GPU memory. If this option is set to true, shaders will be compiled
        /// with the Diligent::SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR flag and
        /// use the row-major layout.
        bool PackMatrixRowMajor = false;
    };

public:
    /// Creates a new coordinate grid renderer.
    CoordinateGridRenderer(IRenderDevice* pDevice, const CreateInfo& CI);

    /// Renders the coordinate grid.
    void Render(const RenderAttributes& Attribs);

    /// Adds the coordinate grid UI elements.
    static bool UpdateUI(HLSL::CoordinateGridAttribs& Attribs, FEATURE_FLAGS& FeatureFlags);

    /// Adds the coordinate grid shader macros.
    static void AddShaderMacros(FEATURE_FLAGS FeatureFlags, ShaderMacroHelper& Macros);

private:
    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_INPUT_DEPTH = 0,
        RESOURCE_IDENTIFIER_INPUT_COLOR,
        RESOURCE_IDENTIFIER_INPUT_LAST = RESOURCE_IDENTIFIER_INPUT_COLOR,
        RESOURCE_IDENTIFIER_CAMERA_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_SETTINGS_CONSTANT_BUFFER,
        RESOURCE_IDENTIFIER_COUNT
    };

    void RenderGridAxes(const RenderAttributes& RenderAttribs);

    RefCntAutoPtr<IPipelineState>& GetPSO(FEATURE_FLAGS FeatureFlags, TEXTURE_FORMAT RTVFormat);

private:
    struct PSOKey
    {
        FEATURE_FLAGS  FeatureFlags;
        TEXTURE_FORMAT RTVFormat;

        PSOKey(FEATURE_FLAGS _FeatureFlags, TEXTURE_FORMAT Format) :
            FeatureFlags{_FeatureFlags}, RTVFormat{Format}
        {}

        constexpr bool operator==(const PSOKey& rhs) const
        {
            return FeatureFlags == rhs.FeatureFlags && RTVFormat == rhs.RTVFormat;
        }

        struct Hasher
        {
            size_t operator()(const PSOKey& Key) const
            {
                return ComputeHash(Key.FeatureFlags, Key.RTVFormat);
            }
        };
    };

    using PipelineStateObjectCache = std::unordered_map<PSOKey, RefCntAutoPtr<IPipelineState>, PSOKey::Hasher>;

    ResourceRegistry m_Resources{RESOURCE_IDENTIFIER_COUNT};

    PipelineStateObjectCache              m_PSOCache;
    RefCntAutoPtr<IShaderResourceBinding> m_SRB;

    std::unique_ptr<HLSL::CoordinateGridAttribs> m_pRenderAttribs;
    CreateInfo                                   m_Settings;
};
DEFINE_FLAG_ENUM_OPERATORS(CoordinateGridRenderer::FEATURE_FLAGS)

} // namespace Diligent
