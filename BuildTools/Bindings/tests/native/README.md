# Shared native binding fixtures

This directory is the native reference for binding conformance tests. Java and
JavaScript compile the same sources and select their supported declarations.
New language backends should reuse this corpus rather than copy its native code.

- `BindingTestAPI.h` includes the portable fixture headers.
- `API.json` records shared ownership, nullability, array bounds, and lifetimes.
  Backend configurations extend it and select the APIs their generators support.
  Policies for contracts currently selected by only one backend remain in that
  backend's configuration; move them here when another backend selects them.
  Paths are relative to `BuildTools/Bindings`.
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
