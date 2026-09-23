# Radient

C++ applications can use the [descriptor wrappers](docs/CppWrappers.md) in
`RadientTypesX.hpp` to build vertex layouts and texture load information with
owned metadata and retained data blobs.

The [mesh import services](docs/MeshData.md) in `RadientMeshImportServices.h`
support importer implementations that create shared vertex, index, and morph
target data and assemble mesh views with multiple geometries and materials.
This C-compatible header is included explicitly and is not part of `Radient.h`.

The [imported document types](interface/RadientImportedDocument.hpp) in
`RadientImportedDocument.hpp` describe assets, scene hierarchies, skins, and
animations using Radient types. Their C++ containers own the metadata and
retain referenced assets. Include this header explicitly from C++ code.
