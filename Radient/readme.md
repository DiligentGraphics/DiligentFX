# Radient

C++ applications can use the [descriptor wrappers](docs/CppWrappers.md) in
`RadientTypesX.hpp` to build vertex layouts and texture load information with
owned metadata and retained data blobs.

The [imported document types](interface/RadientImportedDocument.hpp) in
`RadientImportedDocument.hpp` describe assets, scene hierarchies, skins, and
animations using Radient types. Their C++ containers own the metadata and
retain referenced assets. Include this header explicitly from C++ code.
