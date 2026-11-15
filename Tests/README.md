# Radient native suites in Chrome

`RadientTest` runs CPU tests without a graphics device. `RadientGPUTest` runs GPU
tests with WebGPU and SwiftShader. Both use the shared browser harness from Core,
which runs the selected suite in one Chrome session and streams each test's
output. These complement the JavaScript bindings and golden-image tests under
`Radient/bindings/javascript/tests`.

Configure an Emscripten build with `DILIGENT_BUILD_FX_TESTS=ON`, then build the
`RadientTest` and `RadientGPUTest` targets. Enable
`DILIGENT_EMSCRIPTEN_STRIP_DEBUG_INFO=ON` for browser execution: large embedded
DWARF sections can exceed Chrome's Wasm module-size limit in Debug builds.
The executables enable JSPI and preload four pthread workers per logical CPU.

From an engine checkout, run the CPU suite with:

```sh
python DiligentFX/Tests/RadientTest/browser/run_tests.py \
    --module build/Emscripten/DiligentFX/Tests/RadientTest/RadientTest.js \
    --output RadientCPUBrowserTestResults
```

Use `Tests/RadientGPUTest/browser/run_tests.py` and the `RadientGPUTest.js` module
for GPU tests. The `run-radient-cpu-browser-tests` and
`run-radient-gpu-browser-tests` actions in `DiligentGraphics/github-action` supply
the platform-specific Chrome arguments and upload logs and Google Test XML even
after failures. CPU tests run on Linux, Windows, and macOS; GPU tests run on Linux
and Windows. The actions run already-built executables.

Each suite keeps its browser exclusions in `browser/exclude.txt`. Excluded tests
currently require C++ exception recovery, which the Web build does not support.
New tests run by default. `--filter` selects tests, `--no-exclusions` disables the
exclusion list, and `--browser` overrides automatic Chrome discovery.
