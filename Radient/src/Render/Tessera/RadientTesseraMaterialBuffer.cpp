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

#include "Render/Tessera/RadientTesseraMaterialBuffer.hpp"

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

constexpr size_t InitialMaterialBufferSize = 64u << 10u;

} // namespace

struct RadientTesseraMaterialBufferImpl
{
    RadientTesseraMaterialBufferImpl(Uint32 Alignment, Uint32 MaxAttribsSize) :
        ConstantBufferOffsetAlignment{Alignment},
        MaxMaterialAttribsSize{MaxAttribsSize},
        RegionManager{VariableSizeAllocationsManager::CreateInfo{
            DefaultRawMemoryAllocator::GetAllocator(),
            InitialMaterialBufferSize,
            true,
        }},
        Buffer{nullptr,
               DynamicBufferCreateInfo{
                   BufferDesc{
                       "Radient Tessera material attributes buffer",
                       0,
                       BIND_UNIFORM_BUFFER,
                       USAGE_DEFAULT,
                   },
               }}
    {}

    ~RadientTesseraMaterialBufferImpl()
    {
        VERIFY(RegionManager.GetUsedSize() == 0, "Not all Tessera material buffer regions have been released");
    }

    void Free(VariableSizeAllocationsManager::Allocation&& Allocation)
    {
        std::lock_guard<std::mutex> Lock{Mutex};
        RegionManager.Free(std::move(Allocation));
    }

    const Uint32 ConstantBufferOffsetAlignment;
    const Uint32 MaxMaterialAttribsSize;

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

struct RadientTesseraMaterialBufferAllocationState
{
    RadientTesseraMaterialBufferAllocationState(
        std::shared_ptr<RadientTesseraMaterialBufferImpl> Owner,
        VariableSizeAllocationsManager::Allocation        Region,
        Uint32                                            AlignedOffset,
        Uint32                                            DataSize,
        Uint64                                            DataGeneration) :
        pOwner{std::move(Owner)},
        Allocation{std::move(Region)},
        Offset{AlignedOffset},
        Size{DataSize},
        Generation{DataGeneration}
    {}

    ~RadientTesseraMaterialBufferAllocationState()
    {
        if (pOwner != nullptr && Allocation.IsValid())
            pOwner->Free(std::move(Allocation));
    }

    std::shared_ptr<RadientTesseraMaterialBufferImpl> pOwner;
    VariableSizeAllocationsManager::Allocation        Allocation;
    const Uint32                                      Offset;
    const Uint32                                      Size;
    const Uint64                                      Generation;
};

Uint32 RadientTesseraMaterialBufferAllocation::GetOffset() const noexcept
{
    return m_pState != nullptr ? m_pState->Offset : ~Uint32{0};
}

Uint32 RadientTesseraMaterialBufferAllocation::GetSize() const noexcept
{
    return m_pState != nullptr ? m_pState->Size : 0;
}

bool RadientTesseraMaterialBufferAllocation::IsUploadedThrough(Uint64 UploadedGeneration) const noexcept
{
    return m_pState != nullptr && UploadedGeneration >= m_pState->Generation;
}

RadientTesseraMaterialBuffer::RadientTesseraMaterialBuffer(const CreateInfo& CI)
{
    if (CI.ConstantBufferOffsetAlignment == 0 || !IsPowerOfTwo(CI.ConstantBufferOffsetAlignment))
        LOG_ERROR_AND_THROW("Tessera material buffer offset alignment must be a non-zero power of two");
    if (CI.MaxMaterialAttribsSize == 0)
        LOG_ERROR_AND_THROW("Tessera maximum material attributes size must not be zero");

    m_pImpl = std::make_shared<RadientTesseraMaterialBufferImpl>(
        CI.ConstantBufferOffsetAlignment,
        CI.MaxMaterialAttribsSize);
}

RadientTesseraMaterialBuffer::~RadientTesseraMaterialBuffer() = default;

RadientTesseraMaterialBufferAllocation RadientTesseraMaterialBuffer::Allocate(
    const void* pData,
    Uint32      Size)
{
    if (pData == nullptr || Size == 0 || Size > m_pImpl->MaxMaterialAttribsSize)
        return {};

    std::lock_guard<std::mutex> Lock{m_pImpl->Mutex};

    VariableSizeAllocationsManager::Allocation Allocation =
        m_pImpl->RegionManager.Allocate(Size, m_pImpl->ConstantBufferOffsetAlignment);
    while (!Allocation.IsValid())
    {
        const size_t CurrentSize = m_pImpl->RegionManager.GetMaxSize();
        if (CurrentSize > (std::numeric_limits<size_t>::max)() / 2)
            return {};

        m_pImpl->RegionManager.Extend(CurrentSize);
        Allocation = m_pImpl->RegionManager.Allocate(Size, m_pImpl->ConstantBufferOffsetAlignment);
    }

    const size_t Offset = AlignUp(Allocation.UnalignedOffset, size_t{m_pImpl->ConstantBufferOffsetAlignment});
    if (Offset > (std::numeric_limits<Uint32>::max)() ||
        Offset + Size > (std::numeric_limits<Uint32>::max)())
    {
        m_pImpl->RegionManager.Free(std::move(Allocation));
        return {};
    }

    // Bind each SRB to the maximum material range, so the backing buffer must
    // reserve that full range after every stable material offset.
    m_pImpl->RequiredBufferSize = std::max(
        m_pImpl->RequiredBufferSize,
        AlignUp(Offset + m_pImpl->MaxMaterialAttribsSize, size_t{m_pImpl->ConstantBufferOffsetAlignment}));
    if (m_pImpl->Data.size() < m_pImpl->RequiredBufferSize)
        m_pImpl->Data.resize(m_pImpl->RequiredBufferSize);
    std::memcpy(m_pImpl->Data.data() + Offset, pData, Size);

    m_pImpl->DirtyRangeStart = std::min(m_pImpl->DirtyRangeStart, Offset);
    m_pImpl->DirtyRangeEnd   = std::max(m_pImpl->DirtyRangeEnd, Offset + Size);

    const Uint64 Generation = ++m_pImpl->CurrentGeneration;

    auto pState = std::make_shared<RadientTesseraMaterialBufferAllocationState>(
        m_pImpl,
        std::move(Allocation),
        static_cast<Uint32>(Offset),
        Size,
        Generation);
    return RadientTesseraMaterialBufferAllocation{std::move(pState)};
}

RADIENT_STATUS RadientTesseraMaterialBuffer::Prepare(IRenderDevice*  pDevice,
                                                     IDeviceContext* pContext)
{
    if (pDevice == nullptr || pContext == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> Lock{m_pImpl->Mutex};

    IBuffer* pBuffer = nullptr;
    if (m_pImpl->RequiredBufferSize > m_pImpl->Buffer.GetDesc().Size)
    {
        Uint64 NewBufferSize = std::max<Uint64>(m_pImpl->Buffer.GetDesc().Size,
                                                InitialMaterialBufferSize);
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
        if (pDevice->GetDeviceInfo().Type == RENDER_DEVICE_TYPE_D3D11)
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

IBuffer* RadientTesseraMaterialBuffer::GetBuffer() const noexcept
{
    return m_pImpl->Buffer.GetBuffer();
}

Uint32 RadientTesseraMaterialBuffer::GetVersion() const noexcept
{
    return m_pImpl->Buffer.GetVersion();
}

Uint64 RadientTesseraMaterialBuffer::GetUploadedGeneration() const noexcept
{
    return m_pImpl->UploadedGeneration.load(std::memory_order_acquire);
}

Uint32 RadientTesseraMaterialBuffer::GetMaxMaterialAttribsSize() const noexcept
{
    return m_pImpl->MaxMaterialAttribsSize;
}

} // namespace Diligent
