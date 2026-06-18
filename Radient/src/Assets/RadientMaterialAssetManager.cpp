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
#include "Assets/RadientAssetURI.hpp"
#include "Assets/RadientTextureAssetManager.hpp"
#include "DebugUtilities.hpp"
#include "GLTFBuilder.hpp"
#include "Math/RadientMath.hpp"

#include <utility>

namespace Diligent
{

namespace
{

static constexpr INTERFACE_ID IID_MaterialAssetImpl = {0x1a11a468, 0xbf30, 0x4c4d, {0xb8, 0xcd, 0x48, 0x89, 0xa4, 0x65, 0xa, 0x50}};

struct MaterialStorage
{
    GLTF::Material Material;

    RefCntAutoPtr<IRadientTextureAsset> pBaseColorTexture;
    RefCntAutoPtr<IRadientTextureAsset> pMetallicRoughnessTexture;
    RefCntAutoPtr<IRadientTextureAsset> pNormalTexture;
    RefCntAutoPtr<IRadientTextureAsset> pOcclusionTexture;
    RefCntAutoPtr<IRadientTextureAsset> pEmissiveTexture;
};

class MaterialPayloadImpl final : public RadientAssetPayloadImpl<MaterialStorage, MaterialPayloadImpl>
{
public:
    using TBase = RadientAssetPayloadImpl<MaterialStorage, MaterialPayloadImpl>;
    using TBase::TBase;
};

using MaterialAssetImpl =
    RadientAssetImpl<IRadientMaterialAsset, IID_RadientMaterialAsset, IID_MaterialAssetImpl, RADIENT_ASSET_TYPE_MATERIAL, MaterialPayloadImpl>;

} // namespace

RADIENT_STATUS RadientMaterialAssetManager::CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                                           IRadientMaterialAsset**          ppMaterial)
{
    if (ppMaterial == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppMaterial == nullptr, "Output material pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppMaterial = nullptr;

    MaterialStorage MaterialData;
    MaterialData.Material                  = CreateGLTFMaterial(MaterialCI);
    MaterialData.pBaseColorTexture         = MaterialCI.pBaseColorTexture;
    MaterialData.pMetallicRoughnessTexture = MaterialCI.pMetallicRoughnessTexture;
    MaterialData.pNormalTexture            = MaterialCI.pNormalTexture;
    MaterialData.pOcclusionTexture         = MaterialCI.pOcclusionTexture;
    MaterialData.pEmissiveTexture          = MaterialCI.pEmissiveTexture;

    RefCntAutoPtr<MaterialPayloadImpl> pPayload = MaterialPayloadImpl::Create(std::move(MaterialData));
    RefCntAutoPtr<MaterialAssetImpl>   pMaterial =
        MaterialAssetImpl::Create(MakeRadientAssetURI("material", m_NextAssetID.fetch_add(1, std::memory_order_relaxed)),
                                  std::move(pPayload));
    *ppMaterial = pMaterial.Detach();
    return RADIENT_STATUS_OK;
}

const GLTF::Material* RadientMaterialAssetManager::GetMaterial(IRadientMaterialAsset* pMaterial)
{
    RefCntAutoPtr<MaterialAssetImpl> pImpl = MaterialAssetImpl::ResolveAsset(pMaterial);
    return pImpl ? &pImpl->GetStorage().Material : nullptr;
}

GLTF::Material RadientMaterialAssetManager::CreateGLTFMaterial(const RadientMaterialCreateInfo& MaterialCI)
{
    GLTF::Material        Material;
    GLTF::MaterialBuilder Builder{Material};

    GLTF::Material::ShaderAttribs& Attribs = Builder.GetShaderAttribs();
    Attribs.BaseColorFactor                = RadientMath::ToFloat4(MaterialCI.BaseColorFactor);
    Attribs.MetallicFactor                 = MaterialCI.MetallicFactor;
    Attribs.RoughnessFactor                = MaterialCI.RoughnessFactor;
    Attribs.EmissiveFactor                 = RadientMath::ToFloat3(MaterialCI.EmissiveFactor);
    Attribs.AlphaCutoff                    = MaterialCI.AlphaCutoff;

    Material.DoubleSided = MaterialCI.DoubleSided != False;

    int  TextureId = 0;
    auto AddMaterialTexture =
        [&](Uint32 TextureAttribId, IRadientTextureAsset* pTexture) //
    {
        if (pTexture == nullptr)
            return;

        Builder.SetTextureId(TextureAttribId, TextureId++);
        GLTF::Material::TextureShaderAttribs& TextureAttribs = Builder.GetTextureAttrib(TextureAttribId);
        TextureAttribs.SetUVSelector(0);
        RadientTextureAssetManager::ApplyTextureAtlasAttribs(pTexture, TextureAttribs);
    };

    AddMaterialTexture(GLTF::DefaultBaseColorTextureAttribId, MaterialCI.pBaseColorTexture);
    AddMaterialTexture(GLTF::DefaultMetallicRoughnessTextureAttribId, MaterialCI.pMetallicRoughnessTexture);
    AddMaterialTexture(GLTF::DefaultNormalTextureAttribId, MaterialCI.pNormalTexture);
    AddMaterialTexture(GLTF::DefaultOcclusionTextureAttribId, MaterialCI.pOcclusionTexture);
    AddMaterialTexture(GLTF::DefaultEmissiveTextureAttribId, MaterialCI.pEmissiveTexture);

    Builder.Finalize();
    return Material;
}

} // namespace Diligent
