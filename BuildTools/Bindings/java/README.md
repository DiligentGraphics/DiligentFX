# Java/JNI binding generator

This backend consumes the shared binding model and generates a Java API and JNI
C++ glue. It has no Radient dependency. The standalone conformance project uses
the [shared native fixtures](../tests/native/README.md), declared with the real
Diligent API macros. Its `tests/API.json` selects supported APIs from that common
reference. Java assertions stay in `tests/`; runtime-name collision and native
exception fixtures stay in `tests/fixtures/`.

Generated sources are build artifacts. `generate.py` accepts `--model`, `--module`,
`--package`, `--library`, one or more `--header` arguments, and `--output`.
`--adapters` optionally loads the shared adapter configuration.

## Supported initial surface

- Scalar values, aliases used in signatures, and typed enums. Enums become
  constant containers, preserving numeric values and allowing flags.
- Scalar constants become Java `static final` compile-time constants. Integer,
  boolean, floating-point, and enum values use the same Java types as parameters;
  Uint64 constants preserve all bits in a signed Java `long`.
- Descriptors with scalar, string, and nested descriptor fields. Construction
  obtains defaults from native C++ initialization, including incomplete defaults
  with null required fields. Required fields are checked when a descriptor is
  passed to native code and when native results are converted.
- Fixed-size descriptor arrays of scalars, enums, descriptors, or nested arrays.
  Each dimension must be non-null and have exactly the declared length. Defaults
  and output arrays are independent Java snapshots; input arrays are copied into
  the descriptor, with descriptor-element strings retained for the native call.
- Ordinary UTF-8 strings, copied between native storage and Java UTF-16 strings.
  Nullability follows the model. Embedded NUL and invalid encodings are rejected.
- Copied scalar/enum input arrays, borrowed array results, and caller-filled
  output arrays with explicit capacity and optional written count.
- Scalar and descriptor output pointers/references, and multiple outputs.
- Reference-counted interfaces directly based on an external native interface;
  owned and borrowed interface outputs, and interface pointer/reference inputs.

Java exposes Uint8 as `short`, Uint16 as `int`, and Uint32 as `long`, with checked
ranges. Uint64 uses the complete bit pattern of a `long`; callers can use Java's
unsigned-long utilities. Arrays follow their element type's mapping. JNI copies
array data and validates the supplied count before invoking native code. Nullable
arrays preserve null separately from non-null empty arrays in both directions.
Required input arrays accept Java null only with count zero and provide non-null
empty native storage in that case. Native array results are independent Java snapshots.

Input pointers and references must borrow storage for the duration of the call,
including pointers inside nested descriptors. Retained inputs and ownership
transfers require adapters and currently fail generation. Owned raw pointer
results (including descriptor pointers) also require a release adapter; copying
such a result into Java would otherwise leak its native allocation.

Methods with output parameters return a generated result object. `returnValue`
holds the native return value; the other fields retain the parameter names.
Status values, including failures and pending results, are returned unchanged.
An unwritten output starts with native value initialization. Native exceptions
are contained at the JNI boundary and reported as Java exceptions.

Interface wrappers implement `AutoCloseable`; use try-with-resources or `close()`
for deterministic release. Closing twice is harmless, and using a closed wrapper
throws `IllegalStateException`. Borrowed results acquire their own reference.
A Cleaner is a fallback for forgotten wrappers. JNI retains interface arguments
for the duration of each call, including when another thread closes the wrapper.
This protects lifetime; it does not change the native API's threading contract.
If result conversion fails, all owned outputs are released before returning.

This first backend does not yet cover the entire JavaScript surface. Unsupported
shapes fail generation: callbacks, scoped byte access, interface inheritance,
interface/pointer descriptor fields, pointer-based struct/interface arrays,
read-only fixed-array fields, computed counts, inout parameters, and non-scalar
constant exports. Native default arguments may be present, but Java callers
currently supply every input argument.
Radient selection, packaging, Android integration, and rendering are later steps.

## Build and test

Requirements: CMake 3.24+, a native C++17 compiler, JDK 11 or later, and the same
Python/LLVM 22 environment used by the shared binding model. The JDK and compiler
must target the same architecture. Set `JAVA_HOME` if Java is not discoverable.

```sh
cmake -S DiligentFX/BuildTools/Bindings/java -B build/bindings-java \
  -DBINDINGS_PYTHON_EXECUTABLE=/path/to/venv/bin/python \
  -DBINDINGS_LIBCLANG=/path/to/libclang.so
cmake --build build/bindings-java --config Debug --parallel
ctest --test-dir build/bindings-java -C Debug -V
```

The build extracts the fixture API, generates and compiles JNI, and compiles the
Java tests. CTest only runs the prepared tests. Each Java suite prints individual
case results and runs with `-Xcheck:jni`. Fixtures count live native objects and
verify inputs independently, including ordinary UTF-8 byte sequences and exact
unsigned values; tests also check failure cleanup and use after disposal.
Concurrent-close tests use a native gate, with unconditional release during
cleanup. `CleanerTest` runs in its own JVM and waits for observed destruction
with a bounded deadline; it does not assume that a single GC request is enough.
Large-descriptor tests use the default JVM stack, including simultaneous input
and output storage. Large native values use heap storage while small descriptors
remain inline.

Generator tests cover selection, determinism, native defaults, and rejection of
unsupported shapes. Fixture sources and Java/Python test files are discovered by
CMake; adding a suite does not require another source-list entry. No browser,
renderer, Maven/Gradle dependency, or duplicate native Radient test suite is used.
