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

#include "HnTextureRegistry.hpp"
#include "HnTextureUtils.hpp"
#include "HnTypeConversions.hpp"

#include <mutex>

namespace Diligent
{

namespace USD
{

HnTextureRegistry::HnTextureRegistry(IRenderDevice* pDevice) :
    m_pDevice{pDevice}
{
}

HnTextureRegistry::~HnTextureRegistry()
{
}

static void InitializeHandle(IRenderDevice* pDevice, ITextureLoader* pLoader, const SamplerDesc& SamDesc, HnTextureRegistry::TextureHandle& Handle)
{
    VERIFY_EXPR(!Handle.pTexture);
    if (pLoader->GetTextureDesc().Type == RESOURCE_DIM_TEX_2D)
    {
        auto TexDesc = pLoader->GetTextureDesc();
        // PBR Renderer expects 2D textures to be 2D array textures
        TexDesc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
        TexDesc.ArraySize = 1;

        TextureData InitData = pLoader->GetTextureData();
        pDevice->CreateTexture(TexDesc, &InitData, &Handle.pTexture);
    }
    else
    {
        pLoader->CreateTexture(pDevice, &Handle.pTexture);
    }
    if (!Handle.pTexture)
        return;

    pDevice->CreateSampler(SamDesc, &Handle.pSampler);
    VERIFY_EXPR(Handle.pSampler);
    Handle.pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(Handle.pSampler);
}

void HnTextureRegistry::Commit(IDeviceContext* pContext)
{
    std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
    for (auto tex_it : m_PendingTextures)
    {
        InitializeHandle(m_pDevice, tex_it.second.pLoader, tex_it.second.SamDesc, *tex_it.second.Handle);
    }
    m_PendingTextures.clear();
}

HnTextureRegistry::TextureHandleSharedPtr HnTextureRegistry::Allocate(const HnTextureIdentifier&      TexId,
                                                                      const pxr::HdSamplerParameters& SamplerParams)
{
    return m_Cache.Get(
        TexId.FilePath,
        [&]() {
            auto TexHandle = std::make_shared<TextureHandle>();

            TextureLoadInfo LoadInfo;
            LoadInfo.Name = TexId.FilePath.GetText();

            // TODO: why do textures need to be flipped vertically?
            LoadInfo.FlipVertically   = !TexId.SubtextureId.FlipVertically;
            LoadInfo.IsSRGB           = TexId.SubtextureId.IsSRGB;
            LoadInfo.PermultiplyAlpha = TexId.SubtextureId.PremultiplyAlpha;

            auto pLoader = CreateTextureLoaderFromSdfPath(TexId.FilePath.GetText(), LoadInfo);
            if (!pLoader)
            {
                LOG_ERROR_MESSAGE("Failed to create texture loader for texture ", TexId.FilePath);
                return TextureHandleSharedPtr{};
            }

            auto SamDesc = HdSamplerParametersToSamplerDesc(SamplerParams);
            if (m_pDevice->GetDeviceInfo().Features.MultithreadedResourceCreation)
            {
                InitializeHandle(m_pDevice, pLoader, SamDesc, *TexHandle);
            }
            else
            {
                std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
                m_PendingTextures.emplace(TexId.FilePath, PendingTextureInfo{std::move(pLoader), SamDesc, TexHandle});
            }

            return TexHandle;
        });
}

} // namespace USD

} // namespace Diligent
