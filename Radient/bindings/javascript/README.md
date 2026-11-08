# Radient JavaScript bindings

The initial bindings provide owning read-only and mutable data blobs. They use
the real Radient implementation, compiled to Wasm, and run in browsers or Node
without a Radient engine or graphics device.

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
in `DataBlob.json`. `RadientWasm` compiles the generated wrapper and links the
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

### Browser tests

After building `RadientWasm`, with Chrome, Chromium, or Edge installed, run:

```sh
ctest --test-dir build/web/DiligentFX/Radient/bindings \
    -C Debug -L Browser --output-on-failure --no-tests=error
```

The runner discovers the browser automatically. Set the CMake cache variable
`RADIENT_BINDINGS_BROWSER_EXECUTABLE` to select a specific executable. No browser
automation package or download is needed.

The `Radient-JavaScriptBrowser` CTest suite runs the same blob cases as Node
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

The integration tests call the production blob through the generated JavaScript
and Wasm module. They cover creation, initial-data ownership, access conflicts,
resizing, error statuses, interface queries, scope lifetimes, and active access
across Wasm memory growth. The Node and browser runners share these test cases.
Detailed marshalling tests remain in the general generator's mock-based suite.
CI runs the Node suites and the browser blob tests on Linux in Debug and Release,
using the runner image's preinstalled Chrome. No renderer or GPU is needed.
