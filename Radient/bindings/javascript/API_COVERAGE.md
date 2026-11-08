# JavaScript API execution coverage

Every suite runs against real Radient in Node and a browser, without a GPU.
The model and generator tests separately check declarations and metadata.

| Suite | Executed API |
| --- | --- |
| `data_blob_tests.mjs` | Owning factories; size, scoped reads/writes, resize, QueryInterface and disposal |
| `scene_tests.mjs` | Engine/scene/writer creation; hierarchy, names, flags, visibility, transforms, revisions and commit |
| `components_tests.mjs` | Camera/light getters/setters; component presence and removal |
| `mesh_tests.mjs` | Asset manager access/description; asset identity/type/status; mesh creation/description; cube/sphere helpers; vertex-size helpers; scene mesh and renderer getters/setters |

Native callbacks, externally supplied thread pools, importer implementations,
and GPU/backend objects are not exposed. Asset-manager `Stop()` and its native
task output remain native; disposing the engine performs normal shutdown.
