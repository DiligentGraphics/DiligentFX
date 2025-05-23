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
/// Defines VectorFieldRenderer class

#include <unordered_map>
#include <vector>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/HashUtils.hpp"

namespace Diligent
{

/// Renders 2D vector field (e.g. motion vectors).

/// The renderer draws a grid of lines, where direction and length of each line is
/// determined by the vector field texture.
class VectorFieldRenderer
{
public:
    /// Vector field renderer creation info.
    struct CreateInfo
    {
        /// Render device.
        IRenderDevice* pDevice = nullptr;

        /// An optional render state cache.
        IRenderStateCache* pStateCache = nullptr;

        /// The number of render targets.
        Uint8 NumRenderTargets = 0;

        /// Render target formats.
        TEXTURE_FORMAT RTVFormats[DILIGENT_MAX_RENDER_TARGETS] = {};

        /// Depth-stencil view format.
        TEXTURE_FORMAT DSVFormat = TEX_FORMAT_UNKNOWN;

        /// Custom pixel shader main function source code.
        const char* PSMainSource = nullptr;

        /// Whether shader matrices are laid out in row-major order in GPU memory.

        /// By default, shader matrices are laid out in column-major order
        /// in GPU memory. If this option is set to true, shaders will be compiled
        /// with the Diligent::SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR flag and
        /// use the row-major layout.
        bool PackMatrixRowMajor = false;

        /// Whether to compile shaders asynchronously.
        bool AsyncShaders = false;
    };
    /// Constructs the vector field renderer object.
    VectorFieldRenderer(const CreateInfo& CI);

    /// Vector field rendering attributes.
    struct RenderAttribs
    {
        /// Device context to use for rendering.
        IDeviceContext* pContext = nullptr;

        /// Vector field texture.
        ITextureView* pVectorField = nullptr;

        // Bias to apply to the vector field values.

        // \remarks The bias is applied before the scale.
        float2 Bias = float2{0};

        // Scale to apply to the vector field values.

        // \remarks The scale is applied after the bias.
        float2 Scale = float2{1};

        /// Color of the beginning of the vector.
        float4 StartColor = float4{1};

        /// Color of the end of the vector.
        float4 EndColor = float4{1};

        /// Vector grid size.
        uint2 GridSize;

        /// Manually convert shader output to sRGB color space.
        bool ConvertOutputToSRGB = false;
    };
    /// Renders the vector field.
    void Render(const RenderAttribs& Attribs);


    /// Pipeline state object key.
    struct PSOKey
    {
        const bool ConvertOutputToSRGB;

        PSOKey(bool _ConvertOutputToSRGB) :
            ConvertOutputToSRGB{_ConvertOutputToSRGB}
        {}

        constexpr bool operator==(const PSOKey& rhs) const
        {
            return ConvertOutputToSRGB == rhs.ConvertOutputToSRGB;
        }

        struct Hasher
        {
            size_t operator()(const PSOKey& Key) const
            {
                return ComputeHash(Key.ConvertOutputToSRGB);
            }
        };
    };
    /// Returns the pipeline state object for the specified key.
    IPipelineState* GetPSO(const PSOKey& Key);

private:
    RefCntAutoPtr<IRenderDevice>     m_pDevice;
    RefCntAutoPtr<IRenderStateCache> m_pStateCache;
    RefCntAutoPtr<IBuffer>           m_RenderAttribsCB;

    const std::vector<TEXTURE_FORMAT> m_RTVFormats;
    const TEXTURE_FORMAT              m_DSVFormat;
    const std::string                 m_PSMainSource;
    const bool                        m_PackMatrixRowMajor;
    const bool                        m_AsyncShaders;

    std::unordered_map<PSOKey, RefCntAutoPtr<IPipelineState>, PSOKey::Hasher> m_PSOs;

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    IShaderResourceVariable*              m_pVectorFieldVar = nullptr;
};

} // namespace Diligent
