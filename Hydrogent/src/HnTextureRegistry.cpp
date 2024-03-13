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

#include "HnTextureRegistry.hpp"
#include "HnTextureUtils.hpp"
#include "HnTypeConversions.hpp"
#include "GLTFResourceManager.hpp"
#include "USD_Renderer.hpp"
#include "HnTextureIdentifier.hpp"
#include "GraphicsAccessories.hpp"

#include <mutex>

namespace Diligent
{

namespace USD
{

HnTextureRegistry::HnTextureRegistry(IRenderDevice*         pDevice,
                                     GLTF::ResourceManager* pResourceManager) :
    m_pDevice{pDevice},
    m_pResourceManager{pResourceManager}
{
}

HnTextureRegistry::~HnTextureRegistry()
{
}

void HnTextureRegistry::InitializeHandle(IRenderDevice*     pDevice,
                                         IDeviceContext*    pContext,
                                         ITextureLoader*    pLoader,
                                         const SamplerDesc& SamDesc,
                                         TextureHandle&     Handle)
{
    if (Handle.pAtlasSuballocation != nullptr)
    {
        VERIFY_EXPR(pContext != nullptr);

        IDynamicTextureAtlas* pAtlas      = Handle.pAtlasSuballocation->GetAtlas();
        ITexture*             pDstTex     = pAtlas->GetTexture();
        const TextureDesc&    AtlasDesc   = pAtlas->GetAtlasDesc();
        const TextureData     UploadData  = pLoader->GetTextureData();
        const TextureDesc&    SrcDataDesc = pLoader->GetTextureDesc();
        const uint2&          Origin      = Handle.pAtlasSuballocation->GetOrigin();
        const Uint32          Slice       = Handle.pAtlasSuballocation->GetSlice();

        const Uint32 MipsToUpload = std::min(UploadData.NumSubresources, AtlasDesc.MipLevels);
        for (Uint32 mip = 0; mip < MipsToUpload; ++mip)
        {
            const TextureSubResData& LevelData = UploadData.pSubResources[mip];
            const MipLevelProperties MipProps  = GetMipLevelProperties(SrcDataDesc, mip);

            Box UpdateBox;
            UpdateBox.MinX = Origin.x >> mip;
            UpdateBox.MaxX = UpdateBox.MinX + MipProps.LogicalWidth;
            UpdateBox.MinY = Origin.y >> mip;
            UpdateBox.MaxY = UpdateBox.MinY + MipProps.LogicalHeight;
            pContext->UpdateTexture(pDstTex, mip, Slice, UpdateBox, LevelData, RESOURCE_STATE_TRANSITION_MODE_NONE, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
    }
    else
    {
        if (!Handle)
        {
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
            {
                UNEXPECTED("Failed to create texture");
                return;
            }

            pDevice->CreateSampler(SamDesc, &Handle.pSampler);
            VERIFY_EXPR(Handle.pSampler);
            Handle.pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(Handle.pSampler);
        }

        if (pContext != nullptr)
        {
            StateTransitionDesc Barrier{Handle.pTexture, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE};
            pContext->TransitionResourceStates(1, &Barrier);
        }
    }
}

void HnTextureRegistry::Commit(IDeviceContext* pContext)
{
    if (m_pResourceManager)
    {
        m_pResourceManager->UpdateTextures(m_pDevice, pContext);
    }
    std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
    for (auto tex_it : m_PendingTextures)
    {
        InitializeHandle(m_pDevice, pContext, tex_it.second.pLoader, tex_it.second.SamDesc, *tex_it.second.Handle);
    }
    m_PendingTextures.clear();
}

HnTextureRegistry::TextureHandleSharedPtr HnTextureRegistry::Allocate(const pxr::TfToken&                            FilePath,
                                                                      const TextureComponentMapping&                 Swizzle,
                                                                      const pxr::HdSamplerParameters&                SamplerParams,
                                                                      std::function<RefCntAutoPtr<ITextureLoader>()> CreateLoader)
{
    const pxr::TfToken Key{FilePath.GetString() + '.' + GetTextureComponentMappingString(Swizzle)};
    return m_Cache.Get(
        Key,
        [&]() {
            RefCntAutoPtr<ITextureLoader> pLoader = CreateLoader();
            if (!pLoader)
            {
                LOG_ERROR_MESSAGE("Failed to create texture loader for texture ", FilePath);
                return TextureHandleSharedPtr{};
            }

            auto TexHandle       = std::make_shared<TextureHandle>();
            TexHandle->TextureId = m_NextTextureId.fetch_add(1);

            auto SamDesc = HdSamplerParametersToSamplerDesc(SamplerParams);
            // Try to allocate texture in the atlas first
            if (m_pResourceManager != nullptr)
            {
                const auto& TexDesc   = pLoader->GetTextureDesc();
                const auto& AtlasDesc = m_pResourceManager->GetAtlasDesc(TexDesc.Format);
                if (TexDesc.Width <= AtlasDesc.Width && TexDesc.Height <= AtlasDesc.Height)
                {
                    TexHandle->pAtlasSuballocation = m_pResourceManager->AllocateTextureSpace(TexDesc.Format, TexDesc.Width, TexDesc.Height);
                    if (!TexHandle->pAtlasSuballocation)
                    {
                        LOG_ERROR_MESSAGE("Failed to allocate atlas region for texture ", FilePath);
                    }
                }
                else
                {
                    LOG_WARNING_MESSAGE("Texture ", FilePath, " is too large to fit into atlas (", TexDesc.Width, "x", TexDesc.Height, " vs ", AtlasDesc.Width, "x", AtlasDesc.Height, ")");
                }
            }

            // If the texture was not allocated in the atlas (because the atlas is disabled or because it does not fit),
            // try to create it as a standalone texture.
            if (!TexHandle->pAtlasSuballocation)
            {
                if (m_pDevice->GetDeviceInfo().Features.MultithreadedResourceCreation)
                {
                    InitializeHandle(m_pDevice, nullptr, pLoader, SamDesc, *TexHandle);
                }
            }

            // Finish initialization in the main thread: we either need to upload the texture data to the atlas or
            // create the texture in the main thread if the device does not support multithreaded resource creation
            // and transition it to the shader resource state.
            {
                std::lock_guard<std::mutex> Lock{m_PendingTexturesMtx};
                m_PendingTextures.emplace(Key, PendingTextureInfo{std::move(pLoader), SamDesc, TexHandle});
            }

            return TexHandle;
        });
}

HnTextureRegistry::TextureHandleSharedPtr HnTextureRegistry::Allocate(const HnTextureIdentifier&      TexId,
                                                                      TEXTURE_FORMAT                  Format,
                                                                      const pxr::HdSamplerParameters& SamplerParams)
{
    if (TexId.FilePath.IsEmpty())
    {
        UNEXPECTED("File path must not be empty");
        return {};
    }

    return Allocate(TexId.FilePath, TexId.SubtextureId.Swizzle, SamplerParams,
                    [&TexId, Format]() {
                        TextureLoadInfo LoadInfo;
                        LoadInfo.Name   = TexId.FilePath.GetText();
                        LoadInfo.Format = Format;

                        // TODO: why do textures need to be flipped vertically?
                        LoadInfo.FlipVertically   = !TexId.SubtextureId.FlipVertically;
                        LoadInfo.IsSRGB           = TexId.SubtextureId.IsSRGB;
                        LoadInfo.PermultiplyAlpha = TexId.SubtextureId.PremultiplyAlpha;
                        LoadInfo.Swizzle          = TexId.SubtextureId.Swizzle;

                        return CreateTextureLoaderFromSdfPath(TexId.FilePath.GetText(), LoadInfo);
                    });
}

Uint32 HnTextureRegistry::GetAtlasVersion() const
{
    return m_pResourceManager != nullptr ? m_pResourceManager->GetTextureVersion() : 0;
}

} // namespace USD

} // namespace Diligent
