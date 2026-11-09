# Binding generator

The shared generator extracts an explicitly selected API with Clang, attaches
semantic metadata, and produces a language-neutral JSON model. The JavaScript
backend generates Wasm glue, wrappers, and TypeScript declarations from that model.
The Radient selection covers owning data blobs and scene editing in `Radient/bindings/API.json`.

The public headers are the signature source. Clang expands Diligent's interface
macros and supplies types, inheritance, enum values, documentation, and native
default expressions. Metadata supplies information that C++ types cannot express,
such as ownership and pointer lifetimes. No additional public C API is introduced.

## Organization and extension

The infrastructure lives in FX. Its extraction and model validation are independent
of Radient declarations and language-specific generation:

- `clang_parser.py` reads the selected declarations and returns a plain-data API
  model plus available symbol names. It is the only module that uses libclang.
- `binding_model.py` validates and attaches semantic policies. It can work with
  the extracted model or with JSON loaded in a process without Clang installed.
- `generate.py` composes these stages and writes the model artifact. Header
  selection, preprocessor definitions, and binding policies come from configuration.
- `tests/` contains focused enum, struct, interface, function, constant, and typedef
  fixtures using the actual Diligent headers and macros, plus extraction and policy
  unit tests.
- `javascript/` emits Wasm call glue, JavaScript wrappers, and TypeScript declarations.
  Its standalone tests execute generated bindings against native test objects in Node.
- `Radient/bindings/` contains Radient's configuration, build targets, and
  public-header integration tests.

Radient bindings select only its C-compatible `.h` API. C++-only `.hpp` headers,
including `RadientTypesX.hpp`, `RadientImportedDocument.hpp`, and
`RadientSceneAssetImporter.hpp`, are excluded from conversion. They are not added
to the binding selection when extending coverage.

To cover another API, provide its entry headers and policies, with a source root
appropriate for that API. A future language emitter or adapter for another
generator should consume the validated model and keep its naming conventions,
runtime glue, and language-specific rules separate. New native type information
belongs in the extraction/model stages and needs fixture coverage. The current
model does not yet contain all ABI information required by every possible backend.

Generate a model for each target ABI; a Wasm model must not be used as a native
C# memory layout. Generated artifacts stay outside the source tree. The initial
JavaScript backend and its standalone Wasm conformance tests are described in [javascript/README.md](javascript/README.md). Radient
data-blob and scene bindings are described in [Radient/bindings/javascript/README.md](../../Radient/bindings/javascript/README.md).

## Running

Use Python 3.10 or newer, LLVM 22, and the Clang 22 Python bindings pinned in
`BuildTools/Bindings/requirements.txt`. Install the Python requirements in a
dedicated environment so the generator does not affect other tools:

```sh
python -m venv /path/to/bindings-env
/path/to/bindings-env/bin/python -m pip install -r BuildTools/Bindings/requirements.txt
```

The requirements install the Python package from the checksum-pinned LLVM 22.1.2
source release because Clang 22 Python bindings are not yet published on PyPI.
An uncached install downloads the approximately 167 MB source archive; only the
Python bindings are installed, and LLVM itself is not built.

On Windows the interpreter is `bindings-env/Scripts/python.exe`. The `clang`
Python package does **not** include libclang: install LLVM 22 separately (tested
with LLVM 22.1.2). Its shared library and built-in headers must remain in their
LLVM installation layout. Do not install the older bundled `libclang` Python
package in this environment; both packages provide the same `clang` module.

Set `RADIENT_BINDINGS_PYTHON_EXECUTABLE` to the environment's interpreter and
`RADIENT_BINDINGS_LIBCLANG` to LLVM 22's `bin/libclang.dll` on Windows,
`lib/libclang.so` on Linux, or `lib/libclang.dylib` on macOS. For direct Python
invocation, set the `LIBCLANG_LIBRARY_FILE` environment variable instead. If no
library path is supplied, Clang's normal shared-library search applies. Both the
Python bindings and the native parser must use major version 22.

The CMake targets use the configured C++ target and its standard headers. A native
Visual Studio build needs no Emscripten installation or environment. Win32 and x64
builds select their respective target architectures, independently of Python's
architecture.

Headers are parsed without binding-specific preprocessor switches. The extractor
retains data fields and virtual interface methods and ignores constructors,
operators, nonvirtual methods, and member templates. Free inline functions and
function templates are C++ helpers and are excluded, including overloads sharing
a name with a public out-of-line function. Standard headers included
by C++ helpers still need to be available to Clang, but their declarations are
not part of the selected API. This dependency is independent of Core's C# tooling.

Enable `RADIENT_GENERATE_BINDINGS` in a regular build to add the optional
`Radient-GenerateBindingModel` build target and register the `Bindings-Model` and
`Radient-BindingModel` CTest suites. Generation is not part of the default build;
tests run only through CTest or direct script invocation. The directory can also
be configured independently:

```sh
cmake -S Radient/bindings -B /path/to/build/bindings
cmake --build /path/to/build/bindings --target Radient-GenerateBindingModel
ctest --test-dir /path/to/build/bindings -C Debug --output-on-failure --no-tests=error
```

For example, a standalone Visual Studio configuration can select the separate
toolchain explicitly:

```sh
cmake -S Radient/bindings -B /path/to/build/bindings -A x64 \
    -DRADIENT_BINDINGS_PYTHON_EXECUTABLE=/path/to/bindings-env/Scripts/python.exe \
    -DRADIENT_BINDINGS_LIBCLANG=/path/to/LLVM-22/bin/libclang.dll
```

`RADIENT_BINDINGS_TARGET` is an optional Clang target triple override. Leave it
empty to use the configured C++ target. `RADIENT_BINDINGS_CLANG_ARGS` accepts
additional compiler arguments as a semicolon-separated list. When overriding the
target, provide that target's standard-header paths through these arguments.

If an existing build cache contains the former Wasm default, reconfigure it with
`-DRADIENT_BINDINGS_TARGET=` and remove any old SDK paths from
`RADIENT_BINDINGS_CLANG_ARGS`.

Direct CLI usage still takes an explicit target. For example, on Windows x64:

```sh
python -B BuildTools/Bindings/generate.py \
    --source-root . --config Radient/bindings/API.json \
    --target x86_64-pc-windows-msvc \
    --output /path/to/build/bindings/Radient.json
```

Run the unit tests with `python -B BuildTools/Bindings/tests/run_tests.py`, passing
the same `--target` and `--clang-arg` options as generation. These tests use real
Core headers and therefore need the configured target's standard headers too.
Use `--pattern test_structs.py` (or `test_enums.py`, `test_interfaces.py`,
`test_functions.py`, `test_constants.py`, or `test_typedefs.py`) to run one group
independently. Policy validation and generation checks live in separate test
modules. The fixtures also parse as C, including the interface-call macros.
Run the Radient integration test with
`python -B Radient/bindings/tests/test_public_api.py`, passing the same `--target` and
`--clang-arg` options as generation. It uses the supplied target and SDK (the native
host target by default). CTest registers these as separate suites. For a full
engine build, run them from `/path/to/build/DiligentFX/Radient/bindings` with
`ctest --test-dir /path/to/build/DiligentFX/Radient/bindings -C Debug -L Model --output-on-failure --no-tests=error`.
These model tests parse their own fixtures and do not require building the model
artifact first. CTest never builds artifacts automatically.

## Metadata and model

The configuration has a version, entry headers relative to the supplied source root,
optional Clang arguments, an `exports` list of fully qualified API entry points,
and optional `declarations` containing semantic overrides and interface-method
selections. For example:

```json
{
  "schema_version": 1,
  "headers": ["Radient/interface/RadientSceneWriter.h"],
  "exports": ["Diligent::IRadientSceneWriter"],
  "declarations": {
    "Diligent::IObject": {
      "binding": "external",
      "reason": "Runtime supplies reference counting",
      "ownership": "reference_counted"
    },
    "Diligent::RADIENT_STATUS": {"role": "status"},
    "Diligent::IRadientSceneWriter": {
      "include_methods": ["SetLocalTransform", "CommitChanges"]
    }
  }
}
```

Extraction automatically includes referenced records, enums, nested members,
fixed-array element types, and bases. It follows aliases to their underlying types
and handles recursive pointer graphs. No empty entries are needed for these types.
Only selected methods contribute dependencies; unrelated APIs and C++ helpers stay
out of the model. An `external` declaration stops traversal at a runtime-provided
type. Standalone constants and typedef names can be selected individually or
discovered from public headers. Overrides for unreachable declarations are errors,
so stale or misspelled configuration does not silently go unused.

Instead of enumerating declarations, `exports` can select their source headers:

```json
"exports": {
  "headers": ["MyAPI/interface/*.h"],
  "exclude_headers": ["MyAPI/interface/NativeOnly.h"],
  "exclude": ["MyAPI::UnsupportedCallback"],
  "include": ["OtherAPI::RequiredConstant"]
}
```

`headers` and `exclude_headers` match source-root-relative paths with `/` separators;
`exclude` matches qualified declaration names. Patterns use case-sensitive shell
wildcards. All sections except `headers` are optional. `include` contains explicit
declaration names and takes precedence over exclusions. Discovery examines headers
already parsed through the entry headers; it does not include additional files.
Dependencies remain automatic even when their own headers or names are excluded
from discovery. C++ inline functions and self-aliasing C typedefs are filtered as
they are for explicit selection. Other unsupported declarations produce errors.
This keeps export scope separate from marshalling semantics without maintaining
an inventory of every constant, typedef, or interface member.

For compatibility, omitting `exports` selects the keys of `declarations` as roots.
New configurations should separate selection from semantic overrides.

The model fills in the following information before language generation:

- References are non-null. Const-qualified parameter targets are inputs;
  mutable value pointers/references default to outputs. Interface pointers and
  references are inputs, while pointers/references to interface pointers default
  to outputs. Interfaces are identified from declarations, including external
  types marked `ownership: reference_counted`, rather than their names.
- Direction depends on the immediate target after resolving typedefs:
  `const char**` is an output, while `char* const*` is an input. Returned
  pointer/reference values are outputs. Use explicit `direction: inout` for
  parameters that both read and write caller data, or `direction: in` for mutable
  input buffers. Pointer fields still need an explicit direction, since their
  containing descriptor may be used for input, output, or both.
- Ordinary value references use borrowed, call-scoped storage; returned values
  are copied to snapshots. APIs retaining a value reference must explicitly
  override its lifetime. References to pointers still require the pointed-to
  value's ownership and lifetime.
- Interface reference-counting follows inheritance from a base marked
  `ownership: reference_counted`. Diligent's `IFoo` / `IID_Foo` convention is
  recognized when the sibling ID constant actually exists. Other names can use an
  explicit `iid` override; no missing ID is invented.

An API can opt into Diligent's common pointer contracts once:

```json
"conventions": "diligent"
```

With this setting, ordinary pointer parameters and descriptor fields default to
borrowed, call-scoped storage. Pointers default to non-null; optional inputs and
nullable results require `nullable: true`. Interface output parameters (`IFoo**`
or `IFoo*&`) return an owned strong reference with caller lifetime. Direct interface
pointer returns borrow a reference with object lifetime. Interfaces are identified
from declarations and reference-counted external bases, including through aliases.
Field arrays of interface pointers remain borrowed descriptor storage.

These are API conventions, not ownership guarantees derived from C++. Explicit
value policies override them. A borrowed output interface can specify just
`ownership: borrowed`; its lifetime defaults to `call`. Retention, static storage,
ownership-transferring direct returns, and scoped access need explicit exceptions.
Callbacks still require an adapter contract. The resolved model records the chosen
conventions so backend field overrides and serialized-model validation use the
same defaults. Without this opt-in, pointer ownership, lifetime, and nullability
remain required metadata.

These defaults never infer array length from parameter names. A null descriptor-field default does not prove
that null is accepted by a native operation. Explicit overrides take precedence.

Policies can be attached to declarations, record `fields`, interface `methods`,
and function/method `parameters` and `result`. Only facts not supplied by the rules
above need configuration. The resolved pointer/reference policies contain:

- `direction`: `in`, `out`, or `inout`.
- `nullable`: whether the native argument itself may be null. For `T**`, this
  describes the output slot, not the value written into it.
- `ownership`: `borrowed` or `owned`. An owned interface output already carries
  a strong reference; the wrapper adopts it without another `AddRef()`.
- `lifetime`: the relevant native lifetime, such as `call`, `caller`,
  `read_scope`, or `write_scope`.
- Optional `length`: a sibling integer field/parameter holding the buffer length.

`binding: manual` plus a `reason` explicitly marks an adapter requirement. This
does not constitute an implemented binding: a backend must implement that adapter
or report the declaration as unavailable. In this first selection, callbacks,
reference-mode storage, and mapped read/write access need adapters. Their native
signatures remain in the model. `external` declarations retain their name and
policy, without expanding APIs belonging to another runtime.

Generation fails on missing declarations, stale member names, unknown policy
keys, unresolved pointer semantics, missing type definitions, compiler errors, and
unsupported types or overloads. Adding another API means selecting it, supplying
its semantics, and extending extraction deliberately where necessary.

The generated model always contains the complete effective policies, regardless
of whether they were inferred or configured. Language backends do not need to repeat
inference and continue to consume the same explicit representation.

The JSON model records the target triple, ordered declarations, structured types
(including aliases, qualifiers, pointer depth, and scalar widths), native defaults,
documentation, and validated policies. Source paths are relative; no timestamps,
host addresses, or absolute checkout paths are emitted. Declaration names are
sorted; member and parameter order follows the native declaration. Defaults are
for inspection, not expressions to evaluate in another language: generated glue
must use native descriptor initialization. The model is not a struct-memory ABI;
it does not prescribe JS-side offsets or direct vtable calls.

Standalone typedefs produce `alias` declarations with a `type` describing their
underlying type. A redundant `typedef struct T T` keeps the original record
declaration. Aliases encountered in signatures remain expanded as before, so
selecting a standalone alias is only needed to export its own name and documentation.

Selected const variables produce `constant` declarations with `type` and `value`.
Clang evaluates integer and floating-point expressions; booleans and character
arrays become JSON booleans and strings. Braced record constants use field-name
objects with nested arrays, including UUID constants. References to immutable
named character arrays resolve to their string contents, including in grouped
material parameter names and arrays of names. The referenced strings do not need
to be selected for export. Mutable storage, arbitrary addresses, and pointer
arithmetic remain unsupported; the model never stores host pointers. Unsupported
initializers fail generation. Integer values retain their full precision in Python
and JSON;
model consumers must preserve 64-bit integers rather than read them as doubles.
Arbitrary preprocessor macros are not exported; select their typed public
constants, such as `RADIENT_VERTEX_AUTO_OFFSET`, instead.

Function and method parameters have a `default` expression (or `null` when none
is supplied), matching record fields. `DEFAULT_VALUE` expressions are retained
without translating them into language-specific code.

## Conventions for language backends

- Retain opaque interface identity and reference-counting semantics. A scoped
  buffer access retains its blob until access ends; memory views also need to
  account for Wasm memory growth. A callback bridge handles runtime-thread rules.
- Preserve native status values. `PENDING`, `NO_CHANGE`, and `OUT_OF_DATE` are
  meaningful outcomes, not generic exceptions. Output validity follows the native
  method contract. The status enum is marked with `role: status`.
- Keep native names in the model. Language-specific casing and wrapper names
  belong to the backend, with collisions diagnosed there.
- Preserve 64-bit integer values exactly; the JS backend will use `bigint` for
  64-bit IDs and revisions rather than silently rounding through `number`.
- Generate backend glue against the existing Radient interfaces. JS/Wasm,
  Java/JNI, and Swift can consume the same extraction and semantic policies.

Only the generator, metadata, tests, and handwritten adapters belong in source
control. Generated models, glue, JS, declarations, and binaries belong in the
build/package output. Release packages should contain generated outputs so users
do not need the generator toolchain.

### Output counts and interface-array metadata

`length` names an integer sibling used as an array count or output capacity.
For caller-filled outputs, `written` optionally names an unsigned scalar output
containing the written count. An array result can instead use `length_method`
to name a no-argument unsigned count getter on its interface, including inherited
methods. `length` and `length_method` are mutually exclusive.

Caller-filled output arrays can instead use `length_from` to obtain their required
element count from another non-null, borrowed input interface:

```json
"length_from": {"parameter": "pSkin", "method": "GetDesc", "field": "JointCount"}
```

The method must be const and take no arguments. With `field`, it returns a const
descriptor reference whose named field is an unsigned 32-bit or 64-bit integer.
Without `field`, the method returns that count directly. The count must remain
valid until the native operation completes. This policy is for borrowed, non-null,
call-scoped output buffers; it cannot be combined with `length`, `length_method`,
or `written`. It describes an element count (bytes for `void*`), not a capacity
argument supplied by the caller.
For interface-pointer arrays, `element_nullable` controls null elements separately
from the array pointer's `nullable` policy; elements are non-nullable by default.

### Selecting interface methods

All declared interface methods are included by default; do not repeat the full
method list. To expose only a subset, an interface declaration can specify
`"include_methods": ["CreateScene", "CreateSceneWriter"]`.
Only those directly declared virtual methods are extracted and emitted, in native
declaration order. Omitting the list retains all methods; an empty list exposes
the interface identity and selected base methods only. Base interfaces have their
own selections. Unknown or duplicate names and policies for omitted methods are
errors. Unselected signatures require no type declarations or pointer policies.
The native interface and its vtable are unchanged; generated calls still use its
real C++ declaration. This allows bindings to grow without exposing unrelated APIs.
