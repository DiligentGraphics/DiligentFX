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

void RadientAssets_C_UseTypes(void)
{
    RadientAssetManagerDesc        AssetManagerDesc = {0};
    RadientAssetManagerCreateInfo  AssetManagerCI   = {0};
    RadientMeshPrimitiveCreateInfo Primitive        = {0};
    RadientMeshCreateInfo          MeshCI           = {0};
    RadientMaterialCreateInfo      MaterialCI       = {0};
    RadientTextureLoadInfo         TextureLoadInfo  = {0};
    RadientGLTFLoadInfo            GLTFLoadInfo     = {0};
    RadientAssetReference          Asset            = {0};
    IRadientMeshAsset*             pMesh            = 0;
    IRadientMaterialAsset*         pMaterial        = 0;
    IRadientTextureAsset*          pTexture         = 0;
    IRadientSceneAsset*            pScene           = 0;

    (void)AssetManagerDesc;
    (void)AssetManagerCI;
    (void)Primitive;
    (void)MeshCI;
    (void)MaterialCI;
    (void)TextureLoadInfo;
    (void)GLTFLoadInfo;
    (void)Asset;
    (void)pMesh;
    (void)pMaterial;
    (void)pTexture;
    (void)pScene;
}

void RadientAssets_C_TestMacros(IRadientAssetManager* pAssetManager)
{
    const RadientAssetManagerDesc* pDesc        = IRadientAssetManager_GetDesc(pAssetManager);
    RadientMeshCreateInfo          MeshCI       = {0};
    RadientMaterialCreateInfo      MaterialCI   = {0};
    RadientTextureLoadInfo         TextureInfo  = {0};
    RadientGLTFLoadInfo            GLTFLoadInfo = {0};
    IRadientMeshAsset*             pMesh        = 0;
    IRadientMaterialAsset*         pMaterial    = 0;
    IRadientTextureAsset*          pTexture     = 0;
    IRadientSceneAsset*            pScene       = 0;
    RADIENT_STATUS                 Status       = RADIENT_STATUS_OK;

    Status = IRadientAssetManager_CreateMesh(pAssetManager, &MeshCI, &pMesh);
    Status = IRadientAssetManager_CreateMaterial(pAssetManager, &MaterialCI, &pMaterial);
    Status = IRadientAssetManager_LoadTexture(pAssetManager, &TextureInfo, &pTexture);
    Status = IRadientAssetManager_LoadGLTF(pAssetManager, &GLTFLoadInfo, &pScene);
    Status = IRadientAssetManager_WaitForAssetLoad(pAssetManager, (IRadientAsset*)pScene);

    (void)pDesc;
    (void)pMesh;
    (void)pMaterial;
    (void)pTexture;
    (void)pScene;
    (void)Status;
}
