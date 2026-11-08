# Binding generator

The shared generator extracts an explicitly selected API with Clang, attaches
semantic metadata, and produces a language-neutral JSON model. The JavaScript
backend generates Wasm glue, wrappers, and TypeScript declarations from that model.
The initial Radient selection is the data-blob API in `Radient/bindings/DataBlob.json`.

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
owning data-blob bindings are described in [Radient/bindings/javascript/README.md](../../Radient/bindings/javascript/README.md).

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
`Radient-GenerateBindingModel` and `Radient-TestBindingModel` targets. Neither is
part of the default build. The directory can also be configured independently:

```sh
cmake -S Radient/bindings -B /path/to/build/bindings
cmake --build /path/to/build/bindings --target Radient-GenerateBindingModel
cmake --build /path/to/build/bindings --target Radient-TestBindingModel
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
    --source-root . --config Radient/bindings/DataBlob.json \
    --target x86_64-pc-windows-msvc \
    --output /path/to/build/bindings/RadientDataBlob.json
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
host target by default). The CMake test target runs both suites.

## Metadata and model

The configuration has a version, entry headers relative to the supplied source root,
optional Clang arguments, and a map of fully qualified declaration names to
policies. This explicit selection prevents accidentally exporting every included
Core interface. Referenced records/enums and base interfaces must also be selected,
or explicitly designated `external` when the language runtime supplies them.

Policies can be attached to declarations, record `fields`, interface `methods`,
and function/method `parameters` and `result`. The checked-in blob policy is the
concrete example. Pointer/reference values require:

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
keys, missing pointer policies, missing type policies, compiler errors, and
unsupported types or overloads. Adding another API means selecting it, supplying
its semantics, and extending extraction deliberately where necessary.

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
