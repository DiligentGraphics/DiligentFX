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
#include "DebugUtilities.hpp"
#include "RefCntAutoPtr.hpp"
#include "WeakObjectCache.hpp"

#include <memory>

namespace Diligent
{

class RadientMaterialAssetManager;
struct RadientMaterialShaderDataLayoutDesc;

using RadientMaterialAssetManagerSharedPtr = std::shared_ptr<RadientMaterialAssetManager>;

struct RadientMaterialDefaultTextures
{
    RefCntAutoPtr<IRadientTextureAsset> pWhite;
    RefCntAutoPtr<IRadientTextureAsset> pBlack;
    RefCntAutoPtr<IRadientTextureAsset> pNormal;
    RefCntAutoPtr<IRadientTextureAsset> pPhysicalDesc;
};

/// Texture entry for one material parameter array element.
/// pTexture is either the requested texture or the definition-provided fallback.
struct RadientMaterialTextureEntry
{
    // Parameter index in material definition
    Uint32 ParameterIndex = ~Uint32{0};
    Uint32 ArrayIndex     = 0;

    RefCntAutoPtr<IRadientTextureAsset> pTexture;

    explicit operator bool() const noexcept
    {
        return pTexture != nullptr;
    }
};

/// Immutable renderer-neutral view of a material asset and its selected texture dependencies.
/// Texture entries are ordered by ParameterIndex and ArrayIndex. ParameterIndex
/// refers to the material definition retained by pMaterial. pTextureIndexByParameter
/// maps every definition parameter to its first texture entry; non-texture
/// parameters map to InvalidTextureIndex.
/// The view remains valid while the material asset is retained.
struct RadientMaterialAssetView
{
    static constexpr Uint32 InvalidTextureIndex = ~Uint32{0};

    IRadientMaterialAsset*             pMaterial                = nullptr;
    const RadientMaterialTextureEntry* pTextures                = nullptr;
    Uint32                             TextureCount             = 0;
    const Uint32*                      pTextureIndexByParameter = nullptr;
    Uint32                             ParameterCount           = 0;

    const RadientMaterialTextureEntry* GetTexture(Uint32 ParameterIndex,
                                                  Uint32 ArrayIndex = 0) const noexcept
    {
        if (pTextures == nullptr)
        {
            UNEXPECTED("Material texture entries are not initialized");
            return nullptr;
        }
        if (pTextureIndexByParameter == nullptr)
        {
            UNEXPECTED("Material texture parameter index mapping is not initialized");
            return nullptr;
        }
        if (ParameterIndex >= ParameterCount)
        {
            UNEXPECTED("Material parameter index ", ParameterIndex, " exceeds the parameter count ", ParameterCount);
            return nullptr;
        }

        const Uint32 FirstTextureIndex = pTextureIndexByParameter[ParameterIndex];
        if (FirstTextureIndex == InvalidTextureIndex)
        {
            UNEXPECTED("Material parameter ", ParameterIndex, " is not a texture parameter");
            return nullptr;
        }
        if (FirstTextureIndex >= TextureCount)
        {
            UNEXPECTED("Texture index ", FirstTextureIndex, " mapped from material parameter ",
                       ParameterIndex, " exceeds the texture count ", TextureCount);
            return nullptr;
        }
        if (ArrayIndex >= TextureCount - FirstTextureIndex)
        {
            UNEXPECTED("Texture array index ", ArrayIndex, " for material parameter ",
                       ParameterIndex, " exceeds the material texture data");
            return nullptr;
        }

        const RadientMaterialTextureEntry& Texture = pTextures[FirstTextureIndex + ArrayIndex];
        if (Texture.ParameterIndex != ParameterIndex || Texture.ArrayIndex != ArrayIndex)
        {
            UNEXPECTED("Material texture mapping is inconsistent for parameter ", ParameterIndex, ", array index ", ArrayIndex);
            return nullptr;
        }
        return &Texture;
    }

    IRadientTextureAsset* GetTextureAsset(Uint32 ParameterIndex,
                                          Uint32 ArrayIndex = 0) const noexcept
    {
        const RadientMaterialTextureEntry* pTexture = GetTexture(ParameterIndex, ArrayIndex);
        return pTexture != nullptr ? pTexture->pTexture.RawPtr() : nullptr;
    }

    explicit operator bool() const noexcept
    {
        return pMaterial != nullptr &&
            (pTextures != nullptr || TextureCount == 0) &&
            (pTextureIndexByParameter != nullptr || ParameterCount == 0);
    }
};

class RadientMaterialAssetManager final : public std::enable_shared_from_this<RadientMaterialAssetManager>
{
public:
    struct CreateInfo
    {
        RadientMaterialDefaultTextures DefaultTextures;
    };

    ~RadientMaterialAssetManager();

    static RadientMaterialAssetManagerSharedPtr Create(const CreateInfo& CI = {});

    static RADIENT_STATUS CreateDefinition(const RadientMaterialDefinitionDesc& DefinitionDesc,
                                           IRadientMaterialDefinitionAsset**    ppDefinition);

    static RADIENT_STATUS CreateDefinition(const RadientMaterialDefinitionDesc&       DefinitionDesc,
                                           const RadientMaterialShaderDataLayoutDesc& ShaderDataLayout,
                                           IRadientMaterialDefinitionAsset**          ppDefinition);

    RADIENT_STATUS CreateStandardMaterialDefinition(const RadientStandardMaterialDefinitionCreateInfo& DefinitionCI,
                                                    IRadientMaterialDefinitionAsset**                  ppDefinition);

    // pDefinition must have been created by Radient.
    RADIENT_STATUS CreateMaterial(IRadientMaterialDefinitionAsset* pDefinition,
                                  IRadientMaterialAsset**          ppMaterial);

    // Reports material source/dependency status. A failed requested texture is
    // replaced by its semantic default when one is available. OK means every
    // render texture source has loaded. Once the status becomes terminal,
    // it is cached and no longer rechecks texture dependencies.
    static RADIENT_STATUS GetLoadStatus(IRadientAsset* pMaterial);

    // Reports material GPU resource status. A requested texture that failed to
    // load is replaced by its semantic default when one is available. OK means
    // every selected texture GPU resource is available. NO_GPU_DATA means all
    // sources loaded without a GPU backend.
    static RADIENT_STATUS GetGPUResourceStatus(IRadientAsset* pMaterial);

    // Returns an immutable view of the material asset and its selected textures,
    // including semantic defaults selected for failed requested textures.
    // This method finalizes texture selection and must be called from the render
    // thread. It is not thread-safe and must not race with another
    // GetMaterialView() call for a material asset that retains the same state.
    static RadientMaterialAssetView GetMaterialView(IRadientMaterialAsset* pMaterial);

private:
    explicit RadientMaterialAssetManager(const CreateInfo& CI);

    RadientMaterialDefaultTextures                   m_DefaultTextures;
    WeakObjectCache<IRadientMaterialDefinitionAsset> m_StandardMaterialDefinitions;
};

} // namespace Diligent
