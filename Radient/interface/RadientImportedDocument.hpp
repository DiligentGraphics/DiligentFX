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

/// \file
/// Owning C++ representation of imported Radient scene assets.
/// Include this header explicitly; it is not part of the C-compatible Radient.h.

#include "RadientAnimation.h"
#include "RadientAssets.h"
#include "RadientMaterials.h"
#include "RadientScene.h"
#include "RadientSkinning.h"
#include "../../../DiligentCore/Common/interface/RefCntAutoPtr.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

/// Format-independent scene data produced by asset importers.
namespace RadientImport
{

/// Owning array of strong references to imported texture assets.
using TextureAssetList = std::vector<RefCntAutoPtr<IRadientTextureAsset>>;

/// Owning array of strong references to imported material assets.
using MaterialAssetList = std::vector<RefCntAutoPtr<IRadientMaterialAsset>>;

/// Owning array of strong references to imported mesh assets.
using MeshAssetList = std::vector<RefCntAutoPtr<IRadientMeshAsset>>;

/// Owning array of strong references to imported skin assets.
using SkinAssetList = std::vector<RefCntAutoPtr<IRadientSkinAsset>>;

/// Marks the absence of a skin reference in an imported node or animation mapping.
static constexpr Uint32 InvalidImportedSkinIndex = ~Uint32{0};

/// Marks the absence of an animation-addressable light identifier in an imported node.
static constexpr Uint32 InvalidImportedLightIndex = ~Uint32{0};

/// One node in an imported document's hierarchy.
///
/// Its position in ImportedDocument::Nodes is its document-local node index.
/// Instantiation creates an entity for each occurrence of the node in the
/// selected scene hierarchy. The node owns its metadata and retains its mesh.
struct ImportedNode
{
    /// Owned entity name. An empty string selects a generated name during
    /// instantiation. Names need not be unique and do not identify animation targets.
    std::string Name;

    /// Local transform relative to the parent node, or to the scene instance's
    /// root entity for a root node. The default transform is identity.
    RadientTransform Transform{};

    /// Node-local visibility. Effective visibility also depends on every
    /// ancestor in the instantiated scene hierarchy.
    Bool Visible = True;

    /// Strong reference to the mesh attached to this node. A null reference
    /// creates no mesh component. Multiple nodes may reference the same mesh.
    RefCntAutoPtr<IRadientMeshAsset> pMesh;

    /// Owned node-specific morph weights, in the mesh's morph-target order.
    /// An empty array uses the mesh defaults. When the array and target counts
    /// differ, instantiation applies their common prefix, retains defaults for
    /// remaining targets, and ignores excess weights. Weights are ignored when
    /// the node has no mesh or the mesh has no morph targets.
    std::vector<Float32> MorphWeights;

    /// Zero-based index in ImportedDocument::Skins, or InvalidImportedSkinIndex
    /// when the node does not use skinning. Skinning applies to pMesh. Nodes that
    /// use the same skin share a skeleton pose within one scene instantiation;
    /// separate scene instantiations create independent poses.
    Uint32 SkinIndex = InvalidImportedSkinIndex;

    /// Document-local identifier used by light animation targets, or
    /// InvalidImportedLightIndex when the light has no animation identifier.
    /// A RadientLightAnimationSchemaID target's Object matches this value.
    /// This is not a node index or an index into an asset array. Multiple nodes
    /// may use the same identifier; animation then updates each instantiated
    /// light with that identifier. The identifier is ignored when Light is empty.
    Uint32 LightIndex = InvalidImportedLightIndex;

    /// Optional camera component copied onto the instantiated entity.
    /// An empty optional creates no camera component.
    std::optional<RadientCameraComponent> Camera;

    /// Optional light component copied onto the instantiated entity.
    /// An empty optional creates no light component.
    std::optional<RadientLightComponent> Light;

    /// Owned array of zero-based indices in ImportedDocument::Nodes, in child
    /// instantiation order. Each child is instantiated under this node's entity.
    /// An empty array identifies a leaf. The hierarchy contains no cycles.
    std::vector<Uint32> Children;
};

/// One selectable scene in an imported document.
///
/// Scenes refer to the document's shared node array and own their names and
/// root-index arrays. A document may provide several scenes using the same nodes.
struct ImportedScene
{
    /// Owned descriptive scene name. An empty name is permitted.
    std::string Name;

    /// Owned array of zero-based indices in ImportedDocument::Nodes, in root
    /// instantiation order. These nodes are attached to the scene instance's
    /// root entity. An empty array describes an empty scene.
    std::vector<Uint32> RootNodes;
};

/// Connects an imported animation's targets to one imported skin's skeleton.
///
/// The mapping owns its descriptor array. It is applied to the skeleton pose
/// created for the selected skin when that skin is used in a scene instance.
struct ImportedAnimationSkinMapping
{
    /// Zero-based index in ImportedDocument::Skins. The default sentinel denotes
    /// an unset mapping; a usable mapping identifies an existing skin asset.
    Uint32 SkinIndex = InvalidImportedSkinIndex;

    /// Owned target-to-joint mappings for the containing ImportedAnimation::pClip.
    /// ClipTargetIndex indexes that clip's target array. DestinationElement is
    /// the zero-based joint index in the skeleton referenced by Skins[SkinIndex].
    /// These indices are independent of node indices in ImportedDocument::Nodes.
    std::vector<RadientAnimationDestinationMappingDesc> JointMappings;
};

/// An animation clip and the mappings needed to bind it to scene instances.
///
/// The record retains the clip and owns its skin mappings. Instantiation with
/// an animation registry creates bindings to the instantiated entities, morph
/// weights, and skeleton poses; the clip itself is shared between instances.
struct ImportedAnimation
{
    /// Strong reference to the imported clip. Null clips are skipped during
    /// instantiation. Target Object values follow these document conventions:
    /// - RadientNodeAnimationSchemaID and RadientMorphWeightsAnimationSchemaID
    ///   use zero-based indices in ImportedDocument::Nodes.
    /// - RadientLightAnimationSchemaID uses ImportedNode::LightIndex identifiers.
    /// SkinMappings additionally connects clip targets to skeleton joints.
    /// Target Object values are not runtime entity IDs. Automatic scene binding
    /// handles node transforms and visibility, light properties, and morph weights.
    RefCntAutoPtr<IRadientAnimationClipAsset> pClip;

    /// Owned mappings for skins affected by pClip. Unaffected skins need no
    /// entry. A mapping only creates a binding when its skin is instantiated
    /// in the selected scene.
    std::vector<ImportedAnimationSkinMapping> SkinMappings;
};

/// Radient-native scene data produced by an asset importer.
///
/// The document owns every string, array, and component value stored in its
/// records and holds strong references to its assets. Copying duplicates the
/// metadata and retains the same assets; it does not clone asset data. Moving
/// transfers the owned metadata and asset references. Scene-asset storage takes
/// ownership of a completed document by moving it, so the importer's document
/// object does not need to remain alive. The records have no source-format
/// fields; retained assets may independently retain source data while loading.
///
/// Node, scene, and skin indices are zero-based positions in the corresponding
/// arrays. Reordering an array requires updating all indices that refer to it,
/// including animation targets and skin mappings. Light identifiers instead
/// match ImportedNode::LightIndex and do not index an array in this document.
struct ImportedDocument
{
    /// Strong references to imported textures. Materials reference the textures
    /// they use independently; node and animation indices do not index this array.
    /// Null entries are permitted. Texture load failures do not independently
    /// determine scene readiness; material dependencies account for texture use.
    TextureAssetList Textures;

    /// Strong references to imported materials. Meshes carry their primitives'
    /// material references; this array also records the scene's material load
    /// and GPU-resource dependencies. Null entries are permitted and do not
    /// contribute to those dependencies.
    MaterialAssetList Materials;

    /// Strong references to imported meshes. Nodes select meshes through pMesh;
    /// this array also records the scene's mesh load and GPU-resource dependencies.
    /// Null entries are permitted and do not contribute to those dependencies.
    MeshAssetList Meshes;

    /// Strong references to imported skins, indexed by ImportedNode::SkinIndex
    /// and ImportedAnimationSkinMapping::SkinIndex. Each skin retains its skeleton.
    /// Null entries are permitted; nodes referencing them instantiate without skinning.
    SkinAssetList Skins;

    /// Owned node records shared by all scenes. Children, scene roots, and node
    /// and morph animation targets identify nodes by their positions in this array.
    std::vector<ImportedNode> Nodes;

    /// Owned scene records. Scene selection and DefaultSceneId index this array.
    /// A document with no scenes instantiates an empty hierarchy when the default
    /// scene is requested.
    std::vector<ImportedScene> Scenes;

    /// Owned animation records available to scene instances. Only destinations
    /// present in the selected scene are bound during instantiation.
    std::vector<ImportedAnimation> Animations;

    /// Zero-based index in Scenes selected when no explicit scene is requested.
    /// An out-of-range value falls back to scene zero. If Scenes is empty, the
    /// default scene has no nodes to instantiate.
    Uint32 DefaultSceneId = 0;
};

} // namespace RadientImport

} // namespace Diligent
