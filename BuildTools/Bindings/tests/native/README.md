# Shared native binding fixtures

This directory is the native reference for binding conformance tests. Java and
JavaScript compile the same sources and select their supported declarations.
New language backends should reuse this corpus rather than copy its native code.

- `BindingTestAPI.h` includes the portable fixture headers.
- `BuffersC.c` checks the byte interfaces and inherited dispatch as C in both
  the JNI and Wasm builds.
- `API.json` records shared ownership, nullability, array bounds, and lifetimes.
  Backend configurations extend it and select the APIs their generators support.
  Policies for contracts currently selected by only one backend remain in that
  backend's configuration; move them here when another backend selects them.
  Paths are relative to `BuildTools/Bindings`.
- `Adapters.json` records shared acquire/release contracts for scoped byte access.
- Native functions check known values independently of language-side round trips.
  Distinct descriptor shapes and enum value sets remain separate test cases.
- Language assertions stay in `java/tests` and `javascript/tests`; generator
  failure tests stay with their respective backends.

Wasm linear-memory operations are implemented in
`javascript/tests/fixtures/WasmMemoryHooks.cpp`, which installs `MemoryTestHooks`.
Ordinary native cases never call those hooks. Browser virtual-file fixtures also
remain beside the JavaScript tests. Java runtime-name collisions and native
exception translation stay beside the Java tests.

Add portable contracts here, with semantic policy only when the C/C++ declaration
is ambiguous, then exercise them from every backend that supports that contract.
Keep backend-specific execution hooks out of the shared native implementation.

## JavaScript and Java coverage

The following suites exercise the same native declarations and implementations.
The paths in the table are relative to `javascript/tests` and `java/tests`.
The existing CMake test discovery registers new `*Test.java` files in the Java
suite already run by CI; no additional action or renderer build is required.

| JavaScript suite | Corresponding Java tests |
| --- | --- |
| `test_marshalling.mjs` | `ScalarsTest`, `InterfaceInheritanceTest` |
| `test_strings.mjs` | `StringMarshallingTest` |
| `nested_struct_tests.mjs` | `NestedStructsTest` |
| `struct_result_tests.mjs` | `StructResultsTest` |
| `input_array_tests.mjs` | `InputArraysTest`, `DescriptorArraysTest` |
| `output_array_tests.mjs` | `DescriptorArraysTest`, `OutputParametersTest` |
| `fixed_array_tests.mjs` | `FixedArrayMarshallingTest` |
| `inheriteddescriptors_tests.mjs` | `InheritedDescriptorsTest` |
| `structpointers_tests.mjs` | `DescriptorPointersTest` |
| `outputparameters_tests.mjs`, `outputbuffers_tests.mjs` | `OutputParametersTest` |
| `borrowedinterfaces_tests.mjs` | `InterfaceInheritanceTest`, `OutputParametersTest` |
| `interfacereferences_tests.mjs` | `InterfaceInheritanceTest` |
| `interfacefields_tests.mjs` | `InterfaceFieldsTest`, `DescriptorArraysTest` |
| `count_method_tests.mjs`, `computedoutputs_tests.mjs` | `ComputedLengthsTest` |
| `nested_counts_tests.mjs` | `ComputedFieldsTest` |
| `byte_span_tests.mjs` | `RawBytesTest` |
| `test_buffers.mjs` | `ScopedBytesTest` |
| `exports_tests.mjs` | `ConstantsTest`, `ScalarsTest` |
| `shared_native_tests.mjs` | `ScalarsTest`, `StringsTest`, `DescriptorsTest`, `ArraysTest`, `FixedArraysTest`, `ConstantsTest` |
| `defaultarguments_tests.mjs` | `DefaultArgumentsTest`, `ComputedLengthsTest` (all arguments supplied explicitly) |

Language-specific behavior remains intentional: Java descriptors are complete
values rather than partial JavaScript objects, unsigned scalar types widen as
documented by the Java backend, and malformed UTF-16 input is rejected rather
than replaced by `TextEncoder`. Required empty Java arrays retain non-null native
storage; the shared input fixture observes that distinction at count zero.

Wasm growth, module initialization/`wasmBinary`, virtual files, typed-array realms,
mutable prototypes, JavaScript getters, and Wasm address-range fault injection
remain JavaScript tests. A native JVM cannot validate arbitrary pointers against
a Wasm linear-memory range. Java keeps its own concurrent-close, Cleaner, native
exception, large-stack-input, and runtime-name tests. Temporary Wasm allocation
instrumentation is not copied into JNI; native reference counts and destruction
counts verify ownership, and JNI tests run with `-Xcheck:jni`.
