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
        DefaultRawMemoryAllocator::GetAllocator().Free(Ptr);
        UnregisterAllocation(Ptr);
    }

    virtual void* AllocateAligned(size_t Size, size_t Alignment, const Char* dbgDescription, const char* dbgFileName, const Int32 dbgLineNumber) override final
    {
        void* Ptr = DefaultRawMemoryAllocator::GetAllocator().AllocateAligned(Size, Alignment, dbgDescription, dbgFileName, dbgLineNumber);
        RegisterAllocation(Ptr, Size);
        return Ptr;
    }

    virtual void FreeAligned(void* Ptr) override final
    {
        DefaultRawMemoryAllocator::GetAllocator().FreeAligned(Ptr);
        UnregisterAllocation(Ptr);
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

    void UnregisterAllocation(const void* Ptr)
    {
        if (Ptr == nullptr)
            return;

        std::lock_guard<std::mutex> Guard{m_AllocationsMtx};
        auto                        it = m_Allocations.find(Ptr);
        if (it != m_Allocations.end())
        {
            m_TotalAllocatedSize.fetch_add(-static_cast<int64_t>(it->second));
            m_Allocations.erase(it);
        }
        else
        {
            UNEXPECTED("Failed to find allocation");
        }
    }

private:
    std::mutex                              m_AllocationsMtx;
    std::unordered_map<const void*, size_t> m_Allocations;
    std::atomic<int64_t>                    m_TotalAllocatedSize{0};
};

class AssetDataContainer : public RefCntContainer<std::shared_ptr<const char>>
{
public:
    using TBase = RefCntContainer<std::shared_ptr<const char>>;

    AssetDataContainer(IReferenceCounters* pRefCounters, std::shared_ptr<const char> _Data, size_t _Size) :
        TBase{pRefCounters, std::move(_Data)},
        Size{_Size}
    {
        s_AssetDataSize.fetch_add(static_cast<int64_t>(Size));
    }

    ~AssetDataContainer()
    {
        s_AssetDataSize.fetch_add(-static_cast<int64_t>(Size));
    }

    static RefCntAutoPtr<AssetDataContainer> Create(std::shared_ptr<const char> Data, size_t Size)
    {
        return RefCntAutoPtr<AssetDataContainer>{MakeNewRCObj<AssetDataContainer>()(std::move(Data), Size)};
    }

    static Int64 GetTotalAllocatedSize()
    {
        return s_AssetDataSize.load();
    }

private:
    const size_t Size;

    static std::atomic<int64_t> s_AssetDataSize;
};

std::atomic<int64_t> AssetDataContainer::s_AssetDataSize{0};

} // namespace

RefCntAutoPtr<ITextureLoader> CreateTextureLoaderFromSdfPath(const char*            SdfPath,
                                                             const TextureLoadInfo& _LoadInfo)
{
    pxr::ArResolvedPath ResolvedPath{SdfPath};
    if (ResolvedPath.empty())
        return {};

    const pxr::ArResolver&        Resolver = pxr::ArGetResolver();
    std::shared_ptr<pxr::ArAsset> Asset    = Resolver.OpenAsset(ResolvedPath);
    if (!Asset)
        return {};


    std::shared_ptr<const char> Buffer = Asset->GetBuffer();
    if (!Buffer)
        return {};

    RefCntAutoPtr<IObject>   pAssetData = AssetDataContainer::Create(Buffer, Asset->GetSize());
    RefCntAutoPtr<IDataBlob> pDataBlob  = ProxyDataBlob::Create(Buffer.get(), Asset->GetSize(), pAssetData);

    TextureLoadInfo LoadInfo = _LoadInfo;
    LoadInfo.pAllocator      = &TextureMemoryAllocator::Get();

    RefCntAutoPtr<ITextureLoader> pLoader;
    CreateTextureLoaderFromDataBlob(pDataBlob, LoadInfo, &pLoader);

    return pLoader;
}

Int64 GetTextureLoaderMemoryUsage()
{
    return TextureMemoryAllocator::Get().GetTotalAllocatedSize() + AssetDataContainer::GetTotalAllocatedSize();
}

} // namespace USD

} // namespace Diligent
