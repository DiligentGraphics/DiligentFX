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

#include <array>

#include "../../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../../DiligentCore/Graphics/GraphicsTools/interface/ResourceRegistry.hpp"
#include "../../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../../DiligentCore/Common/interface/BasicMath.hpp"

#include "PostFXRenderTechnique.hpp"

namespace Diligent
{

namespace HLSL
{
struct CameraAttribs;
}

class PostFXContext
{
public:
    struct FrameDesc
    {
        Uint32 Index  = 0;
        Uint32 Width  = 0;
        Uint32 Height = 0;
    };

    struct RenderAttributes
    {
        /// Render device that may be used to create new objects needed for this frame, if any.
        IRenderDevice* pDevice = nullptr;

        /// Optional render state cache to optimize state loading.
        IRenderStateCache* pStateCache = nullptr;

        /// Device context that will record the rendering commands.
        IDeviceContext* pDeviceContext = nullptr;

        /// Current camera settings
        const HLSL::CameraAttribs* pCurrCamera = nullptr;

        /// Previous camera settings
        const HLSL::CameraAttribs* pPrevCamera = nullptr;

        /// If this parameter is null, the effect will use its own buffer.
        IBuffer* pCameraAttribsCB = nullptr;
    };

    enum BLUE_NOISE_DIMENSION : Uint32
    {
        BLUE_NOISE_DIMENSION_XY = 0,
        BLUE_NOISE_DIMENSION_ZW,
        BLUE_NOISE_DIMENSION_COUNT
    };

    struct SupportedDeviceFeatures
    {
        bool TransitionSubresources  = false;
        bool TextureSubresourceViews = false;
        bool CopyDepthToColor        = false;

        /// Indicates whether the Base Vertex is added to the VertexID
        /// in the vertex shader.
        bool ShaderBaseVertexOffset = false;
    };

public:
    PostFXContext(IRenderDevice* pDevice);

    ~PostFXContext();

    void PrepareResources(const FrameDesc& Desc);
    void Execute(const RenderAttributes& RenderAttribs);

    ITextureView* Get2DBlueNoiseSRV(BLUE_NOISE_DIMENSION Dimension) const;

    IBuffer* GetCameraAttribsCB() const;

    const SupportedDeviceFeatures& GetSupportedFeatures() const
    {
        return m_SupportedFeatures;
    }

    const FrameDesc& GetFrameDesc() const
    {
        return m_FrameDesc;
    }

private:
    using RenderTechnique  = PostFXRenderTechnique;
    using ResourceInternal = RefCntAutoPtr<IDeviceObject>;

    enum RENDER_TECH : Uint32
    {
        RENDER_TECH_COMPUTE_BLUE_NOISE_TEXTURE = 0,
        RENDER_TECH_COUNT
    };

    enum RESOURCE_IDENTIFIER : Uint32
    {
        RESOURCE_IDENTIFIER_CONSTANT_BUFFER = 0,
        RESOURCE_IDENTIFIER_INDEX_BUFFER_INTERMEDIATE,
        RESOURCE_IDENTIFIER_SOBOL_BUFFER,
        RESOURCE_IDENTIFIER_SCRAMBLING_TILE_BUFFER,
        RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_XY,
        RESOURCE_IDENTIFIER_BLUE_NOISE_TEXTURE_ZW,
        RESOURCE_IDENTIFIER_COUNT
    };

    std::array<RenderTechnique, RENDER_TECH_COUNT> m_RenderTech{};

    ResourceRegistry m_Resources{RESOURCE_IDENTIFIER_COUNT};

    FrameDesc               m_FrameDesc         = {};
    SupportedDeviceFeatures m_SupportedFeatures = {};
};

} // namespace Diligent
