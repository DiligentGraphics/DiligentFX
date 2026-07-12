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

#include "Assets/RadientMaterialAssetManager.hpp"

#include "Assets/RadientAssetImpl.hpp"
#include "Assets/RadientAssetStatus.hpp"
#include "Assets/RadientAssetURI.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "DebugUtilities.hpp"
#include "GLTFBuilder.hpp"
#include "Math/RadientMath.hpp"

#include <atomic>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_MaterialAssetImpl = {0x1a11a468, 0xbf30, 0x4c4d, {0xb8, 0xcd, 0x48, 0x89, 0xa4, 0x65, 0xa, 0x50}};

struct MaterialStorage
{
    void SetTexture(Uint32 TextureAttribId, IRadientTextureAsset* pTexture)
    {
        if (pTexture == nullptr)
            return;

        if (Textures.size() <= TextureAttribId)
            Textures.resize(TextureAttribId + 1);

        Textures[TextureAttribId] = pTexture;
    }

    void InitLoadStatus() noexcept
    {
        const RADIENT_STATUS InitStatus = !Textures.empty() ? RADIENT_STATUS_PENDING : RADIENT_STATUS_OK;
        LoadStatus.store(InitStatus, std::memory_order_release);
        GPUResourceStatus.store(InitStatus, std::memory_order_release);
    }

    RADIENT_STATUS GetLoadStatus() const noexcept
    {
        RADIENT_STATUS Status = LoadStatus.load(std::memory_order_acquire);
        if (Status != RADIENT_STATUS_PENDING)
            return Status;

        Status = GetTextureDependenciesStatus();
        if (Status != RADIENT_STATUS_PENDING)
            LoadStatus.store(Status, std::memory_order_release);

        return Status;
    }

    RADIENT_STATUS GetTextureDependenciesStatus() const noexcept
    {
        RADIENT_STATUS Status = RADIENT_STATUS_OK;

        for (IRadientTextureAsset* pTexture : Textures)
        {
            if (pTexture == nullptr)
                continue;

            const RADIENT_STATUS TextureStatus = RadientTextureAssetManager::GetLoadStatus(pTexture);
            Status                             = CombineDependencyStatus(Status, TextureStatus);
        }

        return Status;
    }

    RADIENT_STATUS GetGPUResourceStatus() const noexcept
    {
        const RADIENT_STATUS Status = GetLoadStatus();
        if (Status != RADIENT_STATUS_OK)
            return Status;

        RADIENT_STATUS GPUStatus = GPUResourceStatus.load(std::memory_order_acquire);
        if (GPUStatus != RADIENT_STATUS_PENDING)
            return GPUStatus;

        GPUStatus = GetTextureDependenciesGPUResourceStatus();
        if (GPUStatus != RADIENT_STATUS_PENDING)
            GPUResourceStatus.store(GPUStatus, std::memory_order_release);

        return GPUStatus;
    }

    RADIENT_STATUS GetTextureDependenciesGPUResourceStatus() const noexcept
    {
        RADIENT_STATUS Status = RADIENT_STATUS_OK;

        for (IRadientTextureAsset* pTexture : Textures)
        {
            if (pTexture == nullptr)
                continue;

            const RADIENT_STATUS TextureStatus = RadientTextureAssetManager::GetGPUResourceStatus(pTexture);
            Status                             = CombineDependencyStatus(Status, TextureStatus);
        }

        return Status;
    }

    GLTF::Material                      Material;
    bool                                TextureAttribsReady = false;
    mutable std::atomic<RADIENT_STATUS> LoadStatus{RADIENT_STATUS_OK};
    mutable std::atomic<RADIENT_STATUS> GPUResourceStatus{RADIENT_STATUS_OK};

    std::vector<RefCntAutoPtr<IRadientTextureAsset>> Textures;
};

class MaterialPayloadImpl final : public RadientAssetPayloadImpl<MaterialStorage, MaterialPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MaterialStorage, MaterialPayloadImpl>;
    using TBase::TBase;
};

using MaterialAssetImpl =
    RadientAssetImpl<IRadientMaterialAsset, IID_RadientMaterialAsset, IID_MaterialAssetImpl, RADIENT_ASSET_TYPE_MATERIAL, MaterialPayloadImpl>;

RADIENT_STATUS CreateMaterialAsset(RefCntAutoPtr<MaterialPayloadImpl> pPayload,
                                   IRadientMaterialAsset**            ppMaterial)
{
    VERIFY_EXPR(pPayload != nullptr);
    VERIFY_EXPR(ppMaterial != nullptr);
    VERIFY_EXPR(*ppMaterial == nullptr);

    pPayload->GetStorage().InitLoadStatus();

    RefCntAutoPtr<MaterialAssetImpl> pMaterial =
        MaterialAssetImpl::Create(MakeRadientAssetURI("material"), std::move(pPayload));
    *ppMaterial = pMaterial.Detach();
    return RADIENT_STATUS_OK;
}

bool UpdateTextureAtlasAttribs(MaterialStorage& MaterialData)
{
    if (MaterialData.TextureAttribsReady)
        return true;

    if (MaterialData.GetLoadStatus() != RADIENT_STATUS_OK)
        return false;

    GLTF::MaterialBuilder Builder{MaterialData.Material};

    for (size_t TextureAttribId = 0; TextureAttribId < MaterialData.Textures.size(); ++TextureAttribId)
    {
        IRadientTextureAsset* pTexture = MaterialData.Textures[TextureAttribId];
        if (pTexture == nullptr)
            continue;

        GLTF::Material::TextureShaderAttribs& TextureAttribs = Builder.GetTextureAttrib(static_cast<Uint32>(TextureAttribId));
        if (!RadientTextureAssetManager::ApplyTextureAtlasAttribs(pTexture, TextureAttribs))
            return false;
    }

    Builder.Finalize();
    MaterialData.TextureAttribsReady = true;
    return true;
}

} // namespace

RadientMaterialAssetManager::~RadientMaterialAssetManager() = default;

RadientMaterialAssetManagerSharedPtr RadientMaterialAssetManager::Create()
{
    return RadientMaterialAssetManagerSharedPtr{new RadientMaterialAssetManager()};
}

RADIENT_STATUS RadientMaterialAssetManager::CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                           IRadientMaterialAsset**          ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMaterial == nullptr, "Output material pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMaterial = nullptr;

    RefCntAutoPtr<MaterialPayloadImpl> pPayload = MaterialPayloadImpl::Create();

    MaterialStorage&      MaterialData = pPayload->GetStorage();
    GLTF::MaterialBuilder Builder{MaterialData.Material};

    GLTF::Material::ShaderAttribs& Attribs = Builder.GetShaderAttribs();
    Attribs.BaseColorFactor                = RadientMath::ToFloat4(MaterialCI.BaseColorFactor);
    Attribs.MetallicFactor                 = MaterialCI.MetallicFactor;
    Attribs.RoughnessFactor                = MaterialCI.RoughnessFactor;
    Attribs.EmissiveFactor                 = RadientMath::ToFloat3(MaterialCI.EmissiveFactor);
    Attribs.AlphaCutoff                    = MaterialCI.AlphaCutoff;

    MaterialData.Material.DoubleSided = MaterialCI.DoubleSided != False;

    int  TextureId = 0;
    auto AddMaterialTexture =
        [&](Uint32 TextureAttribId, IRadientTextureAsset* pTexture) //
    {
        if (pTexture == nullptr)
            return;

        MaterialData.SetTexture(TextureAttribId, pTexture);
        Builder.SetTextureId(TextureAttribId, TextureId++);
        GLTF::Material::TextureShaderAttribs& TextureAttribs = Builder.GetTextureAttrib(TextureAttribId);
        TextureAttribs.SetUVSelector(0);
    };

    AddMaterialTexture(GLTF::DefaultBaseColorTextureAttribId, MaterialCI.pBaseColorTexture);
    AddMaterialTexture(GLTF::DefaultMetallicRoughnessTextureAttribId, MaterialCI.pMetallicRoughnessTexture);
    AddMaterialTexture(GLTF::DefaultNormalTextureAttribId, MaterialCI.pNormalTexture);
    AddMaterialTexture(GLTF::DefaultOcclusionTextureAttribId, MaterialCI.pOcclusionTexture);
    AddMaterialTexture(GLTF::DefaultEmissiveTextureAttribId, MaterialCI.pEmissiveTexture);

    Builder.Finalize();
    return CreateMaterialAsset(std::move(pPayload), ppMaterial);
}

RADIENT_STATUS RadientMaterialAssetManager::CreateGLTFMaterial(
    GLTF::Material               Material,
    IRadientTextureAsset* const* ppTextures,
    Uint32                       TextureCount,
    IRadientMaterialAsset**      ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    if (ppTextures == nullptr && TextureCount != 0)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMaterial == nullptr, "Output material pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMaterial = nullptr;

    RefCntAutoPtr<MaterialPayloadImpl> pPayload = MaterialPayloadImpl::Create();

    MaterialStorage& MaterialData = pPayload->GetStorage();
    Material.ProcessActiveTextureAttibs(
        [&](Uint32 TextureAttribId, const GLTF::Material::TextureShaderAttribs&, int TextureId) //
        {
            if (TextureId >= 0 && static_cast<Uint32>(TextureId) < TextureCount)
                MaterialData.SetTexture(TextureAttribId, ppTextures[TextureId]);
            return true;
        });
    MaterialData.Material = std::move(Material);
    return CreateMaterialAsset(std::move(pPayload), ppMaterial);
}

RADIENT_STATUS RadientMaterialAssetManager::GetLoadStatus(IRadientAsset* pMaterial)
{
    return MaterialAssetImpl::GetLoadStatus(pMaterial);
}

RADIENT_STATUS RadientMaterialAssetManager::GetGPUResourceStatus(IRadientAsset* pMaterial)
{
    RefCntAutoPtr<MaterialAssetImpl> pImpl{pMaterial, IID_MaterialAssetImpl};
    if (!pImpl)
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RADIENT_STATUS PayloadStatus = pImpl->GetPayloadStatus();
    if (PayloadStatus != RADIENT_STATUS_OK)
        return PayloadStatus;

    return pImpl->GetStorage().GetGPUResourceStatus();
}

const GLTF::Material* RadientMaterialAssetManager::GetMaterial(IRadientMaterialAsset* pMaterial)
{
    RefCntAutoPtr<MaterialAssetImpl> pImpl = MaterialAssetImpl::ResolveAsset(pMaterial);
    if (!pImpl)
        return nullptr;

    MaterialStorage& MaterialData = pImpl->GetStorage();
    return UpdateTextureAtlasAttribs(MaterialData) ? &MaterialData.Material : nullptr;
}

} // namespace Diligent
