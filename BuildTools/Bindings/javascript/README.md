# JavaScript binding generation and conformance tests

This backend generates private Wasm call glue, JavaScript wrappers, and TypeScript
declarations from the validated binding model. It does not import Clang or change
the public native API. Native code initializes descriptors and dispatches interface
calls; JavaScript does not depend on struct offsets or vtable layout.

The standalone test project uses small native test implementations, not Radient.
The fixtures use real Diligent declarations and macros and compile as both C and
C++. Tests run through generated JavaScript into the compiled Wasm module in Node.
No browser, renderer, or GPU is required.

## Build and run

Install Emscripten and activate its environment. Use Node 18 or newer and the
Clang 22 Python environment described in the [binding tools README](../README.md).
The extraction library is Clang 22; Emscripten supplies the Wasm compiler and its
standard headers. Tested with Emscripten 6.0.11 and Node 24.

From the FX source directory:

```sh
emcmake cmake -S BuildTools/Bindings/javascript -B build/binding-js -G Ninja \
    -DBINDINGS_PYTHON_EXECUTABLE=/path/to/bindings-env/bin/python \
    -DBINDINGS_LIBCLANG=/path/to/LLVM-22/lib/libclang.so
cmake --build build/binding-js --target Bindings-TestJavaScript
```

On Windows, use the environment's `Scripts/python.exe` and LLVM's
`bin/libclang.dll`. Alternatively, pass Emscripten's
`cmake/Modules/Platform/Emscripten.cmake` as `CMAKE_TOOLCHAIN_FILE` directly.
`BINDINGS_NODE_EXECUTABLE` can select a particular Node executable.

`Bindings-GenerateJavaScriptTests` generates the model, glue, wrappers, and
declarations. `BindingTestsWasm` compiles the native test implementation and glue.
`Bindings-TestJavaScript` builds these artifacts, runs Python emitter tests, and
then runs the Node marshalling tests. Nothing is built by default. Native model
tests remain independent of this optional Emscripten project.

Generated artifacts reside in the build directory's `generated/` folder and are
not checked in. Only the generator, shared runtime, fixture declarations, metadata,
native test implementations, and tests belong in source control.

## Current binding contract

- Native declaration names are retained, without the `Diligent::` namespace.
- Scalars and enums use JavaScript numbers, except 64-bit integers, which require
  `bigint`. Booleans require JavaScript booleans. Integer range checks prevent
  silent truncation. Floating-point values use native conversion semantics.
- Input descriptors are JavaScript objects. Omitted or `undefined` fields retain
  the native defaults. Unknown fields are rejected. Default expressions are never
  evaluated or duplicated in JavaScript.
- Native status values are returned unchanged, including nonzero success or
  pending statuses. A callable with one output returns `{status, value}`. Output
  storage starts at zero; the native contract determines when the output is valid.
- An owned interface output is adopted without an additional `AddRef`. Call
  `dispose()` explicitly to release it; repeated disposal is harmless, and calls
  on disposed objects fail. Garbage collection does not release native references.
- `QueryInterface(api.InterfaceType)` returns a separately owned wrapper or null.
  Borrowed interface inputs retain their existing ownership. Wrappers cannot be
  passed between independently loaded Wasm modules.
- JavaScript argument errors throw; native status failures remain status results.
  Temporary descriptors and output storage are released even when marshalling fails.

For example, with the generated test module:

```javascript
import loadBindings from "./generated/BindingTests.mjs";

const api = await loadBindings();
const result = api.CreateMarshalObject({Size: 123n}, api.MARSHAL_STATUS.MARSHAL_STATUS_OK, true);
if (result.value !== null) {
    try {
        console.log(result.value.GetSize()); // 123n
    } finally {
        result.value.dispose();
    }
}
```

## Scope and extension

The initial backend supports scalar fields in plain input descriptors,
single-inheritance reference-counted interfaces, scalar arguments/results,
borrowed interface inputs, and one scalar or owned-interface output with a status
result. It rejects unsupported declarations and policies instead of silently
omitting them. Arrays, buffer spans, callbacks, aggregate outputs, default function
arguments, and manual adapters are not implemented yet. RadientDataBlob's byte
buffers and scoped access will be added separately.

Add target-independent native type information to the shared model. Put JavaScript
conversion and lifetime behavior in this backend. Add a native fixture and an
execution test for each new supported API shape; emitter-only string checks cannot
establish correct marshalling.
