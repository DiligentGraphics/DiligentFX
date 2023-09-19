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

#include "../../../Primitives/interface/Object.h"
#include "../../../Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../Common/interface/BasicMath.hpp"

namespace Diligent
{

namespace USD
{

// {EA95099B-E894-47A6-AF33-B20096C4CF44}
static const INTERFACE_ID IID_HnRenderer =
    {0xea95099b, 0xe894, 0x47a6, {0xaf, 0x33, 0xb2, 0x0, 0x96, 0xc4, 0xcf, 0x44}};

struct HnRendererCreateInfo
{
    /// Render target format.
    TEXTURE_FORMAT RTVFormat = TEX_FORMAT_UNKNOWN;

    /// Depth-buffer format.
    TEXTURE_FORMAT DSVFormat = TEX_FORMAT_UNKNOWN;

    bool FrontCCW = false;

    bool ConvertOutputToSRGB = false;

    /// Camera attributes constant buffer.
    IBuffer* pCameraAttribsCB = nullptr;

    /// Light attributes constant buffer.
    IBuffer* pLightAttribsCB = nullptr;
};

struct HnDrawAttribs
{
    float4x4 Transform = float4x4::Identity();

    int   DebugView         = 0;
    float OcclusionStrength = 1;
    float EmissionScale     = 1;
    float AverageLogLum     = 0.3f;
    float MiddleGray        = 0.18f;
    float WhitePoint        = 3.0f;
    float IBLScale          = 1;
};

class IHnRenderer : public IObject
{
public:
    virtual void LoadUSDStage(const char* FileName) = 0;

    virtual void Update() = 0;

    virtual void Draw(IDeviceContext* pCtx, const HnDrawAttribs& Attribs) = 0;

    virtual void SetEnvironmentMap(IDeviceContext* pCtx, ITextureView* pEnvironmentMapSRV) = 0;
};

void CreateHnRenderer(IRenderDevice* pDevice, IDeviceContext* pContext, const HnRendererCreateInfo& CI, IHnRenderer** ppRenderer);

} // namespace USD

} // namespace Diligent
