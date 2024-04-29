# Bloom

![](media/bloom.jpg)

## Table of contents
- [Introduction](#introduction)
- [Integration guidelines](#integration-guidelines)
    - [Input resources](#input-resources)
    - [Host API](#host-api)
- [Implementation details](#implementation-details)
- [References](#references)

## Introduction
Displays have a low dynamic range (LDR), limiting brightness levels to a scale from black to full brightness (RGB 0 to 1), unlike the unlimited brightness in real life. High dynamic range (HDR) technology allows for brightness levels above 1, though actual displays still limit these to their maximum LDR brightness. HDR scenes are adapted for LDR displays through tonemapping and auto-exposure, which adjust the image's brightness and contrast.

Bloom is an effect that simulates intense brightness by causing bright pixels to bleed into adjacent ones.

## Integration guidelines

### Input resources

The following table enumerates all external inputs required by Bloom effect.

| **Name**                          |  **Format**                        | **Notes**                                           |
| --------------------------------- |------------------------------------|---------------------------------------------------- |
| Color buffer                      | `APPLICATION SPECIFIED (3x FLOAT)` | The HDR render target of the current frame containing the scene radiance |


The effect uses a number of parameters that control the quality and performance and are organized into the `HLSL::ScreenSpaceAmbientOcclusionAttribs` structure.
The following table lists the parameters and their descriptions.

| **Name**                                 | **Notes** |
| -----------------------------------------|-----------|
| `Intensity`                              | Intensity of the bloom effect. |
| `Threshold`                              | The minimum brightness required for a pixel to contribute to the bloom effect. |
| `SoftTreshold`                           | The softness of the threshold. A higher value will result in a softer threshold. |
| `Radius`                                 | The size of the bloom effect. A larger radius will result in a larger area of the image being affected by the bloom effect. |

### Host API

To integrate Bloom into your project, include the following header files:
```cpp
#include "PostFXContext.hpp"
#include "Bloom.hpp"
```
```cpp
namespace HLSL
{
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PostProcess/Bloom/public/BloomStructures.fxh"
} // namespace HLSL
```

Now, create the necessary objects:
```cpp
m_PostFXContext = std::make_unique<PostFXContext>(m_pDevice);
m_Bloom         = std::make_unique<Bloom>(m_pDevice);
```

Next, call the methods to prepare resources for the `PostFXContext` and `Bloom` objects.
This needs to be done every frame before starting the rendering process.
```cpp
PostFXContext::FrameDesc FrameDesc;
FrameDesc.Index  = m_CurrentFrameNumber; // Current frame number.
FrameDesc.Width  = SCDesc.Width;         // Current screen width.
FrameDesc.Height = SCDesc.Height;        // Current screen height.
m_PostFXContext->PrepareResources(m_pDevice, FrameDesc, PostFXContext::FEATURE_FLAG_NONE);

m_Bloom->PrepareResources(m_pDevice, m_pImmediateContext, m_PostFXContext.get(), Bloom::FEATURE_FLAG_NONE);
```

Next, call the `PostFXContext::Execute` method prepare intermediate resources required by all post-processing objects
dependent on `PostFXContext`. This method can take a constant buffer containing the current and previous-frame
cameras (refer to these code examples: [[0](https://github.com/DiligentGraphics/DiligentSamples/blob/380b0a05b6c72d80fd6d574d7343ead77d6dd7eb/Tutorials/Tutorial27_PostProcessing/src/Tutorial27_PostProcessing.cpp#L164)] and [[1](https://github.com/DiligentGraphics/DiligentSamples/blob/380b0a05b6c72d80fd6d574d7343ead77d6dd7eb/Tutorials/Tutorial27_PostProcessing/src/Tutorial27_PostProcessing.cpp#L228)]).
Alternatively, you can pass the corresponding pointers `const HLSL::CameraAttribs* pCurrCamera` and `const HLSL::CameraAttribs* pPrevCamera` for the current
and previous cameras, respectively. You also need to pass the depth of the current and previous frames, and a buffer with motion vectors in NDC space, via the corresponding `ITextureView* pCurrDepthBufferSRV`, `ITextureView* pPrevDepthBufferSRV`, and `ITextureView* pMotionVectorsSRV` pointers.

```cpp
PostFXContext::RenderAttributes PostFXAttibs;
PostFXAttibs.pDevice             = m_pDevice;
PostFXAttibs.pDeviceContext      = m_pImmediateContext;
PostFXAttibs.pCameraAttribsCB    = m_FrameAttribsCB;
m_PostFXContext->Execute(PostFXAttibs);
```

To calculate bloom effect, call the `Bloom::Execute` method. Before this, fill the `BloomAttribs` and `Bloom::RenderAttributes ` structures with the necessary data. Refer to the [Input resources section](#input-resources) for parameter description.
```cpp
HLSL::BloomAttribs BloomSettings{};

Bloom::RenderAttributes BloomRenderAttribs{};
BloomRenderAttribs.pDevice         = m_pDevice;
BloomRenderAttribs.pDeviceContext  = m_pImmediateContext;
BloomRenderAttribs.pPostFXContext  = m_PostFXContext.get();
BloomRenderAttribs.pColorBufferSRV = m_ColorBuffer;
BloomRenderAttribs.pBloomAttribs   = &BloomSettings;
m_Bloom->Execute(SSAORenderAttribs);
```

To obtain an `ITextureView` of the texture containing the bloom result, use the `Bloom::GetBloomTextureSRV` method.

## Implementation details

Our implementation is based on the technique described in **[Léna Piquet, 2021]**.
We modified it by adding a threshold and using the Additive Bloom as described in **[Alexander Christensen, 2022]**.
Additionally, we changed the weighting to eliminate fireflies from this article (section Bonus material 2: Karis average) as follows:

```cpp
float Weights[5];
Weights[0] = 0.125f;
Weights[1] = 0.125f;
Weights[2] = 0.125f;
Weights[3] = 0.125f;
Weights[4] = 0.5f;

float3 Groups[5];
Groups[0] = (A + B + D + E) / 4.0f;
Groups[1] = (B + C + E + F) / 4.0f;
Groups[2] = (D + E + G + H) / 4.0f;
Groups[3] = (E + F + H + I) / 4.0f;
Groups[4] = (J + K + L + M) / 4.0f;
    
float4 ColorSum = float4(0.0, 0.0, 0.0, 0.0);
for (int GroupId = 0; GroupId < 5; ++GroupId) {
    float Weight = Weights[GroupId] * KarisAverage(Groups[GroupId]);
    ColorSum += float4(Groups[GroupId], 1.0) * Weight;
}
float3 ResultColor = ColorSum.xyz / ColorSum.w;
```

## References

- **[Jimenez, 2014]**: Next Generation Post Processing in Call of Duty: Advanced Warfare - https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
- **[Léna Piquet, 2021]**: Custom Bloom Post-Process in Unreal Engine - https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/
- **[Alexander Christensen, 2022]**: Physically Based Bloom - https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom/
