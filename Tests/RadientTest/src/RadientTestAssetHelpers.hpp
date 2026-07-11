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

#include "RadientAssets.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

namespace Testing
{

template <typename InterfaceType, const INTERFACE_ID& InterfaceID, RADIENT_ASSET_TYPE AssetType>
class TestRadientAsset final : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    TestRadientAsset(IReferenceCounters* pRefCounters, const char* URI, Uint64 Version) :
        TBase{pRefCounters},
        m_URI{URI != nullptr ? URI : ""}
    {
        m_Ref.URI     = m_URI.empty() ? nullptr : m_URI.c_str();
        m_Ref.Version = Version;
    }

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Ref;
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return AssetType;
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == InterfaceID || IID == IID_RadientAsset)
        {
            *ppInterface = this;
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

private:
    std::string           m_URI;
    RadientAssetReference m_Ref{};
};

using TestMeshAsset     = TestRadientAsset<IRadientMeshAsset, IID_RadientMeshAsset, RADIENT_ASSET_TYPE_MESH>;
using TestMaterialAsset = TestRadientAsset<IRadientMaterialAsset, IID_RadientMaterialAsset, RADIENT_ASSET_TYPE_MATERIAL>;
using TestTextureAsset  = TestRadientAsset<IRadientTextureAsset, IID_RadientTextureAsset, RADIENT_ASSET_TYPE_TEXTURE>;
using TestSceneAsset    = TestRadientAsset<IRadientSceneAsset, IID_RadientSceneAsset, RADIENT_ASSET_TYPE_SCENE>;

struct TestRadientAssetResolverStats
{
    Uint32      CheckCount            = 0;
    Uint32      ResolveCount          = 0;
    Uint32      AssetDataDestroyCount = 0;
    std::string LastURI;
    std::string LastBaseURI;
};

class TestRadientAssetData final : public ObjectBase<IRadientAssetData>
{
public:
    using TBase = ObjectBase<IRadientAssetData>;

    TestRadientAssetData(IReferenceCounters*                            pRefCounters,
                         std::vector<Uint8>                             Data,
                         std::string                                    ResolvedURI,
                         std::shared_ptr<TestRadientAssetResolverStats> pStats) :
        TBase{pRefCounters},
        m_Data{std::move(Data)},
        m_ResolvedURI{std::move(ResolvedURI)},
        m_pStats{std::move(pStats)}
    {
    }

    ~TestRadientAssetData()
    {
        ++m_pStats->AssetDataDestroyCount;
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetData, TBase)

    virtual const void* DILIGENT_CALL_TYPE GetData() const override final
    {
        return !m_Data.empty() ? m_Data.data() : nullptr;
    }

    virtual size_t DILIGENT_CALL_TYPE GetSize() const override final
    {
        return m_Data.size();
    }

    virtual const Char* DILIGENT_CALL_TYPE GetResolvedURI() const override final
    {
        return m_ResolvedURI.c_str();
    }

private:
    std::vector<Uint8>                             m_Data;
    std::string                                    m_ResolvedURI;
    std::shared_ptr<TestRadientAssetResolverStats> m_pStats;
};

/// In-memory asset resolver used to verify URI resolution and resolved-data lifetime.
class TestRadientAssetResolver final : public ObjectBase<IRadientAssetResolver>
{
public:
    using TBase = ObjectBase<IRadientAssetResolver>;

    explicit TestRadientAssetResolver(IReferenceCounters* pRefCounters) :
        TBase{pRefCounters},
        m_pStats{std::make_shared<TestRadientAssetResolverStats>()}
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetResolver, TBase)

    void AddAsset(std::string URI, std::string ResolvedURI, std::vector<Uint8> Data)
    {
        m_Assets.emplace(std::move(URI), Entry{std::move(ResolvedURI), std::move(Data)});
    }

    const TestRadientAssetResolverStats& GetStats() const
    {
        return *m_pStats;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CheckAsset(const RadientAssetResolveInfo& ResolveInfo) override final
    {
        ++m_pStats->CheckCount;
        m_pStats->LastURI     = ResolveInfo.URI != nullptr ? ResolveInfo.URI : "";
        m_pStats->LastBaseURI = ResolveInfo.BaseURI != nullptr ? ResolveInfo.BaseURI : "";

        if (m_pStats->LastURI.empty())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        return FindAsset(m_pStats->LastURI) != nullptr ?
            RADIENT_STATUS_OK :
            RADIENT_STATUS_NOT_FOUND;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ResolveAsset(const RadientAssetResolveInfo& ResolveInfo,
                                                           IRadientAssetData**            ppData) override final
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppData = nullptr;

        ++m_pStats->ResolveCount;
        m_pStats->LastURI     = ResolveInfo.URI != nullptr ? ResolveInfo.URI : "";
        m_pStats->LastBaseURI = ResolveInfo.BaseURI != nullptr ? ResolveInfo.BaseURI : "";

        if (m_pStats->LastURI.empty())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const Entry* pEntry = FindAsset(m_pStats->LastURI);
        if (pEntry == nullptr)
            return RADIENT_STATUS_NOT_FOUND;

        RefCntAutoPtr<TestRadientAssetData> pData{
            MakeNewRCObj<TestRadientAssetData>()(pEntry->Data, pEntry->ResolvedURI, m_pStats)};
        pData->QueryInterface(IID_RadientAssetData, ppData);
        return *ppData != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;
    }

private:
    struct Entry
    {
        std::string        ResolvedURI;
        std::vector<Uint8> Data;
    };

    const Entry* FindAsset(const std::string& URI) const
    {
        auto It = m_Assets.find(URI);
        if (It == m_Assets.end())
        {
            const size_t SlashPos = URI.find_last_of("/\\");
            if (SlashPos != std::string::npos)
                It = m_Assets.find(URI.substr(SlashPos + 1));
        }
        return It != m_Assets.end() ? &It->second : nullptr;
    }

    std::map<std::string, Entry>                   m_Assets;
    std::shared_ptr<TestRadientAssetResolverStats> m_pStats;
};

inline constexpr size_t TransparentPngSize = 67;

inline constexpr std::array<Uint8, TransparentPngSize> TransparentPng{
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
    0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
    0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
    0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
    0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
    0x42, 0x60, 0x82};

inline RefCntAutoPtr<IRadientMeshAsset> MakeTestMeshAsset(const char* URI = "mesh://test", Uint64 Version = 1)
{
    RefCntAutoPtr<TestMeshAsset> pAsset{MakeNewRCObj<TestMeshAsset>()(URI, Version)};
    return RefCntAutoPtr<IRadientMeshAsset>{pAsset};
}

inline RefCntAutoPtr<IRadientMaterialAsset> MakeTestMaterialAsset(const char* URI = "material://test", Uint64 Version = 1)
{
    RefCntAutoPtr<TestMaterialAsset> pAsset{MakeNewRCObj<TestMaterialAsset>()(URI, Version)};
    return RefCntAutoPtr<IRadientMaterialAsset>{pAsset};
}

inline RefCntAutoPtr<IRadientTextureAsset> MakeTestTextureAsset(const char* URI = "texture://test", Uint64 Version = 1)
{
    RefCntAutoPtr<TestTextureAsset> pAsset{MakeNewRCObj<TestTextureAsset>()(URI, Version)};
    return RefCntAutoPtr<IRadientTextureAsset>{pAsset};
}

inline RefCntAutoPtr<IRadientSceneAsset> MakeTestSceneAsset(const char* URI = "scene://test", Uint64 Version = 1)
{
    RefCntAutoPtr<TestSceneAsset> pAsset{MakeNewRCObj<TestSceneAsset>()(URI, Version)};
    return RefCntAutoPtr<IRadientSceneAsset>{pAsset};
}

} // namespace Testing

} // namespace Diligent
