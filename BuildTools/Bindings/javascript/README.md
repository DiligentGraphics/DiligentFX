# JavaScript binding generation and conformance tests

This backend generates private Wasm call glue, JavaScript wrappers, and TypeScript
declarations from the validated binding model. It does not import Clang or change
the public native API. Native code initializes descriptors and dispatches interface
calls; JavaScript does not depend on struct offsets or vtable layout.

The standalone test project uses small native test implementations, not Radient.
The fixtures use real Diligent declarations and macros and compile as both C and
C++. Tests run through generated JavaScript into the compiled Wasm module in Node.
The descriptor, array, ownership, output-parameter, constant, and default-argument
suites also run in a headless browser.
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
CTest runs Python emitter tests, browser-harness tests, and separate Node suites
for each supported marshalling shape. Shared conformance suites run unchanged in
Node and the browser, including the output and ownership cases below.
Build `BindingTestsWasm` before running CTest; test execution never generates or
rebuilds the artifacts. Use `ctest -N` to list tests or `ctest -R <pattern>` to
select suites. Nothing is built by default. Native model tests remain independent
of this optional Emscripten project.

Fixture sources and test suites are discovered automatically:

- `tests/fixtures/*.c` and `*.cpp` are compiled into `BindingTestsWasm`.
- `tests/test_*.py` run with the generated model path and `-v`.
- `tests/test_*.mjs` run in Node with the generated module path.
- `tests/*_tests.mjs` run through the browser runner as shared suites.

CTest names contain the runner and filename, and retain the `Bindings`,
`JavaScript`, and `Browser` labels. Adding or removing matching files refreshes
the build and test lists on the next build through CMake's `CONFIGURE_DEPENDS`;
there is no per-suite CMake registration to maintain.

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
  pending statuses. Status-returning callables with outputs return `{status, value}`;
  void callables return the output value directly. Scalar output storage starts at
  zero and struct outputs use native defaults. The native contract determines when
  the output is valid.
- An owned interface output is adopted without an additional `AddRef`. Call
  `dispose()` explicitly to release it; repeated disposal is harmless, and calls
  on disposed objects fail. Garbage collection does not release native references.
- `QueryInterface(api.InterfaceType)` returns a separately owned wrapper or null.
  Borrowed interface inputs acquire a temporary reference for the call. This also
  applies to mutable and const interface-reference arguments: pass the same native
  wrapper, and the bridge preserves constness when dereferencing its pointer.
  References reject null. Mutable references operate on the existing native object;
  they do not copy it or transfer ownership. Wrappers cannot be passed between
  independently loaded Wasm modules.
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

The backend supports scalar and string marshalling, nested and inherited
descriptors, individual struct pointers, fixed and dynamic arrays, caller-filled
outputs, interface ownership, and native default arguments. Standalone constants
and TypeScript typedef aliases are exported. Unsupported declarations and policies
are rejected rather than silently omitted. Callbacks, by-value struct results,
and additional custom lifetime adapters remain outside this backend's support.
The first production integration is [Radient's owning data blobs](../../../Radient/bindings/javascript/README.md).

Add target-independent native type information to the shared model. Put JavaScript
conversion and lifetime behavior in this backend. Add a native fixture and an
execution test for each new supported API shape; emitter-only string checks cannot
establish correct marshalling.

## Output sizes queried from native objects

An output buffer annotated with `length_from` needs no JavaScript count argument.
The generated C++ helper queries the selected input interface directly, including
descriptor fields such as `pSkin->GetDesc().JointCount`. Once all inputs have been
marshalled and retained, the wrapper queries the count, allocates exactly that many
native elements, and invokes the operation. Output structs use native construction
and layout. The result is copied into a JavaScript array, and temporary storage is
released on success, native failure, or snapshot-conversion failure.

Zero counts produce empty arrays. Oversized counts are rejected before allocating
the output. Native statuses are preserved; as with explicitly sized outputs, the
API contract determines whether the returned data is valid for a given status.
The source count must remain stable from the query through the native operation.

## Struct results

A `const T&` result (declared with `const T REF` in a Diligent C-compatible header)
becomes a plain JavaScript object. Supported fields are scalars, enums, UTF-8
strings, embedded value structs, and borrowed pointer/count arrays. Nested data is copied before
the wrapper returns or releases temporary inputs. Later native mutation or owner
destruction does not affect the result, and editing the result does not modify
native data. The snapshot does not retain the containing native owner. Any
interface fields are separately retained wrappers and must be disposed explicitly.

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
By-value struct returns and mutable-reference results
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
      "success": 0
    }
  }
}
```

The acquire method returns a signed 32-bit status and one borrowed `const void*`
output (`void*` for writable access). Read/write access is inferred from that
pointee's constness, including typedefs. The adapter's exact `success` status means
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

- `native_only` lists qualified enum, external-type, or descriptor-field names omitted from the
  JavaScript surface. Omitted fields require native defaults and remain initialized
  by the native descriptor constructor. Passing such a field in JavaScript is an
  unknown-field error.
- `field_policies` maps descriptor names to field-policy overrides, using the
  same metadata format as the model. Overrides merge with the original policy,
  so unchanged properties such as array lengths need not be repeated. Switching
  a manual field to `binding: automatic` removes its manual reason unless a new
  reason is supplied. For example, an input whose lifetime depends
  on storage mode can become a call-scoped byte span when JavaScript fixes that
  mode to copying.
- `fixed_arguments` maps free-function names to parameter/enumerator pairs. The
  native call receives that enumerator, while the parameter is absent from the
  JavaScript and TypeScript signatures. Only enumerators of the parameter's actual
  type are accepted; arbitrary C++ expressions are not allowed.

These choices must agree: a temporary byte input cannot be combined with a native
mode that retains its pointer. Radient's blob configuration fixes COPY storage,
excludes callbacks and their context, and adapts only the owning creation path.

## Scalar and struct output parameters

Writable scalar and struct pointer/reference outputs use native temporary storage.
Scalar outputs start at zero; structs use their native defaults. The caller omits
the output argument. Status-returning calls return `{status, value}`; void calls
return the output value directly. Struct results are snapshots copied before the
temporary output is destroyed, including on conversion failure. Output storage
requires `direction: out`, non-nullability, and borrowed call-scoped ownership.

## Caller-filled arrays and multiple outputs

An output pointer with `length` names the input capacity; optional `written`
names an unsigned scalar output containing the number actually written. The
wrapper allocates the buffer and returns an owned JS copy of the written prefix.
Zero capacity is supported. An invalid written count throws before reading data.
Multiple outputs are returned in an object keyed by their native parameter names,
inside `value` for status-returning functions. Output arguments are omitted from
the JS call. Struct arrays use native default construction and destruction.

## Borrowed interface results

Returned interface pointers explicitly declare `ownership` and `lifetime`.
Borrowed results acquire a reference for their JS wrapper; owned results transfer
the existing reference. Call `dispose()` on either wrapper. Pointer outputs follow
the same rule. Multiple-output conversion failures release both already wrapped
and not yet wrapped owned results. Nullable results preserve null.

## Interfaces in descriptors and arrays

Interface input fields and array elements accept live wrappers from this module.
They are retained until the call and snapshot conversion finish. Borrowed
interface outputs acquire independent wrapper references, including when nested
inside descriptors or arrays. Dispose those wrappers when finished.
The pointer policy describes borrowed element ownership; `element_nullable`
allows null interface elements independently of the array's own `nullable` flag.

## Individual struct pointers

Borrowed `const T*` inputs accept a descriptor object, or null when the policy
allows it. Fields use the same representation; omitted fields preserve native
defaults. Native temporary storage survives through result conversion. Borrowed
struct pointers returned directly or through fields become copied snapshots,
including null for optional results. Use `length` only for arrays, not individual
struct pointers.

## Inherited descriptors

Descriptors declared with `DILIGENT_DERIVE` expose inherited and own fields in one
JS object. The native derived constructor supplies all defaults; generated native
accessors perform base adjustment and field addressing. Pointer/count relationships
may refer to inherited count fields. The model retains inheritance, while emitted
TypeScript descriptor interfaces list the complete field set.

## Array counts from another method

An array result may use `length_method: GetWeightCount` instead of `length`.
The named method must belong to the same interface or a base, take no arguments,
and return an unsigned 32-bit or 64-bit count. The wrapper queries the native
owner and copies the array before returning; no count argument is exposed to JS.
The pointer and count must describe the same stable data for the synchronous call.

## Standalone constants and typedefs

Selected constants are named ES module exports and read-only members of the
created API. Integer constants wider than 32 bits use BigInt, preserving invalid
IDs exactly. Strings use JS strings. Aggregate constants are recursively frozen
objects and ordinary arrays, so importing a constant never needs native storage.
Selected typedefs are exported TypeScript aliases; they have no runtime wrapper
or allocation. Alias use in native signatures continues to resolve to the same
marshalling representation.

## Default function arguments

Defaulted declarations accept fully specified calls. Omitted trailing arguments
(or trailing `undefined`) use generated native call forms, so C++ evaluates the
defaults at each invocation. TypeScript marks these arguments optional. Interior
`undefined` is not a placeholder for a default when later arguments are supplied.

Counts needed for JS array validation remain explicit. Pointer/reference inputs
also remain explicit when their default temporary could escape through borrowed
results or output parameters; this preserves snapshot lifetime guarantees.

Descriptor span counts may also name nested value fields (for example, `VertexLayout.BufferCount`) or a fixed integer extent. Native count getters run after the descriptor has been populated, preserving nested defaults and independence from JavaScript property order. Paths cannot traverse pointers.

A `field_count_functions` JavaScript adapter can name a native function that
accepts a descriptor by const reference and returns its span element count.
Use this for type-tagged byte payloads whose size is not a descriptor field.
The header supplied to the generator declares the function; a matching
`field_policies` entry supplies the borrowed span's direction and lifetime.
The adapter name is a qualified C++ function identifier, not an expression.

`ModuleOptions.files` optionally supplies a map of absolute POSIX paths to
`Uint8Array` contents. The wrapper snapshots the bytes before asynchronous module
initialization and populates Emscripten's virtual filesystem before native code
runs. Targets using this option must export the `FS` runtime method. Native code
continues to use its ordinary filesystem APIs; no native callback adapter is
required. Existing Emscripten `preRun` hooks run after the files are installed.
