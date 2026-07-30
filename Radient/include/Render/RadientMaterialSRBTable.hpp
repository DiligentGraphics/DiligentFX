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

#include "Render/RadientMaterialTextureBinding.hpp"

#include "RefCntAutoPtr.hpp"
#include "ShaderResourceBinding.h"

#include <functional>
#include <memory>

namespace Diligent
{

class RadientMaterialSRBState;
class RadientMaterialSRBTable;

/// Owning reference to a stable material SRB table entry. The entry and its
/// texture resources are released when the last lease is destroyed.
/// GetSRB() must only be called from the render thread after table preparation.
class RadientMaterialSRBLease
{
public:
    RadientMaterialSRBLease() noexcept = default;

    IShaderResourceBinding* GetSRB() const noexcept;

    explicit operator bool() const noexcept
    {
        return m_pState != nullptr;
    }

private:
    explicit RadientMaterialSRBLease(std::shared_ptr<RadientMaterialSRBState> pState) noexcept :
        m_pState{std::move(pState)}
    {}

    std::shared_ptr<RadientMaterialSRBState> m_pState;

    friend class RadientMaterialSRBTable;
};

/// Identifies an SRB exclusively by its complete ordered logical texture slots.
/// Shader texture indexing intentionally does not participate in this key.
struct RadientMaterialSRBIdentity
{
    absl::InlinedVector<RadientTextureBindingIdentity, 8> Slots;

    bool operator==(const RadientMaterialSRBIdentity& Rhs) const noexcept;

    struct Hasher
    {
        size_t operator()(const RadientMaterialSRBIdentity& Identity) const noexcept;
    };
};

bool BuildRadientMaterialSRBIdentity(const RadientMaterialTextureBindingPlan& Plan,
                                     RadientMaterialSRBIdentity&              Identity) noexcept;

/// Renderer-owned table that reserves stable material SRB entries by logical
/// texture recipe. Reservation is thread-safe; Prepare() and SRB access are
/// render-thread-only.
class RadientMaterialSRBTable final
{
public:
    using ResolveTextureSRVCallbackType =
        std::function<ITextureView*(const RadientMaterialTextureRenderData&)>;
    using CreateSRBCallbackType =
        std::function<RefCntAutoPtr<IShaderResourceBinding>(ITextureView* const*, Uint32)>;


    RadientMaterialSRBTable();
    ~RadientMaterialSRBTable();

    RadientMaterialSRBTable(const RadientMaterialSRBTable&)            = delete;
    RadientMaterialSRBTable& operator=(const RadientMaterialSRBTable&) = delete;
    RadientMaterialSRBTable(RadientMaterialSRBTable&&)                 = delete;
    RadientMaterialSRBTable& operator=(RadientMaterialSRBTable&&)      = delete;

    /// Reuses or reserves an entry without creating GPU objects.
    RadientMaterialSRBLease Acquire(const RadientMaterialTextureBindingPlan& Plan);

    /// Creates new SRBs and recreates existing SRBs when TextureVersion changes.
    RADIENT_STATUS Prepare(Uint32                               TextureVersion,
                           const ResolveTextureSRVCallbackType& ResolveTextureSRV,
                           const CreateSRBCallbackType&         CreateSRB);

    /// Returns the number of retained table records. Expired weak entries may
    /// remain included until the next Prepare() call removes them.
    size_t GetSize() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace Diligent
