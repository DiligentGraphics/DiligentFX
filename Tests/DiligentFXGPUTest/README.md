# DiligentFX GPU tests

Enable `DILIGENT_BUILD_FX_TESTS` and build the `DiligentFXGPUTest` target.
The executable uses the DiligentCore GPU test runner and its backend options.
Run tests with `Tests/DiligentFXGPUTest/assets` as the working directory.
For example, on Windows:

```text
DiligentFXGPUTest.exe --mode=d3d11 --gtest_filter=*AtlasSamplingTest*
```

Other supported build configurations can use `--mode=d3d12`, `--mode=gl`,
`--mode=vk`, or `--mode=wgpu`. Software adapters can use `d3d11_sw`,
`d3d12_sw`, or `vk_sw` where available.

The atlas tests use generated textures and the production `SampleTextureAtlas`
shader. They cover mip selection, region boundaries, UV margins, anisotropic
filtering, and mean-color sampling. They require no scene assets or Radient.

Two known issues have disabled regression tests: rotated anisotropic footprints
in the fallback LOD calculation, and fractional-LOD mip-tail bleed on affected
hardware. Run them explicitly with `--gtest_also_run_disabled_tests`; failures
are expected on affected backends until those shader issues are fixed.
