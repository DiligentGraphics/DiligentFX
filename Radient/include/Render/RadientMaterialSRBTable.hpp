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

#include "Assets/RadientMaterialAssetManager.hpp"
#include "PBR_Renderer.hpp"
#include "RefCntAutoPtr.hpp"
#include "ShaderResourceBinding.h"

#include <array>
#include <functional>
#include <memory>

namespace Diligent
{

/// Resolved default material texture bindings used to initialize SRB slots.
/// White has separate linear and sRGB entries so default-filled slots use the
/// same typed views as real textures with the corresponding semantics.
struct RadientMaterialDefaultTextureBindings
{
    RadientMaterialTextureRenderData WhiteLinear;
    RadientMaterialTextureRenderData WhiteSRGB;
    RadientMaterialTextureRenderData BlackSRGB;
    RadientMaterialTextureRenderData Normal;
    RadientMaterialTextureRenderData PhysicalDesc;

    const RadientMaterialTextureRenderData* Get(PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) const noexcept;

    explicit operator bool() const noexcept
    {
        return WhiteLinear && WhiteSRGB && BlackSRGB && Normal && PhysicalDesc;
    }
};

class RadientMaterialSRBState;
class RadientMaterialSRBTable;

struct RadientMaterialTextureSRVResolveResult
{
    RADIENT_STATUS Status      = RADIENT_STATUS_INVALID_ARGUMENT;
    ITextureView*  pTextureSRV = nullptr;
};

/// Owning reference to a stable material SRB table entry. The entry and its
/// texture resources are released when the last lease is destroyed.
/// SRB accessors must only be called from the render thread after table preparation.
class RadientMaterialSRBLease
{
public:
    RadientMaterialSRBLease() noexcept = default;

    IShaderResourceBinding*  GetSRB() const noexcept;
    IShaderResourceVariable* GetPrimitiveAttribsVariable() const noexcept;
    IShaderResourceVariable* GetMaterialAttribsVariable() const noexcept;

    /// Returns the material buffer version and uploaded generation bound by
    /// this SRB. These values remain unchanged while replacement is pending.
    Uint32 GetMaterialBufferVersion() const noexcept;
    Uint64 GetMaterialBufferGeneration() const noexcept;

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

/// Cache of material SRBs indexed by their ordered logical texture slots.
/// Acquire() is thread-safe and only reserves logical entries; Prepare() and
/// SRB access are render-thread-only. Entries are owned by
/// RadientMaterialSRBLease instances, and unused records are removed by
/// Prepare().
class RadientMaterialSRBTable final
{
public:
    using ResolveTextureSRVCallbackType =
        std::function<RadientMaterialTextureSRVResolveResult(const RadientMaterialTextureRenderData&)>;
    using CreateSRBCallbackType =
        std::function<RefCntAutoPtr<IShaderResourceBinding>(ITextureView* const*, Uint32)>;


    RadientMaterialSRBTable();
    ~RadientMaterialSRBTable();

    RadientMaterialSRBTable(const RadientMaterialSRBTable&)            = delete;
    RadientMaterialSRBTable& operator=(const RadientMaterialSRBTable&) = delete;
    RadientMaterialSRBTable(RadientMaterialSRBTable&&)                 = delete;
    RadientMaterialSRBTable& operator=(RadientMaterialSRBTable&&)      = delete;

    /// Thread-safely builds the material texture mapping and reuses or reserves
    /// the matching SRB entry without creating GPU objects. Slot texture
    /// references are only retained when a new entry is inserted.
    RADIENT_STATUS Acquire(
        const RadientMaterialRenderData&                              MaterialData,
        const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
        PBR_Renderer::PSO_FLAGS                                       PSOFlags,
        Uint32                                                        MaxTextureSlots,
        const RadientMaterialDefaultTextureBindings&                  DefaultTextures,
        RadientMaterialSRBLease&                                      Lease,
        PBR_Renderer::StaticShaderTextureIdsArrayType&                ShaderTextureIds);

    /// Thread-safely reuses or reserves an entry for a complete ordered slot
    /// recipe without retaining the supplied texture references on a cache hit.
    RadientMaterialSRBLease Acquire(
        const RadientMaterialTextureRenderData* const* ppSlots,
        Uint32                                         SlotCount);

    /// Creates new SRBs and recreates existing SRBs when the texture resources
    /// or shared material buffer change. Pending texture resolutions are
    /// skipped and retried by the next call.
    RADIENT_STATUS Prepare(Uint32                               TextureVersion,
                           Uint32                               MaterialBufferVersion,
                           Uint64                               MaterialBufferGeneration,
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
