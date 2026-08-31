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

#pragma once

#include "Buffer.h"
#include "RadientTypes.h"

#include <memory>
#include <utility>

namespace Diligent
{

struct IBuffer;
struct IDeviceContext;
struct IRenderDevice;

struct RadientTesseraBufferSuballocatorImpl;
struct RadientTesseraBufferAllocationState;

/// Owning handle to one persistent region in a shared Tessera buffer. The
/// region is returned to the allocator when the last handle is destroyed.
class RadientTesseraBufferAllocation final
{
public:
    RadientTesseraBufferAllocation() noexcept = default;

    explicit operator bool() const noexcept
    {
        return m_pState != nullptr;
    }

    Uint32 GetOffset() const noexcept;
    Uint32 GetSize() const noexcept;

    /// Returns true if this allocation is included in the specified uploaded
    /// generation of the owning buffer.
    bool IsUploadedThrough(Uint64 UploadedGeneration) const noexcept;

private:
    explicit RadientTesseraBufferAllocation(
        std::shared_ptr<RadientTesseraBufferAllocationState> pState) noexcept :
        m_pState{std::move(pState)}
    {}

    std::shared_ptr<RadientTesseraBufferAllocationState> m_pState;

    friend class RadientTesseraBufferSuballocator;
};

/// Thread-safe allocator, CPU shadow, and render-thread GPU uploader for
/// persistent Tessera buffer regions. Stable allocation offsets survive
/// buffer growth, while the buffer version identifies replacement GPU buffers.
class RadientTesseraBufferSuballocator final
{
public:
    using UpdateCallbackType = RADIENT_STATUS (*)(void* pData, Uint32 Size, void* pUserData);

    struct CreateInfo
    {
        /// Description of the resizable GPU buffer. A non-zero Size creates an
        /// initial GPU buffer during the first Prepare(); zero keeps GPU
        /// storage lazy until the first allocation.
        BufferDesc Desc;

        /// Initial capacity of the CPU allocation arena.
        Uint32 InitialSize = 64u << 10u;

        /// Alignment of every allocation offset. Must be a non-zero power of two.
        Uint32 AllocationAlignment = 1;

        /// Minimum number of GPU-buffer bytes that must exist starting at every
        /// allocation offset. This does not change the number of bytes owned by
        /// the allocation. It supports bindings whose fixed range is larger
        /// than the data stored in an individual allocation.
        Uint32 MinimumBoundRange = 0;
    };

    explicit RadientTesseraBufferSuballocator(const CreateInfo& CI);
    ~RadientTesseraBufferSuballocator();

    // clang-format off
    RadientTesseraBufferSuballocator           (const RadientTesseraBufferSuballocator&) = delete;
    RadientTesseraBufferSuballocator& operator=(const RadientTesseraBufferSuballocator&) = delete;
    RadientTesseraBufferSuballocator           (RadientTesseraBufferSuballocator&&)      = delete;
    RadientTesseraBufferSuballocator& operator=(RadientTesseraBufferSuballocator&&)      = delete;
    // clang-format on

    /// Allocates an aligned region. If pInitialData is not null, Size bytes are
    /// copied into the CPU shadow. Otherwise, the allocation remains pending
    /// until Update() populates it. This method may be called concurrently from
    /// worker threads.
    RadientTesseraBufferAllocation Allocate(Uint32 Size, const void* pInitialData = nullptr);

    /// Replaces a byte range in an existing allocation and marks it for
    /// upload. RelativeOffset is measured from the start of Allocation, not
    /// from the start of the shared buffer. The allocation remains at
    /// the same buffer offset and becomes pending until Prepare() uploads the
    /// new generation. An empty range is accepted as a successful no-op and
    /// pData may be null in that case.
    RADIENT_STATUS Update(const RadientTesseraBufferAllocation& Allocation,
                          Uint32                                RelativeOffset,
                          const void*                           pData,
                          Uint32                                Size);

    /// Invokes UpdateData synchronously with direct access to a range in the
    /// allocation's CPU shadow. The range is marked for upload only when the
    /// callback returns RADIENT_STATUS_OK. The callback executes while the
    /// allocator is locked and must not call back into this allocator.
    RADIENT_STATUS Update(const RadientTesseraBufferAllocation& Allocation,
                          Uint32                                RelativeOffset,
                          Uint32                                Size,
                          UpdateCallbackType                    UpdateData,
                          void*                                 pUserData = nullptr);

    /// Creates or grows the GPU buffer and uploads all dirty regions.
    /// This method must only be called from the render thread.
    RADIENT_STATUS Prepare(IRenderDevice* pDevice, IDeviceContext* pContext);

    IBuffer* GetBuffer() const noexcept;
    Uint32   GetVersion() const noexcept;
    Uint64   GetUploadedGeneration() const noexcept;

private:
    std::shared_ptr<RadientTesseraBufferSuballocatorImpl> m_pImpl;
};

} // namespace Diligent
