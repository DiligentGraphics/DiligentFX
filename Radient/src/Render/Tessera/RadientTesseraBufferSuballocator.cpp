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

#include "Render/Tessera/RadientTesseraBufferSuballocator.hpp"

#include "DebugUtilities.hpp"
#include "DefaultRawMemoryAllocator.hpp"
#include "DynamicBuffer.hpp"
#include "GraphicsAccessories.hpp"
#include "VariableSizeAllocationsManager.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace Diligent
{

namespace
{

RADIENT_STATUS CopyUpdateData(void* pDstData, Uint32 Size, void* pUserData)
{
    if (pUserData == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::memcpy(pDstData, pUserData, Size);
    return RADIENT_STATUS_OK;
}

} // namespace

struct RadientTesseraBufferSuballocatorImpl
{
    explicit RadientTesseraBufferSuballocatorImpl(const RadientTesseraBufferSuballocator::CreateInfo& CI) :
        AllocationAlignment{CI.AllocationAlignment},
        MinimumBoundRange{CI.MinimumBoundRange},
        RegionManager{VariableSizeAllocationsManager::CreateInfo{
            DefaultRawMemoryAllocator::GetAllocator(),
            CI.InitialSize,
            true,
        }},
        Buffer{nullptr, DynamicBufferCreateInfo{CI.Desc}}
    {}

    ~RadientTesseraBufferSuballocatorImpl()
    {
        VERIFY(RegionManager.GetUsedSize() == 0, "Not all Tessera buffer regions have been released");
    }

    void Free(VariableSizeAllocationsManager::Allocation&& Allocation)
    {
        std::lock_guard<std::mutex> Lock{Mutex};
        RegionManager.Free(std::move(Allocation));
    }

    const Uint32 AllocationAlignment;
    const Uint32 MinimumBoundRange;

    mutable std::mutex             Mutex;
    VariableSizeAllocationsManager RegionManager;
    DynamicBuffer                  Buffer;
    std::vector<Uint8>             Data;
    size_t                         RequiredBufferSize = 0;
    size_t                         DirtyRangeStart    = (std::numeric_limits<size_t>::max)();
    size_t                         DirtyRangeEnd      = 0;
    Uint64                         CurrentGeneration  = 0;
    std::atomic<Uint64>            UploadedGeneration{0};
};

struct RadientTesseraBufferAllocationState
{
    RadientTesseraBufferAllocationState(
        std::shared_ptr<RadientTesseraBufferSuballocatorImpl> Owner,
        VariableSizeAllocationsManager::Allocation            Region,
        Uint32                                                AlignedOffset,
        Uint32                                                DataSize,
        Uint64                                                DataGeneration) :
        pOwner{std::move(Owner)},
        Allocation{std::move(Region)},
        Offset{AlignedOffset},
        Size{DataSize},
        Generation{DataGeneration}
    {}

    ~RadientTesseraBufferAllocationState()
    {
        if (pOwner != nullptr && Allocation.IsValid())
            pOwner->Free(std::move(Allocation));
    }

    std::shared_ptr<RadientTesseraBufferSuballocatorImpl> pOwner;
    VariableSizeAllocationsManager::Allocation            Allocation;
    const Uint32                                          Offset;
    const Uint32                                          Size;
    std::atomic<Uint64>                                   Generation;
};

Uint32 RadientTesseraBufferAllocation::GetOffset() const noexcept
{
    return m_pState != nullptr ? m_pState->Offset : ~Uint32{0};
}

Uint32 RadientTesseraBufferAllocation::GetSize() const noexcept
{
    return m_pState != nullptr ? m_pState->Size : 0;
}

bool RadientTesseraBufferAllocation::IsUploadedThrough(Uint64 UploadedGeneration) const noexcept
{
    return m_pState != nullptr &&
        UploadedGeneration >= m_pState->Generation.load(std::memory_order_acquire);
}

RadientTesseraBufferSuballocator::RadientTesseraBufferSuballocator(const CreateInfo& CI)
{
    if (CI.Desc.Size != 0)
        LOG_ERROR_AND_THROW("Tessera suballocated buffer description size must be zero");
    if (CI.Desc.Usage != USAGE_DEFAULT)
        LOG_ERROR_AND_THROW("Tessera suballocated buffer must use USAGE_DEFAULT");
    if (CI.InitialSize == 0)
        LOG_ERROR_AND_THROW("Tessera suballocated buffer initial size must not be zero");
    if (CI.AllocationAlignment == 0 || !IsPowerOfTwo(CI.AllocationAlignment))
        LOG_ERROR_AND_THROW("Tessera suballocated buffer alignment must be a non-zero power of two");

    m_pImpl = std::make_shared<RadientTesseraBufferSuballocatorImpl>(CI);
}

RadientTesseraBufferSuballocator::~RadientTesseraBufferSuballocator() = default;

RadientTesseraBufferAllocation RadientTesseraBufferSuballocator::Allocate(
    Uint32      Size,
    const void* pInitialData)
{
    if (Size == 0)
        return {};

    std::lock_guard<std::mutex> Lock{m_pImpl->Mutex};

    VariableSizeAllocationsManager::Allocation Allocation =
        m_pImpl->RegionManager.Allocate(Size, m_pImpl->AllocationAlignment);
    while (!Allocation.IsValid())
    {
        const size_t CurrentSize = m_pImpl->RegionManager.GetMaxSize();
        if (CurrentSize > (std::numeric_limits<size_t>::max)() / 2)
            return {};

        m_pImpl->RegionManager.Extend(CurrentSize);
        Allocation = m_pImpl->RegionManager.Allocate(Size, m_pImpl->AllocationAlignment);
    }

    const size_t     Offset           = AlignUp(Allocation.UnalignedOffset, size_t{m_pImpl->AllocationAlignment});
    constexpr size_t MaxAllocationEnd = (std::numeric_limits<Uint32>::max)();
    if (Offset > MaxAllocationEnd ||
        Size > MaxAllocationEnd - Offset)
    {
        m_pImpl->RegionManager.Free(std::move(Allocation));
        return {};
    }

    // A fixed resource binding may expose more bytes than this allocation owns.
    // Ensure that range exists after every stable allocation offset without
    // reserving it exclusively in the CPU allocator.
    const size_t RequiredRange = std::max<size_t>(Size, m_pImpl->MinimumBoundRange);
    if (RequiredRange > MaxAllocationEnd - Offset)
    {
        m_pImpl->RegionManager.Free(std::move(Allocation));
        return {};
    }

    const size_t RequiredEnd  = Offset + RequiredRange;
    const size_t AlignmentPad = m_pImpl->AllocationAlignment - 1u;
    if (RequiredEnd > MaxAllocationEnd - AlignmentPad)
    {
        m_pImpl->RegionManager.Free(std::move(Allocation));
        return {};
    }

    m_pImpl->RequiredBufferSize = std::max(
        m_pImpl->RequiredBufferSize,
        AlignUp(RequiredEnd, size_t{m_pImpl->AllocationAlignment}));
    if (m_pImpl->Data.size() < m_pImpl->RequiredBufferSize)
        m_pImpl->Data.resize(m_pImpl->RequiredBufferSize);

    // An uninitialized reservation must remain pending for every uploaded
    // generation until its first successful Update().
    Uint64 Generation = (std::numeric_limits<Uint64>::max)();
    if (pInitialData != nullptr)
    {
        std::memcpy(m_pImpl->Data.data() + Offset, pInitialData, Size);

        m_pImpl->DirtyRangeStart = std::min(m_pImpl->DirtyRangeStart, Offset);
        m_pImpl->DirtyRangeEnd   = std::max(m_pImpl->DirtyRangeEnd, Offset + Size);
        Generation               = ++m_pImpl->CurrentGeneration;
    }

    auto pState = std::make_shared<RadientTesseraBufferAllocationState>(
        m_pImpl,
        std::move(Allocation),
        static_cast<Uint32>(Offset),
        Size,
        Generation);
    return RadientTesseraBufferAllocation{std::move(pState)};
}

RADIENT_STATUS RadientTesseraBufferSuballocator::Update(
    const RadientTesseraBufferAllocation& Allocation,
    Uint32                                RelativeOffset,
    const void*                           pData,
    Uint32                                Size)
{
    return Update(Allocation,
                  RelativeOffset,
                  Size,
                  CopyUpdateData,
                  const_cast<void*>(pData));
}

RADIENT_STATUS RadientTesseraBufferSuballocator::Update(
    const RadientTesseraBufferAllocation& Allocation,
    Uint32                                RelativeOffset,
    Uint32                                Size,
    UpdateCallbackType                    UpdateData,
    void*                                 pUserData)
{
    const std::shared_ptr<RadientTesseraBufferAllocationState>& pState =
        Allocation.m_pState;
    if (pState == nullptr || pState->pOwner != m_pImpl ||
        RelativeOffset > pState->Size)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (Size == 0)
        return RADIENT_STATUS_OK;

    if (UpdateData == nullptr || Size > pState->Size - RelativeOffset)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> Lock{m_pImpl->Mutex};

    const size_t BufferOffset = size_t{pState->Offset} + RelativeOffset;
    VERIFY_EXPR(BufferOffset + Size <= m_pImpl->Data.size());
    const RADIENT_STATUS Status = UpdateData(m_pImpl->Data.data() + BufferOffset, Size, pUserData);
    if (Status != RADIENT_STATUS_OK)
        return Status;

    m_pImpl->DirtyRangeStart = std::min(m_pImpl->DirtyRangeStart, BufferOffset);
    m_pImpl->DirtyRangeEnd   = std::max(m_pImpl->DirtyRangeEnd, BufferOffset + Size);

    const Uint64 Generation = ++m_pImpl->CurrentGeneration;
    pState->Generation.store(Generation, std::memory_order_release);
    return RADIENT_STATUS_OK;
}

RADIENT_STATUS RadientTesseraBufferSuballocator::Prepare(IRenderDevice*  pDevice,
                                                         IDeviceContext* pContext)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> Lock{m_pImpl->Mutex};

    IBuffer* pBuffer = nullptr;
    if (m_pImpl->RequiredBufferSize > m_pImpl->Buffer.GetDesc().Size)
    {
        Uint64 NewBufferSize = std::max<Uint64>(m_pImpl->Buffer.GetDesc().Size,
                                                m_pImpl->RegionManager.GetMaxSize());
        while (NewBufferSize < m_pImpl->RequiredBufferSize)
            NewBufferSize *= 2;

        pBuffer = m_pImpl->Buffer.Resize(pDevice, pContext, NewBufferSize);
    }
    else
    {
        pBuffer = m_pImpl->Buffer.GetBuffer();
    }

    if (pBuffer == nullptr && m_pImpl->RequiredBufferSize != 0)
        return RADIENT_STATUS_FAILED;

    if (m_pImpl->DirtyRangeStart < m_pImpl->DirtyRangeEnd)
    {
        size_t UploadStart = m_pImpl->DirtyRangeStart;
        size_t UploadEnd   = m_pImpl->DirtyRangeEnd;
        if (pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11 &&
            (pBuffer->GetDesc().BindFlags & BIND_UNIFORM_BUFFER) != 0)
        {
            // D3D11 does not support partial constant-buffer updates, so the
            // CPU shadow must cover the geometrically overallocated buffer.
            const size_t BufferSize = StaticCast<size_t>(pBuffer->GetDesc().Size);
            m_pImpl->Data.resize(BufferSize);
            UploadStart = 0;
            UploadEnd   = BufferSize;
        }

        pContext->UpdateBuffer(pBuffer,
                               UploadStart,
                               UploadEnd - UploadStart,
                               m_pImpl->Data.data() + UploadStart,
                               RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImpl->DirtyRangeStart = (std::numeric_limits<size_t>::max)();
        m_pImpl->DirtyRangeEnd   = 0;
    }

    // Allocate() and Prepare() use the same mutex, so this publishes every
    // CPU record observed by this upload. A later allocation receives a newer
    // generation and remains pending until the next Prepare().
    if (pBuffer != nullptr)
        m_pImpl->UploadedGeneration.store(m_pImpl->CurrentGeneration, std::memory_order_release);

    return RADIENT_STATUS_OK;
}

IBuffer* RadientTesseraBufferSuballocator::GetBuffer() const noexcept
{
    return m_pImpl->Buffer.GetBuffer();
}

Uint32 RadientTesseraBufferSuballocator::GetVersion() const noexcept
{
    return m_pImpl->Buffer.GetVersion();
}

Uint64 RadientTesseraBufferSuballocator::GetUploadedGeneration() const noexcept
{
    return m_pImpl->UploadedGeneration.load(std::memory_order_acquire);
}

} // namespace Diligent
