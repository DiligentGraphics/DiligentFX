# Radient JavaScript bindings

The bindings expose Radient APIs for scenes, components, mesh and texture
assets, material creation and editing, skeletons, skins, morph targets, animation,
and GLTF/OBJ scene import. They use the real Radient implementation compiled to
Wasm and run in browsers or Node without a graphics device. Owning read-only and
mutable data blobs can also be created without an engine. Renderer and view
bindings also support browser WebGPU through the canvas adapter below. See
[API execution coverage](tests/API_COVERAGE.md) for the tested methods and exclusions.

## Build and test

Install the LLVM 22 Python requirements described in
[the generator README](../../../BuildTools/Bindings/README.md), then configure
the DiligentEngine project with the Emscripten toolchain and bindings enabled:

```sh
emcmake cmake -S . -B build/web -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DRADIENT_GENERATE_BINDINGS=ON \
    -DRADIENT_BINDINGS_PYTHON_EXECUTABLE=/path/to/bindings-env/bin/python \
    -DRADIENT_BINDINGS_LIBCLANG=/path/to/LLVM-22/lib/libclang.so
cmake --build build/web --target RadientWasm
ctest --test-dir build/web/DiligentFX/Radient/bindings \
    -C Debug -LE Browser --output-on-failure --no-tests=error
```

On Windows, use the environment's `Scripts/python.exe` and LLVM's
`bin/libclang.dll`. `RADIENT_BINDINGS_NODE_EXECUTABLE` can select Node explicitly.
Node 18 or newer is required.

`Radient-GenerateBindingModel` extracts the native API, and
`Radient-GenerateJavaScript` consumes that model with the JavaScript configuration
in `API.json`. `RadientWasm` compiles the generated wrapper and links the
existing `Radient` library. Build `RadientWasm` to generate and compile all
required artifacts, then run CTest separately for the model, generation, and Node
integration tests. CTest never generates or rebuilds artifacts. Use `ctest -N` to
list suites and `ctest -R <pattern>` to select them.

Binding tests are registered in the `DiligentFX/Radient/bindings` build directory,
so point `ctest --test-dir` there. Pass `-C Release` for a Release build. Native
builds and standalone configurations of `Radient/bindings` provide only model
generation and tests.

The generated files are under
`build/web/DiligentFX/Radient/bindings/javascript/generated/`.
Keep `Radient.mjs`, `RadientWasm.mjs`, `RadientWasm.wasm`, `runtime.mjs`, and
`byte_access.mjs` together when using the module. `Radient.d.mts` supplies
TypeScript declarations. Generated files are build artifacts, not source files.

The module uses Diligent's threaded Emscripten build. Browser deployments require
cross-origin isolation to make `SharedArrayBuffer` available.

### Reviewing API changes

The checked-in `api_surface.json` sits beside `API.json` and records the reviewed
binding contract. `API.json` supplies generation configuration. The snapshot
contains the selected native declarations and effective JavaScript binding
policies: signatures, descriptor fields and defaults,
inheritance, constants, typedefs, ownership, nullability, array counts, and adapter
contracts. It omits comments, source locations, and generated implementation details.
Native-only fields and methods replaced by scoped access are omitted; the scope's
acquire/release contract is recorded instead. The snapshot always uses Wasm32.

Binding generation writes the current surface to the build directory and compares
it with the reviewed snapshot. Additions, removals, and changes fail the build with
a diff, including APIs generated automatically. Production and browser render-test
bindings both require this check before their generated glue can be compiled.
Normal builds never update the checked-in snapshot. Unit tests separately verify
the checker's behavior.

For an intentional API change, let the build regenerate the current surface (it
will stop at the comparison). Add or update the corresponding tests, then explicitly
accept the generated surface by running this script from the DiligentEngine root:

```sh
python DiligentFX/BuildTools/Bindings/javascript/api_surface.py \
    --actual build/web/DiligentFX/Radient/bindings/javascript/generated/api_surface.json \
    --baseline DiligentFX/Radient/bindings/javascript/api_surface.json --update
```

Use your configured build directory in place of `build/web`. The script copies the
freshly generated contract; it does not parse headers or rebuild bindings. Review
and commit the snapshot diff together with the API and test changes, then resume
the build and run the binding tests. There is no CMake update target or option.
The snapshot requires acknowledgment of API changes; it does not prove that new
tests were added or that behavior changed without a declaration change is covered.

### Browser tests

After building `RadientWasm`, with Chrome, Chromium, or Edge installed, run:

```sh
ctest --test-dir build/web/DiligentFX/Radient/bindings \
    -C Debug -L Browser --output-on-failure --no-tests=error
```

The runner discovers the browser automatically. Set the environment variable
`BINDINGS_TEST_BROWSER_EXECUTABLE` when running CTest to select a specific
executable. This setting is shared with the generic binding tests and requires
no reconfiguration or rebuild. No browser automation package or download is needed.

The `Radient-JavaScript-Browser-*` CTest suites run the same cases as Node
against the generated production Wasm module. A temporary local server sends the
cross-origin isolation headers needed for shared memory. The test checks that isolation and shared memory are active.
Chrome runs headlessly with a temporary profile; the runner closes it after the
tests. Failed assertions, loading errors, and a 60-second timeout fail the test.
The runner also supports headless Edge for local Windows testing.

## Owning blobs

Both factories accept `{Size?: bigint, pData?: Uint8Array | null}` and return
`{status, value}`. `CreateRadientDataBlob` always selects COPY storage; its
JavaScript signature has no storage-mode argument. `CreateRadientMutableDataBlob`
creates owning mutable storage. Omitted sizes default to zero; an omitted or null
`pData` initializes the requested storage to zero. When supplied, the first `Size`
bytes of the array view are copied during creation.

```javascript
import loadRadient from "./generated/Radient.mjs";

const api = await loadRadient();
const {status, value: blob} = api.CreateRadientMutableDataBlob({Size: 4n});
if (blob === null) throw new Error(`Blob creation failed: ${status}`);

try {
    const result = blob.BeginWrite();
    if (result.value === null) throw new Error(`Write access failed: ${result.status}`);
    try {
        result.value.copyFrom(new Uint8Array([17, 128, 254, 42]));
    } finally {
        result.value.dispose();
    }

    console.log(blob.GetSize()); // 4n
} finally {
    blob.dispose();
}
```

`OnLastReaderReleased`, `OnDestroy`, and `pUserData` are native-only fields.
Supplying them in a JavaScript descriptor is an error, even with a null value.
They retain their null native defaults. REFERENCE storage is not exposed. The
native headers and language-neutral model still include all these capabilities.

## Access and ownership

`BeginRead()` and `BeginWrite()` return a status and a scoped access object, or
null on failure. Read access supports `readByte`, `copyTo`, and `toUint8Array`;
write access additionally supports `writeByte` and `copyFrom`. See the
[general byte-access contract](../../../BuildTools/Bindings/javascript/README.md#scoped-byte-access)
for the method signatures and bounds rules.

The scope retains the blob until `dispose()` ends access. Native memory is never
exposed as a JavaScript array view. Copies returned by `toUint8Array()` remain
usable after the scope ends. Access methods fail after disposal, and garbage
collection does not close scopes or release interface wrappers.

Multiple readers may coexist. Writes and `Resize(NewSize)` fail with
`RADIENT_STATUS_INVALID_OPERATION` while conflicting access is active. Native
statuses are returned unchanged; invalid JavaScript values throw before calling
the native API. Resizing preserves the retained prefix and zero-initializes added
bytes.

`QueryInterface(api.IRadientDataBlob)` and
`QueryInterface(api.IRadientMutableDataBlob)` return separately owned wrappers.
All wrappers for the same native blob share its access state. A read-only blob
cannot be queried for the mutable interface. Dispose every acquired wrapper.

## Tests

The integration tests call production Radient through the generated JavaScript
and Wasm module. They cover creation, initial-data ownership, access conflicts,
resizing, error statuses, interface queries, scope lifetimes, and active access
across Wasm memory growth. The Node and browser runners share these test cases.
Detailed marshalling tests remain in the general generator's mock-based suite.
CI runs the Node suites and the browser tests on Linux in Debug and Release,
using the runner image's preinstalled Chrome. No renderer or GPU is needed.

## Engine resource configuration

`RadientEngineCreateInfo.Resources` accepts a partial
`RadientResourceManagerCreateInfo` to configure GPU resource pools. Omitted fields
keep their native defaults. The three 64-bit byte limits (`MaxIndexBufferSize`,
`MaxMorphTargetBufferSize`, and `TextureAtlasMipLevel0Size`) use JavaScript `bigint`;
the other fields use `number`. Zero retains each field's documented native meaning.
These settings take effect when the engine has a GPU backend.

```js
const browser = await createRadientBrowser({
    canvas,
    engineCreateInfo: {Resources: {
        TextureAtlasSize: 1024,
        TextureAtlasMipLevel0Size: 4n * 1024n * 1024n,
        VertexPoolSize: 32768,
    }},
});
```

## Scenes

`Radient/bindings/API.json` selects entry points and supplies semantic overrides;
referenced types, ordinary reference policies, and inherited interface ownership
are discovered automatically. The adjacent `javascript/API.json` defines JavaScript
adaptations. The Wasm target links the production Radient library.
In addition to owning data blobs, it exposes engine/scene creation and scene writers
for entity names, hierarchy, visibility, and transforms. `CreateRadientEngine`
creates an engine without a GPU backend; use the browser adapter for WebGPU.
Externally supplied thread pools and native backend objects are not exposed.

```js
const {status, value: engine} = api.CreateRadientEngine({WorkerThreadCount: 1});
if (status !== api.RADIENT_STATUS.RADIENT_STATUS_OK) throw new Error("Engine creation failed");
const {value: scene} = engine.CreateScene({Desc: {Name: "Example"}});
const {value: writer} = engine.CreateSceneWriter(scene);
try {
    const {value: entity} = writer.CreateEntity({Name: "Root"});
    writer.SetLocalTransform(entity, {Position: {x: 2, y: 3, z: 4}});
    writer.CommitChanges();
    console.log(scene.GetWorldMatrix(entity).value.Data); // Float32Array snapshot
    console.log(scene.GetRootEntities(10).value.pEntities); // BigUint64Array snapshot
} finally {
    writer.dispose();
    scene.dispose();
    engine.dispose();
}
```

Check native status values when using the API. Entity IDs and revisions use `bigint`;
outputs are independent JavaScript snapshots. `GetScene()` returns a retained wrapper
that must also be disposed. Writers modify the shared scene immediately;
`CommitChanges()` refreshes derived transforms and visibility, rather than publishing
a transaction. Omitted descriptor members and default arguments keep native defaults.

The Wasm build preloads `RADIENT_BINDINGS_PTHREAD_POOL_SIZE` workers (one by default).
Set `WorkerThreadCount` explicitly and size this pool for all simultaneously active
engines. `WorkerThreadCount: 0` retains native automatic sizing and can exceed the
preloaded pool. Workers created beyond the pool need the browser event loop to start;
synchronously joining those workers can deadlock. Preloading allows immediate engine
creation and disposal in synchronous JavaScript code. Browser hosting must supply the
cross-origin isolation headers required by shared Wasm memory.

Real-library integration suites in `Radient/bindings/javascript/tests/*_tests.mjs` are
discovered by CMake and run unchanged in Node and a browser. They check public API
selection, metadata, marshalling, ownership, and a few connected scene operations;
native tests remain responsible for exhaustive scene behavior.

## Camera and light components

`SetCamera(entity, descriptor)` and `SetLight(entity, descriptor)` add or replace
components through a scene writer. Omitted fields take native defaults on each
call; these methods replace the component rather than patching individual fields.
`GetCamera(entity)` and `GetLight(entity)` return `{status, value}` with independent
descriptor snapshots, including nested clipping-range and color objects. Check
the status: absent components return `RADIENT_STATUS_NOT_FOUND` with default data.

```js
writer.SetCamera(cameraEntity, {
    Projection: api.RADIENT_CAMERA_PROJECTION.RADIENT_CAMERA_PROJECTION_PERSPECTIVE,
    FocalLength: 35,
    ClippingRange: {x: 0.1, y: 500},
});
writer.SetLight(lightEntity, {
    Type: api.RADIENT_LIGHT_TYPE.RADIENT_LIGHT_TYPE_POINT,
    Color: {x: 1, y: 0.75, z: 0.5},
    Intensity: 4,
});
scene.HasComponent(cameraEntity, api.RADIENT_COMPONENT_TYPE_CAMERA);
writer.RemoveComponent(lightEntity, api.RADIENT_COMPONENT_TYPE_LIGHT);
```

Component type constants are `bigint`; projection/light-type enumerators are
numbers. Camera and light units and validation rules follow the native descriptors.
Setting these components does not create a renderer or require a graphics device.

## Mesh assets

Use `engine.GetAssetManager()` to create meshes from blob-backed vertex/index
data or the cube/sphere helpers. Vertex layouts use nested descriptor arrays;
`ppVertexBuffers` contains one blob wrapper per `VertexLayout.BufferCount`.
Radient retains source blobs during processing. Loading can return either `OK`
or `PENDING`; poll `GetRadientAssetLoadStatus()` asynchronously. CPU-only loading
completes without GPU uploads.

Mesh descriptions are copied snapshots. Interface fields returned by scene or
asset getters are retained wrappers and must be disposed separately.

## Textures

`LoadTexture` accepts either an encoded `pDataBlob` or a `pTextureData` descriptor
with per-mip blobs, offsets, and strides. Native defaults apply, including
generation of missing mip levels for uncompressed raw data.
`EncodedTextureIsSRGB` applies only to encoded images; raw data uses its format.
Loading retains its blobs, so their JS wrappers can be disposed after submission.
Await CPU load completion before inspecting the final texture description.

## Materials

Create a standard definition through the asset manager, then create materials
from that definition. Parameter names are exported as constants; definitions
also enumerate their actual parameter set and resolve names to handles.
Reflected default values are copied `Uint8Array` snapshots; their byte count is
computed by Radient from the parameter type and array size. Texture references
returned in descriptions and component snapshots are separate owned wrappers.

Material `GetParameter(handle, byteCount)` returns copied bytes;
`SetParameter(handle, bytes, byteCount)` accepts a `Uint8Array`. Use a byte view
of a typed array for numeric values. Counts must match the reflected type and
array size. Writers copy assignments and apply them on `Commit()`. Texture
assignments follow the native contract: initialize them before querying material
load status; texture changes after finalization are rejected.

## Skeletons and skins

Skeleton/skin descriptions and joint arrays are copied snapshots. Pose writers
support deferred propagation just like native code. `ComputeSkinningMatrices`
queries the skin joint count natively and returns an array of matrix snapshots;
JavaScript does not allocate or size native matrix storage. Its optional boolean
arguments retain their native defaults.

Morph data uses blob-backed deltas in mesh creation. Create weights after mesh
CPU loading completes. `GetWeights()` returns a copied `Float32Array`, sized from
`GetWeightCount()` natively; use `SetWeights()` to modify the native object.

## Animation

Clips accept typed keyframe times and copied value bytes. Bind targets to the
scene writer, a material, a skeleton pose, or morph weights through
`QueryInterface(api.IRadientAnimationDestination)`. `Evaluate()` performs the
native update; direct writable native output pointers are never exposed.
Registry snapshots retain every returned clip and binding independently, so
dispose those wrappers after use, including when the registry entry is removed.

### Scene imports without a GPU

Pass model files and their relative dependencies when creating the module:

```js
const api = await createRadient({files: {
    "/assets/model.glb": new Uint8Array(await (await fetch("model.glb")).arrayBuffer()),
}});
const result = manager.LoadScene({URI: "/assets/model.glb"});
```

`files` copies bytes into the module's virtual filesystem before initialization.
Paths are absolute POSIX paths. Native loaders can resolve adjacent files using
ordinary relative URIs; provide those files under the corresponding paths too.
Poll `GetRadientAssetLoadStatus()` while yielding to the JavaScript event loop.
Use `InstantiateScene()` for an existing scene asset, or `ImportScene()` to load
and instantiate together. Call `ProcessPendingImports()` to finish pending scene
instantiations. An optional animation registry receives the imported bindings.

The integration tests load generated GLB and OBJ/MTL files, inspect their scene
components, and evaluate imported animation in both Node and Chromium.

## Browser rendering

Build with `-DDILIGENT_NO_WEBGPU=OFF`. The browser entry point,
`RadientBrowser.mjs`, is copied alongside the generated bindings, with
`RadientBrowser.d.mts` for TypeScript (using the browser WebGPU types).
It requests a WebGPU adapter/device asynchronously and creates a GPU-backed
engine using Diligent's WebGPU backend. Each instance owns one canvas, device,
engine, and Wasm module. Use HTTPS or localhost and serve the cross-origin
isolation headers required by Wasm workers.

```js
import createRadientBrowser from "./generated/RadientBrowser.mjs";

const browser = await createRadientBrowser({canvas: document.querySelector("canvas")});
const {engine, api} = browser;
function value(result) {
    if (result.status < 0 || result.value === null) throw new Error(`Radient status ${result.status}`);
    return result.value;
}
const renderer = value(engine.CreateRenderer({}));
const target = browser.createRenderTarget(renderer);
const scene = value(engine.CreateScene({}));
const view = value(renderer.CreateView({pScene: scene, pRenderTarget: target}));
// Populate the scene and set view.Camera through the regular generated API.

function render(time, deltaTime) {
    const status = renderer.BeginFrame({Time: time, DeltaTime: deltaTime});
    if (status < 0) throw new Error(`BeginFrame failed: ${status}`);
    try {
        const result = renderer.Render({pView: view});
        if (result < 0) throw new Error(`Render failed: ${result}`);
    } finally {
        renderer.EndFrame();
        browser.present();
    }
}
// Invoke render from requestAnimationFrame, with time values in seconds.
// Do not await between rendering and presentation: canvas textures expire
// when control returns to the browser.

browser.resize(1280, 720); // Physical pixels; call between frames.
console.log(target.GetSize()); // {Width: 1280, Height: 720}; the target is retained.

// Stop the animation loop, then release all application-owned objects.
view.dispose();
scene.dispose();
target.dispose();
renderer.dispose();
browser.dispose();
```

`moduleOptions` accepts the same options as `Radient.mjs`, including virtual
filesystem `files` for asset loading. `engineCreateInfo` supplies engine options;
the default is one worker, matching `RADIENT_BINDINGS_PTHREAD_POOL_SIZE`. Use a
matching preloaded pool when increasing the worker count. `powerPreference` and
`forceFallbackAdapter` are forwarded to WebGPU adapter selection.

Canvas size is explicit; the adapter does not install a resize observer or alter
CSS dimensions. Suspended/hidden canvases can skip rendering instead of requesting
a zero-sized swap chain. `device.lost` and `uncapturederror` are available on
`browser.device`; device recreation is the application's responsibility.

Renderer and view methods are exercised without a GPU in both Node and the
browser. WebGPU-enabled builds additionally register
`Radient-JavaScript-Browser-WebGPU`, which checks real device attachment, frame
submission, resizing an existing target, and shutdown. This smoke test requires
a working browser WebGPU adapter and fails if one is unavailable. The golden-image
suite below covers rendered glTF scenes.

## Browser golden-image tests

Build `RadientBrowserRenderTest` in a WebGPU-enabled Emscripten configuration.
It links the production Radient library and generated bindings, plus test-only
image capture/comparison helpers. `RadientWasm` does not contain those helpers.

The suite reads `Tests/RadientRenderTest/assets/RadientRenderTests.json` unchanged.
Every entry, animation timestamp, and debug visualization runs by default at
512 x 512 pixels. A single browser page and Wasm instance share the engine,
renderer, and view across all cases. Scene import, cameras, lighting, animation,
and frame preparation all run through the JavaScript bindings. Native code only
captures the GPU image, compares pixels, and encodes PNG files.

Provide a checkout of glTF-Sample-Assets and the `radient` branch of
DiligentGraphics/DiligentTestData. Chrome references live in
`GoldenImages/Chrome/GLTF/<case>/<capture>_wgpu.png`, alongside the native
references in `GoldenImages/GLTF`. Shared assets, including environment maps,
remain common to both suites. `GoldenImages/Chrome/metadata.json` records the
browser and rendering configuration used for the reference images.

```sh
python Radient/bindings/javascript/tests/browser/render/run_render_tests.py \
  --module <build>/DiligentFX/Radient/bindings/javascript/tests/browser/render/generated/Radient.mjs \
  --models <glTF-Sample-Assets> --assets <DiligentTestData> \
  --output <BrowserRenderResults> --browser <chrome-executable-or-wrapper>
```

The browser executable and asset locations are run-time settings. CTest uses
`BINDINGS_TEST_BROWSER_EXECUTABLE`, `RADIENT_BROWSER_TEST_MODELS`,
`RADIENT_BROWSER_TEST_ASSETS`, and `RADIENT_BROWSER_TEST_OUTPUT` for these paths.
Use `--reference-directory` (or `RADIENT_BROWSER_TEST_REFERENCES`) to override
the reference directory, for example for a future `GoldenImages/Safari/GLTF`
baseline in the same branch.

Run `ctest -R Radient-Browser-GoldenImages --verbose` from the bindings build
directory. The Emscripten workflow supplies Chrome/SwiftShader options and uploads
the rendered images, differences, and `results.json` with capture/case/total timings.

Use `--filter 'Animated*'` to narrow a local run. `--update-golden-images` writes
the captures into the selected reference directory, in per-case folders, after
comparison. Missing or differing references still fail that run; candidates must
be reviewed before committing them to the reference branch. Normal runs never
modify references. By default,
RGB comparison permits a channel error of 2 and a bad-pixel ratio of 0.0001,
matching the native CI defaults; manifest overrides are honored.

Capture copies the back buffer before presentation and polls a fence while
yielding to the browser. It never uses `WaitForIdle()` or a browser screenshot.
The browser adapter's optional `enableCapture` setting enables copy-source usage
on the canvas swap chain; it is false for ordinary applications.
