# DiligentFX

DiligentFX is the [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine)'s high-level rendering framework.

| Platform                                                                                                                                    |   Build Status                    |
| ------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/windows-logo.png" width=24 valign="middle"> Windows            | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-windows.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-windows.yml?query=branch%3Amaster) |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/uwindows-logo.png" width=24 valign="middle"> Universal Windows | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-windows.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-windows.yml?query=branch%3Amaster) |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/linux-logo.png" width=24 valign="middle"> Linux                | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-linux.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-linux.yml?query=branch%3Amaster) |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/macos-logo.png" width=24 valign="middle"> MacOS                | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-apple.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-apple.yml?query=branch%3Amaster) |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/apple-logo.png" width=24 valign="middle"> iOS                  | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-apple.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-apple.yml?query=branch%3Amaster) |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/tvos-logo.png" width=24 valign="middle"> tvOS                  | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-apple.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-apple.yml?query=branch%3Amaster) |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/emscripten-logo.png" width=24 valign="middle"> Emscripten      | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-emscripten.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/build-emscripten.yml?query=branch%3Amaster) | 

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](License.txt)
[![Chat on Discord](https://img.shields.io/discord/730091778081947680?logo=discord)](https://discord.gg/t7HGBK7)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/github/DiligentGraphics/DiligentFX?svg=true)](https://ci.appveyor.com/project/DiligentGraphics/diligentfx)
[![MSVC Code Analysis](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/msvc_analysis.yml/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/msvc_analysis.yml?query=branch%3Amaster)
[![Lines of Code](https://tokei.rs/b1/github.com/DiligentGraphics/DiligentFX)](https://github.com/DiligentGraphics/DiligentFX)

The following components are now available:

<a name="gltf_loader_and_renderer"></a>
* [GLTF2.0 Loader](https://github.com/DiligentGraphics/DiligentTools/tree/master/AssetLoader)
  and [Physically-based renderer with image-based lighting](https://github.com/DiligentGraphics/DiligentFX/tree/master/PBR).
  
|||
|-----------------|-----------------|
| ![](https://github.com/DiligentGraphics/DiligentFX/blob/master/PBR/screenshots/damaged_helmet.jpg) | ![](https://github.com/DiligentGraphics/DiligentFX/blob/master/PBR/screenshots/flight_helmet.jpg) |
| ![](https://github.com/DiligentGraphics/DiligentFX/blob/master/PBR/screenshots/mr_spheres.jpg)     | ![](https://github.com/DiligentGraphics/DiligentSamples/blob/master/Samples/GLTFViewer/screenshots/cesium_man_large.gif)  |


* [Hydrogent](https://github.com/DiligentGraphics/DiligentFX/tree/master/Hydrogent), an implementation of the Hydra rendering API in Diligent Engine.
<img src="https://github.com/DiligentGraphics/DiligentSamples/blob/master/Samples/USDViewer/Screenshot.jpg" width=400>

* [Shadows](https://github.com/DiligentGraphics/DiligentFX/tree/master/Components#shadows)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/Components/media/Powerplant-Shadows.jpg" width=400>


**Post-processing effects**

* [Screen-Space Reflections](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/ScreenSpaceReflection)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/PostProcess/ScreenSpaceReflection/media/ssr-logo.jpg" width=400>

* [Screen-Space Ambient Occlusion](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/ScreenSpaceAmbientOcclusion)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/PostProcess/ScreenSpaceAmbientOcclusion/media/ssao-kitchen.jpg" width=400>

* [Depth of Field](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/DepthOfField)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/PostProcess/DepthOfField/media/depth_of_field.jpg" width=400>

* [Bloom](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/Bloom)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/PostProcess/Bloom/media/bloom.jpg" width=400>

* [Epipolar light scattering](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/EpipolarLightScattering)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/PostProcess/EpipolarLightScattering/media/LightScattering.png" width=400>

* [Temporal Anti-Aliasing](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/TemporalAntiAliasing)

* [Tone mapping shader utilities](https://github.com/DiligentGraphics/DiligentFX/tree/master/Shaders/PostProcess/ToneMapping/public)


# License

See [Apache 2.0 license](License.txt).


<a name="contributing"></a>
# Contributing

To contribute your code, submit a [Pull Request](https://github.com/DiligentGraphics/DiligentFX/pulls) 
to this repository. **Diligent Engine** is licensed under the [Apache 2.0 license](License.txt) that guarantees 
that content in the **DiligentFX** repository is free of Intellectual Property encumbrances.
In submitting any content to this repository,
[you license that content under the same terms](https://docs.github.com/en/free-pro-team@latest/github/site-policy/github-terms-of-service#6-contributions-under-repository-license),
and you agree that the content is free of any Intellectual Property claims and you have the right to license it under those terms. 

Diligent Engine uses [clang-format](https://clang.llvm.org/docs/ClangFormat.html) to ensure
consistent source code style throughout the code base. The format is validated by CI
for each commit and pull request, and the build will fail if any code formatting issue is found. Please refer
to [this page](https://github.com/DiligentGraphics/DiligentCore/blob/master/doc/code_formatting.md) for instructions
on how to set up clang-format and automatic code formatting.

------------------------------

[diligentgraphics.com](http://diligentgraphics.com)

[![Diligent Engine on Twitter](https://github.com/DiligentGraphics/DiligentCore/blob/master/media/twitter.png)](https://twitter.com/diligentengine)
[![Diligent Engine on Facebook](https://github.com/DiligentGraphics/DiligentCore/blob/master/media/facebook.png)](https://www.facebook.com/DiligentGraphics/)
