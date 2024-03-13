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

#include <unordered_map>
#include <vector>
#include <memory>

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/HashUtils.hpp"

namespace Diligent
{

namespace HLSL
{
struct ToneMappingAttribs;
} // namespace HLSL

/// Renders the environment map.
class EnvMapRenderer
{
public:
    struct CreateInfo
    {
        IRenderDevice*     pDevice          = nullptr;
        IRenderStateCache* pStateCache      = nullptr;
        IBuffer*           pCameraAttribsCB = nullptr;

        Uint8          NumRenderTargets                        = 1;
        TEXTURE_FORMAT RTVFormats[DILIGENT_MAX_RENDER_TARGETS] = {TEX_FORMAT_RGBA8_UNORM_SRGB};
        TEXTURE_FORMAT DSVFormat                               = TEX_FORMAT_D32_FLOAT;

        const char* PSMainSource = nullptr;
    };
    EnvMapRenderer(const CreateInfo& CI);
    ~EnvMapRenderer();

    struct RenderAttribs
    {
        ITextureView* pEnvMap = nullptr;

        float AverageLogLum = 1;
        float MipLevel      = 0;
        float Alpha         = 1;

        /// Manually convert shader output to sRGB color space.
        bool ConvertOutputToSRGB = false;

        bool ComputeMotionVectors = false;
    };
    void Prepare(IDeviceContext*                 pContext,
                 const RenderAttribs&            Attribs,
                 const HLSL::ToneMappingAttribs& ToneMapping);
    void Render(IDeviceContext* pContext);


    struct PSOKey
    {
        const int  ToneMappingMode;
        const bool ConvertOutputToSRGB;
        const bool ComputeMotionVectors;

        PSOKey(int _ToneMappingMode, bool _ConvertOutputToSRGB, bool _ComputeMotionVectors) :
            ToneMappingMode{_ToneMappingMode},
            ConvertOutputToSRGB{_ConvertOutputToSRGB},
            ComputeMotionVectors{_ComputeMotionVectors}
        {}

        constexpr bool operator==(const PSOKey& rhs) const
        {
            return (ToneMappingMode == rhs.ToneMappingMode &&
                    ConvertOutputToSRGB == rhs.ConvertOutputToSRGB &&
                    ComputeMotionVectors == rhs.ComputeMotionVectors);
        }

        struct Hasher
        {
            size_t operator()(const PSOKey& Key) const
            {
                return ComputeHash(Key.ToneMappingMode, Key.ConvertOutputToSRGB, Key.ComputeMotionVectors);
            }
        };
    };
    IPipelineState* GetPSO(const PSOKey& Key);

private:
    RefCntAutoPtr<IRenderDevice>     m_pDevice;
    RefCntAutoPtr<IRenderStateCache> m_pStateCache;
    RefCntAutoPtr<IBuffer>           m_pCameraAttribsCB;
    RefCntAutoPtr<IBuffer>           m_RenderAttribsCB;

    const std::vector<TEXTURE_FORMAT> m_RTVFormats;
    const TEXTURE_FORMAT              m_DSVFormat;
    const std::string                 m_PSMainSource;

    std::unordered_map<PSOKey, RefCntAutoPtr<IPipelineState>, PSOKey::Hasher> m_PSOs;

    IPipelineState* m_pCurrentPSO = nullptr;

    RefCntAutoPtr<IShaderResourceBinding> m_SRB;
    IShaderResourceVariable*              m_pEnvMapVar = nullptr;

    struct EnvMapShaderAttribs;
    std::unique_ptr<EnvMapShaderAttribs> m_ShaderAttribs;
};

} // namespace Diligent
