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

#include "Render/RadientMaterialSRBTable.hpp"

#include "Assets/RadientAssetStatus.hpp"
#include "HashUtils.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127) // conditional expression is constant
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <mutex>
#include <utility>
#include <vector>

namespace Diligent
{

const RadientMaterialTextureRenderData* RadientMaterialDefaultTextureBindings::Get(
    PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) const noexcept
{
    if (TextureAttribId >= PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT)
    {
        UNEXPECTED("Invalid PBR texture attribute ID ", Uint32{TextureAttribId});
        return nullptr;
    }

    static_assert(PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT == 17, "Update the switch statement below to handle new PBR texture attributes");
    switch (TextureAttribId)
    {
        case PBR_Renderer::TEXTURE_ATTRIB_ID_NORMAL:
        case PBR_Renderer::TEXTURE_ATTRIB_ID_CLEAR_COAT_NORMAL:
            return &Normal;

        case PBR_Renderer::TEXTURE_ATTRIB_ID_PHYS_DESC:
            return &PhysicalDesc;

        case PBR_Renderer::TEXTURE_ATTRIB_ID_EMISSIVE:
            return &BlackSRGB;

        default:
            return PBR_Renderer::IsSRGBTextureAttribute(TextureAttribId) ?
                &WhiteSRGB :
                &WhiteLinear;
    }
}

namespace
{

/// Identifies an SRB exclusively by its complete ordered logical texture slots.
/// Shader texture indexing intentionally does not participate in this key.
struct RadientMaterialSRBIdentity
{
    absl::InlinedVector<RadientTextureBindingIdentity, 8> Slots;

    bool operator==(const RadientMaterialSRBIdentity& Rhs) const noexcept
    {
        return Slots == Rhs.Slots;
    }

    struct Hasher
    {
        size_t operator()(const RadientMaterialSRBIdentity& Identity) const noexcept
        {
            size_t                                      Hash = ComputeHash(Identity.Slots.size());
            const RadientTextureBindingIdentity::Hasher IdentityHasher;
            for (const RadientTextureBindingIdentity& Slot : Identity.Slots)
                HashCombine(Hash, IdentityHasher(Slot));
            return Hash;
        }
    };
};

} // namespace

class RadientMaterialSRBState
{
public:
    absl::InlinedVector<RadientMaterialTextureRenderData, 8> Slots;

    // Accessed exclusively from the render thread.
    RefCntAutoPtr<IShaderResourceBinding> pSRB;
    IShaderResourceVariable*              pPrimitiveAttribsVar = nullptr;

    Uint32 PreparedTextureVersion = ~0u;
};

IShaderResourceBinding* RadientMaterialSRBLease::GetSRB() const noexcept
{
    return m_pState != nullptr ? m_pState->pSRB.RawPtr() : nullptr;
}

IShaderResourceVariable* RadientMaterialSRBLease::GetPrimitiveAttribsVariable() const noexcept
{
    return m_pState != nullptr ? m_pState->pPrimitiveAttribsVar : nullptr;
}

struct RadientMaterialSRBTable::Impl
{
    mutable std::mutex Mutex;

    // Leases own SRB states. The table keeps weak records so unused standalone
    // textures and their SRBs are released with the last material reference.
    absl::flat_hash_map<RadientMaterialSRBIdentity,
                        std::weak_ptr<RadientMaterialSRBState>,
                        RadientMaterialSRBIdentity::Hasher>
        Lookup;

    // Prepare() snapshots strong references under Mutex, then creates GPU
    // objects without blocking worker-thread acquisition.
    std::vector<std::shared_ptr<RadientMaterialSRBState>> PrepareEntries;
};

RadientMaterialSRBTable::RadientMaterialSRBTable() :
    m_Impl{std::make_unique<Impl>()}
{}

RadientMaterialSRBTable::~RadientMaterialSRBTable() = default;

RADIENT_STATUS RadientMaterialSRBTable::Acquire(
    const RadientMaterialRenderData&                              MaterialData,
    const std::array<int, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT>& TextureAttribIndices,
    PBR_Renderer::PSO_FLAGS                                       PSOFlags,
    Uint32                                                        MaxTextureSlots,
    const RadientMaterialDefaultTextureBindings&                  DefaultTextures,
    RadientMaterialSRBLease&                                      Lease,
    PBR_Renderer::StaticShaderTextureIdsArrayType&                ShaderTextureIds)
{
    Lease = {};
    ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);

    if (MaxTextureSlots == 0)
    {
        UNEXPECTED("Maximum material texture slot count must not be zero");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (MaxTextureSlots > PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT)
    {
        UNEXPECTED("Maximum material texture slot count ", MaxTextureSlots,
                   " exceeds the number of PBR texture attributes ",
                   Uint32{PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT});
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (!DefaultTextures)
    {
        UNEXPECTED("Default material texture bindings are not initialized");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    bool CanUseDefaultMapping = true;
    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            if (AttribId >= MaxTextureSlots)
                CanUseDefaultMapping = false;
        });

    std::array<const RadientMaterialTextureRenderData*, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> SlotSources{};
    for (Uint32 Slot = 0; Slot < MaxTextureSlots; ++Slot)
    {
        const auto                              TextureAttribId = static_cast<PBR_Renderer::TEXTURE_ATTRIB_ID>(Slot);
        const RadientMaterialTextureRenderData* pDefaultTexture = DefaultTextures.Get(TextureAttribId);
        VERIFY(pDefaultTexture != nullptr && *pDefaultTexture,
               "Default material texture for PBR texture attribute ", Slot,
               " is not initialized. This cannot happen since we have already verified that DefaultTextures is valid.");
        SlotSources[Slot] = pDefaultTexture;
    }

    if (CanUseDefaultMapping)
    {
        for (Uint32 Slot = 0; Slot < MaxTextureSlots; ++Slot)
            ShaderTextureIds[Slot] = static_cast<Uint16>(Slot);
    }

    Uint32         NextTextureSlot = 0;
    RADIENT_STATUS Status          = RADIENT_STATUS_OK;
    PBR_Renderer::ProcessTexturAttribs(
        PSOFlags,
        [&](int, PBR_Renderer::TEXTURE_ATTRIB_ID AttribId) {
            if (Status != RADIENT_STATUS_OK)
                return;

            const int TextureId = TextureAttribIndices[AttribId];
            if (TextureId < 0)
            {
                UNEXPECTED("PBR texture attribute ", Uint32{AttribId}, " does not have a material texture mapping");
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            const RadientMaterialTextureRenderData* pTexture =
                MaterialData.GetTextureData(static_cast<Uint32>(TextureId));
            if (pTexture == nullptr || !*pTexture)
            {
                LOG_ERROR_MESSAGE("Material texture ", TextureId, " used by PBR texture attribute ",
                                  Uint32{AttribId}, " is not initialized");
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            size_t SlotIndex = AttribId;
            if (!CanUseDefaultMapping)
            {
                SlotIndex = 0;
                while (SlotIndex < NextTextureSlot &&
                       SlotSources[SlotIndex]->BindingIdentity != pTexture->BindingIdentity)
                    ++SlotIndex;

                if (SlotIndex == NextTextureSlot)
                {
                    if (NextTextureSlot >= MaxTextureSlots)
                    {
                        LOG_ERROR_MESSAGE("Material requires more than ", MaxTextureSlots, " distinct texture bindings");
                        Status = RADIENT_STATUS_INVALID_OPERATION;
                        return;
                    }
                    ++NextTextureSlot;
                }
            }

            SlotSources[SlotIndex]     = pTexture;
            ShaderTextureIds[AttribId] = static_cast<Uint16>(SlotIndex);
        });

    if (Status != RADIENT_STATUS_OK)
    {
        ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
        return Status;
    }

    Lease = Acquire(SlotSources.data(), MaxTextureSlots);
    if (!Lease)
    {
        ShaderTextureIds.fill(PBR_Renderer::InvalidMaterialTextureId);
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    return RADIENT_STATUS_OK;
}

RadientMaterialSRBLease RadientMaterialSRBTable::Acquire(
    const RadientMaterialTextureRenderData* const* ppSlots,
    Uint32                                         SlotCount)
{
    if (ppSlots == nullptr || SlotCount == 0)
        return {};

    RadientMaterialSRBIdentity Identity;
    Identity.Slots.reserve(SlotCount);
    for (Uint32 Slot = 0; Slot < SlotCount; ++Slot)
    {
        if (ppSlots[Slot] == nullptr || !*ppSlots[Slot])
            return {};
        Identity.Slots.push_back(ppSlots[Slot]->BindingIdentity);
    }

    std::lock_guard<std::mutex> Lock{m_Impl->Mutex};

    const auto LookupIt = m_Impl->Lookup.find(Identity);
    if (LookupIt != m_Impl->Lookup.end())
    {
        if (std::shared_ptr<RadientMaterialSRBState> pState = LookupIt->second.lock())
            return RadientMaterialSRBLease{std::move(pState)};

        m_Impl->Lookup.erase(LookupIt);
    }

    auto pState = std::make_shared<RadientMaterialSRBState>();
    pState->Slots.reserve(SlotCount);
    for (Uint32 Slot = 0; Slot < SlotCount; ++Slot)
        pState->Slots.push_back(*ppSlots[Slot]);

    m_Impl->Lookup.emplace(std::move(Identity), pState);
    return RadientMaterialSRBLease{std::move(pState)};
}

RADIENT_STATUS RadientMaterialSRBTable::Prepare(
    Uint32                               TextureVersion,
    const ResolveTextureSRVCallbackType& ResolveTextureSRV,
    const CreateSRBCallbackType&         CreateSRB)
{
    if (!ResolveTextureSRV || !CreateSRB)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    m_Impl->PrepareEntries.clear();
    {
        std::lock_guard<std::mutex> Lock{m_Impl->Mutex};
        m_Impl->PrepareEntries.reserve(m_Impl->Lookup.size());
        for (auto LookupIt = m_Impl->Lookup.begin(); LookupIt != m_Impl->Lookup.end();)
        {
            std::shared_ptr<RadientMaterialSRBState> pState = LookupIt->second.lock();
            if (pState == nullptr)
            {
                m_Impl->Lookup.erase(LookupIt++);
                continue;
            }

            m_Impl->PrepareEntries.push_back(std::move(pState));
            ++LookupIt;
        }
    }

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const std::shared_ptr<RadientMaterialSRBState>& pState : m_Impl->PrepareEntries)
    {
        if (pState->pSRB != nullptr && pState->PreparedTextureVersion == TextureVersion)
            continue;

        absl::InlinedVector<ITextureView*, 8> TextureSRVs;
        TextureSRVs.reserve(pState->Slots.size());
        RADIENT_STATUS EntryStatus = RADIENT_STATUS_OK;
        for (const RadientMaterialTextureRenderData& Slot : pState->Slots)
        {
            const RadientMaterialTextureSRVResolveResult ResolveResult = ResolveTextureSRV(Slot);
            RADIENT_STATUS                               TextureStatus = ResolveResult.Status;
            if (TextureStatus == RADIENT_STATUS_OK && ResolveResult.pTextureSRV == nullptr)
            {
                UNEXPECTED("Texture SRV resolver returned OK without an SRV");
                TextureStatus = RADIENT_STATUS_INVALID_OPERATION;
            }

            EntryStatus = CombineDependencyStatus(EntryStatus, TextureStatus);
            if (TextureStatus == RADIENT_STATUS_OK)
                TextureSRVs.push_back(ResolveResult.pTextureSRV);
        }

        Status = CombineDependencyStatus(Status, EntryStatus);
        if (EntryStatus != RADIENT_STATUS_OK)
            continue;

        RefCntAutoPtr<IShaderResourceBinding> pSRB =
            CreateSRB(TextureSRVs.data(), static_cast<Uint32>(TextureSRVs.size()));
        if (pSRB == nullptr)
        {
            Status = CombineDependencyStatus(Status, RADIENT_STATUS_INVALID_OPERATION);
            continue;
        }

        IShaderResourceVariable* const pPrimitiveAttribsVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs");
        if (pPrimitiveAttribsVar == nullptr)
        {
            UNEXPECTED("Material SRB does not contain the PBR primitive attributes buffer variable");
            Status = CombineDependencyStatus(Status, RADIENT_STATUS_INVALID_OPERATION);
            continue;
        }

        pState->pSRB                   = std::move(pSRB);
        pState->pPrimitiveAttribsVar   = pPrimitiveAttribsVar;
        pState->PreparedTextureVersion = TextureVersion;
    }
    m_Impl->PrepareEntries.clear();

    return Status;
}

size_t RadientMaterialSRBTable::GetSize() const noexcept
{
    std::lock_guard<std::mutex> Lock{m_Impl->Mutex};
    return m_Impl->Lookup.size();
}

} // namespace Diligent
