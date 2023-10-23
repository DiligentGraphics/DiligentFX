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

#include <memory>
#include <mutex>
#include <unordered_map>

#include "pxr/pxr.h"
#include "pxr/base/tf/token.h"
#include "pxr/imaging/hd/types.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "RefCntAutoPtr.hpp"
#include "TextureLoader.h"
#include "ObjectsRegistry.hpp"

#include "HnTextureIdentifier.hpp"

namespace Diligent
{

namespace USD
{

class HnTextureRegistry final
{
public:
    HnTextureRegistry(IRenderDevice* pDevice);
    ~HnTextureRegistry();

    void Commit(IDeviceContext* pContext);

    struct TextureHandle
    {
        RefCntAutoPtr<ITexture> pTexture;
        RefCntAutoPtr<ISampler> pSampler;
    };

    using TextureHandleSharedPtr = std::shared_ptr<TextureHandle>;

    TextureHandleSharedPtr Allocate(const HnTextureIdentifier&      TexId,
                                    const pxr::HdSamplerParameters& SamplerParams);

private:
    RefCntAutoPtr<IRenderDevice> m_pDevice;

    ObjectsRegistry<pxr::TfToken, TextureHandleSharedPtr, pxr::TfToken::HashFunctor> m_Cache;

    struct PendingTextureInfo
    {
        RefCntAutoPtr<ITextureLoader> pLoader;
        SamplerDesc                   SamDesc;
        TextureHandleSharedPtr        Handle;
    };

    std::mutex                                                                      m_PendingTexturesMtx;
    std::unordered_map<pxr::TfToken, PendingTextureInfo, pxr::TfToken::HashFunctor> m_PendingTextures;
};

} // namespace USD

} // namespace Diligent
