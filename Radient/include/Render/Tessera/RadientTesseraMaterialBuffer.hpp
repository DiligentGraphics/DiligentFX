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

#include "RadientTypes.h"

#include <memory>
#include <utility>

namespace Diligent
{

struct IBuffer;
struct IDeviceContext;
struct IRenderDevice;

struct RadientTesseraMaterialBufferImpl;
struct RadientTesseraMaterialBufferAllocationState;

/// Owning handle to one immutable material record in the shared Tessera
/// material buffer. The region is returned to the allocator when the last
/// handle is destroyed.
class RadientTesseraMaterialBufferAllocation final
{
public:
    RadientTesseraMaterialBufferAllocation() noexcept = default;

    explicit operator bool() const noexcept
    {
        return m_pState != nullptr;
    }

    Uint32 GetOffset() const noexcept;
    Uint32 GetSize() const noexcept;

private:
    explicit RadientTesseraMaterialBufferAllocation(
        std::shared_ptr<RadientTesseraMaterialBufferAllocationState> pState) noexcept :
        m_pState{std::move(pState)}
    {}

    std::shared_ptr<RadientTesseraMaterialBufferAllocationState> m_pState;

    friend class RadientTesseraMaterialBuffer;
};

/// Thread-safe CPU allocator and render-thread GPU uploader for immutable
/// Tessera material shader data. Stable allocation offsets survive buffer
/// growth, while the buffer version identifies replacement GPU buffers.
class RadientTesseraMaterialBuffer final
{
public:
    struct CreateInfo
    {
        Uint32 ConstantBufferOffsetAlignment = 0;
        Uint32 MaxMaterialAttribsSize        = 0;
    };

    explicit RadientTesseraMaterialBuffer(const CreateInfo& CI);
    ~RadientTesseraMaterialBuffer();

    RadientTesseraMaterialBuffer(const RadientTesseraMaterialBuffer&)            = delete;
    RadientTesseraMaterialBuffer& operator=(const RadientTesseraMaterialBuffer&) = delete;
    RadientTesseraMaterialBuffer(RadientTesseraMaterialBuffer&&)                 = delete;
    RadientTesseraMaterialBuffer& operator=(RadientTesseraMaterialBuffer&&)      = delete;

    /// Allocates an aligned region and copies immutable shader data into the
    /// CPU shadow. This method may be called concurrently from worker threads.
    RadientTesseraMaterialBufferAllocation Allocate(const void* pData, Uint32 Size);

    /// Creates or grows the GPU buffer and uploads all dirty material records.
    /// This method must only be called from the render thread.
    RADIENT_STATUS Prepare(IRenderDevice* pDevice, IDeviceContext* pContext);

    IBuffer* GetBuffer() const noexcept;
    Uint32   GetVersion() const noexcept;
    Uint32   GetMaxMaterialAttribsSize() const noexcept;

private:
    std::shared_ptr<RadientTesseraMaterialBufferImpl> m_pImpl;
};

} // namespace Diligent
