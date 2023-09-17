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

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"

namespace Diligent
{

namespace HLSL
{
#include "../../Shaders/PostProcess/ToneMapping/public/ToneMappingStructures.fxh"
} // namespace HLSL

/// Renders environment map.
class EnvMapRenderer
{
public:
    struct CreateInfo
    {
        IRenderDevice*     pDevice          = nullptr;
        IRenderStateCache* pStateCache      = nullptr;
        IBuffer*           pCameraAttribsCB = nullptr;

        TEXTURE_FORMAT RTVFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;
        TEXTURE_FORMAT DSVFormat = TEX_FORMAT_D32_FLOAT;

        int ToneMappingMode = TONE_MAPPING_MODE_UNCHARTED2;

        /// Manually convert shader output to sRGB color space.
        bool ConvertOutputToSRGB = false;
    };
    EnvMapRenderer(const CreateInfo& CI);

    struct RenderAttribs
    {
        IDeviceContext* pContext = nullptr;
        ITextureView*   pEnvMap  = nullptr;

        float AverageLogLum = 1;
        float MipLevel      = 0;
    };
    void Render(const RenderAttribs& Attribs, const HLSL::ToneMappingAttribs& ToneMapping);

private:
    RefCntAutoPtr<IPipelineState>         m_PSO;
    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    RefCntAutoPtr<IBuffer>                m_RenderAttribsCB;
    IShaderResourceVariable*              m_pEnvMapVar = nullptr;
};

} // namespace Diligent
