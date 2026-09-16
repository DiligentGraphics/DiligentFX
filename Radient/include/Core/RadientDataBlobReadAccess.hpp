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

/// \file
/// Internal scoped read access to Radient data blobs.

#include <utility>

#include "RadientDataBlob.h"
#include "RefCntAutoPtr.hpp"

namespace Diligent
{

/// Owns one shared read scope and keeps its blob alive until access is released.
///
/// Accepts both read-only and mutable blobs. Construction attempts BeginRead;
/// use operator bool() to check whether access was acquired. Successful empty
/// scopes are active even though GetData() is null. Moving transfers the read
/// scope without calling BeginRead or EndRead on it. Copying is not allowed.
///
/// Destruction and move assignment release any previously owned scope and ignore
/// its EndRead result. Use Reset() explicitly to observe callback errors. Never
/// call EndRead directly for a read owned by this helper. Concurrent access to the
/// same helper requires external synchronization, as does handing it to another
/// thread. Data pointers are valid only while the owning read scope remains active.
class RadientDataBlobReadAccess
{
public:
    /// Creates an inactive scope.
    RadientDataBlobReadAccess() noexcept = default;

    /// Attempts to acquire read access, retaining pBlob through the matching EndRead.
    /// A null blob or failed acquisition leaves the scope inactive, retains no
    /// reference, and never calls EndRead.
    explicit RadientDataBlobReadAccess(IRadientDataBlob* pBlob) :
        m_pBlob{pBlob}
    {
        if (m_pBlob != nullptr && m_pBlob->BeginRead(&m_pData) == RADIENT_STATUS_OK)
        {
            // Query only after acquisition: a mutable blob's size is now stable.
            m_Size = m_pBlob->GetSize();
        }
        else
        {
            m_pData = nullptr;
            m_pBlob.Release();
        }
    }

    RadientDataBlobReadAccess(const RadientDataBlobReadAccess&)            = delete;
    RadientDataBlobReadAccess& operator=(const RadientDataBlobReadAccess&) = delete;

    // clang-format off
    RadientDataBlobReadAccess(RadientDataBlobReadAccess&& Other) noexcept :
        m_pBlob {std::move(Other.m_pBlob)},
        m_pData {std::exchange(Other.m_pData, nullptr)},
        m_Size  {std::exchange(Other.m_Size, Uint64{0})}
    // clang-format on
    {}

    RadientDataBlobReadAccess& operator=(RadientDataBlobReadAccess&& Other) noexcept
    {
        if (this != &Other)
        {
            RadientDataBlobReadAccess Previous{std::move(Other)};
            m_pBlob.swap(Previous.m_pBlob);
            std::swap(m_pData, Previous.m_pData);
            std::swap(m_Size, Previous.m_Size);
            // Release the old scope only after both helpers expose their new state:
            // EndRead may invoke a callback that accesses either helper.
        }
        return *this;
    }

    ~RadientDataBlobReadAccess()
    {
        Reset();
    }

    /// Returns true if this helper owns read access, including for an empty blob.
    explicit operator bool() const noexcept { return m_pBlob != nullptr; }

    /// Returns the read pointer, or null for an inactive scope or an empty blob.
    const void* GetData() const noexcept { return m_pData; }

    /// Returns the size captured after acquisition, or zero for an inactive scope.
    Uint64 GetSize() const noexcept { return m_Size; }

    /// Releases read access and the strong reference, leaving this helper inactive.
    /// Returns EndRead's result, or RADIENT_STATUS_NO_CHANGE if already inactive.
    /// The scope is released even if a callback makes EndRead return FAILED.
    /// State is cleared before invoking EndRead, so reentrant Reset() is harmless.
    RADIENT_STATUS Reset() noexcept
    {
        RefCntAutoPtr<IRadientDataBlob> pBlob{std::move(m_pBlob)};
        m_pData = nullptr;
        m_Size  = 0;
        return pBlob != nullptr ? pBlob->EndRead() : RADIENT_STATUS_NO_CHANGE;
    }

private:
    RefCntAutoPtr<IRadientDataBlob> m_pBlob;
    const void*                     m_pData = nullptr;
    Uint64                          m_Size  = 0;
};

} // namespace Diligent
