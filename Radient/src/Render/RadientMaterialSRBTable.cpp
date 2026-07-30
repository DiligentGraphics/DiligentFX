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

#include "HashUtils.hpp"

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4127) // conditional expression is constant
#    pragma warning(disable : 4702) // unreachable code
#endif
#include "absl/container/flat_hash_map.h"
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

#include <mutex>
#include <utility>
#include <vector>

namespace Diligent
{

class RadientMaterialSRBState
{
public:
    absl::InlinedVector<RadientMaterialTextureRenderData, 8> Slots;

    // Accessed exclusively from the render thread.
    RefCntAutoPtr<IShaderResourceBinding> pSRB;
    Uint32                                PreparedTextureVersion = ~0u;
};

IShaderResourceBinding* RadientMaterialSRBLease::GetSRB() const noexcept
{
    return m_pState != nullptr ? m_pState->pSRB.RawPtr() : nullptr;
}

bool RadientMaterialSRBIdentity::operator==(const RadientMaterialSRBIdentity& Rhs) const noexcept
{
    return Slots == Rhs.Slots;
}

size_t RadientMaterialSRBIdentity::Hasher::operator()(const RadientMaterialSRBIdentity& Identity) const noexcept
{
    size_t                                      Hash = ComputeHash(Identity.Slots.size());
    const RadientTextureBindingIdentity::Hasher IdentityHasher;
    for (const RadientTextureBindingIdentity& Slot : Identity.Slots)
        HashCombine(Hash, IdentityHasher(Slot));
    return Hash;
}

bool BuildRadientMaterialSRBIdentity(const RadientMaterialTextureBindingPlan& Plan,
                                     RadientMaterialSRBIdentity&              Identity) noexcept
{
    RadientMaterialSRBIdentity NewIdentity;
    NewIdentity.Slots.reserve(Plan.Slots.size());
    for (const RadientMaterialTextureRenderData& Slot : Plan.Slots)
    {
        if (!Slot)
        {
            Identity = {};
            return false;
        }
        NewIdentity.Slots.push_back(Slot.BindingIdentity);
    }

    Identity = std::move(NewIdentity);
    return !Identity.Slots.empty();
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

    // Render-thread snapshot reused by Prepare() to avoid allocating every frame.
    std::vector<std::shared_ptr<RadientMaterialSRBState>> PrepareEntries;
};

RadientMaterialSRBTable::RadientMaterialSRBTable() :
    m_Impl{std::make_unique<Impl>()}
{}

RadientMaterialSRBTable::~RadientMaterialSRBTable() = default;

RadientMaterialSRBLease RadientMaterialSRBTable::Acquire(const RadientMaterialTextureBindingPlan& Plan)
{
    RadientMaterialSRBIdentity Identity;
    if (!BuildRadientMaterialSRBIdentity(Plan, Identity))
        return {};

    std::lock_guard<std::mutex> Lock{m_Impl->Mutex};

    const auto LookupIt = m_Impl->Lookup.find(Identity);
    if (LookupIt != m_Impl->Lookup.end())
    {
        if (std::shared_ptr<RadientMaterialSRBState> pState = LookupIt->second.lock())
            return RadientMaterialSRBLease{std::move(pState)};

        m_Impl->Lookup.erase(LookupIt);
    }

    auto pState   = std::make_shared<RadientMaterialSRBState>();
    pState->Slots = Plan.Slots;

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

    {
        std::lock_guard<std::mutex> Lock{m_Impl->Mutex};
        m_Impl->PrepareEntries.clear();
        m_Impl->PrepareEntries.reserve(m_Impl->Lookup.size());

        for (auto LookupIt = m_Impl->Lookup.begin(); LookupIt != m_Impl->Lookup.end();)
        {
            if (std::shared_ptr<RadientMaterialSRBState> pState = LookupIt->second.lock())
            {
                m_Impl->PrepareEntries.push_back(std::move(pState));
                ++LookupIt;
            }
            else
            {
                m_Impl->Lookup.erase(LookupIt++);
            }
        }
    }

    RADIENT_STATUS Status = RADIENT_STATUS_OK;
    for (const std::shared_ptr<RadientMaterialSRBState>& pState : m_Impl->PrepareEntries)
    {
        if (pState->pSRB != nullptr && pState->PreparedTextureVersion == TextureVersion)
            continue;

        absl::InlinedVector<ITextureView*, 8> TextureSRVs;
        TextureSRVs.reserve(pState->Slots.size());
        for (const RadientMaterialTextureRenderData& Slot : pState->Slots)
        {
            ITextureView* const pTextureSRV = ResolveTextureSRV(Slot);
            if (pTextureSRV == nullptr)
            {
                Status = RADIENT_STATUS_INVALID_OPERATION;
                break;
            }
            TextureSRVs.push_back(pTextureSRV);
        }
        if (TextureSRVs.size() != pState->Slots.size())
            continue;

        RefCntAutoPtr<IShaderResourceBinding> pSRB =
            CreateSRB(TextureSRVs.data(), static_cast<Uint32>(TextureSRVs.size()));
        if (pSRB == nullptr)
        {
            Status = RADIENT_STATUS_INVALID_OPERATION;
            continue;
        }

        pState->pSRB                   = std::move(pSRB);
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
