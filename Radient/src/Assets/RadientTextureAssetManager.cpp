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

#include "Assets/RadientTextureAssetManager.hpp"

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientAssetValidation.hpp"
#include "Assets/RadientTextureSource.hpp"
#include "Cast.hpp"
#include "DebugUtilities.hpp"
#include "GLTFResourceManager.hpp"
#include "GPUUploadManager.h"
#include "GraphicsAccessories.hpp"
#include "TextureLoader.h"
#include "ThreadPool.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_TextureAssetImpl = {0x8bd4869c, 0x6ec8, 0x4944, {0xbc, 0x3d, 0xe7, 0xcc, 0x5d, 0xb, 0x26, 0xc5}};

struct TextureStorage
{
    TextureStorage() = default;
    TextureStorage(TextureStorage&& Rhs) noexcept :
        SourceURI{std::move(Rhs.SourceURI)},
        pTexture{std::move(Rhs.pTexture)},
        pAtlasSuballocation{std::move(Rhs.pAtlasSuballocation)},
        LoadStatus{Rhs.LoadStatus.load(std::memory_order_relaxed)},
        GPUResourcesReady{Rhs.GPUResourcesReady.load(std::memory_order_relaxed)},
        PendingUploads{Rhs.PendingUploads.load(std::memory_order_relaxed)}
    {
    }

    TextureStorage& operator=(TextureStorage&& Rhs)  = delete;
    TextureStorage(const TextureStorage&)            = delete;
    TextureStorage& operator=(const TextureStorage&) = delete;

    std::string                               SourceURI;
    RefCntAutoPtr<ITexture>                   pTexture;
    RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation;
    std::atomic<RADIENT_STATUS>               LoadStatus{RADIENT_STATUS_OK};
    std::atomic_bool                          GPUResourcesReady{false};
    std::atomic<Uint32>                       PendingUploads{0};
};

using TextureAssetImpl =
    RadientAssetImpl<IRadientTextureAsset, IID_RadientTextureAsset, IID_TextureAssetImpl, RADIENT_ASSET_TYPE_TEXTURE, TextureStorage>;

} // namespace

RadientTextureAssetManager::RadientTextureAssetManager(RadientAssetManagerImpl& Owner) noexcept :
    m_Owner{Owner}
{
}

RADIENT_STATUS RadientTextureAssetManager::LoadTexture(const RadientTextureLoadInfo& LoadInfo,
                                                       IRadientTextureAsset**        ppTexture)
{
    if (ppTexture == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppTexture == nullptr, "Output texture pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppTexture = nullptr;

    if (!ValidateTextureLoadInfo(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_Owner.m_pThreadPool == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    RadientTextureSource TextureSource{LoadInfo};
    const std::string    SourceURI = TextureSource.GetURI();
    const std::string    CacheKey  = RadientTextureSource::MakeCacheKey(LoadInfo);
    auto [pTextureAsset, TextureCreated] =
        m_TextureCache.GetOrCreate<TextureAssetImpl>(
            CacheKey,
            IID_TextureAssetImpl,
            [this, &SourceURI]() {
                TextureStorage TextureData;
                TextureData.SourceURI = SourceURI;
                TextureData.LoadStatus.store(RADIENT_STATUS_PENDING, std::memory_order_release);
                return RefCntAutoPtr<TextureAssetImpl>{
                    MakeNewRCObj<TextureAssetImpl>()(m_Owner.MakeURI("texture"),
                                                     SourceURI.empty() ? nullptr : SourceURI.c_str(),
                                                     std::move(TextureData))};
            });
    VERIFY_EXPR(pTextureAsset != nullptr);
    if (!pTextureAsset)
        return RADIENT_STATUS_INVALID_OPERATION;

    if (!TextureCreated)
    {
        const RADIENT_STATUS Status = pTextureAsset->GetLoadStatus();
        pTextureAsset->QueryInterface(IID_RadientTextureAsset, ppTexture);
        return Status;
    }

    RefCntWeakPtr<RadientAssetManagerImpl> pWeakOwner{&m_Owner};
    RefCntAutoPtr<TextureAssetImpl>        pTextureForWorker{pTextureAsset};

    TextureSource.MakeMemoryCopy();

    pTextureAsset->QueryInterface(IID_RadientTextureAsset, ppTexture);

    EnqueueAsyncWork(
        m_Owner.m_pThreadPool,
        [pWeakOwner, pTextureForWorker, TextureSource = std::move(TextureSource)](Uint32) mutable //
        {
            RefCntAutoPtr<RadientAssetManagerImpl> pOwner = pWeakOwner.Lock();
            if (pOwner == nullptr)
                return ASYNC_TASK_STATUS_CANCELLED;

            RefCntAutoPtr<ITextureLoader> pLoader = TextureSource.CreateLoader();
            if (pLoader == nullptr)
            {
                pTextureForWorker->GetStorage().LoadStatus.store(RADIENT_STATUS_INVALID_OPERATION, std::memory_order_release);
                return ASYNC_TASK_STATUS_COMPLETE;
            }

            const RADIENT_STATUS Status = pOwner->m_TextureManager.ScheduleGPUUpload(*pTextureForWorker, *pLoader);
            pTextureForWorker->GetStorage().LoadStatus.store(Status, std::memory_order_release);
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return pTextureAsset->GetLoadStatus();
}

ITextureView* RadientTextureAssetManager::GetTextureSRV(IRadientTextureAsset* pTextureAsset)
{
    RefCntAutoPtr<TextureAssetImpl> pImpl{pTextureAsset, IID_TextureAssetImpl};
    if (!pImpl)
        return nullptr;

    const TextureStorage& Texture = pImpl->GetStorage();
    if (Texture.LoadStatus.load(std::memory_order_acquire) != RADIENT_STATUS_OK ||
        !Texture.GPUResourcesReady.load(std::memory_order_acquire))
    {
        return nullptr;
    }

    ITexture* pTexture = Texture.pTexture;
    if (pTexture == nullptr && Texture.pAtlasSuballocation != nullptr)
    {
        if (IDynamicTextureAtlas* pAtlas = Texture.pAtlasSuballocation->GetAtlas())
            pTexture = pAtlas->GetTexture();
    }

    return pTexture != nullptr ? pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
}

RADIENT_STATUS RadientTextureAssetManager::GetLoadStatus(IRadientAsset* pTextureAsset)
{
    return TextureAssetImpl::GetLoadStatus(pTextureAsset);
}

bool RadientTextureAssetManager::ApplyTextureAtlasAttribs(IRadientTextureAsset*                 pTexture,
                                                          GLTF::Material::TextureShaderAttribs& Attribs)
{
    RefCntAutoPtr<TextureAssetImpl> pImpl{
        pTexture,
        IID_TextureAssetImpl};
    if (!pImpl)
        return false;

    ITextureAtlasSuballocation* pAtlasSuballocation = pImpl->GetStorage().pAtlasSuballocation;
    if (pAtlasSuballocation == nullptr)
        return false;

    Attribs.AtlasUVScaleAndBias = pAtlasSuballocation->GetUVScaleBias();
    Attribs.TextureSlice        = static_cast<float>(pAtlasSuballocation->GetSlice());
    return true;
}

RADIENT_STATUS RadientTextureAssetManager::ScheduleGPUUpload(IRadientTextureAsset& TextureAsset,
                                                             ITextureLoader&       Loader) const
{
    RefCntAutoPtr<TextureAssetImpl> pTextureAsset{
        &TextureAsset,
        IID_TextureAssetImpl};
    if (!pTextureAsset)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    IRenderDevice* const     pDevice          = m_Owner.m_pDevice;
    GLTF::ResourceManager*   pResourceManager = m_Owner.m_pResourceManager;
    IGPUUploadManager* const pUploadManager   = m_Owner.m_pUploadManager;

    if (pDevice == nullptr || pUploadManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    TextureStorage&    Texture = pTextureAsset->GetStorage();
    const TextureDesc& TexDesc = Loader.GetTextureDesc();

    struct TextureCopyData
    {
        RefCntAutoPtr<TextureAssetImpl>           pTexture;
        TextureStorage*                           pStorage = nullptr;
        RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation;
        RefCntAutoPtr<IRenderDevice>              pDevice;
    };

    Texture.PendingUploads.store(0, std::memory_order_release);
    Texture.GPUResourcesReady.store(false, std::memory_order_release);
    Texture.pTexture.Release();
    Texture.pAtlasSuballocation.Release();

    if (TexDesc.Type != RESOURCE_DIM_TEX_2D || TexDesc.GetArraySize() != 1)
    {
        Loader.CreateTexture(pDevice, &Texture.pTexture);
        Texture.GPUResourcesReady.store(Texture.pTexture != nullptr, std::memory_order_release);
        return Texture.pTexture != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;
    }

    if (pResourceManager == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    Texture.pAtlasSuballocation = pResourceManager->AllocateTextureSpace(TexDesc.Format,
                                                                         TexDesc.Width,
                                                                         TexDesc.Height,
                                                                         TextureAsset.GetReference().URI);
    if (Texture.pAtlasSuballocation == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const TextureDesc&          AtlasDesc        = Texture.pAtlasSuballocation->GetAtlas()->GetAtlasDesc();
    const TextureFormatAttribs& FmtAttribs       = GetTextureFormatAttribs(TexDesc.Format);
    const uint2                 Origin           = Texture.pAtlasSuballocation->GetOrigin();
    const Uint32                MipLevels        = std::min(AtlasDesc.MipLevels, TexDesc.MipLevels);
    Uint32                      ScheduledUploads = 0;

    for (Uint32 Mip = 0; Mip < MipLevels; ++Mip)
    {
        const MipLevelProperties MipProps = GetMipLevelProperties(TexDesc, Mip);
        if (FmtAttribs.ComponentType == COMPONENT_TYPE_COMPRESSED)
        {
            // Do not copy mip levels that are smaller than the block size.
            if (MipProps.LogicalWidth < FmtAttribs.BlockWidth ||
                MipProps.LogicalHeight < FmtAttribs.BlockHeight)
                break;
        }

        const TextureSubResData& SubRes = Loader.GetSubresourceData(Mip);

        TextureCopyData* pCopyData = new TextureCopyData{
            pTextureAsset,
            &Texture,
            Texture.pAtlasSuballocation,
            RefCntAutoPtr<IRenderDevice>{pDevice},
        };

        ScheduleTextureUpdateInfo UpdateInfo;
        UpdateInfo.Format      = AtlasDesc.Format;
        UpdateInfo.pSrcData    = SubRes.pData;
        UpdateInfo.Stride      = SubRes.Stride;
        UpdateInfo.DepthStride = SubRes.DepthStride;
        UpdateInfo.DstMipLevel = Mip;
        UpdateInfo.DstSlice    = Texture.pAtlasSuballocation->GetSlice();

        UpdateInfo.DstBox.MinX      = Origin.x >> Mip;
        UpdateInfo.DstBox.MaxX      = UpdateInfo.DstBox.MinX + MipProps.LogicalWidth;
        UpdateInfo.DstBox.MinY      = Origin.y >> Mip;
        UpdateInfo.DstBox.MaxY      = UpdateInfo.DstBox.MinY + MipProps.LogicalHeight;
        UpdateInfo.pCopyTextureData = pCopyData;

        Texture.PendingUploads.fetch_add(1, std::memory_order_acq_rel);
        ++ScheduledUploads;

        UpdateInfo.CopyTexture =
            [](IDeviceContext*          pContext,
               Uint32                   DstMipLevel,
               Uint32                   DstSlice,
               const Box&               DstBox,
               const TextureSubResData& SrcData,
               void*                    pUserData) {
                std::unique_ptr<TextureCopyData> Data{static_cast<TextureCopyData*>(pUserData)};

                bool CopyScheduled = false;
                if (pContext != nullptr)
                {
                    ITexture* pAtlasTexture = Data->pAtlasSuballocation->GetAtlas()->Update(Data->pDevice, pContext);
                    if (pAtlasTexture != nullptr)
                    {
                        pContext->UpdateTexture(pAtlasTexture, DstMipLevel, DstSlice, DstBox, SrcData,
                                                RESOURCE_STATE_TRANSITION_MODE_VERIFY,
                                                RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        CopyScheduled = true;
                    }
                }

                VERIFY_EXPR(Data->pStorage != nullptr);
                if (Data->pStorage != nullptr)
                {
                    const Uint32 PrevPendingUploads = Data->pStorage->PendingUploads.fetch_sub(1, std::memory_order_acq_rel);
                    VERIFY_EXPR(PrevPendingUploads > 0);
                    if (PrevPendingUploads == 1)
                        Data->pStorage->GPUResourcesReady.store(CopyScheduled, std::memory_order_release);
                }
            };

        UpdateInfo.CopyD3D11Texture =
            [](IDeviceContext* pContext,
               Uint32          DstMipLevel,
               Uint32          DstSlice,
               const Box&      DstBox,
               ITexture*       pSrcTexture,
               Uint32          SrcX,
               Uint32          SrcY,
               void*           pUserData) {
                std::unique_ptr<TextureCopyData> Data{static_cast<TextureCopyData*>(pUserData)};

                bool CopyScheduled = false;
                if (pContext != nullptr && pSrcTexture != nullptr)
                {
                    ITexture* pAtlasTexture = Data->pAtlasSuballocation->GetAtlas()->Update(Data->pDevice, pContext);
                    if (pAtlasTexture != nullptr)
                    {
                        CopyTextureAttribs CopyAttribs;
                        CopyAttribs.pSrcTexture              = pSrcTexture;
                        CopyAttribs.pDstTexture              = pAtlasTexture;
                        CopyAttribs.DstMipLevel              = DstMipLevel;
                        CopyAttribs.DstSlice                 = DstSlice;
                        CopyAttribs.DstX                     = DstBox.MinX;
                        CopyAttribs.DstY                     = DstBox.MinY;
                        CopyAttribs.DstZ                     = DstBox.MinZ;
                        CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;
                        CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

                        const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(pAtlasTexture->GetDesc().Format);

                        Box SrcBox;
                        SrcBox.MinX = SrcX;
                        SrcBox.MinY = SrcY;
                        SrcBox.MaxX = AlignUp(SrcX + DstBox.Width(), FmtAttribs.BlockWidth);
                        SrcBox.MaxY = AlignUp(SrcY + DstBox.Height(), FmtAttribs.BlockHeight);

                        CopyAttribs.pSrcBox = &SrcBox;

                        pContext->CopyTexture(CopyAttribs);
                        CopyScheduled = true;
                    }
                }

                VERIFY_EXPR(Data->pStorage != nullptr);
                if (Data->pStorage != nullptr)
                {
                    const Uint32 PrevPendingUploads = Data->pStorage->PendingUploads.fetch_sub(1, std::memory_order_acq_rel);
                    VERIFY_EXPR(PrevPendingUploads > 0);
                    if (PrevPendingUploads == 1)
                        Data->pStorage->GPUResourcesReady.store(CopyScheduled, std::memory_order_release);
                }
            };

        pUploadManager->ScheduleTextureUpdate(UpdateInfo);
    }

    if (ScheduledUploads == 0)
        Texture.GPUResourcesReady.store(true, std::memory_order_release);

    return RADIENT_STATUS_OK;
}

} // namespace Diligent
