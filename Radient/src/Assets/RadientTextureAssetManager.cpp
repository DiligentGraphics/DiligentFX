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

#include "Assets/RadientAssetImpl.hpp"
#include "Assets/RadientAssetURI.hpp"
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
#include <string>
#include <utility>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_TextureAssetImpl = {0x8bd4869c, 0x6ec8, 0x4944, {0xbc, 0x3d, 0xe7, 0xcc, 0x5d, 0xb, 0x26, 0xc5}};

class TextureStorage
{
public:
    explicit TextureStorage(RADIENT_STATUS InitLoadStatus = RADIENT_STATUS_OK) :
        m_LoadStatus{InitLoadStatus}
    {
    }

    TextureStorage(const TextureStorage&)            = delete;
    TextureStorage& operator=(const TextureStorage&) = delete;
    TextureStorage(TextureStorage&&)                 = delete;
    TextureStorage& operator=(TextureStorage&&)      = delete;

    void SetLoadStatus(RADIENT_STATUS Status) noexcept
    {
        m_LoadStatus.store(Status, std::memory_order_release);
    }

    RADIENT_STATUS GetLoadStatus() const noexcept
    {
        return m_LoadStatus.load(std::memory_order_acquire);
    }

    void ResetGPUResourceState()
    {
        m_PendingSubresourceUploads.store(0, std::memory_order_release);
        m_AllCopyCommandsEnqueued.store(false, std::memory_order_release);
        m_pTexture.Release();
        m_pAtlasSuballocation.Release();
    }

    ITexture* CreateTexture(IRenderDevice* pDevice, const TextureDesc& Desc)
    {
        VERIFY_EXPR(pDevice != nullptr);

        RefCntAutoPtr<ITexture> pTexture;
        if (pDevice != nullptr)
            pDevice->CreateTexture(Desc, nullptr, pTexture.GetAddressOfEmpty());

        m_pTexture = std::move(pTexture);
        m_AllCopyCommandsEnqueued.store(m_pTexture != nullptr, std::memory_order_release);
        return m_pTexture;
    }

    ITexture* GetTexture() const noexcept
    {
        return m_pTexture;
    }

    void SetAtlasSuballocation(RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation)
    {
        m_pAtlasSuballocation = std::move(pAtlasSuballocation);
    }

    ITextureAtlasSuballocation* GetAtlasSuballocation() const noexcept
    {
        return m_pAtlasSuballocation;
    }

    ITextureView* GetTextureSRV() const
    {
        if (GetLoadStatus() != RADIENT_STATUS_OK ||
            !m_AllCopyCommandsEnqueued.load(std::memory_order_acquire))
        {
            return nullptr;
        }

        ITexture* pTexture = m_pTexture;
        if (pTexture == nullptr)
        {
            ITextureAtlasSuballocation* pAtlasSuballocation = GetAtlasSuballocation();
            if (pAtlasSuballocation != nullptr)
            {
                if (IDynamicTextureAtlas* pAtlas = pAtlasSuballocation->GetAtlas())
                    pTexture = pAtlas->GetTexture();
            }
        }

        return pTexture != nullptr ? pTexture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    }

    void BeginSubresourceUploads(Uint32 SubresourceUploadCount)
    {
        m_PendingSubresourceUploads.store(SubresourceUploadCount, std::memory_order_release);
        m_AnyCopyCommandEnqueueFailed.store(false, std::memory_order_release);
        m_AllCopyCommandsEnqueued.store(SubresourceUploadCount == 0, std::memory_order_release);
    }

    // Records the result of a copy command enqueue operation.
    // Returns true if this was the last pending subresource upload and the final load status has been set.
    bool RecordCopyCommandEnqueueResult(bool CopyEnqueued)
    {
        if (!CopyEnqueued)
            m_AnyCopyCommandEnqueueFailed.store(true, std::memory_order_release);

        const Uint32 PrevPendingSubresourceUploads = m_PendingSubresourceUploads.fetch_sub(1, std::memory_order_acq_rel);
        VERIFY_EXPR(PrevPendingSubresourceUploads > 0);
        if (PrevPendingSubresourceUploads == 1)
        {
            const bool AllCopiesEnqueued = !m_AnyCopyCommandEnqueueFailed.load(std::memory_order_acquire);
            m_AllCopyCommandsEnqueued.store(AllCopiesEnqueued, std::memory_order_release);
            SetLoadStatus(AllCopiesEnqueued ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION);
            return true;
        }

        return false;
    }

private:
    RefCntAutoPtr<ITexture>                   m_pTexture;
    RefCntAutoPtr<ITextureAtlasSuballocation> m_pAtlasSuballocation;

    std::atomic<RADIENT_STATUS> m_LoadStatus{RADIENT_STATUS_OK};

    // True when no deferred copy is required or all required copy callbacks
    // have enqueued commands. This is not a GPU completion fence.
    std::atomic_bool m_AllCopyCommandsEnqueued{false};

    // True if any copy command failed to enqueue. This is used to set the final load status.
    std::atomic_bool m_AnyCopyCommandEnqueueFailed{false};

    // Number of subresource upload callbacks that have not reported whether
    // their copy commands were enqueued. This is not GPU completion tracking.
    std::atomic<Uint32> m_PendingSubresourceUploads{0};
};

void IncrementCounter(std::atomic<Uint32>& Counter,
                      Uint32               Count = 1) noexcept
{
    if (Count != 0)
        Counter.fetch_add(Count, std::memory_order_acq_rel);
}

void DecrementCounter(std::atomic<Uint32>& Counter,
                      Uint32               Count = 1) noexcept
{
    if (Count == 0)
        return;

    const Uint32 PrevValue = Counter.fetch_sub(Count, std::memory_order_acq_rel);
    VERIFY_EXPR(PrevValue >= Count);
}

class DecrementCounterGuard final
{
public:
    DecrementCounterGuard(std::atomic<Uint32>& Counter,
                          Uint32               Count = 1) noexcept :
        m_Counter{Counter},
        m_Count{Count}
    {
    }

    ~DecrementCounterGuard()
    {
        DecrementCounter(m_Counter, m_Count);
    }

    DecrementCounterGuard(const DecrementCounterGuard&)            = delete;
    DecrementCounterGuard& operator=(const DecrementCounterGuard&) = delete;
    DecrementCounterGuard(DecrementCounterGuard&&)                 = delete;
    DecrementCounterGuard& operator=(DecrementCounterGuard&&)      = delete;

    void Reset() noexcept
    {
        m_Count = 0;
    }

private:
    std::atomic<Uint32>& m_Counter;
    Uint32               m_Count = 0;
};

} // namespace


class TexturePayloadImpl final : public RadientAssetPayloadImpl<TextureStorage, TexturePayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<TextureStorage, TexturePayloadImpl>;
    using TBase::TBase;
};

namespace
{

using TextureAssetImpl =
    RadientAssetImpl<IRadientTextureAsset, IID_RadientTextureAsset, IID_TextureAssetImpl, RADIENT_ASSET_TYPE_TEXTURE, TexturePayloadImpl>;

struct TextureCopyData
{
    RadientTextureAssetManagerSharedPtr       pManager;
    RefCntAutoPtr<IRenderDevice>              pDevice;
    RefCntAutoPtr<TextureAssetImpl>           pTexture;
    RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation;

    TextureDesc TexDesc;
    std::string TextureName;

    std::atomic<Uint32>& PendingTextureLoads;
    std::atomic<Uint32>& PendingGPUUploads;

    static void CopyTextureCallback(IDeviceContext*          pContext,
                                    Uint32                   DstMipLevel,
                                    Uint32                   DstSlice,
                                    const Box&               DstBox,
                                    const TextureSubResData& SrcData,
                                    void*                    pUserData)
    {
        std::unique_ptr<TextureCopyData> Data{static_cast<TextureCopyData*>(pUserData)};
        Data->CopyTexture(pContext, DstMipLevel, DstSlice, DstBox, SrcData);
    }

    static void CopyD3D11TextureCallback(IDeviceContext* pContext,
                                         Uint32          DstMipLevel,
                                         Uint32          DstSlice,
                                         const Box&      DstBox,
                                         ITexture*       pSrcTexture,
                                         Uint32          SrcX,
                                         Uint32          SrcY,
                                         void*           pUserData)
    {
        std::unique_ptr<TextureCopyData> Data{static_cast<TextureCopyData*>(pUserData)};
        Data->CopyD3D11Texture(pContext, DstMipLevel, DstSlice, DstBox, pSrcTexture, SrcX, SrcY);
    }

private:
    ITexture* GetDestinationTexture(IDeviceContext* pContext)
    {
        if (pContext == nullptr)
            return nullptr;

        if (pAtlasSuballocation != nullptr)
            return pAtlasSuballocation->GetAtlas()->Update(pDevice, pContext);

        TextureStorage& Storage     = pTexture->GetStorage();
        ITexture*       pDstTexture = Storage.GetTexture();
        if (pDstTexture == nullptr)
        {
            TextureDesc Desc = TexDesc;
            Desc.Name        = TextureName.empty() ? nullptr : TextureName.c_str();

            pDstTexture = Storage.CreateTexture(pDevice, Desc);
        }

        return pDstTexture;
    }

    void RecordCopyCommandEnqueueResult(bool CopyEnqueued)
    {
        if (pTexture->GetStorage().RecordCopyCommandEnqueueResult(CopyEnqueued))
            DecrementCounter(PendingTextureLoads);

        DecrementCounter(PendingGPUUploads);
    }

    void CopyTexture(IDeviceContext*          pContext,
                     Uint32                   DstMipLevel,
                     Uint32                   DstSlice,
                     const Box&               DstBox,
                     const TextureSubResData& SrcData)
    {
        bool CopyEnqueued = false;
        if (pContext != nullptr)
        {
            if (ITexture* pDstTexture = GetDestinationTexture(pContext))
            {
                pContext->UpdateTexture(pDstTexture, DstMipLevel, DstSlice, DstBox, SrcData,
                                        RESOURCE_STATE_TRANSITION_MODE_VERIFY,
                                        RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                CopyEnqueued = true;
            }
        }

        RecordCopyCommandEnqueueResult(CopyEnqueued);
    }

    void CopyD3D11Texture(IDeviceContext* pContext,
                          Uint32          DstMipLevel,
                          Uint32          DstSlice,
                          const Box&      DstBox,
                          ITexture*       pSrcTexture,
                          Uint32          SrcX,
                          Uint32          SrcY)
    {
        bool CopyEnqueued = false;
        if (pContext != nullptr && pSrcTexture != nullptr)
        {
            if (ITexture* pDstTexture = GetDestinationTexture(pContext))
            {
                CopyTextureAttribs CopyAttribs;
                CopyAttribs.pSrcTexture              = pSrcTexture;
                CopyAttribs.pDstTexture              = pDstTexture;
                CopyAttribs.DstMipLevel              = DstMipLevel;
                CopyAttribs.DstSlice                 = DstSlice;
                CopyAttribs.DstX                     = DstBox.MinX;
                CopyAttribs.DstY                     = DstBox.MinY;
                CopyAttribs.DstZ                     = DstBox.MinZ;
                CopyAttribs.SrcTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_VERIFY;
                CopyAttribs.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

                const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(pDstTexture->GetDesc().Format);

                Box SrcBox;
                SrcBox.MinX = SrcX;
                SrcBox.MinY = SrcY;
                SrcBox.MaxX = AlignUp(SrcX + DstBox.Width(), FmtAttribs.BlockWidth);
                SrcBox.MaxY = AlignUp(SrcY + DstBox.Height(), FmtAttribs.BlockHeight);

                CopyAttribs.pSrcBox = &SrcBox;

                pContext->CopyTexture(CopyAttribs);
                CopyEnqueued = true;
            }
        }

        RecordCopyCommandEnqueueResult(CopyEnqueued);
    }
};

} // namespace

RadientTextureAssetManagerStats RadientTextureAssetManager::AtomicStats::GetSnapshot() const noexcept
{
    RadientTextureAssetManagerStats Stats;
    Stats.PendingTextureLoads       = PendingTextureLoads.load(std::memory_order_acquire);
    Stats.PendingTextureSourceLoads = PendingTextureSourceLoads.load(std::memory_order_acquire);
    Stats.PendingUploadScheduling   = PendingUploadScheduling.load(std::memory_order_acquire);
    Stats.PendingGPUUploads         = PendingGPUUploads.load(std::memory_order_acquire);
    return Stats;
}

RadientTextureAssetManager::RadientTextureAssetManager(const CreateInfo& CI) noexcept :
    m_pDevice{CI.pDevice},
    m_WeakResourceManager{CI.pResourceManager},
    m_WeakUploadManager{CI.pUploadManager}
{
}

RadientTextureAssetManager::~RadientTextureAssetManager() = default;

RadientTextureAssetManagerSharedPtr RadientTextureAssetManager::Create(const CreateInfo& CI)
{
    return RadientTextureAssetManagerSharedPtr{new RadientTextureAssetManager{CI}};
}

RadientTextureAssetManagerStats RadientTextureAssetManager::GetStats() const noexcept
{
    return m_Stats.GetSnapshot();
}

RADIENT_STATUS RadientTextureAssetManager::LoadTexture(IThreadPool&                  ThreadPool,
                                                       const RadientTextureLoadInfo& LoadInfo,
                                                       IRadientTextureAsset**        ppTexture)
{
    if (ppTexture == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppTexture == nullptr, "Output texture pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppTexture = nullptr;

    if (!ValidateTextureLoadInfo(LoadInfo))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    RadientTextureSource TextureSource{LoadInfo};

    std::string AssetURI = TextureSource.GetURI();
    if (AssetURI.empty())
        AssetURI = MakeRadientAssetURI("texture", m_NextAssetID.fetch_add(1, std::memory_order_relaxed));

    RefCntAutoPtr<TextureAssetImpl> pTextureAsset = TextureAssetImpl::Create(std::move(AssetURI));
    VERIFY_EXPR(pTextureAsset != nullptr);
    if (!pTextureAsset)
        return RADIENT_STATUS_INVALID_OPERATION;

    TextureSource.MakeMemoryCopy();

    pTextureAsset->QueryInterface(IID_RadientTextureAsset, ppTexture);

    IncrementCounter(m_Stats.PendingTextureLoads);
    IncrementCounter(m_Stats.PendingTextureSourceLoads);

    EnqueueAsyncWork(
        &ThreadPool,
        [pTextureAsset,
         pSelf         = shared_from_this(),
         TextureSource = std::move(TextureSource)](Uint32) mutable //
        {
            DecrementCounterGuard PendingTextureLoadGuard{pSelf->m_Stats.PendingTextureLoads};
            DecrementCounterGuard PendingSourceLoadGuard{pSelf->m_Stats.PendingTextureSourceLoads};

            const std::string TextureCacheKey = TextureSource.MakeCacheKey();

            auto [pTexturePayload, PayloadCreated] =
                pSelf->m_TextureCache.GetOrCreate(
                    TextureCacheKey.c_str(),
                    []() {
                        return TexturePayloadImpl::Create(RADIENT_STATUS_PENDING);
                    });

            if (!pTextureAsset->SetPayload(std::move(pTexturePayload)))
                return ASYNC_TASK_STATUS_COMPLETE;

            if (!PayloadCreated)
                return ASYNC_TASK_STATUS_COMPLETE;

            RefCntAutoPtr<ITextureLoader> pLoader = TextureSource.CreateLoader();
            if (pLoader == nullptr)
            {
                pTextureAsset->GetStorage().SetLoadStatus(RADIENT_STATUS_INVALID_OPERATION);
                return ASYNC_TASK_STATUS_COMPLETE;
            }

            RefCntAutoPtr<GLTF::ResourceManager> pResourceManager = pSelf->m_WeakResourceManager.Lock();
            RefCntAutoPtr<IGPUUploadManager>     pUploadManager   = pSelf->m_WeakUploadManager.Lock();
            if (!pResourceManager || !pUploadManager)
            {
                pTextureAsset->GetStorage().SetLoadStatus(RADIENT_STATUS_INVALID_OPERATION);
                return ASYNC_TASK_STATUS_COMPLETE;
            }

            PendingTextureLoadGuard.Reset();
            const RADIENT_STATUS Status = pSelf->ScheduleTextureGPUUpload(*pResourceManager, *pUploadManager, *pTextureAsset, *pLoader);
            if (Status != RADIENT_STATUS_PENDING)
                pTextureAsset->GetStorage().SetLoadStatus(Status);
            return ASYNC_TASK_STATUS_COMPLETE;
        });

    return pTextureAsset->GetPayloadStatus();
}

ITextureView* RadientTextureAssetManager::GetTextureSRV(IRadientTextureAsset* pTextureAsset)
{
    RefCntAutoPtr<TextureAssetImpl> pImpl = TextureAssetImpl::ResolveAsset(pTextureAsset);
    if (!pImpl)
        return nullptr;

    return pImpl->GetStorage().GetTextureSRV();
}

RADIENT_STATUS RadientTextureAssetManager::GetLoadStatus(IRadientAsset* pTextureAsset)
{
    return TextureAssetImpl::GetLoadStatus(pTextureAsset);
}

const TexturePayloadImpl* RadientTextureAssetManager::GetTexturePayload(IRadientTextureAsset* pTextureAsset)
{
    RefCntAutoPtr<TextureAssetImpl> pImpl = TextureAssetImpl::ResolveAsset(pTextureAsset);
    return pImpl ? pImpl->GetPayload().RawPtr() : nullptr;
}

bool RadientTextureAssetManager::ApplyTextureAtlasAttribs(IRadientTextureAsset*                 pTexture,
                                                          GLTF::Material::TextureShaderAttribs& Attribs)
{
    RefCntAutoPtr<TextureAssetImpl> pImpl = TextureAssetImpl::ResolveAsset(pTexture);
    if (!pImpl)
        return false;

    ITextureAtlasSuballocation* pAtlasSuballocation = pImpl->GetStorage().GetAtlasSuballocation();
    if (pAtlasSuballocation == nullptr)
        return false;

    Attribs.AtlasUVScaleAndBias = pAtlasSuballocation->GetUVScaleBias();
    Attribs.TextureSlice        = static_cast<float>(pAtlasSuballocation->GetSlice());
    return true;
}

RADIENT_STATUS RadientTextureAssetManager::ScheduleTextureGPUUpload(GLTF::ResourceManager& ResourceManager,
                                                                    IGPUUploadManager&     UploadManager,
                                                                    IRadientTextureAsset&  TextureAsset,
                                                                    ITextureLoader&        Loader)
{
    DecrementCounterGuard PendingTextureLoadGuard{m_Stats.PendingTextureLoads};

    RefCntAutoPtr<TextureAssetImpl> pTextureAsset{&TextureAsset, IID_TextureAssetImpl};
    if (!pTextureAsset)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    if (m_pDevice == nullptr)
        return RADIENT_STATUS_INVALID_OPERATION;

    const TextureDesc& TexDesc = Loader.GetTextureDesc();
    if (TexDesc.Is3D())
        return RADIENT_STATUS_INVALID_OPERATION;

    TextureStorage& Texture = pTextureAsset->GetStorage();
    Texture.ResetGPUResourceState();

    const TextureDesc AtlasDescForFit = ResourceManager.GetAtlasDesc(TexDesc.Format);
    const bool        UseTextureAtlas =
        TexDesc.Type == RESOURCE_DIM_TEX_2D &&
        TexDesc.GetArraySize() == 1 &&
        AtlasDescForFit.Type != RESOURCE_DIM_UNDEFINED &&
        TexDesc.Width <= AtlasDescForFit.Width &&
        TexDesc.Height <= AtlasDescForFit.Height;

    RefCntAutoPtr<ITextureAtlasSuballocation> pAtlasSuballocation;

    uint2          AtlasOrigin{};
    Uint32         UploadMipLevels = TexDesc.MipLevels;
    Uint32         UploadSlices    = TexDesc.GetArraySize();
    TEXTURE_FORMAT UploadFormat    = TexDesc.Format;

    if (UseTextureAtlas)
    {
        pAtlasSuballocation = ResourceManager.AllocateTextureSpace(TexDesc.Format,
                                                                   TexDesc.Width,
                                                                   TexDesc.Height,
                                                                   TextureAsset.GetReference().URI);
        if (pAtlasSuballocation == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;
        Texture.SetAtlasSuballocation(pAtlasSuballocation);

        const TextureDesc           AtlasDesc  = pAtlasSuballocation->GetAtlas()->GetAtlasDesc();
        const TextureFormatAttribs& FmtAttribs = GetTextureFormatAttribs(TexDesc.Format);
        const Uint32                MipLevels  = std::min(AtlasDesc.MipLevels, TexDesc.MipLevels);

        UploadMipLevels = 0;
        UploadSlices    = 1;
        UploadFormat    = AtlasDesc.Format;
        AtlasOrigin     = pAtlasSuballocation->GetOrigin();

        for (; UploadMipLevels < MipLevels; ++UploadMipLevels)
        {
            const MipLevelProperties MipProps = GetMipLevelProperties(TexDesc, UploadMipLevels);
            if (FmtAttribs.ComponentType == COMPONENT_TYPE_COMPRESSED)
            {
                // Do not copy mip levels that are smaller than the block size.
                if (MipProps.LogicalWidth < FmtAttribs.BlockWidth ||
                    MipProps.LogicalHeight < FmtAttribs.BlockHeight)
                    break;
            }
        }
    }
    else
    {
        if (m_pDevice->GetDeviceInfo().Features.MultithreadedResourceCreation != DEVICE_FEATURE_STATE_DISABLED)
        {
            if (Texture.CreateTexture(m_pDevice, TexDesc) == nullptr)
                return RADIENT_STATUS_INVALID_OPERATION;
        }
    }

    const Uint32 UploadSubresourceCount = UploadSlices * UploadMipLevels;

    // Render-thread callbacks may run while this worker is still scheduling uploads.
    // Publish the full pending count before scheduling any subresource to avoid transiently
    // reaching zero and marking the texture ready before the complete batch is queued.
    Texture.BeginSubresourceUploads(UploadSubresourceCount);

    const Uint32 PendingUploadScheduling = UploadSubresourceCount != 0 ? 1u : 0u;
    IncrementCounter(m_Stats.PendingUploadScheduling, PendingUploadScheduling);
    DecrementCounterGuard PendingUploadScheduleGuard{m_Stats.PendingUploadScheduling, PendingUploadScheduling};
    IncrementCounter(m_Stats.PendingGPUUploads, UploadSubresourceCount);

    RadientTextureAssetManagerSharedPtr pSelf = shared_from_this();

    for (Uint32 Slice = 0; Slice < UploadSlices; ++Slice)
    {
        for (Uint32 Mip = 0; Mip < UploadMipLevels; ++Mip)
        {
            const MipLevelProperties MipProps = GetMipLevelProperties(TexDesc, Mip);
            const TextureSubResData& SubRes   = Loader.GetSubresourceData(Mip, Slice);

            TextureCopyData* pCopyData = new TextureCopyData{
                pSelf,
                m_pDevice,
                pTextureAsset,
                pAtlasSuballocation,
                pAtlasSuballocation ? TextureDesc{} : TexDesc,
                pAtlasSuballocation || TexDesc.Name == nullptr ? std::string{} : TexDesc.Name,
                m_Stats.PendingTextureLoads,
                m_Stats.PendingGPUUploads,
            };

            ScheduleTextureUpdateInfo UpdateInfo;
            UpdateInfo.Format      = UploadFormat;
            UpdateInfo.pSrcData    = SubRes.pData;
            UpdateInfo.Stride      = SubRes.Stride;
            UpdateInfo.DepthStride = SubRes.DepthStride;
            UpdateInfo.DstMipLevel = Mip;

            Box& DstBox = UpdateInfo.DstBox;
            if (pAtlasSuballocation != nullptr)
            {
                VERIFY_EXPR(Slice == 0);
                UpdateInfo.DstSlice = pAtlasSuballocation->GetSlice();

                DstBox.MinX = AtlasOrigin.x >> Mip;
                DstBox.MaxX = DstBox.MinX + MipProps.LogicalWidth;
                DstBox.MinY = AtlasOrigin.y >> Mip;
                DstBox.MaxY = DstBox.MinY + MipProps.LogicalHeight;
            }
            else
            {
                UpdateInfo.DstSlice = Slice;

                DstBox = Box{0, MipProps.LogicalWidth, 0, MipProps.LogicalHeight};
            }

            UpdateInfo.pCopyTextureData = pCopyData;
            UpdateInfo.CopyTexture      = TextureCopyData::CopyTextureCallback;
            UpdateInfo.CopyD3D11Texture = TextureCopyData::CopyD3D11TextureCallback;

            UploadManager.ScheduleTextureUpdate(UpdateInfo);
        }
    }

    if (UploadSubresourceCount != 0)
        PendingTextureLoadGuard.Reset();

    return UploadSubresourceCount != 0 ? RADIENT_STATUS_PENDING : RADIENT_STATUS_OK;
}

} // namespace Diligent
