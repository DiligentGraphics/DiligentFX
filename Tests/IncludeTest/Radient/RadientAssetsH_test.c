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
    RadientAssetManagerDesc          AssetManagerDesc    = {0};
    RadientAssetManagerCreateInfo    AssetManagerCI      = {0};
    RadientMeshPrimitiveCreateInfo   Primitive           = {0};
    RadientMeshCreateInfo            MeshCI              = {0};
    RadientMeshAssetDesc             MeshDesc            = {0};
    RadientMeshGeometryDesc          GeometryDesc        = {0};
    RadientMeshPrimitiveDesc         PrimitiveDesc       = {0};
    RadientTextureLoadInfo           TextureLoadInfo     = {0};
    RadientTextureAssetDesc          TextureDesc         = {0};
    RadientSceneLoadInfo             SceneLoadInfo       = {0};
    RadientSceneAssetDesc            SceneAssetDesc      = {0};
    RadientAssetReference            Asset               = {0};
    IRadientMeshAsset*               pMesh               = 0;
    IRadientMaterialDefinitionAsset* pMaterialDefinition = 0;
    IRadientMaterialAsset*           pMaterial           = 0;
    IRadientTextureAsset*            pTexture            = 0;
    IRadientSceneAsset*              pScene              = 0;

    (void)AssetManagerDesc;
    (void)AssetManagerCI;
    (void)Primitive;

    const RadientVertexAttributeDesc VertexAttribute = {
        "POSITION",
        0,
        DILIGENT_RADIENT_VERTEX_AUTO_OFFSET,
        RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32,
        3,
        0,
    };
    const RadientVertexBufferLayoutDesc VertexBuffer    = {DILIGENT_RADIENT_VERTEX_AUTO_STRIDE};
    IRadientDataBlob* const             pVertexBlob     = 0;
    RadientMeshVertexData               VertexData      = {0};
    RadientMeshIndexData                IndexData       = {0};
    RadientMeshMorphTargetData          MorphTargetData = {0};
    VertexData.VertexLayout.pAttributes                 = &VertexAttribute;
    VertexData.VertexLayout.AttributeCount              = 1;
    VertexData.VertexLayout.pBuffers                    = &VertexBuffer;
    VertexData.VertexLayout.BufferCount                 = 1;
    VertexData.ppVertexBuffers                          = &pVertexBlob;
    VertexData.VertexCount                              = 1;
    IndexData.pIndexBuffer                              = pVertexBlob;
    IndexData.IndexCount                                = 3;
    IndexData.IndexType                                 = RADIENT_INDEX_TYPE_UINT32;
    MeshCI.VertexData                                   = VertexData;
    MeshCI.IndexData                                    = IndexData;
    MeshCI.MorphTargetData                              = MorphTargetData;
    (void)MeshCI;
    GeometryDesc.VertexLayout   = MeshCI.VertexData.VertexLayout;
    GeometryDesc.VertexCount    = MeshCI.VertexData.VertexCount;
    GeometryDesc.IndexType      = RADIENT_INDEX_TYPE_UINT32;
    GeometryDesc.IndexCount     = 3;
    PrimitiveDesc.Name          = "Primitive";
    PrimitiveDesc.GeometryIndex = 0;
    PrimitiveDesc.FirstElement  = 0;
    PrimitiveDesc.ElementCount  = 3;
    PrimitiveDesc.pMaterial     = pMaterial;
    MeshDesc.pGeometries        = &GeometryDesc;
    MeshDesc.GeometryCount      = 1;
    MeshDesc.pPrimitives        = &PrimitiveDesc;
    MeshDesc.PrimitiveCount     = 1;
    (void)MeshDesc;
    IRadientDataBlob* pBlob         = TextureLoadInfo.pDataBlob;
    TextureLoadInfo.pDataBlob       = pBlob;
    RadientTextureData    PixelData = {0};
    RadientTextureMipData Mip       = {0};
    Mip.pDataBlob                   = pBlob;
    Mip.ByteOffset                  = 0;
    Mip.Stride                      = 8;
    PixelData.pMipLevels            = &Mip;
    PixelData.MipLevelCount         = 1;
    PixelData.GenerateMips          = True;
    TextureLoadInfo.pTextureData    = &PixelData;
    (void)TextureLoadInfo;
    TextureDesc.Width     = 16;
    TextureDesc.Height    = 8;
    TextureDesc.Format    = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
    TextureDesc.MipLevels = 5;
    (void)TextureDesc;
    (void)SceneLoadInfo;
    (void)SceneAssetDesc;
    (void)Asset;
    (void)pMesh;
    (void)pMaterialDefinition;
    (void)pMaterial;
    (void)pTexture;
    (void)pScene;
}

void RadientAssets_C_TestMacros(IRadientAssetManager* pAssetManager)
{
    const RadientAssetManagerDesc*   pDesc               = IRadientAssetManager_GetDesc(pAssetManager);
    RadientMeshCreateInfo            MeshCI              = {0};
    RadientTextureLoadInfo           TextureInfo         = {0};
    RadientSceneLoadInfo             SceneLoadInfo       = {0};
    IRadientMeshAsset*               pMesh               = 0;
    IRadientMaterialDefinitionAsset* pMaterialDefinition = 0;
    IRadientMaterialAsset*           pMaterial           = 0;
    IRadientTextureAsset*            pTexture            = 0;
    IRadientSceneAsset*              pScene              = 0;
    RADIENT_STATUS                   Status              = RADIENT_STATUS_OK;

    Status = IRadientAssetManager_CreateMesh(pAssetManager, &MeshCI, &pMesh);
    if (pMesh != 0)
    {
        IRadientMorphTargetWeights* pWeights  = 0;
        const RadientMeshAssetDesc* pMeshDesc = IRadientMeshAsset_GetDesc(pMesh);
        Status                                = IRadientMeshAsset_CreateMorphTargetWeights(pMesh, &pWeights);
        if (pMeshDesc->PrimitiveCount != 0)
        {
            const RadientMeshPrimitiveDesc* pPrimitiveDesc = &pMeshDesc->pPrimitives[0];
            if (pPrimitiveDesc->GeometryIndex < pMeshDesc->GeometryCount)
            {
                const RadientMeshGeometryDesc* pGeometry = &pMeshDesc->pGeometries[pPrimitiveDesc->GeometryIndex];
                const RadientVertexLayoutDesc* pLayout   = &pGeometry->VertexLayout;
                const RADIENT_INDEX_TYPE       IndexType = pGeometry->IndexType;
                (void)pLayout;
                (void)IndexType;
            }
        }
        (void)pWeights;
    }
    Status = IRadientAssetManager_CreateMaterial(pAssetManager, pMaterialDefinition, &pMaterial);
    Status = IRadientAssetManager_LoadTexture(pAssetManager, &TextureInfo, &pTexture);
    if (pTexture != 0)
    {
        const RadientTextureAssetDesc* pTextureDesc = IRadientTextureAsset_GetDesc(pTexture);
        const Uint32                   Width        = pTextureDesc->Width;
        const Uint32                   Height       = pTextureDesc->Height;
        const RADIENT_TEXTURE_FORMAT   Format       = pTextureDesc->Format;
        const Uint32                   MipLevels    = pTextureDesc->MipLevels;
        (void)Width;
        (void)Height;
        (void)Format;
        (void)MipLevels;
    }
    Status = IRadientAssetManager_LoadScene(pAssetManager, &SceneLoadInfo, &pScene);
    Status = IRadientAssetManager_WaitForAssetLoad(pAssetManager, (IRadientAsset*)pScene);
    if (pScene != 0)
    {
        const RadientSceneAssetDesc* pSceneDesc = IRadientSceneAsset_GetDesc(pScene);
        if (pSceneDesc->AnimationClipCount != 0)
        {
            IRadientAnimationClipAsset* pClip = pSceneDesc->ppAnimationClips[0];
            (void)pClip;
        }
        (void)pSceneDesc;
    }

    (void)pDesc;
    (void)pMesh;
    (void)pMaterial;
    (void)pTexture;
    (void)pScene;
    (void)Status;
}

void RadientAssets_C_TestAnimationClipFactory(IRadientAssetManager*           pAssetManager,
                                              const RadientAnimationClipDesc* pClipDesc,
                                              IRadientAnimationClipAsset**    ppClip)
{
    (void)IRadientAssetManager_CreateAnimationClip(pAssetManager, pClipDesc, ppClip);
}
