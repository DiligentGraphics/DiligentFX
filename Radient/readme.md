# Radient

C++ applications can use the [descriptor wrappers](docs/CppWrappers.md) in
`RadientTypesX.hpp` to build vertex layouts and texture load information with
owned metadata and retained data blobs.

C++ [scene asset importers](docs/SceneAssetImporters.md) can register support for
additional formats and produce `RadientImport::ImportedDocument`. Register an
importer with `IRadientAssetManager::RegisterSceneAssetImporter`; the built-in
GLTF and [OBJ](docs/OBJImporter.md) importers participate in the same selection process. Import calls receive
the asset manager, resolver, source data, default material, and mesh import
services in their context. Include `RadientSceneAssetImporter.hpp` explicitly to
implement importers.

The [mesh import services](docs/MeshData.md) in `RadientMeshImportServices.h`
support importer implementations that create shared vertex, index, and morph
target data and assemble mesh views with multiple geometries and materials.
This C-compatible header is included explicitly and is not part of `Radient.h`.

The [imported document types](interface/RadientImportedDocument.hpp) in
`RadientImportedDocument.hpp` describe assets, scene hierarchies, skins, and
animations using Radient types. Their C++ containers own the metadata and
retain referenced assets. Include this header explicitly from C++ code.

## Animation property names

Animation channels and destination bindings identify properties with nonempty,
case-sensitive strings within their animation schema. Use the built-in constants
from `RadientAnimation.h`, such as `RadientNodeTranslationPropertyName`
(`"Translation"`) and `RadientMorphWeightsPropertyName` (`"Weights"`). Custom
schemas can define their own property names. Names are compared by text, so
separate strings with identical contents identify the same property.

## Material parameter updates

Use `IRadientMaterialWriter` to change non-texture material parameters at runtime,
including base color, roughness, and texture-coordinate transforms.
`IRadientSurfaceMaterialWriter::SetAlphaCutoff()` also supports runtime updates.
Commit changes on the render thread before rendering a frame. A successful commit
updates material getters immediately, and the next frame uses the new values
without reloading the material or changing the scene.

Texture assignments, surface mode, and double-sided state must be configured
before the material's load status, GPU resource status, or render view is first
queried. After that, a commit that changes any of these properties returns
`RADIENT_STATUS_INVALID_OPERATION` and applies none of its assignments.

Material assets also expose `IRadientAnimationDestination`. Use
`RadientMaterialAnimationSchemaID` with the parameter's exact, case-sensitive name
from the material definition, such as `RadientStandardMaterialBaseColorFactorName`.
Reflection supplies the value type and array size for both standard and custom
parameters. Non-texture parameters support animation, including array ranges;
Boolean and integer values use STEP interpolation. Surface alpha cutoff uses
`RadientSurfaceMaterialAnimationSchemaID` and
`RadientSurfaceMaterialAlphaCutoffPropertyName`.

Map each clip target to destination element zero, then evaluate the binding on the
render thread before rendering. Property names are resolved when the binding is
created. Animation updates follow the same runtime rules as writer commits.

The GLTF importer supports `KHR_animation_pointer` for the corresponding material
factors, alpha cutoff, and `KHR_texture_transform` offset, scale, and rotation.
Texture rotations are scalar angles in radians: interpolation preserves the
authored angles, including complete turns, without selecting the shortest arc.
Emissive-strength animation is not yet supported. Scene
instantiation with an animation registry creates material bindings automatically.
Materials remain shared between scene instances: evaluating a material binding
affects every instance that uses that asset. Independent playback requires
distinct material assets.

## Asset resolvers

Asset resolvers locate assets and provide their bytes. Leaving
`RadientEngineCreateInfo::Assets.pAssetResolver` null uses the default filesystem
resolver. Network requests require an explicitly supplied network resolver;
automatic routing between filesystem and network resources is not implemented yet.

To configure resolution explicitly, create a resolver with
`CreateRadientFilesystemAssetResolver` or `CreateRadientNetworkAssetResolver`,
then assign it to `RadientEngineCreateInfo::Assets.pAssetResolver` before calling
`CreateRadientEngine`. When creating an asset manager directly, use
`RadientAssetManagerCreateInfo::pAssetResolver`. The engine retains the supplied
resolver, which receives requests for all assets and their dependencies.

The filesystem factory accepts an optional `BaseDirectory` for relative requests
without `BaseURI`. A supplied directory is captured as an absolute path at
creation time; it does not restrict access to that directory. With no directory,
relative requests use the working directory. On the Web, filesystem paths refer
to the virtual filesystem.

The network resolver accepts HTTP/HTTPS URLs and relative URL references with an
HTTP/HTTPS `BaseURI`. The network factory accepts `ConnectTimeoutMs`,
`RequestTimeoutMs`, and `MaxRedirects`. Connection and network-transfer timeouts
default to 10 and 60 seconds. Stopping an asset manager does not interrupt an
active transfer. With the curl backend, cleanup may wait for an outstanding OS
DNS lookup even after a timeout. Consequently, `CheckAsset`, `OpenAsset`, and
shutdown waits can exceed both configured timeouts; they are not strict
return-time guarantees. HTTPS verifies server certificates and hostnames.

On Apple platforms, `ConnectTimeoutMs` instead limits inactivity, including waits
for response data; receiving data restarts this timeout. `RequestTimeoutMs` still
bounds the entire request. Apple's networking policies and compressed-response
validation apply.

For custom storage, implement `IRadientAssetResolver`; its methods must support
concurrent calls. `ResolveAssetLocation` produces an identity without loading
contents, `CheckAsset` checks availability, and `OpenAsset` returns asset data.
The built-in network resolver checks availability with HTTP HEAD, so servers
must support HEAD to use `CheckAsset`.

When resolving dependencies, supply the dependency reference in `URI` and the
referring asset's `IRadientAssetData::GetResolvedURI()` in `BaseURI`. The base is
the referring asset's full identity, not just its directory. This also resolves
dependencies relative to the final location after a redirect. Built-in importers
pass this context automatically; custom resolvers define how to interpret it.

The browser network transport is not implemented yet;
`CreateRadientNetworkAssetResolver` returns `RADIENT_STATUS_UNSUPPORTED` on the
Web. See [RadientAssetResolver.h](interface/RadientAssetResolver.h) for the
complete resolver contract and failure statuses.

For source-build prerequisites and offline configuration, see
[Building Radient](docs/Building.md).
