# Radient Java bindings

The generated `diligent.radient.Radient` class and `RadientJNI` library call the
production Radient implementation. The Java target consumes the shared
[`API.json`](../API.json), [`Adapters.json`](../Adapters.json), and native adapters;
it does not duplicate Radient sources or use a mock engine.

The initial integration supports CPU use. Owning blobs, engines, scenes, assets,
and animation use the existing shared binding selection. Native graphics handles,
external thread pools, callbacks, and reference-storage blobs are not exposed.
Creating an engine therefore uses the default CPU backend and an internally owned
thread pool. Android packaging and graphics-device integration are separate work.

## Build and test

Use a native DiligentEngine build, with a JDK 11+ matching the native target's
architecture and Python configured with the LLVM 22 binding requirements.
`JAVA_HOME` must identify the JDK, including its JNI development headers.

```sh
cmake -S DiligentEngine -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRADIENT_BUILD_JAVA_BINDINGS=ON \
  -DRADIENT_BINDINGS_PYTHON_EXECUTABLE=/path/to/venv/bin/python \
  -DRADIENT_BINDINGS_LIBCLANG=/path/to/libclang.so
cmake --build build --target Radient-JavaTests --parallel
ctest --test-dir build/DiligentFX/Radient/bindings/java/tests \
  --verbose --output-on-failure --no-tests=error
```

For a multi-configuration generator, pass `--config Debug` when building and
`-C Debug` to CTest. The option is off by default, so normal native builds do not
require Java. When enabled, a normal project build also builds the bindings and
their tests. Generation uses the configured native target rather than Wasm's ABI.

Generated sources and classes reside in the build directory under
`DiligentFX/Radient/bindings/java/generated`. Applications use the `generated/classes`
directory on their classpath and the directory containing `RadientJNI` on
`java.library.path`. Generated sources are not checked in. JAR distribution is not
part of this initial build integration.

## API conventions

Descriptors are Java objects initialized with native defaults. Fill their fields
before passing them to native code. Strings, arrays, and descriptor outputs are
independent snapshots. Native status values are preserved: methods with outputs
return a result object containing `returnValue` and fields named after the native
output arguments.

```java
import static diligent.radient.Radient.*;
import static diligent.radient.Radient.RADIENT_STATUS.*;

RadientDataBlobCreateInfo info = new RadientDataBlobCreateInfo();
info.pData = new byte[]{17, (byte)128, (byte)254};
info.Size = info.pData.length;
var created = CreateRadientMutableDataBlob(info);
try (IRadientMutableDataBlob blob = created.ppBlob) {
    if (created.returnValue != RADIENT_STATUS_OK)
        throw new IllegalStateException("Blob creation failed");
    var acquired = blob.BeginWrite();
    try (WriteByteScope bytes = acquired.ppData) {
        if (acquired.returnValue != RADIENT_STATUS_OK)
            throw new IllegalStateException("Blob write access failed");
        bytes.writeByte(0, 42);
    }
}
```

Close interface wrappers and byte scopes with try-with-resources. Borrowed
interface returns retain their own reference and must also be closed. Scope
release methods are hidden on the blob interface; closing the scope releases
native access. Java callers supply every input argument, including native default
arguments. See the [generic Java backend](../../../BuildTools/Bindings/java/README.md)
for unsigned integer mappings and other marshalling and ownership rules.

## Integration coverage

The tests in `tests/` use real Radient objects and print each case to the console.
They run with `-Xcheck:jni` and require no GPU or browser. The 41 cases mirror the
existing JavaScript suites, using the same input and expected-value
[fixtures](../tests/integration/README.md). CTest supplies the fixture path.

- `DataBlobTest`: copied and zero-initialized data, empty scopes, access exclusion,
  resize, interface queries, and lifetimes after wrapper closure.
- `SceneTest`: CPU engine creation, UTF-8 names, hierarchy enumeration, scalar and
  matrix outputs, transform revisions, reparenting, visibility, and retained scenes.
- `ComponentsTest`: camera/light defaults, setters, independent snapshots,
  component presence, removal, and native status propagation.
- `MeshTest` and `TextureTest`: asynchronous CPU loading, source blob lifetimes,
  layout/mip reflection, procedural helpers, encoded decoding, and validation.
- `MaterialTest` and `MaterialEditTest`: reflection/defaults, retained assets,
  shader/surface writes, texture initialization, and rejected later texture edits.
- `AnimationTest`: copied sampler bytes, scene/material/pose/morph evaluation,
  registry snapshots/removals, retained bindings, and quaternion reference outputs.
- `MorphTest`: delta blob retention, weights/versioning/defaults, validation, and
  scene-component removal with retained mesh/weight wrappers.
- `SkinningTest`: joint/skin descriptors, pose arrays, deferred commits, computed
  and transposed matrices, default/invalid inputs, and retained skin/pose snapshots.
- `ImportTest`: shared GLB and OBJ/MTL fixtures, scene instantiation, imported
  animation evaluation, relative dependencies, and unsupported importer failures.

These tests verify production integration rather than repeat the full native test
suite. Exhaustive marshalling and failure cleanup remain in the
[shared fixture tests](../../../BuildTools/Bindings/tests/native/README.md).
Linux CI runs both sets in the Clang/Debug job; its main build produces the Radient
JNI library and classes before CTest executes the integration tests.
