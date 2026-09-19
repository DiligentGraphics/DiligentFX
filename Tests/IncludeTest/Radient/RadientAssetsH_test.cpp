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

#include <type_traits>

using namespace Diligent;

static_assert(RADIENT_ASSET_TYPE_MESH == 0, "Unexpected RADIENT_ASSET_TYPE_MESH value");
static_assert(RADIENT_ASSET_TYPE_MATERIAL == 1, "Unexpected RADIENT_ASSET_TYPE_MATERIAL value");
static_assert(RADIENT_ASSET_TYPE_TEXTURE == 2, "Unexpected RADIENT_ASSET_TYPE_TEXTURE value");
static_assert(RADIENT_ASSET_TYPE_SCENE == 3, "Unexpected RADIENT_ASSET_TYPE_SCENE value");
static_assert(RADIENT_ASSET_TYPE_MATERIAL_DEFINITION == 4, "Unexpected RADIENT_ASSET_TYPE_MATERIAL_DEFINITION value");
static_assert(RADIENT_ASSET_TYPE_SKELETON == 5, "Unexpected RADIENT_ASSET_TYPE_SKELETON value");
static_assert(RADIENT_ASSET_TYPE_SKIN == 6, "Unexpected RADIENT_ASSET_TYPE_SKIN value");
static_assert(RADIENT_ASSET_TYPE_ANIMATION_CLIP == 7, "Unexpected RADIENT_ASSET_TYPE_ANIMATION_CLIP value");

static_assert(RADIENT_SCENE_FORMAT_AUTO == 0, "Unexpected RADIENT_SCENE_FORMAT_AUTO value");
static_assert(RADIENT_SCENE_FORMAT_GLTF == 1, "Unexpected RADIENT_SCENE_FORMAT_GLTF value");

static_assert(RADIENT_INDEX_TYPE_NONE == 0, "Unexpected RADIENT_INDEX_TYPE_NONE value");
static_assert(RADIENT_INDEX_TYPE_UINT16 == 1, "Unexpected RADIENT_INDEX_TYPE_UINT16 value");
static_assert(RADIENT_INDEX_TYPE_UINT32 == 2, "Unexpected RADIENT_INDEX_TYPE_UINT32 value");

static_assert(std::is_standard_layout<RadientTextureAssetDesc>::value, "RadientTextureAssetDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientTextureAssetDesc>::value, "RadientTextureAssetDesc must be trivially copyable");

static_assert(sizeof(RadientColorRGBA8) == 4, "Unexpected RadientColorRGBA8 size");
static_assert(sizeof(RadientBoneIndices4) == 8, "Unexpected RadientBoneIndices4 size");
static_assert(std::is_standard_layout<RadientMeshGeometryDesc>::value, "RadientMeshGeometryDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientMeshGeometryDesc>::value, "RadientMeshGeometryDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientMeshPrimitiveDesc>::value, "RadientMeshPrimitiveDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientMeshPrimitiveDesc>::value, "RadientMeshPrimitiveDesc must be trivially copyable");
static_assert(std::is_standard_layout<RadientMeshAssetDesc>::value, "RadientMeshAssetDesc must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientMeshAssetDesc>::value, "RadientMeshAssetDesc must be trivially copyable");

void RadientAssets_CPP_UseMeshCreateInfo()
{
    RadientMeshPrimitiveCreateInfo Primitive;
    RadientMeshCreateInfo          MeshCI;
    RadientTextureLoadInfo         TextureLoadInfo;
    RadientSceneLoadInfo           SceneLoadInfo;

    (void)Primitive;
    const RadientVertexAttributeDesc VertexAttribute = {
        "POSITION",
        0,
        DILIGENT_RADIENT_VERTEX_AUTO_OFFSET,
        RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32,
        3,
        0,
    };
    const RadientVertexBufferLayoutDesc VertexBuffer = {DILIGENT_RADIENT_VERTEX_AUTO_STRIDE};
    IRadientDataBlob* const             VertexData   = 0;
    MeshCI.VertexLayout.pAttributes                  = &VertexAttribute;
    MeshCI.VertexLayout.AttributeCount               = 1;
    MeshCI.VertexLayout.pBuffers                     = &VertexBuffer;
    MeshCI.VertexLayout.BufferCount                  = 1;
    MeshCI.ppVertexBuffers                           = &VertexData;
    MeshCI.VertexCount                               = 1;
    MeshCI.pIndexBuffer                              = VertexData;
    (void)MeshCI;
    IRadientDataBlob* pBlob   = TextureLoadInfo.pDataBlob;
    TextureLoadInfo.pDataBlob = pBlob;
    RadientTextureData    PixelData;
    RadientTextureMipData Mip{};
    Mip.pDataBlob                = pBlob;
    Mip.ByteOffset               = 0;
    Mip.Stride                   = 8;
    PixelData.pMipLevels         = &Mip;
    PixelData.MipLevelCount      = 1;
    PixelData.GenerateMips       = True;
    TextureLoadInfo.pTextureData = &PixelData;
    (void)TextureLoadInfo;
    (void)SceneLoadInfo;
}

void RadientAssets_CPP_UseTextureAsset(IRadientTextureAsset* pTexture)
{
    const RadientTextureAssetDesc& Desc      = pTexture->GetDesc();
    const Uint32                   Width     = Desc.Width;
    const Uint32                   Height    = Desc.Height;
    const RADIENT_TEXTURE_FORMAT   Format    = Desc.Format;
    const Uint32                   MipLevels = Desc.MipLevels;
    (void)Width;
    (void)Height;
    (void)Format;
    (void)MipLevels;
}

void RadientAssets_CPP_UseMeshAsset(IRadientMeshAsset* pMesh)
{
    IRadientMorphTargetWeights* pWeights = nullptr;
    const RadientMeshAssetDesc& Desc     = pMesh->GetDesc();
    (void)pMesh->CreateMorphTargetWeights(&pWeights);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < Desc.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveDesc& Primitive = Desc.pPrimitives[PrimitiveIndex];
        if (Primitive.GeometryIndex < Desc.GeometryCount)
        {
            const RadientMeshGeometryDesc& Geometry  = Desc.pGeometries[Primitive.GeometryIndex];
            const RadientVertexLayoutDesc& Layout    = Geometry.VertexLayout;
            const RADIENT_INDEX_TYPE       IndexType = Geometry.IndexType;
            const Uint32                   Available = IndexType == RADIENT_INDEX_TYPE_NONE ? Geometry.VertexCount : Geometry.IndexCount;
            const Uint32                   First     = Primitive.FirstElement;
            const Uint32                   Count     = Primitive.ElementCount;
            IRadientMaterialAsset*         pMaterial = Primitive.pMaterial;
            const Char*                    Name      = Primitive.Name;
            (void)Layout;
            (void)Available;
            (void)First;
            (void)Count;
            (void)pMaterial;
            (void)Name;
        }
    }
    (void)pWeights;
}

void RadientAssets_CPP_UseAnimationClipFactory(IRadientAssetManager*           pAssetManager,
                                               const RadientAnimationClipDesc& ClipDesc,
                                               IRadientAnimationClipAsset**    ppClip)
{
    (void)pAssetManager->CreateAnimationClip(ClipDesc, ppClip);
}

void RadientAssets_CPP_UseSceneAnimationClips(IRadientSceneAsset* pScene)
{
    const RadientSceneAssetDesc& Desc = pScene->GetDesc();
    if (Desc.AnimationClipCount != 0)
    {
        IRadientAnimationClipAsset* pClip = Desc.ppAnimationClips[0];
        (void)pClip;
    }
}
