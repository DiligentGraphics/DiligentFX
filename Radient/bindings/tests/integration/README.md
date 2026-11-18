# Shared Radient binding integration tests

The Java and JavaScript tests exercise the real Radient implementation. Keep the
three reconciled suites aligned by case name, input values, assertions, failure
statuses, and ownership checks. Language-specific setup and API calls remain in
their respective test files; test operations are not moved behind native helpers.

| Java | JavaScript | Shared cases |
| --- | --- | --- |
| `DataBlobTest.java` | `data_blob_tests.mjs` | 11: owning copies, zero initialization, empty scopes, API exclusions, invalid ranges, native size rejection, read-only queries, shared access state, resize, retained scopes, cleanup after exceptions |
| `SceneTest.java` | `scene_tests.mjs` | 6: resource configuration, names/defaults, hierarchy, transforms/reparenting, visibility/flags, retained scenes |
| `ComponentsTest.java` | `components_tests.mjs` | 3: all camera fields, all light fields, defaults/validation/removal |

[`fixtures.properties`](fixtures.properties) supplies the byte patterns, UTF-8
names, transform inputs and expected matrix, resource configuration, and component
values to both implementations. It uses plain UTF-8 `key=value` lines and
comma-separated numeric lists, without escapes or continuations. Java reads it
with the JDK's `Properties.load(Reader)`; the JS helper reads the same subset.
Every array/descriptor accessor returns a fresh value so mutation tests do not
change later tests' inputs or expectations. No generated fixture copies or JSON
library dependency are needed.

Java's CTest command passes the file through `radient.test.fixtures`. The Radient
Node runner loads it directly. Browser CTest commands use `--test-data` to pass
the identical text to the suite. Missing fixtures fail the test rather than use
fallback values.

Intentional language-specific coverage:

- JavaScript also tests active byte scopes across Wasm memory growth (the 21st
  case), typed-array subviews, BigInt/Number mismatches, omitted arguments/fields,
  and dynamic rejection of native-only fields.
- Java has statically typed descriptors and no byte-array subview. It selects the
  same bytes into an input array and mutates that array after creation. Reflection
  checks that native-only fields and release methods are absent. Native `Uint64`
  values use Java `long` bits; negative values in widened `Uint32` fields are
  rejected, while `-1L` in `Uint64` means `UINT64_MAX`.
- Java explicitly passes the native `KeepWorldTransform=true` default; JS also
  tests omission of that argument. Java checks native-initialized descriptor
  constructors before the same values round-trip through the scene.
- Java uses try-with-resources for the processing-failure case; JS uses `finally`.
  Both verify that write access was released and the written byte remains visible.

The original Java checks for retained base interfaces, mutable empty reads and
growth, a retained writer copying byte 255, visibility restoration, and removal
of edited components are retained and also exercised in JavaScript.

The other asset, animation, and renderer suites remain JavaScript-only for now.
Extend this mapping when their existing cases are ported to Java.
