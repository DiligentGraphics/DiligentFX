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

#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>

#include "pxr/pxr.h"
#include "pxr/base/tf/token.h"
#include "pxr/imaging/hd/types.h"

#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "../../../DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "../../../DiligentCore/Common/interface/ObjectsRegistry.hpp"
#include "../../../DiligentTools/TextureLoader/interface/TextureLoader.h"

namespace Diligent
{

struct ITextureAtlasSuballocation;

namespace GLTF
{
class ResourceManager;
}

namespace USD
{

struct HnTextureIdentifier;

class HnTextureRegistry final
{
public:
    HnTextureRegistry(IRenderDevice*         pDevice,
                      GLTF::ResourceManager* pResourceManager);
    ~HnTextureRegistry();

    void Commit(IDeviceContext* pContext);

    struct TextureHandle
    {
        RefCntAutoPtr<ITexture> pTexture;
        RefCntAutoPtr<ISampler> pSampler;

        RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation;

        Uint32 TextureId = ~0u;

        explicit operator bool() const noexcept
        {
            return pTexture != nullptr || pAtlasSuballocation != nullptr;
        }
    };

    using TextureHandleSharedPtr = std::shared_ptr<TextureHandle>;

    TextureHandleSharedPtr Allocate(const HnTextureIdentifier&      TexId,
                                    TEXTURE_FORMAT                  Format,
                                    const pxr::HdSamplerParameters& SamplerParams);

    // Allocates texture handle for the specified texture file path.
    // If the texture is not loaded, calls CreateLoader() to create the texture loader.
    TextureHandleSharedPtr Allocate(const pxr::TfToken&                            FilePath,
                                    const TextureComponentMapping&                 Swizzle,
                                    const pxr::HdSamplerParameters&                SamplerParams,
                                    std::function<RefCntAutoPtr<ITextureLoader>()> CreateLoader);

    TextureHandleSharedPtr Get(const pxr::TfToken& Path)
    {
        return m_Cache.Get(Path);
    }

    Uint32 GetAtlasVersion() const;

    template <typename HandlerType>
    void ProcessTextures(HandlerType&& Handler)
    {
        m_Cache.ProcessElements(Handler);
    }

private:
    void InitializeHandle(IRenderDevice*     pDevice,
                          IDeviceContext*    pContext,
                          ITextureLoader*    pLoader,
                          const SamplerDesc& SamDesc,
                          TextureHandle&     Handle);

private:
    RefCntAutoPtr<IRenderDevice> m_pDevice;

    GLTF::ResourceManager* const m_pResourceManager;

    ObjectsRegistry<pxr::TfToken, TextureHandleSharedPtr, pxr::TfToken::HashFunctor> m_Cache;

    struct PendingTextureInfo
    {
        RefCntAutoPtr<ITextureLoader> pLoader;
        SamplerDesc                   SamDesc;
        TextureHandleSharedPtr        Handle;
    };

    std::mutex                                                                      m_PendingTexturesMtx;
    std::unordered_map<pxr::TfToken, PendingTextureInfo, pxr::TfToken::HashFunctor> m_PendingTextures;

    std::atomic<Uint32> m_NextTextureId{0};
};

} // namespace USD

} // namespace Diligent
