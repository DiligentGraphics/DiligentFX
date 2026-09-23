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

void RadientMeshImportServices_C_UseInterfaces(IRadientAssetManager* pManager, IRadientMeshImportServices* pServices)
{
    RadientMeshVertexData        VertexCI   = {0};
    RadientMeshIndexData         IndexCI    = {0};
    RadientMeshMorphTargetData   MorphCI    = {0};
    IRadientMeshVertexData*      pVertices  = 0;
    IRadientMeshIndexData*       pIndices   = 0;
    IRadientMeshMorphTargetData* pMorphData = 0;
    IndexCI.IndexType                       = RADIENT_INDEX_TYPE_UINT8;
    (void)IRadientMeshImportServices_CreateMeshVertexData(pServices, &VertexCI, &pVertices);
    (void)IRadientMeshImportServices_CreateMeshIndexData(pServices, &IndexCI, &pIndices);
    (void)IRadientMeshImportServices_CreateMeshMorphTargetData(pServices, &MorphCI, VertexCI.VertexCount, &pMorphData);
    (void)IRadientAssetManager_WaitForAssetLoad(pManager, (IRadientAsset*)pVertices);
    (void)IRadientAssetManager_WaitForAssetLoad(pManager, (IRadientAsset*)pIndices);
    (void)IRadientAssetManager_WaitForAssetLoad(pManager, (IRadientAsset*)pMorphData);

    RadientMeshGeometryData        Geometry  = {pVertices, pIndices, pMorphData};
    RadientMeshPrimitiveCreateInfo Primitive = {0};
    RadientMeshViewCreateInfo      ViewCI    = {0};
    Primitive.IndexCount                     = 3;
    ViewCI.pPrimitives                       = &Primitive;
    ViewCI.PrimitiveCount                    = 1;
    ViewCI.pGeometryData                     = &Geometry;
    ViewCI.GeometryCount                     = 1;
    IRadientMeshAsset* pMesh                 = 0;
    (void)IRadientMeshImportServices_CreateMeshView(pServices, &ViewCI, &pMesh);

    if (pVertices != 0)
    {
        (void)CALL_IFACE_METHOD(RadientAsset, GetType, pVertices);
        (void)CALL_IFACE_METHOD(RadientAsset, GetReference, pVertices);
        IObject_Release(pVertices);
    }
    if (pIndices != 0)
    {
        (void)CALL_IFACE_METHOD(RadientAsset, GetType, pIndices);
        IObject_Release(pIndices);
    }
    if (pMorphData != 0)
    {
        (void)CALL_IFACE_METHOD(RadientAsset, GetType, pMorphData);
        IObject_Release(pMorphData);
    }
}
