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
#include "RadientSceneImporter.h"

#include <memory>

namespace Diligent
{

struct IRadientSceneWriter;
class RadientAssetManagerImpl;
class RadientMeshIndexSource;
class RadientMeshVertexSource;

namespace GLTF
{

class Document;
struct Model;
struct TinyGltfModelView;
struct TinyGltfPrimitiveView;

} // namespace GLTF

namespace RadientGLTFConverter
{

struct MeshVertexSourceResult
{
    /// Conversion status. On failure, the whole result remains default-initialized.
    RADIENT_STATUS Status = RADIENT_STATUS_INVALID_ARGUMENT;

    /// CPU vertex source created from the GLTF primitive.
    std::unique_ptr<RadientMeshVertexSource> pSource;

    /// Primitive bounds computed from the POSITION accessor.
    float3 BBMin{};
    float3 BBMax{};
};

struct MeshIndexSourceResult
{
    /// Conversion status. On failure, the whole result remains default-initialized.
    RADIENT_STATUS Status = RADIENT_STATUS_INVALID_ARGUMENT;

    /// CPU index source created from the GLTF primitive.
    std::unique_ptr<RadientMeshIndexSource> pSource;
};

/// Creates a Radient vertex source for a GLTF primitive.
///
/// The primitive must have a valid POSITION accessor. Other supported default
/// GLTF attributes are added when present. The returned source borrows GLTF
/// buffer spans, but keeps \p pDocument alive internally, so the caller may
/// release its document reference after this function succeeds.
///
/// Returns a default MeshVertexSourceResult on failure.
MeshVertexSourceResult CreateMeshVertexSource(const GLTF::TinyGltfModelView&               GltfModel,
                                              const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                              const std::shared_ptr<const GLTF::Document>& pDocument);

/// Creates a Radient index source for a GLTF primitive.
///
/// If the primitive has an index accessor, it must use a supported tightly
/// packed unsigned index type. If the primitive is not indexed, sequential
/// Uint32 indices are generated for \p VertexCount vertices. The returned
/// source borrows GLTF buffer spans or generated index storage, but keeps
/// \p pDocument alive internally, so the caller may release its document
/// reference after this function succeeds.
///
/// Returns a default MeshIndexSourceResult on failure.
MeshIndexSourceResult CreateMeshIndexSource(const GLTF::TinyGltfModelView&               GltfModel,
                                            const GLTF::TinyGltfPrimitiveView&           GltfPrimitive,
                                            const std::shared_ptr<const GLTF::Document>& pDocument,
                                            Uint32                                       VertexCount);

RADIENT_STATUS InstantiateSceneGraph(const GLTF::Model&       GLTFModel,
                                     IRadientSceneAsset*      pModel,
                                     Uint32                   SceneIndex,
                                     RadientAssetManagerImpl& AssetManager,
                                     IRadientSceneWriter&     Writer,
                                     RadientEntityID          RootEntity);

} // namespace RadientGLTFConverter

} // namespace Diligent
