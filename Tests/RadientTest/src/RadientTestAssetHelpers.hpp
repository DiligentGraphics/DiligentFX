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

#include <string>

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
