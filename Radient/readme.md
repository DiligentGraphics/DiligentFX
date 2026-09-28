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
