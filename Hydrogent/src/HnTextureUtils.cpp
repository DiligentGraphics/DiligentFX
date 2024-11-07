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

#include "HnTextureUtils.hpp"

#include "ProxyDataBlob.hpp"
#include "RefCntContainer.hpp"
#include "DefaultRawMemoryAllocator.hpp"

#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolver.h"

namespace Diligent
{

namespace USD
{

namespace
{

class TextureMemoryAllocator final : public IMemoryAllocator
{
public:
    virtual void* Allocate(size_t Size, const Char* dbgDescription, const char* dbgFileName, const Int32 dbgLineNumber) override final
    {
        void* Ptr = DefaultRawMemoryAllocator::GetAllocator().Allocate(Size, dbgDescription, dbgFileName, dbgLineNumber);
        RegisterAllocation(Ptr, Size);
        return Ptr;
    }

    virtual void Free(void* Ptr) override final
    {
        // Unregister the allocation before freeing it as the pointer may be reused
        size_t Size = UnregisterAllocation(Ptr);
        DefaultRawMemoryAllocator::GetAllocator().Free(Ptr);
        m_TotalAllocatedSize.fetch_add(-static_cast<int64_t>(Size));
    }

    virtual void* AllocateAligned(size_t Size, size_t Alignment, const Char* dbgDescription, const char* dbgFileName, const Int32 dbgLineNumber) override final
    {
        void* Ptr = DefaultRawMemoryAllocator::GetAllocator().AllocateAligned(Size, Alignment, dbgDescription, dbgFileName, dbgLineNumber);
        RegisterAllocation(Ptr, Size);
        return Ptr;
    }

    virtual void FreeAligned(void* Ptr) override final
    {
        // Unregister the allocation before freeing it as the pointer may be reused
        size_t Size = UnregisterAllocation(Ptr);
        DefaultRawMemoryAllocator::GetAllocator().FreeAligned(Ptr);
        m_TotalAllocatedSize.fetch_add(-static_cast<int64_t>(Size));
    }

    static TextureMemoryAllocator& Get()
    {
        static TextureMemoryAllocator Allocator;
        return Allocator;
    }

    int64_t GetTotalAllocatedSize() const
    {
        return m_TotalAllocatedSize.load();
    }

    ~TextureMemoryAllocator()
    {
        DEV_CHECK_ERR(m_Allocations.empty(), "There are ", m_Allocations.size(), " outstanding allocations");
    }

private:
    TextureMemoryAllocator()
    {
    }

    void RegisterAllocation(const void* Ptr, size_t Size)
    {
        if (Ptr == nullptr)
            return;

        std::lock_guard<std::mutex> Guard{m_AllocationsMtx};
        m_Allocations.emplace(Ptr, Size);
        m_TotalAllocatedSize.fetch_add(Size);
    }

    size_t UnregisterAllocation(const void* Ptr)
    {
        size_t Size = 0;
        if (Ptr != nullptr)
        {
            std::lock_guard<std::mutex> Guard{m_AllocationsMtx};

            auto it = m_Allocations.find(Ptr);
            if (it != m_Allocations.end())
            {
                Size = it->second;
                m_Allocations.erase(it);
            }
            else
            {
                UNEXPECTED("Failed to find allocation");
            }
        }

        return Size;
    }

private:
    std::mutex                              m_AllocationsMtx;
    std::unordered_map<const void*, size_t> m_Allocations;
    std::atomic<int64_t>                    m_TotalAllocatedSize{0};
};

class AssetDataContainer : public ObjectBase<IDataBlob>
{
public:
    using TBase = ObjectBase<IDataBlob>;

    AssetDataContainer(IReferenceCounters* pRefCounters, std::shared_ptr<const char> Data, size_t Size) :
        TBase{pRefCounters},
        m_Data{std::move(Data)},
        m_Size{Size}
    {
        s_AssetDataSize.fetch_add(static_cast<int64_t>(m_Size));
    }

    ~AssetDataContainer()
    {
        s_AssetDataSize.fetch_add(-static_cast<int64_t>(m_Size));
    }

    static RefCntAutoPtr<AssetDataContainer> Create(std::shared_ptr<const char> Data, size_t Size)
    {
        return RefCntAutoPtr<AssetDataContainer>{MakeNewRCObj<AssetDataContainer>()(std::move(Data), Size)};
    }

    static Int64 GetTotalAllocatedSize()
    {
        return s_AssetDataSize.load();
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_DataBlob, TBase)

    virtual void DILIGENT_CALL_TYPE Resize(size_t NewSize) override
    {
        UNEXPECTED("Resize is not supported by asset data container.");
    }

    virtual size_t DILIGENT_CALL_TYPE GetSize() const override
    {
        return m_Size;
    }

    virtual void* DILIGENT_CALL_TYPE GetDataPtr(size_t Offset = 0) override
    {
        UNEXPECTED("Non-const data access is not supported by asset data container.");
        return nullptr;
    }

    virtual const void* DILIGENT_CALL_TYPE GetConstDataPtr(size_t Offset = 0) const override
    {
        VERIFY(Offset < m_Size, "Offset (", Offset, ") exceeds the data size (", m_Size, ")");
        return m_Data.get() + Offset;
    }

private:
    std::shared_ptr<const char> m_Data;
    const size_t                m_Size;

    static std::atomic<int64_t> s_AssetDataSize;
};

std::atomic<int64_t> AssetDataContainer::s_AssetDataSize{0};

} // namespace

HnLoadTextureResult LoadTextureFromSdfPath(const char*            SdfPath,
                                           const TextureLoadInfo& _LoadInfo,
                                           Uint64                 MemoryBudget)
{
    pxr::ArResolvedPath ResolvedPath{SdfPath};
    if (ResolvedPath.empty())
    {
        return {HN_LOAD_TEXTURE_STATUS_INVALID_PATH};
    }

    std::shared_ptr<const char> Buffer;
    size_t                      Size = 0;
    {
        const pxr::ArResolver&        Resolver = pxr::ArGetResolver();
        std::shared_ptr<pxr::ArAsset> Asset    = Resolver.OpenAsset(ResolvedPath);
        if (!Asset)
        {
            return {HN_LOAD_TEXTURE_STATUS_ASSET_NOT_FOUND};
        }

        Buffer = Asset->GetBuffer();
        if (!Buffer)
        {
            return {HN_LOAD_TEXTURE_STATUS_EMPTY_ASSET};
        }

        Size = Asset->GetSize();
    }

    RefCntAutoPtr<IDataBlob> pAssetData = AssetDataContainer::Create(std::move(Buffer), Size);

    TextureLoadInfo LoadInfo = _LoadInfo;
    LoadInfo.pAllocator      = &TextureMemoryAllocator::Get();

    HnLoadTextureResult Res;
    CreateTextureLoaderFromDataBlob(std::move(pAssetData), LoadInfo, &Res.pLoader);
    Res.LoadStatus = Res.pLoader ? HN_LOAD_TEXTURE_STATUS_SUCCESS : HN_LOAD_TEXTURE_STATUS_FAILED;

    return Res;
}

Int64 GetTextureLoaderMemoryUsage()
{
    return TextureMemoryAllocator::Get().GetTotalAllocatedSize() + AssetDataContainer::GetTotalAllocatedSize();
}

} // namespace USD

} // namespace Diligent
