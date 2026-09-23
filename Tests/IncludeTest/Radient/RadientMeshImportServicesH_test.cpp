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

#include "Radient/interface/RadientMeshImportServices.h"

#include <type_traits>

using namespace Diligent;

static_assert(std::is_base_of<IObject, IRadientMeshImportServices>::value, "Mesh import services must implement IObject");
static_assert(std::is_base_of<IRadientAsset, IRadientMeshVertexData>::value, "Vertex data must implement IRadientAsset");
static_assert(std::is_base_of<IRadientAsset, IRadientMeshIndexData>::value, "Index data must implement IRadientAsset");
static_assert(std::is_base_of<IRadientAsset, IRadientMeshMorphTargetData>::value, "Morph data must implement IRadientAsset");
static_assert(std::is_standard_layout<RadientMeshVertexDataCreateInfo>::value, "Vertex data create info must have a C-compatible layout");
static_assert(std::is_standard_layout<RadientMeshIndexDataCreateInfo>::value, "Index data create info must have a C-compatible layout");
static_assert(std::is_standard_layout<RadientMeshMorphTargetDataCreateInfo>::value, "Morph data create info must have a C-compatible layout");
static_assert(std::is_standard_layout<RadientMeshViewCreateInfo>::value, "Mesh view create info must have a C-compatible layout");
static_assert(std::is_trivially_copyable<RadientMeshGeometryData>::value, "Geometry data must be trivially copyable");

void RadientMeshImportServices_CPP_UseInterfaces(IRadientAssetManager* pManager, IRadientMeshImportServices* pServices)
{
    RadientMeshVertexDataCreateInfo      VertexCI;
    RadientMeshIndexDataCreateInfo       IndexCI;
    RadientMeshMorphTargetDataCreateInfo MorphCI;
    IndexCI.IndexType                       = RADIENT_INDEX_TYPE_UINT8;
    IRadientMeshVertexData*      pVertices  = nullptr;
    IRadientMeshIndexData*       pIndices   = nullptr;
    IRadientMeshMorphTargetData* pMorphData = nullptr;
    (void)pServices->CreateMeshVertexData(VertexCI, &pVertices);
    (void)pServices->CreateMeshIndexData(IndexCI, &pIndices);
    (void)pServices->CreateMeshMorphTargetData(MorphCI, &pMorphData);
    (void)pManager->WaitForAssetLoad(pVertices);
    (void)pManager->WaitForAssetLoad(pIndices);
    (void)pManager->WaitForAssetLoad(pMorphData);
    RadientMeshGeometryData        Geometry{pVertices, pIndices, pMorphData};
    RadientMeshPrimitiveCreateInfo Primitive;
    Primitive.IndexCount = 3;
    RadientMeshViewCreateInfo ViewCI;
    ViewCI.pPrimitives       = &Primitive;
    ViewCI.PrimitiveCount    = 1;
    ViewCI.pGeometryData     = &Geometry;
    ViewCI.GeometryCount     = 1;
    IRadientMeshAsset* pMesh = nullptr;
    (void)pServices->CreateMeshView(ViewCI, &pMesh);
}
