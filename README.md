# DiligentFX

DiligentFX is the [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine)'s high-level rendering framework.

| Platform                                                                                                                                    |   Build Status                    |
| ------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------- |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/windows-logo.png" width=24 valign="middle"> Windows            | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/workflows/Windows/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/windows.yml?query=branch%3Amaster) |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/uwindows-logo.png" width=24 valign="middle"> Universal Windows | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/workflows/UWP/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/uwp.yml?query=branch%3Amaster)         |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/linux-logo.png" width=24 valign="middle"> Linux                | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/workflows/Linux/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/linux.yml?query=branch%3Amaster)     |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/macos-logo.png" width=24 valign="middle"> MacOS                | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/workflows/MacOS/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/macos.yml?query=branch%3Amaster)     |
| <img src="https://github.com/DiligentGraphics/DiligentCore/blob/master/media/apple-logo.png" width=24 valign="middle"> iOS                  | [![Build Status](https://github.com/DiligentGraphics/DiligentFX/workflows/iOS/badge.svg?branch=master)](https://github.com/DiligentGraphics/DiligentFX/actions/workflows/ios.yml?query=branch%3Amaster)         |

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](License.txt)
[![Chat on gitter](https://badges.gitter.im/gitterHQ/gitter.png)](https://gitter.im/diligent-engine)
[![Chat on Discord](https://img.shields.io/discord/730091778081947680?logo=discord)](https://discord.gg/t7HGBK7)
[![Build Status](https://ci.appveyor.com/api/projects/status/github/DiligentGraphics/DiligentFX?svg=true)](https://ci.appveyor.com/project/DiligentGraphics/diligentfx)
[![Build Status](https://travis-ci.org/DiligentGraphics/DiligentFX.svg?branch=master)](https://travis-ci.org/DiligentGraphics/DiligentFX)
[![Codacy Badge](https://api.codacy.com/project/badge/Grade/e676fd09e6b34ce1a0b42242738b86e0)](https://www.codacy.com/manual/DiligentGraphics/DiligentFX?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=DiligentGraphics/DiligentFX&amp;utm_campaign=Badge_Grade)
[![Lines of Code](https://tokei.rs/b1/github.com/DiligentGraphics/DiligentFX)](https://github.com/DiligentGraphics/DiligentFX)

The framework implements the following components

* [Epipolar light scattering post-effect](https://github.com/DiligentGraphics/DiligentFX/tree/master/PostProcess/EpipolarLightScattering)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/PostProcess/EpipolarLightScattering/media/LightScattering.png" width=240>

* [Tone mapping utilities](https://github.com/DiligentGraphics/DiligentFX/tree/master/Shaders/PostProcess/ToneMapping/public)

* [Physically-Based GLTF2.0 Renderer](https://github.com/DiligentGraphics/DiligentFX/tree/master/GLTF_PBR_Renderer)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/GLTF_PBR_Renderer/screenshots/flight_helmet.jpg" width=240>

* [Shadows](https://github.com/DiligentGraphics/DiligentFX/tree/master/Components#shadows)
<img src="https://github.com/DiligentGraphics/DiligentFX/blob/master/Components/media/Powerplant-Shadows.jpg" width=240>

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
consistent source code style throughout the code base. The format is validated by appveyor and travis
for each commit and pull request, and the build will fail if any code formatting issue is found. Please refer
to [this page](https://github.com/DiligentGraphics/DiligentCore/blob/master/doc/code_formatting.md) for instructions
on how to set up clang-format and automatic code formatting.

------------------------------

[diligentgraphics.com](http://diligentgraphics.com)

[![Diligent Engine on Twitter](https://github.com/DiligentGraphics/DiligentCore/blob/master/media/twitter.png)](https://twitter.com/diligentengine)
[![Diligent Engine on Facebook](https://github.com/DiligentGraphics/DiligentCore/blob/master/media/facebook.png)](https://www.facebook.com/DiligentGraphics/)
