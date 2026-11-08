# JavaScript binding generation and conformance tests

This backend generates private Wasm call glue, JavaScript wrappers, and TypeScript
declarations from the validated binding model. It does not import Clang or change
the public native API. Native code initializes descriptors and dispatches interface
calls; JavaScript does not depend on struct offsets or vtable layout.

The standalone test project uses small native test implementations, not Radient.
The fixtures use real Diligent declarations and macros and compile as both C and
C++. Tests run through generated JavaScript into the compiled Wasm module in Node.
The nested-input, struct-result, input-array, output-array, and fixed-array suites also run in
a headless browser.
No renderer or GPU is required.

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
cmake --build build/binding-js --target BindingTestsWasm
ctest --test-dir build/binding-js --output-on-failure --no-tests=error
```

On Windows, use the environment's `Scripts/python.exe` and LLVM's
`bin/libclang.dll`. Alternatively, pass Emscripten's
`cmake/Modules/Platform/Emscripten.cmake` as `CMAKE_TOOLCHAIN_FILE` directly.
`BINDINGS_NODE_EXECUTABLE` can select a particular Node executable.
Browser tests discover Chrome, Chromium, or Edge at test execution time. Use
`ctest -LE Browser` to run without a browser.

`Bindings-GenerateJavaScriptTests` generates the model, glue, wrappers, and
declarations. `BindingTestsWasm` compiles the native test implementation and glue.
CTest runs Python emitter tests and separate Node suites for scalar/interface,
byte-buffer, string, nested-input, struct-result, input-array, output-array, and fixed-array
marshalling, plus browser-harness unit tests. The nested-input, struct-result,
input-array, output-array, and fixed-array cases run in both Node and the browser.
Build `BindingTestsWasm` before running CTest; test execution never generates or
rebuilds the artifacts. Use `ctest -N` to list tests or `ctest -R <pattern>` to
select suites. Nothing is built by default. Native model tests remain independent
of this optional Emscripten project.

Generated artifacts reside in the build directory's `generated/` folder and are
not checked in. Only the generator, shared runtime, fixture declarations, metadata,
native test implementations, and tests belong in source control.

## Browser test runner

`tests/run_browser_tests.py` runs a shared suite in headless Chrome, Chromium, or
Edge without an automation package. It serves a generated module and the suite
from a temporary local HTTP server with cross-origin isolation headers, and exits
with an error if loading fails, a test fails, or the browser does not report before
the timeout. `--shared-memory` additionally requires the module to use shared Wasm
memory.

To select a browser when running CTest, set the `BINDINGS_TEST_BROWSER_EXECUTABLE`
environment variable; no reconfiguration or rebuild is needed:

```sh
BINDINGS_TEST_BROWSER_EXECUTABLE=/path/to/chrome \
    ctest --test-dir build/binding-js -L Browser --verbose --output-on-failure --no-tests=error
```

In PowerShell, set `$env:BINDINGS_TEST_BROWSER_EXECUTABLE` before running CTest.
When invoking the runner directly, `--browser` overrides the environment variable.
If neither specifies a browser, the runner discovers one automatically.

A suite exports `registerTests({test, assert, api, constants, wasm})`. Node supplies
its standard test runner and assertions; the browser adapter supplies the small
assertion subset used by these tests. Browser cleanup registered with `t.after()`
runs even after failure. The production example is
[Radient's blob suite](../../../Radient/bindings/javascript/README.md#browser-tests).

## Current binding contract

- Native declaration names are retained, without the `Diligent::` namespace.
- Scalars and enums use JavaScript numbers, except 64-bit integers, which require
  `bigint`. Booleans require JavaScript booleans. Integer range checks prevent
  silent truncation. Floating-point values use native conversion semantics.
- Input descriptors are JavaScript objects. Omitted or `undefined` fields retain
  the native defaults. Unknown fields are rejected. Default expressions are never
  evaluated or duplicated in JavaScript. Nested value structs accept nested
  objects and update the child already initialized inside its parent, preserving
  defaults supplied by the containing field. An empty child object changes
  nothing; `null` is rejected. Only the outer descriptor is allocated, and nested
  strings and byte spans share its call-scoped cleanup.
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

The backend supports scalar fields and nested value structs in plain input descriptors,
single-inheritance reference-counted interfaces, scalar arguments/results,
borrowed interface inputs, and one scalar or owned-interface output with a status
result. Borrowed const-reference struct results become JavaScript snapshots.
It also supports UTF-8 strings, scalar/struct input and borrowed output arrays,
fixed-size array fields, input byte spans, and the scoped byte adapters described below. It rejects unsupported declarations
and policies instead of silently omitting them. Caller-filled output buffers,
interface-pointer arrays, callbacks, struct output parameters, by-value struct
results, default function arguments, and other manual adapters are not implemented
yet. The first production integration is [Radient's owning data blobs](../../../Radient/bindings/javascript/README.md).

Add target-independent native type information to the shared model. Put JavaScript
conversion and lifetime behavior in this backend. Add a native fixture and an
execution test for each new supported API shape; emitter-only string checks cannot
establish correct marshalling.

## Struct results

A `const T&` result (declared with `const T REF` in a Diligent C-compatible header)
becomes a plain JavaScript object. Supported fields are scalars, enums, UTF-8
strings, embedded value structs, and borrowed pointer/count arrays. Nested data is copied before
the wrapper returns or releases temporary inputs. Later native mutation or owner
destruction does not affect the result, and editing the result does not modify
native data. The snapshot does not retain the native owner and needs no disposal.

The result policy requires `direction: out`, `nullable: false`,
`ownership: borrowed`, and `lifetime: call`, `object`, or `static`. Native storage
must remain valid and stable until synchronous snapshot conversion finishes.
String fields use `direction: out` (or `inout` for a descriptor used in both
directions), borrowed ownership, their documented lifetime, and explicit
nullability. Inputs still require call-scoped string storage. These are data-field
policies; an output parameter that points directly to const data remains invalid.

Generated TypeScript uses `StructSnapshot<T>` to make all returned fields,
including nested fields, required while preserving optional input descriptor
fields. For example, a getter is declared as
`GetDesc(): StructSnapshot<TextureDesc>`. Native pointers and struct offsets stay
private. Reading a borrowed result needs no temporary native descriptor allocation.

Pointers other than supported strings and annotated pointer/count arrays are rejected during generation.
By-value struct returns, struct output parameters, and mutable-reference results
are not supported yet.

## UTF-8 strings

Null-terminated `const Char*` and `const char*` values map to JavaScript `string`.
Pointers with a `length` policy remain byte spans instead. Mutable character
buffers, wide strings, and owned native string outputs require separate adapters.

- Input parameters and descriptor fields require `direction: in` (`inout` for
  fields also read in snapshots), `ownership: borrowed`, and `lifetime: call`.
  The wrapper encodes UTF-8 into
  temporary null-terminated Wasm storage and frees it after the native call.
  Native code that retains the text must copy it during that call.
- Direct string results require `direction: out`, `ownership: borrowed`, and
  `lifetime: call`, `object`, or `static`. The storage must remain valid through
  immediate result conversion. The wrapper copies the text into a JavaScript
  string before releasing temporary inputs. Later native mutation or destruction
  does not change the returned JavaScript value.
- A status-returning method with one `const Char**` or `const Char* REF` output
  returns `{status, value: string | null}`. Its policy uses `direction: out`,
  `nullable: false` for the output slot, and the same borrowed lifetimes as direct
  results. The wrapper initializes the slot to null and preserves the status.

For inputs and direct results, `nullable: true` permits `null`, which is distinct
from `""`. Omitted or `undefined` descriptor fields retain native defaults.
Non-string inputs and embedded null characters are rejected before the native
call. Unpaired JavaScript UTF-16 surrogates become U+FFFD, following `TextEncoder`.
Malformed native UTF-8 is rejected; a leading U+FEFF is preserved as content.

String conversion uses the current Wasm heap after allocation and native calls,
including memory growth. Native addresses and string views are not exposed.

## Input arrays

An input `const T*` with a `length` policy naming an unsigned 32-bit or 64-bit
integer sibling becomes an array input. This works for parameters and descriptor
fields. Counts remain explicit and measure elements; omitted descriptor fields
retain native defaults. Metadata requires `direction: in`, `ownership: borrowed`,
and `lifetime: call`. Native code that retains the data must copy it during the call.

- Integer and floating-point arrays use the corresponding typed array:
  `Int16Array`, `Uint32Array`, `BigInt64Array`, `Float32Array`, and so on.
  Enums use the typed array matching their underlying integer type.
  Boolean arrays use `Uint8Array` containing only zero and one.
- Struct arrays use `ReadonlyArray<T>` in TypeScript and arrays of objects in
  JavaScript. One contiguous native allocation initializes every element with its
  native defaults. Each object overrides only supplied fields, using the same
  nested-struct, string, and array conversions as a standalone input descriptor.
- `const void*` and character spans keep their existing `Uint8Array` byte-input
  convention. Signed eight-bit arrays also continue accepting `Uint8Array` as raw
  byte representations, in addition to `Int8Array`.

Only the requested prefix is copied; a typed array's byte offset is respected.
Counts larger than the supplied array or supported native storage are rejected
before calling the API. Missing struct elements and invalid fields are errors.
An empty array with a zero count becomes a null pointer. Explicit `null` is allowed
only with `nullable: true`; the native API determines its meaning for the given
count. Pointer/count descriptor fields are processed after counts are initialized,
so JavaScript property order does not change the result.

All temporary arrays and nested data are released after the call, including when
a later element, argument, or property getter fails. Input conversion does not
modify the JavaScript arrays. Native pointers are never exposed.

```javascript
// Given a generated function with const VertexAttribute* pAttributes and Uint32 Count:
api.UseAttributes([{Semantic: "POSITION"}, {Semantic: "NORMAL"}], 2);
```

Arrays of interface pointers are not supported yet.

## Fixed-size array fields

Embedded C arrays such as `Float32 Data[16]` use the extent from their native
declaration; no pointer/count policy is required. Numeric, enum, boolean, and
character elements use the same typed-array representations as pointer/count
arrays. Struct elements use arrays of descriptor objects. Multidimensional arrays
use nested JavaScript arrays, with typed arrays for numeric rows. For example,
`Float32 Rows[2][4]` takes two `Float32Array(4)` rows. Array typedefs preserve these
dimensions.

A supplied field must have exactly the declared number of elements at every
dimension. Short, long, sparse, null, and incorrectly typed arrays are rejected.
Omitting the field or passing `undefined` leaves its native initializer unchanged,
including matrix identity defaults. Struct elements may omit individual members;
their defaults come from the containing descriptor's initializer. Empty objects
preserve those defaults, while missing array elements are errors.

Input values are written directly into the containing native descriptor. Fixed
arrays and their struct elements require no separate native allocation. Nested
strings and dynamic spans share the call's existing temporary-storage cleanup.
Results are independent typed arrays or recursively copied struct arrays, with
the same lifetime rules as other snapshots. Const arrays and const element fields
are supported in output-only descriptors.

TypeScript preserves the element types and dimensions; exact lengths are checked
at runtime. Fixed arrays of pointers, callbacks, and other unsupported element
types still require adapters. This support applies to embedded fields: C array
function parameters decay to pointers and use pointer/count metadata instead.

## Borrowed output arrays

Borrowed `const T*` arrays become independent JavaScript snapshots. Supported
forms are pointer/count fields in returned descriptors and direct pointer results
whose `length` policy names an explicit integer input parameter. Counts measure
elements and use unsigned 32-bit or 64-bit types. Metadata requires
`direction: out`, `ownership: borrowed`, and `lifetime: call`, `object`, or `static`.
Fields used in both input and output descriptors use `direction: inout` and
`lifetime: call`.

- Numeric and enum data becomes a new typed array with the corresponding element
  type. Boolean data uses `Uint8Array`; character and void spans remain bytes,
  including embedded nulls. Signed eight-bit output uses `Int8Array`.
- Struct arrays become arrays of recursively copied snapshots. Strings, embedded
  value structs, and nested pointer/count arrays are copied as well. TypeScript
  uses `Array<StructSnapshot<T>>`, making all returned element fields required.
  Numeric typed arrays retain their typed-array declarations.
- A non-null pointer with zero count produces an empty array. A null pointer with
  zero count produces `null` for nullable metadata, otherwise an empty array.
  A null pointer with a nonzero count is invalid, regardless of nullability.

The wrapper checks counts, alignment, and the complete Wasm memory range before
reading elements or allocating the snapshot. These checks cannot verify a native
allocation's extent: the native API must return accurate counts and keep all
referenced data valid and stable until synchronous copying finishes.

Snapshots do not retain native owners or expose native memory. They remain valid
after native mutation, memory growth, or owner destruction, require no disposal,
and can be edited without changing native data. Conversion finishes before
temporary inputs are released; failed conversion also releases those inputs.
Reading borrowed arrays does not allocate temporary native arrays.

Caller-filled output buffers with capacity/written-count parameters, owned array
results, and counts obtained through separate getter methods require additional
adapters and are not supported by this step.

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
