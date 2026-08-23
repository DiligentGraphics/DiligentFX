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
#include "RadientMaterials.h"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "ShaderResourceBinding.h"

#include <array>
#include <cstring>
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

using TestMeshAsset    = TestRadientAsset<IRadientMeshAsset, IID_RadientMeshAsset, RADIENT_ASSET_TYPE_MESH>;
using TestTextureAsset = TestRadientAsset<IRadientTextureAsset, IID_RadientTextureAsset, RADIENT_ASSET_TYPE_TEXTURE>;
using TestSceneAsset   = TestRadientAsset<IRadientSceneAsset, IID_RadientSceneAsset, RADIENT_ASSET_TYPE_SCENE>;

class TestMaterialAsset final : public ObjectBase<IRadientMaterialAsset>
{
public:
    using TBase = ObjectBase<IRadientMaterialAsset>;

    TestMaterialAsset(IReferenceCounters* pRefCounters, const char* URI, Uint64 Version) :
        TBase{pRefCounters},
        m_URI{URI != nullptr ? URI : ""},
        m_Version{Version}
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
        return RADIENT_ASSET_TYPE_MATERIAL;
    }

    virtual IRadientMaterialDefinitionAsset* DILIGENT_CALL_TYPE GetDefinition() const override final
    {
        return nullptr;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_Version;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameter(RadientMaterialParameterHandle,
                                                           void*,
                                                           Uint32) const override final
    {
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetTexture(RadientMaterialParameterHandle,
                                                         Uint32,
                                                         IRadientTextureAsset** ppTexture) const override final
    {
        if (ppTexture == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppTexture = nullptr;
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialWriter** ppWriter) const override final
    {
        if (ppWriter == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppWriter = nullptr;
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientMaterialAsset || IID == IID_RadientAsset)
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
    Uint64                m_Version = 0;
};

class TestShaderResourceVariable final : public ObjectBase<IShaderResourceVariable>
{
public:
    using TBase = ObjectBase<IShaderResourceVariable>;

    TestShaderResourceVariable(IReferenceCounters* pRefCounters, const Char* Name) :
        TBase{pRefCounters},
        m_Name{Name != nullptr ? Name : ""}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_ShaderResourceVariable, TBase)

    virtual void DILIGENT_CALL_TYPE Set(IDeviceObject* pObject,
                                        SET_SHADER_RESOURCE_FLAGS) override final
    {
        m_pObject = pObject;
    }

    virtual void DILIGENT_CALL_TYPE SetArray(IDeviceObject* const* ppObjects,
                                             Uint32                FirstElement,
                                             Uint32                NumElements,
                                             SET_SHADER_RESOURCE_FLAGS) override final
    {
        if (FirstElement == 0 && NumElements != 0)
            m_pObject = ppObjects[0];
    }

    virtual void DILIGENT_CALL_TYPE SetBufferRange(IDeviceObject* pObject,
                                                   Uint64,
                                                   Uint64,
                                                   Uint32,
                                                   SET_SHADER_RESOURCE_FLAGS) override final
    {
        m_pObject = pObject;
    }

    virtual void DILIGENT_CALL_TYPE SetBufferOffset(Uint32 Offset, Uint32) override final
    {
        m_BufferOffset = Offset;
    }

    virtual void DILIGENT_CALL_TYPE SetInlineConstants(const void*, Uint32, Uint32) override final
    {}

    virtual SHADER_RESOURCE_VARIABLE_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    }

    virtual void DILIGENT_CALL_TYPE GetResourceDesc(ShaderResourceDesc& ResourceDesc) const override final
    {
        ResourceDesc = {m_Name.c_str(), SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, 1};
    }

    virtual Uint32 DILIGENT_CALL_TYPE GetIndex() const override final
    {
        return 0;
    }

    virtual IDeviceObject* DILIGENT_CALL_TYPE Get(Uint32) const override final
    {
        return m_pObject;
    }

private:
    std::string                  m_Name;
    RefCntAutoPtr<IDeviceObject> m_pObject;
    Uint32                       m_BufferOffset = 0;
};

class TestShaderResourceBinding final : public ObjectBase<IShaderResourceBinding>
{
public:
    using TBase = ObjectBase<IShaderResourceBinding>;

    explicit TestShaderResourceBinding(IReferenceCounters* pRefCounters) :
        TBase{pRefCounters},
        m_pPrimitiveAttribsVar{MakeNewRCObj<TestShaderResourceVariable>()("cbPrimitiveAttribs")},
        m_pMaterialAttribsVar{MakeNewRCObj<TestShaderResourceVariable>()("cbMaterialAttribs")}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_ShaderResourceBinding, TBase)

    virtual IPipelineResourceSignature* DILIGENT_CALL_TYPE GetPipelineResourceSignature() const override final
    {
        return nullptr;
    }

    virtual void DILIGENT_CALL_TYPE BindResources(SHADER_TYPE,
                                                  IResourceMapping*,
                                                  BIND_SHADER_RESOURCES_FLAGS) override final
    {}

    virtual SHADER_RESOURCE_VARIABLE_TYPE_FLAGS DILIGENT_CALL_TYPE CheckResources(
        SHADER_TYPE,
        IResourceMapping*,
        BIND_SHADER_RESOURCES_FLAGS) const override final
    {
        return SHADER_RESOURCE_VARIABLE_TYPE_FLAG_NONE;
    }

    virtual IShaderResourceVariable* DILIGENT_CALL_TYPE GetVariableByName(SHADER_TYPE ShaderType, const Char* Name) override final
    {
        if (ShaderType != SHADER_TYPE_PIXEL || Name == nullptr)
            return nullptr;

        if (std::strcmp(Name, "cbPrimitiveAttribs") == 0)
            return m_pPrimitiveAttribsVar;
        if (std::strcmp(Name, "cbMaterialAttribs") == 0)
            return m_pMaterialAttribsVar;
        return nullptr;
    }

    virtual Uint32 DILIGENT_CALL_TYPE GetVariableCount(SHADER_TYPE ShaderType) const override final
    {
        return ShaderType == SHADER_TYPE_PIXEL ? 2 : 0;
    }

    virtual IShaderResourceVariable* DILIGENT_CALL_TYPE GetVariableByIndex(SHADER_TYPE ShaderType, Uint32 Index) override final
    {
        if (ShaderType != SHADER_TYPE_PIXEL)
            return nullptr;
        if (Index == 0)
            return m_pPrimitiveAttribsVar;
        if (Index == 1)
            return m_pMaterialAttribsVar;
        return nullptr;
    }

    virtual Bool DILIGENT_CALL_TYPE StaticResourcesInitialized() const override final
    {
        return True;
    }

private:
    RefCntAutoPtr<IShaderResourceVariable> m_pPrimitiveAttribsVar;
    RefCntAutoPtr<IShaderResourceVariable> m_pMaterialAttribsVar;
};

inline RefCntAutoPtr<IShaderResourceBinding> MakeTestShaderResourceBinding()
{
    return RefCntAutoPtr<IShaderResourceBinding>{MakeNewRCObj<TestShaderResourceBinding>()()};
}

struct TestRadientAssetResolverStats
{
    Uint32      CheckCount            = 0;
    Uint32      ResolveLocationCount  = 0;
    Uint32      OpenCount             = 0;
    Uint32      AssetDataDestroyCount = 0;
    std::string LastURI;
    std::string LastBaseURI;
    std::string LastResolvedURI;
};

class TestRadientAssetLocation final : public ObjectBase<IRadientAssetLocation>
{
public:
    using TBase = ObjectBase<IRadientAssetLocation>;

    TestRadientAssetLocation(IReferenceCounters* pRefCounters, std::string Location) :
        TBase{pRefCounters},
        m_Location{std::move(Location)}
    {
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAssetLocation, TBase)

    virtual const Char* DILIGENT_CALL_TYPE GetLocation() const override final
    {
        return m_Location.c_str();
    }

private:
    std::string m_Location;
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

    void SetOpenAssetStatus(RADIENT_STATUS Status)
    {
        m_OpenAssetStatus = Status;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CheckAsset(IRadientAssetLocation* pLocation) override final
    {
        ++m_pStats->CheckCount;
        if (pLocation == nullptr || pLocation->GetLocation() == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        m_pStats->LastResolvedURI = pLocation->GetLocation();
        return FindResolvedAsset(m_pStats->LastResolvedURI) != nullptr ?
            RADIENT_STATUS_OK :
            RADIENT_STATUS_NOT_FOUND;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ResolveAssetLocation(const RadientAssetResolveInfo& ResolveInfo,
                                                                   IRadientAssetLocation**        ppLocation) override final
    {
        if (ppLocation == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppLocation = nullptr;

        ++m_pStats->ResolveLocationCount;
        m_pStats->LastURI     = ResolveInfo.URI != nullptr ? ResolveInfo.URI : "";
        m_pStats->LastBaseURI = ResolveInfo.BaseURI != nullptr ? ResolveInfo.BaseURI : "";

        if (m_pStats->LastURI.empty())
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const Entry* pEntry = FindAsset(m_pStats->LastURI);
        if (pEntry == nullptr)
            return RADIENT_STATUS_NOT_FOUND;

        RefCntAutoPtr<TestRadientAssetLocation> pLocation{
            MakeNewRCObj<TestRadientAssetLocation>()(pEntry->ResolvedURI)};
        pLocation->QueryInterface(IID_RadientAssetLocation, ppLocation);
        return *ppLocation != nullptr ? RADIENT_STATUS_OK : RADIENT_STATUS_INVALID_OPERATION;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE OpenAsset(IRadientAssetLocation* pLocation,
                                                        IRadientAssetData**    ppData) override final
    {
        if (ppData == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppData = nullptr;

        if (pLocation == nullptr || pLocation->GetLocation() == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        ++m_pStats->OpenCount;
        m_pStats->LastResolvedURI = pLocation->GetLocation();

        if (m_OpenAssetStatus != RADIENT_STATUS_OK)
            return m_OpenAssetStatus;

        const Entry* pEntry = FindResolvedAsset(m_pStats->LastResolvedURI);
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

    const Entry* FindResolvedAsset(const std::string& ResolvedURI) const
    {
        for (const auto& Asset : m_Assets)
        {
            if (Asset.second.ResolvedURI == ResolvedURI)
                return &Asset.second;
        }
        return nullptr;
    }

    std::map<std::string, Entry>                   m_Assets;
    std::shared_ptr<TestRadientAssetResolverStats> m_pStats;
    RADIENT_STATUS                                 m_OpenAssetStatus = RADIENT_STATUS_OK;
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
