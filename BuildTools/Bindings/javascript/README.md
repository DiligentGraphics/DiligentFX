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
then runs the Node scalar/interface and byte-buffer marshalling tests. Nothing is
built by default. Native model tests remain independent of this optional
Emscripten project.

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
result. It also supports input byte spans and the scoped byte adapters described
below. It rejects unsupported declarations and policies instead of silently
omitting them. General arrays, callbacks, aggregate outputs, default function
arguments, and other manual adapters are not implemented yet. The first production
integration is [Radient's owning data blobs](../../../Radient/bindings/javascript/README.md).

Add target-independent native type information to the shared model. Put JavaScript
conversion and lifetime behavior in this backend. Add a native fixture and an
execution test for each new supported API shape; emitter-only string checks cannot
establish correct marshalling.

## Byte inputs

A `const void*` or pointer to const eight-bit elements can be bound as a byte
span when its metadata specifies `direction: in`, `ownership: borrowed`,
`lifetime: call`, and `length` naming an unsigned 32-bit or 64-bit integer sibling.
This applies to function parameters and descriptor fields. JavaScript passes a
`Uint8Array`; the existing integer conventions still apply to the explicit length.

The wrapper copies the requested prefix of the array into temporary Wasm storage,
respecting the array's byte offset. The native call may copy these bytes but must
not retain their pointer. Temporary storage is freed after the call, including
when conversion of a later argument fails. Lengths exceeding the supplied array
or Wasm32 address space are rejected before the native call.

`null` is accepted only for a nullable pointer and is forwarded without an
allocation; the native API determines its meaning. An empty array becomes a null
pointer with zero length. Omitted descriptor fields retain their native defaults.
All supplied scalar fields are initialized before buffer lengths are read, so
JavaScript property order does not affect marshalling.

## Scoped byte access

Pass `--adapters path/to/Adapters.json` to the JavaScript generator to bind an
interface's acquire/release pair. This configuration is specific to the JavaScript
backend; the language-neutral model continues to mark those methods as manual.
For example:

```json
{
  "schema_version": 1,
  "scoped_bytes": {
    "Diligent::IMarshalBytes::MapRead": {
      "release": "UnmapRead",
      "size": "Size",
      "writable": false,
      "success": 0
    }
  }
}
```

The acquire method returns a signed 32-bit status and one borrowed `const void*`
output (`void*` for writable access). The adapter's exact `success` status means
that access has been acquired; any other status returns `{status, value: null}`.
The no-argument size query returns `Uint32` or `Uint64` and runs after acquisition.
The no-argument release method returns a status and consumes the scope, including
when that status indicates an error. The size query may be inherited. Both acquire and
release methods must be marked manual in the API model.

A successful call returns `{status, value: scope}`, including for an empty span.
The scope owns an additional strong reference to the native object and is usable
even if the original interface wrapper is disposed. The release method is omitted
from the public wrapper; call `scope.dispose()` exactly as shown below. Disposal
is idempotent and returns the native release status. Garbage collection does not
release scopes.

```javascript
const {status, value: access} = object.MapWrite();
if (access !== null) {
    try {
        access.copyFrom(new Uint8Array([17, 128, 254]), 0);
    } finally {
        access.dispose();
    }
}
```

Read scopes provide `byteLength`, `readByte(index)`,
`copyTo(destination, sourceOffset = 0)`, and `toUint8Array()`.
Write scopes also provide `writeByte(index, value)` and
`copyFrom(source, destinationOffset = 0)`. Copy methods use the complete supplied
array view and check the requested range. `toUint8Array()` returns an independent
copy; no method exposes a writable view into read-only storage. All byte operations
fail after disposal. Operations obtain the current Wasm heap each time, so scopes
remain usable after memory growth without requiring callers to refresh views.

The native API must keep the span valid and its size unchanged until release.
The adapter does not add locking or emulate reader/writer exclusion: conflicts
and resize restrictions remain the responsibility of the native implementation.

## Selecting a JavaScript subset

The same adapter configuration can restrict an API without changing its native
headers or language-neutral model:

- `native_only` lists qualified enum or descriptor-field names omitted from the
  JavaScript surface. Omitted fields require native defaults and remain initialized
  by the native descriptor constructor. Passing such a field in JavaScript is an
  unknown-field error.
- `field_policies` maps descriptor names to field-policy overrides, using the
  same metadata format as the model. For example, an input whose lifetime depends
  on storage mode can become a call-scoped byte span when JavaScript fixes that
  mode to copying.
- `fixed_arguments` maps free-function names to parameter/enumerator pairs. The
  native call receives that enumerator, while the parameter is absent from the
  JavaScript and TypeScript signatures. Only enumerators of the parameter's actual
  type are accepted; arbitrary C++ expressions are not allowed.

These choices must agree: a temporary byte input cannot be combined with a native
mode that retains its pointer. Radient's blob configuration fixes COPY storage,
excludes callbacks and their context, and adapts only the owning creation path.
