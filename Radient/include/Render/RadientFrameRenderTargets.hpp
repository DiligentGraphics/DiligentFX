/*
 *  Copyright 2026 Diligent Graphics LLC
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

#include "RadientRenderer.h"
#include "RefCntAutoPtr.hpp"

namespace Diligent
{

/// Owns the linear HDR scene color target and references the external output
/// and depth targets supplied by the view.
class RadientFrameRenderTargets
{
public:
    static constexpr TEXTURE_FORMAT SceneColorFormat = TEX_FORMAT_RGBA16_FLOAT;

    RADIENT_STATUS Prepare(IRenderDevice* pDevice, IRadientRenderTarget& Target);
    void           ClearSceneColor(IDeviceContext* pContext) const;

    const RadientExtent2D& GetSize() const;
    Uint32                 GetVersion() const;

    ITextureView* GetSceneColorRTV() const;
    ITextureView* GetSceneColorSRV() const;
    ITextureView* GetOutputColorRTV() const;
    ITextureView* GetDepthDSV() const;

private:
    RadientExtent2D m_Size;
    Uint32          m_Version = 0;

    RefCntAutoPtr<ITexture> m_pSceneColor;

    ITextureView* m_pOutputColorRTV = nullptr;
    ITextureView* m_pDepthDSV       = nullptr;
};

} // namespace Diligent
