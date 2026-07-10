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

#include "Radient/interface/RadientAssets.h"

using namespace Diligent;

static_assert(RADIENT_ASSET_TYPE_MESH == 0, "Unexpected RADIENT_ASSET_TYPE_MESH value");
static_assert(RADIENT_ASSET_TYPE_MATERIAL == 1, "Unexpected RADIENT_ASSET_TYPE_MATERIAL value");
static_assert(RADIENT_ASSET_TYPE_TEXTURE == 2, "Unexpected RADIENT_ASSET_TYPE_TEXTURE value");
static_assert(RADIENT_ASSET_TYPE_SCENE == 3, "Unexpected RADIENT_ASSET_TYPE_SCENE value");

static_assert(RADIENT_SCENE_FORMAT_AUTO == 0, "Unexpected RADIENT_SCENE_FORMAT_AUTO value");
static_assert(RADIENT_SCENE_FORMAT_GLTF == 1, "Unexpected RADIENT_SCENE_FORMAT_GLTF value");

static_assert(RADIENT_INDEX_TYPE_NONE == 0, "Unexpected RADIENT_INDEX_TYPE_NONE value");
static_assert(RADIENT_INDEX_TYPE_UINT16 == 1, "Unexpected RADIENT_INDEX_TYPE_UINT16 value");
static_assert(RADIENT_INDEX_TYPE_UINT32 == 2, "Unexpected RADIENT_INDEX_TYPE_UINT32 value");

static_assert(sizeof(RadientColorRGBA8) == 4, "Unexpected RadientColorRGBA8 size");
static_assert(sizeof(RadientBoneIndices4) == 8, "Unexpected RadientBoneIndices4 size");

void RadientAssets_CPP_UseMeshCreateInfo()
{
    RadientMeshPrimitiveCreateInfo Primitive;
    RadientMeshCreateInfo          MeshCI;
    RadientSceneLoadInfo           SceneLoadInfo;

    (void)Primitive;
    (void)MeshCI;
    (void)SceneLoadInfo;
}
