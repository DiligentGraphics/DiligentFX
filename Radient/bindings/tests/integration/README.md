# Shared Radient binding integration tests

The Java and JavaScript tests exercise the real Radient implementation. Keep the
reconciled suites aligned by case name, input values, assertions, failure
statuses, and ownership checks. Language-specific setup and API calls remain in
their respective test files; test operations are not moved behind native helpers.

| Java | JavaScript | Shared cases |
| --- | --- | --- |
| `ExportsTest.java` | `exports_tests.mjs` | 3: invalid IDs and integer types, interface-ID fields and constant isolation, morph-target limit |
| `RendererTest.java` | `renderer_tests.mjs` | 3: CPU renderer/view descriptors and retained references, all effect settings/defaults, frame calls and invalid arguments |
| `DataBlobTest.java` | `data_blob_tests.mjs` | 11: owning copies, zero initialization, empty scopes, API exclusions, invalid ranges, native size rejection, read-only queries, shared access state, resize, retained scopes, cleanup after exceptions |
| `SceneTest.java` | `scene_tests.mjs` | 6: resource configuration, names/defaults, hierarchy, transforms/reparenting, visibility/flags, retained scenes |
| `ComponentsTest.java` | `components_tests.mjs` | 3: all camera fields, all light fields, defaults/validation/removal |
| `MeshTest.java` | `mesh_tests.mjs` | 3: blob/layout ownership and reflection, retained scene meshes and 64-bit masks, procedural meshes and validation |
| `TextureTest.java` | `texture_tests.mjs` | 4: supplied mip chains and blob ownership, mip generation, encoded images, invalid inputs/active writers |
| `MaterialTest.java` | `material_tests.mjs` | 2: parameter reflection/defaults, definition/material/scene binding ownership |
| `MaterialEditTest.java` | `material_edit_tests.mjs` | 2: copied writer bytes and surface changes, texture initialization and edit restrictions |
| `AnimationTest.java` | `animation_tests.mjs` | 4: sampler copies and scene evaluation, registry removal/retention, quaternion outputs, material/pose/morph destinations |
| `MorphTest.java` | `morph_tests.mjs` | 1: delta blobs and offsets, mutable weight snapshots/versioning, validation/defaults, scene-component lifetimes |
| `SkinningTest.java` | `skinning_tests.mjs` | 2: descriptor copies and pose arrays, deferred commits/versioning, skinning/transpose/defaults/validation, retained scene skin/pose |
| `NetworkTest.java` | `network_tests.mjs` | 4: factory validation, default routing, retained explicit resolver, disabled redirects |
| `ImportTest.java` | `import_tests.mjs` | 3: GLB animation and instantiation, OBJ with relative MTL, unsupported importer outputs |

[`fixtures.properties`](fixtures.properties) supplies the byte patterns, UTF-8
names, transform inputs and expected matrix, resource configuration, and component
values to both implementations, along with mesh, texture, material, animation,
morph, skinning, import, renderer, and constant expectations. It uses plain UTF-8
`key=value` lines and comma-separated numeric lists, without escapes or continuations. Java reads it
with the JDK's `Properties.load(Reader)`; the JS helper reads the same subset.
Every array/descriptor accessor returns a fresh value so mutation tests do not
change later tests' inputs or expectations. No generated fixture copies or JSON
library dependency are needed.

Java's CTest command passes the file through `radient.test.fixtures`. The Radient
Node runner loads it directly. Browser CTest commands use `--test-data` to pass
the identical text to the suite. Missing fixtures fail the test rather than use
fallback values.

The 51 Java cases match their JavaScript counterparts. `AssetFixture.java` and
`asset_test_helpers.mjs` create the same triangle, retain returned interfaces,
and poll CPU load status for up to 15 seconds, accepting both `OK` and `PENDING`
when work is submitted. These tests create no GPU and never wait for uploads.

The shared files in [`assets/`](assets/) preserve the original JavaScript data:

- `animated.glb`: one non-indexed triangle, one node named `Animated triangle 世界`,
  and a `Move` translation animation from X=0 to X=2 over one second. The binary
  chunk contains 36 bytes of positions, 8 bytes of times, and 24 bytes of translations.
- `obj/triangle.obj` and `obj/triangle.mtl`: one triangle referencing a sibling MTL
  with diffuse color `(0.1, 0.8, 0.2)`, testing relative dependency resolution.
- `colors.tga`: an uncompressed 2×1, 24-bit, top-left-origin image with red and
  green BGR pixels, testing the production encoded-image decoder.

Java imports the files directly from this directory. JS suites declare
`assetFiles`; the Node runner reads them here, while the browser runner serves
only the directory specified by `--test-assets`. Both preload the same bytes at
`/assets/` before Wasm initialization, preserving the relative OBJ/MTL paths.
Encoded-image tests pass the shared TGA bytes through owning data blobs in both
languages. Missing files fail initialization before Wasm workers are started.

Both CMake test runners discover suites automatically, so the existing Linux
Clang/Debug Java CI step runs the new cases without a separate workflow entry.

Intentional language-specific coverage:

- JavaScript also tests active byte scopes across Wasm memory growth (one extra
  case), typed-array subviews, BigInt/Number mismatches, omitted arguments/fields,
  and dynamic rejection of native-only fields.
- Java has statically typed descriptors and no byte-array subview. It selects the
  same bytes into an input array and mutates that array after creation. Reflection
  checks that native-only fields and release methods are absent. Native `Uint64`
  values use Java `long` bits; negative values in widened `Uint32` fields are
  rejected, while `-1L` in `Uint64` means `UINT64_MAX`.
- Java explicitly passes the native `KeepWorldTransform=true` and skinning
  `TransposeMatrices=false`/`UpdateGlobalMatrices=true` defaults; JS also
  tests omission of these arguments. Java checks native-initialized descriptor
  constructors before the same values round-trip through the scene.
- Java exposes scalar constants as static final fields and aggregate constants as
  functions returning independent mutable snapshots. JS also checks its module
  exports, frozen descriptors, and copied constant-array accessors. Both read the
  same expected interface-ID fields and verify that mutations cannot change the
  exported constant.
- Java uses try-with-resources for the processing-failure case; JS uses `finally`.
  Both verify that write access was released and the written byte remains visible.

The original Java checks for retained base interfaces, mutable empty reads and
growth, a retained writer copying byte 255, visibility restoration, and removal
of edited components are retained and also exercised in JavaScript.

Animation clips share their times, sample values, schema/property names from the
API, and expected results. Java mutates the actual submitted `byte[]` after clip
creation, matching JS's mutation of the float view over those bytes. Morph tests
share weights and delta values; skinning tests share joint transforms and the
inverse-bind matrix. The helpers only construct descriptors and invoke public
bindings; evaluation, deferred propagation, and lifetime assertions stay in Java/JS.

All Radient CPU API integration suites now have Java counterparts. The renderer
suite creates no GPU or attachments: it checks the zero-sized target, all eight
effect setters, retained snapshots, frame status, and rejection of a null view.
Java uses native-initialized descriptors where JS passes partial objects, with
independent shared expectations for the untouched AgX defaults.

Browser initialization, Wasm runtime behavior, and browser golden-image rendering
remain JavaScript-specific. These ports do not repeat the native GPU render suite.

Network suites reuse the HTTP handler and generated glTF/GLB, vertex buffer, and
texture fixtures in `Tests/RadientCommon/python/network_server.py`, also used by
the native CPU tests in Chrome. `run_network_tests.py` starts a loopback server on
an ephemeral port, runs the selected Java/Node/browser command, and stops the
server on completion or failure. CTest starts it automatically; no external
server or internet access is needed. The browser runner uses the production
Radient Wasm module without JSPI/Asyncify. Imports, instantiation, and texture
inspection are performed through the language API, not native test adapters.
