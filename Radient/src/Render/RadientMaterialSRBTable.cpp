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

#include "Utilities/RadientAbseilFlatHashMap.hpp"
#include "Utilities/RadientAbseilInlinedVector.hpp"

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace Diligent
{

const RadientMaterialTextureSRBSlot* RadientMaterialDefaultTextureBindings::Get(
    PBR_Renderer::TEXTURE_ATTRIB_ID TextureAttribId) const noexcept
{
    if (TextureAttribId >= PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT)
    {
        UNEXPECTED("Invalid PBR texture attribute ID ", Uint32{TextureAttribId});
        return nullptr;
    }

    static_assert(PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT == 19, "Update the switch statement below to handle new PBR texture attributes");
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
    explicit RadientMaterialSRBState(std::shared_ptr<std::atomic<Uint64>> pRetirementRevision) noexcept :
        m_pRetirementRevision{std::move(pRetirementRevision)}
    {}

    ~RadientMaterialSRBState()
    {
        // Leases may outlive the table or be released on a worker. Notify through
        // shared bookkeeping without calling the table or taking its mutex.
        m_pRetirementRevision->fetch_add(1, std::memory_order_release);
    }

    absl::InlinedVector<RadientMaterialTextureSRBSlot, 8> Slots;

    // Accessed exclusively from the render thread.
    RADIENT_STATUS                        PreparationStatus = RADIENT_STATUS_PENDING;
    RefCntAutoPtr<IShaderResourceBinding> pSRB;
    IShaderResourceVariable*              pPrimitiveAttribsVar = nullptr;
    IShaderResourceVariable*              pMaterialAttribsVar  = nullptr;

    Uint32 PreparedTextureVersion           = ~0u;
    Uint32 PreparedMaterialBufferVersion    = ~0u;
    Uint64 PreparedMaterialBufferGeneration = 0;

private:
    const std::shared_ptr<std::atomic<Uint64>> m_pRetirementRevision;
};

RADIENT_STATUS RadientMaterialSRBLease::GetPreparationStatus() const noexcept
{
    return m_pState != nullptr ? m_pState->PreparationStatus : RADIENT_STATUS_INVALID_OPERATION;
}

IShaderResourceBinding* RadientMaterialSRBLease::GetSRB() const noexcept
{
    return m_pState != nullptr ? m_pState->pSRB.RawPtr() : nullptr;
}

IShaderResourceVariable* RadientMaterialSRBLease::GetPrimitiveAttribsVariable() const noexcept
{
    return m_pState != nullptr ? m_pState->pPrimitiveAttribsVar : nullptr;
}

IShaderResourceVariable* RadientMaterialSRBLease::GetMaterialAttribsVariable() const noexcept
{
    return m_pState != nullptr ? m_pState->pMaterialAttribsVar : nullptr;
}

Uint32 RadientMaterialSRBLease::GetMaterialBufferVersion() const noexcept
{
    return m_pState != nullptr ? m_pState->PreparedMaterialBufferVersion : ~0u;
}

Uint64 RadientMaterialSRBLease::GetMaterialBufferGeneration() const noexcept
{
    return m_pState != nullptr ? m_pState->PreparedMaterialBufferGeneration : 0;
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

    // New entries are published under Mutex. Cache hits do not add work.
    std::vector<std::weak_ptr<RadientMaterialSRBState>> NewEntries;
    std::atomic<Uint64>                                 AddedRevision{0};
    const std::shared_ptr<std::atomic<Uint64>>          pRetirementRevision = std::make_shared<std::atomic<Uint64>>(0);

    // Render-thread work lists stay weak so they do not extend resource lifetimes.
    std::vector<std::weak_ptr<RadientMaterialSRBState>> PrepareEntries;
    std::vector<std::weak_ptr<RadientMaterialSRBState>> RetryEntries;

    bool   Initialized                  = false;
    Uint64 LastAddedRevision            = 0;
    Uint64 LastRetirementRevision       = 0;
    Uint32 LastTextureVersion           = 0;
    Uint32 LastMaterialBufferVersion    = 0;
    Uint64 LastMaterialBufferGeneration = 0;
};

RadientMaterialSRBTable::RadientMaterialSRBTable() :
    m_Impl{std::make_unique<Impl>()}
{}

RadientMaterialSRBTable::~RadientMaterialSRBTable() = default;

RADIENT_STATUS RadientMaterialSRBTable::Acquire(
    const RadientMaterialTextureSRBSlotArray&      MaterialTextures,
    PBR_Renderer::PSO_FLAGS                        PSOFlags,
    Uint32                                         MaxTextureSlots,
    const RadientMaterialDefaultTextureBindings&   DefaultTextures,
    RadientMaterialSRBLease&                       Lease,
    PBR_Renderer::StaticShaderTextureIdsArrayType& ShaderTextureIds)
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

    std::array<const RadientMaterialTextureSRBSlot*, PBR_Renderer::TEXTURE_ATTRIB_ID_COUNT> SlotSources{};
    for (Uint32 Slot = 0; Slot < MaxTextureSlots; ++Slot)
    {
        const auto                           TextureAttribId = static_cast<PBR_Renderer::TEXTURE_ATTRIB_ID>(Slot);
        const RadientMaterialTextureSRBSlot* pDefaultTexture = DefaultTextures.Get(TextureAttribId);
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

            const RadientMaterialTextureSRBSlot& Texture = MaterialTextures[AttribId];
            if (!Texture)
            {
                LOG_ERROR_MESSAGE("Material texture used by PBR texture attribute ",
                                  Uint32{AttribId}, " is not initialized");
                Status = RADIENT_STATUS_INVALID_OPERATION;
                return;
            }

            size_t SlotIndex = AttribId;
            if (!CanUseDefaultMapping)
            {
                SlotIndex = 0;
                while (SlotIndex < NextTextureSlot &&
                       SlotSources[SlotIndex]->BindingIdentity != Texture.BindingIdentity)
                    ++SlotIndex;

                if (SlotIndex == NextTextureSlot)
                {
                    if (NextTextureSlot >= MaxTextureSlots)
                    {
                        LOG_ERROR_MESSAGE("Material requires more than ", MaxTextureSlots, " distinct texture bindings");
                        Status = RADIENT_STATUS_UNSUPPORTED;
                        return;
                    }
                    ++NextTextureSlot;
                }
            }

            SlotSources[SlotIndex]     = &Texture;
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
        return RADIENT_STATUS_FAILED;
    }

    return RADIENT_STATUS_OK;
}

RadientMaterialSRBLease RadientMaterialSRBTable::Acquire(
    const RadientMaterialTextureSRBSlot* const* ppSlots,
    Uint32                                      SlotCount)
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

    std::shared_ptr<RadientMaterialSRBState> pState = std::make_shared<RadientMaterialSRBState>(m_Impl->pRetirementRevision);
    pState->Slots.reserve(SlotCount);
    for (Uint32 Slot = 0; Slot < SlotCount; ++Slot)
        pState->Slots.push_back(*ppSlots[Slot]);

    m_Impl->Lookup.emplace(std::move(Identity), pState);
    m_Impl->NewEntries.emplace_back(pState);
    m_Impl->AddedRevision.fetch_add(1, std::memory_order_release);
    return RadientMaterialSRBLease{std::move(pState)};
}

RADIENT_STATUS RadientMaterialSRBTable::Prepare(
    Uint32                               TextureVersion,
    Uint32                               MaterialBufferVersion,
    Uint64                               MaterialBufferGeneration,
    const ResolveTextureSRVCallbackType& ResolveTextureSRV,
    const CreateSRBCallbackType&         CreateSRB)
{
    if (!ResolveTextureSRV || !CreateSRB)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    // Capture retirement before the snapshot. Releases during preparation must
    // remain visible to the next call rather than being acknowledged here.
    const Uint64 RetirementRevision = m_Impl->pRetirementRevision->load(std::memory_order_acquire);
    const bool   FullRefresh        = !m_Impl->Initialized ||
        m_Impl->LastTextureVersion != TextureVersion ||
        m_Impl->LastMaterialBufferVersion != MaterialBufferVersion ||
        m_Impl->LastMaterialBufferGeneration != MaterialBufferGeneration ||
        m_Impl->LastRetirementRevision != RetirementRevision;
    const bool HasNewEntries = m_Impl->AddedRevision.load(std::memory_order_acquire) != m_Impl->LastAddedRevision;
    if (!FullRefresh && !HasNewEntries && m_Impl->RetryEntries.empty())
        return RADIENT_STATUS_OK;

    // If preparation throws, the next call rebuilds its work list. Scratch lists
    // are weak and therefore cannot retain texture or SRB resources on that path.
    m_Impl->Initialized = false;
    m_Impl->PrepareEntries.clear();
    if (FullRefresh)
    {
        std::lock_guard<std::mutex> Lock{m_Impl->Mutex};
        m_Impl->PrepareEntries.reserve(m_Impl->Lookup.size());
        for (auto LookupIt = m_Impl->Lookup.begin(); LookupIt != m_Impl->Lookup.end();)
        {
            if (LookupIt->second.expired())
            {
                m_Impl->Lookup.erase(LookupIt++);
                continue;
            }

            m_Impl->PrepareEntries.push_back(LookupIt->second);
            ++LookupIt;
        }
        m_Impl->NewEntries.clear();
        m_Impl->LastAddedRevision = m_Impl->AddedRevision.load(std::memory_order_relaxed);
    }
    else
    {
        if (HasNewEntries)
        {
            std::lock_guard<std::mutex> Lock{m_Impl->Mutex};
            m_Impl->PrepareEntries.swap(m_Impl->NewEntries);
            // Acknowledge only this batch. Acquisitions after unlocking remain
            // queued and advance the revision for the next prepared view.
            m_Impl->LastAddedRevision = m_Impl->AddedRevision.load(std::memory_order_relaxed);
        }
        m_Impl->PrepareEntries.insert(m_Impl->PrepareEntries.end(),
                                      m_Impl->RetryEntries.begin(), m_Impl->RetryEntries.end());
    }
    m_Impl->RetryEntries.clear();

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const std::weak_ptr<RadientMaterialSRBState>& WeakState : m_Impl->PrepareEntries)
    {
        const std::shared_ptr<RadientMaterialSRBState> pState = WeakState.lock();
        if (pState == nullptr)
            continue;

        // The texture recipe is immutable. A failed dependency or unsupported
        // texture binding cannot be repaired by rebuilding its SRB.
        if (RADIENT_FAILED(pState->PreparationStatus))
            continue;

        if (pState->pSRB != nullptr &&
            pState->PreparedTextureVersion == TextureVersion &&
            pState->PreparedMaterialBufferVersion == MaterialBufferVersion)
        {
            // Uploading more records does not replace either resource bound by
            // the SRB. Advance its material coverage without recreating it.
            pState->PreparedMaterialBufferGeneration = MaterialBufferGeneration;
            continue;
        }

        absl::InlinedVector<ITextureView*, 8> TextureSRVs;
        TextureSRVs.reserve(pState->Slots.size());
        RADIENT_STATUS EntryStatus = RADIENT_STATUS_OK;
        for (const RadientMaterialTextureSRBSlot& Slot : pState->Slots)
        {
            const RadientMaterialTextureSRVResolveResult ResolveResult = ResolveTextureSRV(Slot);
            RADIENT_STATUS                               TextureStatus = ResolveResult.Status;
            if (TextureStatus == RADIENT_STATUS_OK && ResolveResult.pTextureSRV == nullptr)
            {
                UNEXPECTED("Texture SRV resolver returned OK without an SRV");
                TextureStatus = RADIENT_STATUS_FAILED;
            }

            EntryStatus = CombineDependencyStatus(EntryStatus, TextureStatus);
            if (TextureStatus == RADIENT_STATUS_OK)
                TextureSRVs.push_back(ResolveResult.pTextureSRV);
        }

        pState->PreparationStatus = EntryStatus;
        if (EntryStatus != RADIENT_STATUS_OK)
        {
            // Invalid material content only disables this entry. Pending
            // dependencies still contribute to the frame readiness status.
            if (!RADIENT_FAILED(EntryStatus))
            {
                Status = CombineDependencyStatus(Status, EntryStatus);
                m_Impl->RetryEntries.emplace_back(pState);
            }
            continue;
        }

        // SRB creation and shared signature errors remain table-wide failures
        // and may be retried. Only texture-resolution failures are terminal.
        pState->PreparationStatus = RADIENT_STATUS_PENDING;

        RefCntAutoPtr<IShaderResourceBinding> pSRB =
            CreateSRB(TextureSRVs.data(), static_cast<Uint32>(TextureSRVs.size()));
        if (pSRB == nullptr)
        {
            Status = CombineDependencyStatus(Status, RADIENT_STATUS_FAILED);
            m_Impl->RetryEntries.emplace_back(pState);
            continue;
        }

        IShaderResourceVariable* const pPrimitiveAttribsVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbPrimitiveAttribs");
        if (pPrimitiveAttribsVar == nullptr)
        {
            UNEXPECTED("Material SRB does not contain the PBR primitive attributes buffer variable");
            Status = CombineDependencyStatus(Status, RADIENT_STATUS_INVALID_OPERATION);
            m_Impl->RetryEntries.emplace_back(pState);
            continue;
        }

        IShaderResourceVariable* const pMaterialAttribsVar = pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "cbMaterialAttribs");
        if (pMaterialAttribsVar == nullptr)
        {
            UNEXPECTED("Material SRB does not contain the PBR material attributes buffer variable");
            Status = CombineDependencyStatus(Status, RADIENT_STATUS_INVALID_OPERATION);
            m_Impl->RetryEntries.emplace_back(pState);
            continue;
        }

        pState->PreparationStatus                = RADIENT_STATUS_OK;
        pState->pSRB                             = std::move(pSRB);
        pState->pPrimitiveAttribsVar             = pPrimitiveAttribsVar;
        pState->pMaterialAttribsVar              = pMaterialAttribsVar;
        pState->PreparedTextureVersion           = TextureVersion;
        pState->PreparedMaterialBufferVersion    = MaterialBufferVersion;
        pState->PreparedMaterialBufferGeneration = MaterialBufferGeneration;
    }
    m_Impl->PrepareEntries.clear();
    m_Impl->LastRetirementRevision       = RetirementRevision;
    m_Impl->LastTextureVersion           = TextureVersion;
    m_Impl->LastMaterialBufferVersion    = MaterialBufferVersion;
    m_Impl->LastMaterialBufferGeneration = MaterialBufferGeneration;
    m_Impl->Initialized                  = true;

    return Status;
}

size_t RadientMaterialSRBTable::GetSize() const noexcept
{
    std::lock_guard<std::mutex> Lock{m_Impl->Mutex};
    return m_Impl->Lookup.size();
}

} // namespace Diligent
