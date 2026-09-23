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

#include "BasicMath.hpp"
#include "RadientImportedDocument.hpp"
#include "RadientMeshImportServices.h"

#include <memory>

namespace Diligent
{

struct IRadientMaterialDefinitionAsset;
struct IRadientMaterialWriter;
struct IRadientTextureAsset;
struct RadientStandardMaterialDefinitionCreateInfo;

namespace GLTF
{

class Document;
struct Material;
struct Model;
struct TinyGltfModelView;
struct TinyGltfPrimitiveView;

} // namespace GLTF

namespace RadientGLTFConverter
{

/// Infers the immutable standard-material schema required by a GLTF material.
RADIENT_STATUS ConvertMaterialDefinition(
    const GLTF::Material&                        Material,
    RadientStandardMaterialDefinitionCreateInfo& DefinitionCI);

/// Copies GLTF material values and resolved texture assets into a material writer.
///
/// \p Definition must describe the schema returned by ConvertMaterialDefinition().
/// The function does not commit \p Writer.
RADIENT_STATUS PopulateMaterial(
    const GLTF::Material&            Material,
    IRadientTextureAsset* const*     ppTextures,
    Uint32                           TextureCount,
    IRadientMaterialDefinitionAsset& Definition,
    IRadientMaterialWriter&          Writer);

struct MeshVertexDataResult
{
    /// Conversion or mesh-data creation status. Successful creation may be pending.
    RADIENT_STATUS Status = RADIENT_STATUS_INVALID_DATA;

    /// Vertex-data handle created by the import services, or null on failure.
    RefCntAutoPtr<IRadientMeshVertexData> pVertexData;

    /// Number of vertices described by the POSITION accessor, or zero on failure.
    Uint32 VertexCount = 0;

    /// Primitive bounds computed from the POSITION accessor.
    float3 BBMin{};
    float3 BBMax{};
};

struct MeshIndexDataResult
{
    /// Conversion or mesh-data creation status. Successful creation may be pending.
    RADIENT_STATUS Status = RADIENT_STATUS_INVALID_DATA;

    /// Index-data handle created by the import services, or null on failure.
    RefCntAutoPtr<IRadientMeshIndexData> pIndexData;

    /// Number of accessor or generated indices, or zero on failure.
    Uint32 IndexCount = 0;
};

/// Creates reusable Radient vertex data for a GLTF primitive.
///
/// The primitive must have a valid POSITION accessor. Other supported default
/// GLTF attributes are added when present. The import services copy attribute
/// metadata and retain source blobs that reference the document's vertex bytes
/// without copying them. The caller may release its document reference after
/// this function succeeds, even if processing is still pending.
///
/// Conversion failures return a default result, except for a null document,
/// which returns RADIENT_STATUS_INVALID_ARGUMENT. Mesh-data creation failures
/// return their status with no handle, count, or bounds; rejected mesh data is
/// reported as RADIENT_STATUS_INVALID_DATA.
MeshVertexDataResult CreateMeshVertexData(IRadientMeshImportServices&                  MeshImportServices,
                                          const GLTF::TinyGltfModelView&               GltfModel,
                                          const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                          const std::shared_ptr<const GLTF::Document>& pDocument);

/// Creates reusable Radient index data for a GLTF primitive.
///
/// If the primitive has an index accessor, it must use a supported tightly
/// packed unsigned index type. If the primitive is not indexed, sequential
/// Uint32 indices are generated directly in an owning data blob for VertexCount
/// vertices. Accessor data is referenced without copying through a blob that
/// retains the document. The import services retain the blob with read access;
/// the caller may release its document reference after this function succeeds,
/// even if processing is still pending.
///
/// Conversion failures return a default result, except for a null document,
/// which returns RADIENT_STATUS_INVALID_ARGUMENT. Mesh-data creation failures
/// return their status with no handle or count; rejected mesh data is reported
/// as RADIENT_STATUS_INVALID_DATA.
MeshIndexDataResult CreateMeshIndexData(IRadientMeshImportServices&                  MeshImportServices,
                                        const GLTF::TinyGltfModelView&               GltfModel,
                                        const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                        const std::shared_ptr<const GLTF::Document>& pDocument,
                                        Uint32                                       VertexCount);

/// Converts GLTF scene metadata and immutable animation and skin resources.
/// pAssetManager is required when GLTFModel contains animations or skins.
/// Malformed or unsupported animation channels and clips are logged and
/// skipped; they do not fail conversion of an otherwise usable scene.
RADIENT_STATUS ExtractSceneGraph(const GLTF::Model&               GLTFModel,
                                 RadientImport::ImportedDocument& Scene,
                                 IRadientAssetManager*            pAssetManager = nullptr);

} // namespace RadientGLTFConverter

} // namespace Diligent
