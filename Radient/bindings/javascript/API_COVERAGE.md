# JavaScript API execution coverage

Every suite runs against real Radient in Node and a browser, without a GPU.
The model and generator tests separately check declarations and metadata.

| Suite | Executed API |
| --- | --- |
| `data_blob_tests.mjs` | Owning factories; size, scoped reads/writes, resize, QueryInterface and disposal |
| `scene_tests.mjs` | Engine/scene/writer creation; hierarchy, names, flags, visibility, transforms, revisions and commit |
| `components_tests.mjs` | Camera/light getters/setters; component presence and removal |
| `mesh_tests.mjs` | Asset manager access/description; asset identity/type/status; mesh creation/description; cube/sphere helpers; vertex-size helpers; scene mesh and renderer getters/setters |
| `texture_tests.mjs` | LoadTexture from encoded bytes and supplied mips; texture description; load status, validation and blob lifetime |
| `material_tests.mjs` | Standard definitions, all parameter reflection/lookup methods, material creation/definition/version/texture access, surface queries, scene material bindings |
| `material_edit_tests.mjs` | Material parameter reads, reusable writers, SetParameter/SetTexture/Commit, surface writers and standard texture-parameter helper |
| `skinning_tests.mjs` | Skeleton/skin creation and descriptions; every pose and pose-writer method, native-sized matrix outputs, scene skin component |
| `morph_tests.mjs` | Blob-backed target creation/reflection; every weights method; mesh weight factory; scene morph component and removal |
| `animation_tests.mjs` | Clip creation/reflection, binding creation/evaluation, every registry method, scene/material/pose/morph destinations, quaternion helpers |

Native callbacks, externally supplied thread pools, importer implementations,
and GPU/backend objects are not exposed. Asset-manager `Stop()` takes a native device context and remains outside the
CPU-only binding subset; disposing the engine performs normal shutdown.

Direct-write animation destination scopes expose native addresses and are not exported. Use clip bindings and `Evaluate()`; native destinations are obtained through `QueryInterface` and their direct-write scopes are used internally.
