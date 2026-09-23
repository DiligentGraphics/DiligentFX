# Scene asset importers

Applications can register C++ importers for additional scene and model formats.
An importer converts its source into `RadientImport::ImportedDocument`, the same
format-independent representation used by the built-in GLTF importer. Radient
then loads its asset dependencies and instantiates scenes from that document.

Include `RadientSceneAssetImporter.hpp` explicitly to implement an importer.
It exposes the importer and import context interfaces and includes the
imported-document and mesh-import service declarations. This C++ header is not
included by `Radient.h`. Registration is declared in the C-compatible
`RadientAssets.h` and accepts an opaque importer pointer.

## Register and select an importer

Implement `IRadientSceneAssetImporter` and register it directly with the asset
manager:

```cpp
RADIENT_STATUS Status = pAssetManager->RegisterSceneAssetImporter(pObjImporter);
if (RADIENT_FAILED(Status))
    return Status;

RadientSceneLoadInfo LoadInfo;
LoadInfo.URI = "models/scene.obj";

RefCntAutoPtr<IRadientSceneAsset> pScene;
Status = pAssetManager->LoadScene(LoadInfo, &pScene);
```

`GetIdentifier()` returns the importer's immutable, nonempty, case-sensitive
identifier, such as `"obj"`. Its string remains valid and unchanged for the
importer's lifetime. Registration copies the identifier and retains a strong
reference to the importer until the asset manager is destroyed. A null
importer, an empty identifier, or an already registered identifier returns
`RADIENT_STATUS_INVALID_ARGUMENT`.

With the default `Format = RADIENT_SCENE_FORMAT_AUTO`, `LoadScene` calls
`CanImport(URI)` in registration order. This receives the original requested URI,
before the asset resolver opens it. Keep it a quick format-selection predicate,
such as checking a file extension. If several importers match, Radient logs an
informational message and uses the first. The built-in importer has identifier
`"gltf"` and is registered first.

Importer callbacks run without the manager's registration lock held. Automatic
selection uses a fixed snapshot of registered importers, so later registrations
do not affect that selection. `Import` also runs without the registration lock held.

To select a particular importer, set `LoadInfo.ImporterId = "obj"`. A nonempty
identifier selects that exact registered importer and overrides `Format`;
`CanImport` is not consulted. A null or empty identifier uses `Format` instead.
`RADIENT_SCENE_FORMAT_GLTF` continues to select the built-in `"gltf"` importer.

The selected importer is captured during `LoadScene`. Later registrations do
not change that request. If the selected importer fails, loading reports its
failure; Radient does not retry the source with another importer.

## Implement the import

`Import(const RadientSceneAssetImportContext&, RadientImport::ImportedDocument&)`
runs as an asset-loading task, either on a worker or a thread calling
`WaitForAssetLoad`. It has no guaranteed thread affinity. Its context provides:

- `pSourceData`: the opened source bytes and their canonical URI. Read the URI
  with `GetResolvedURI()` and the bytes with `GetData()` and `GetSize()`.
- `pAssetResolver`: resolves and opens related files, such as external images or
  material files. Use the source's canonical URI when resolving relative paths.
- `pAssetManager`: creates materials, textures, meshes, skins, and animations.
- `pMeshImportServices`: creates reusable geometry data and mesh views; see
  [mesh import services](MeshData.md).
- `pDefaultMaterial`: the material to use where the source provides none.

The context pointers are borrowed and valid throughout the call. The registered
importer must not retain the asset manager or mesh-import service: the manager
already owns the importer, so retaining either would create an ownership cycle.

Build the complete document before returning `RADIENT_STATUS_OK`. Parsing and
metadata construction finish during the call, but assets submitted through the
context may still be pending. Record the imported assets in the document's
corresponding arrays so Radient can track their dependencies. Return a failure
status if the source cannot be imported; do not return `RADIENT_STATUS_PENDING`
to continue constructing the document later.

For example, after creating a mesh through the context, an importer can expose
one node in one scene as follows:

```cpp
Document.Meshes.push_back(pMesh);

RadientImport::ImportedNode Node;
Node.Name  = "Model";
Node.pMesh = pMesh;
Document.Nodes.push_back(std::move(Node));

RadientImport::ImportedScene Scene;
Scene.Name = "Scene";
Scene.RootNodes.push_back(0);
Document.Scenes.push_back(std::move(Scene));
Document.DefaultSceneId = 0;

return RADIENT_STATUS_OK;
```

The document owns its metadata and retains its assets. Node indices, scene root
indices, and animation targets refer to the document's arrays; see
[`RadientImportedDocument.hpp`](../interface/RadientImportedDocument.hpp) for
their conventions.

When an asset references source bytes without copying them, retain the blob or
the owner of those bytes for as long as the asset needs them. For example, a
reference-mode blob can retain `pSourceData` or a parsed-document owner through
its callback context. The import context itself does not extend that lifetime
beyond `Import`.

An importer may receive concurrent calls. Keep per-import parsing state local
to the call and synchronize any shared mutable state. `GetIdentifier()` and
`CanImport()` must also be safe to call concurrently.

## Use the loaded scene

`LoadScene` returns the scene asset through the usual asynchronous asset API.
Call `IRadientAssetManager::WaitForAssetLoad` to wait for imported metadata and
its asset dependencies, then instantiate the desired scene normally. GPU
uploads follow the existing asset upload pipeline.
