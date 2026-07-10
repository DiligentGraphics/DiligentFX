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
#include "RadientScene.h"
#include "RefCntAutoPtr.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Diligent
{

namespace RadientImport
{

using TextureAssetList  = std::vector<RefCntAutoPtr<IRadientTextureAsset>>;
using MaterialAssetList = std::vector<RefCntAutoPtr<IRadientMaterialAsset>>;
using MeshAssetList     = std::vector<RefCntAutoPtr<IRadientMeshAsset>>;

struct ImportedNode
{
    std::string Name;

    RadientTransform Transform{};

    RefCntAutoPtr<IRadientMeshAsset> pMesh;

    std::optional<RadientCameraComponent> Camera;
    std::optional<RadientLightComponent>  Light;

    std::vector<Uint32> Children;
};

struct ImportedScene
{
    std::string         Name;
    std::vector<Uint32> RootNodes;
};

/// Radient-native data produced by an asset importer. Source-format documents
/// are temporary loading artifacts and are not retained here.
struct ImportedDocument
{
    TextureAssetList  Textures;
    MaterialAssetList Materials;
    MeshAssetList     Meshes;

    std::vector<ImportedNode>  Nodes;
    std::vector<ImportedScene> Scenes;

    Uint32 DefaultSceneId = 0;
};

} // namespace RadientImport

} // namespace Diligent
