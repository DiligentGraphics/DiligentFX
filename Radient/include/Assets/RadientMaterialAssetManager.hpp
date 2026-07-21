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
#include "GLTFLoader.hpp"
#include "RefCntAutoPtr.hpp"

#include <memory>

namespace Diligent
{

class RadientMaterialAssetManager;

using RadientMaterialAssetManagerSharedPtr = std::shared_ptr<RadientMaterialAssetManager>;

/// Immutable renderer-facing view of a resolved material and its texture dependencies.
/// The view remains valid while the material asset is retained.
struct RadientMaterialRenderData
{
    const GLTF::Material*                      pMaterial    = nullptr;
    const RefCntAutoPtr<IRadientTextureAsset>* pTextures    = nullptr;
    Uint32                                     TextureCount = 0;

    IRadientTextureAsset* GetTexture(Uint32 TextureAttribId) const noexcept
    {
        return pTextures != nullptr && TextureAttribId < TextureCount ? pTextures[TextureAttribId].RawPtr() : nullptr;
    }

    explicit operator bool() const noexcept
    {
        return pMaterial != nullptr && (pTextures != nullptr || TextureCount == 0);
    }
};

class RadientMaterialAssetManager final : public std::enable_shared_from_this<RadientMaterialAssetManager>
{
public:
    ~RadientMaterialAssetManager();

    static RadientMaterialAssetManagerSharedPtr Create();

    RADIENT_STATUS CreateMaterial(const RadientMaterialCreateInfo& MaterialCI,
                                  IRadientMaterialAsset**          ppMaterial);

    RADIENT_STATUS CreateGLTFMaterial(GLTF::Material               Material,
                                      IRadientTextureAsset* const* ppTextures,
                                      Uint32                       TextureCount,
                                      IRadientMaterialAsset**      ppMaterial);

    // Reports material source/dependency status. OK means all dependent texture
    // sources have loaded. Once the status becomes terminal, it is cached and
    // no longer rechecks texture dependencies.
    static RADIENT_STATUS GetLoadStatus(IRadientAsset* pMaterial);

    // Reports material GPU resource status. OK means all dependent texture GPU
    // resources are available. NO_GPU_DATA means all sources loaded without a GPU backend.
    static RADIENT_STATUS GetGPUResourceStatus(IRadientAsset* pMaterial);

    // Returns the resolved material and its immutable texture dependencies.
    // This method lazily updates texture atlas attributes in the stored material
    // and must be called from the render thread. It is not thread-safe and must
    // not race with another GetRenderData() call for the same material asset.
    static RadientMaterialRenderData GetRenderData(IRadientMaterialAsset* pMaterial);

private:
    RadientMaterialAssetManager() = default;
};

} // namespace Diligent
