# Temporal Anti Aliasing

## Table of contents
- [Introduction](#introduction)
- [Integration guidelines](#integration-guidelines)
    - [Input resources](#input-resources)
    - [Host API](#host-api)
- [Implementation details](#implementation-details)
- [References](#references)

## Introduction
We needed to add temporal antialiasing to our project that could run in WebGL. We based our implementation on the method described in [**[Emilio López, 2022]**](https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/)

## Integration guidelines

### Input resources

The following table enumerates all external inputs required by TAA.

| **Name**                          |  **Format**                        | **Notes**                                           |
| --------------------------------- |------------------------------------|---------------------------------------------------- |
| Color buffer                      | `APPLICATION SPECIFIED (3x FLOAT)` | The color buffer of the current frame provided by the application, in linear space. |


The effect uses a number of parameters to control the quality of the effect organized into the `HLSL::TemporalAntiAliasingAttribs` structure.
The following table lists the parameters and their descriptions.

| **Name**                                 | **Notes** |
| -----------------------------------------|-----------|
| Temporal stability factor                | The current implementation of TAA uses linear weighting based on the number of accumulated samples for each pixel. However, we can still determine a minimum alpha when blending the current and previous frames. |
| Reset accumulation                       | If this parameter is set to true, the current frame will be written to the current history buffer without interpolation with the previous history buffer. |
| Skip rejection                           | This parameter allows skipping the disocclusion check stage. Disocclusion check can cause flickering on a high-frequency signal. Use this parameter for static images to achieve honest supersampling. |

The effect can be configured using the `TemporalAntiAliasing::FEATURE_FLAGS` enumeration. The following table lists the flags and their descriptions.

| **Name**                                 | **Notes** |
| -----------------------------------------|-----------|
| `FEATURE_FLAG_GAUSSIAN_WEIGHTING`        | Use Gaussian weighting in the variance clipping step |
| `FEATURE_FLAG_BICUBIC_FILTER`            | Use Catmull-Rom filter to sample from the history buffer |


### Host API

To integrate TAA into your project, include the following necessary header files:
```cpp
#include "PostFXContext.hpp"
#include "TemporalAntiAliasing.hpp"
```
```cpp
namespace HLSL
{
#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PostProcess/TemporalAntiAliasing/public/TemporalAntiAliasingStructures.fxh"
} // namespace HLSL
```

Now, create the necessary objects:
```cpp
m_PostFXContext = std::make_unique<PostFXContext>(m_pDevice);
m_TAA           = std::make_unique<TemporalAntiAliasing>(m_pDevice);
```

Next, call the methods to prepare resources for the `PostFXContext` and `TemporalAntiAliasing` objects.
This needs to be done every frame before starting the rendering process.
```cpp
{
    PostFXContext::FrameDesc FrameDesc;
    FrameDesc.Index  = m_CurrentFrameNumber; // Current frame number.
    FrameDesc.Width  = SCDesc.Width;         // Current screen width.
    FrameDesc.Height = SCDesc.Height;        // Current screen height.
    m_PostFXContext->PrepareResources(m_pDevice, FrameDesc, PostFXContext::FEATURE_FLAG_NONE);

    TemporalAntiAliasing::FEATURE_FLAGS ActiveFeatures = ...;
    m_TAA->PrepareResources(m_pDevice, m_pImmediateContext, m_PostFXContext.get(), ActiveFeatures);
}
```

After that, invoke the `PostFXContext::Execute` method. At this stage, some intermediate resources necessary for all post-processing objects
dependent on `PostFXContext` are calculated. This method can take a constant buffer directly containing an array from the current and previous
cameras (for this method, you can refer to this section of the code [[0](https://github.com/DiligentGraphics/DiligentSamples/blob/380b0a05b6c72d80fd6d574d7343ead77d6dd7eb/Tutorials/Tutorial27_PostProcessing/src/Tutorial27_PostProcessing.cpp#L164)] and [[1](https://github.com/DiligentGraphics/DiligentSamples/blob/380b0a05b6c72d80fd6d574d7343ead77d6dd7eb/Tutorials/Tutorial27_PostProcessing/src/Tutorial27_PostProcessing.cpp#L228)]).
Alternatively, you can pass the corresponding pointers `const HLSL::CameraAttribs* pCurrCamera` and `const HLSL::CameraAttribs* pPrevCamera` for the current
and previous cameras, respectively. You also need to pass the depth of the current and previous frames (the depth buffers should not contain transparent objects), and a buffer with motion vectors in NDC space, into the corresponding `ITextureView* pCurrDepthBufferSRV`, `ITextureView* pPrevDepthBufferSRV`, `ITextureView* pMotionVectorsSRV` pointers.

```cpp
{
    PostFXContext::RenderAttributes PostFXAttibs;
    PostFXAttibs.pDevice             = m_pDevice;
    PostFXAttibs.pDeviceContext      = m_pImmediateContext;
    PostFXAttibs.pCameraAttribsCB    = m_FrameAttribsCB;  // m_Resources[RESOURCE_IDENTIFIER_CAMERA_CONSTANT_BUFFER].AsBuffer();
    PostFXAttibs.pCurrDepthBufferSRV = m_CurrDepthBuffer; // m_Resources[RESOURCE_IDENTIFIER_DEPTH0 + CurrFrameIdx].GetTextureSRV();
    PostFXAttibs.pPrevDepthBufferSRV = m_PrevDepthBuffer; // m_Resources[RESOURCE_IDENTIFIER_DEPTH0 + PrevFrameIdx].GetTextureSRV();
    PostFXAttibs.pMotionVectorsSRV   = m_MotionBuffer;    // m_GBuffer->GetBuffer(GBUFFER_RT_MOTION_VECTORS)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    m_TAA->Execute(PostFXAttibs);
}
```

TAA requires that each frame be rendered with some offset relative to the pixel. Therefore, you need to pass a modified projection matrix and jitter when updating the camera buffer `HLSL::CameraAttribs` for the current frame, as in the code below (you can refer to this section of the code [[2](https://github.com/DiligentGraphics/DiligentSamples/blob/380b0a05b6c72d80fd6d574d7343ead77d6dd7eb/Tutorials/Tutorial27_PostProcessing/src/Tutorial27_PostProcessing.cpp#L270)])
```cpp
    auto ComputeProjJitterMatrix = [&](const float4x4& ProjMatrix, float2 Jitter) -> float4x4 {
        float4x4 Result = ProjMatrix;
        Result[2][0]    = Jitter.x;
        Result[2][1]    = Jitter.y;
        return Result;
    };

    const float2 Jitter = m_TAA->GetJitterOffset();

    const float4x4 CameraView     = m_Camera.GetViewMatrix();
    const float4x4 CameraProj     = ComputeProjJitterMatrix(GetAdjustedProjectionMatrix(YFov, ZNear, ZFar), Jitter);
    const float4x4 CameraViewProj = CameraView * CameraProj;
  
    auto& CurrCamAttribs          = m_CameraAttribs[CurrFrameIdx];
    
    // ...
    CurrCamAttribs.mViewT         = CameraView.Transpose();
    CurrCamAttribs.mProjT         = CameraProj.Transpose();
    CurrCamAttribs.mViewProjT     = CameraViewProj.Transpose();
    CurrCamAttribs.mViewInvT      = CameraView.Inverse().Transpose();
    CurrCamAttribs.mProjInvT      = CameraProj.Inverse().Transpose();
    CurrCamAttribs.mViewProjInvT  = CameraViewProj.Inverse().Transpose();
    CurrCamAttribs.f2Jitter.x     = Jitter.x;
    CurrCamAttribs.f2Jitter.y     = Jitter.y;
    // ...
```

After rendering the entire frame, you need to invoke the TAA computation. Note that the frame passed in the color buffer argument must be in linear space, that is, before the tone mapping, as well as before effects such as Bloom and Depth Of Field. To do this, we call the `TemporalAntiAliasing::Execute` method. Before this, we need to fill the passed structures `TemporalAntiAliasingAttribs` and `TemporalAntiAliasing::RenderAttributes ` with the necessary data. Refer to the [Input resources section](#input-resources) for parameter description.
```cpp
{
    HLSL::TemporalAntiAliasingAttribs TAASettings{};

    TemporalAntiAliasing::RenderAttributes TAARenderAttribs{};
    TAARenderAttribs.pDevice         = m_pDevice;
    TAARenderAttribs.pDeviceContext  = m_pImmediateContext;
    TAARenderAttribs.pPostFXContext  = m_PostFXContext.get();
    TAARenderAttribs.pColorBufferSRV = m_ColorBuffer; // m_Resources[RESOURCE_IDENTIFIER_RADIANCE0 + CurrFrameIdx].GetTextureSRV();
    TAARenderAttribs.pTAAAttribs     = &TAASettings;
    m_TAA->Execute(TAARenderAttribs);
}

```

Now, you can directly obtain a `ITextureView` on the texture containing the TAA result using the method `TemporalAntiAliasing::GetAccumulatedFrameSRV`.
After this, you can pass the TAA result to the tone mapping stage.

## Implementation details

In general, all TAA implementations use similar core concepts. For a brief introduction to how the initial implementation of temporal anti-aliasing works, refer to [**[Emilio López, 2022]**](https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/). We based our implementation on the one described in the article, but we made some modifications:
* We use linear weighting instead of exponential weighting, meaning we keep track of the number of accumulated samples for each pixel. This allows us to increase the rate of convergence and reduce ghosting. You can read more about it in [**[ReBLUR, 2020]**](https://developer.download.nvidia.com/video/gputechconf/gtc/2020/presentations/s22699-fast-denoising-with-self-stabilizing-recurrent-blurs.pdf) page 25
* We use variance clipping instead of AABB clamping. This results in slightly better quality, more detailed description can be found here [**[Marco Salvi, 2016]**](https://developer.download.nvidia.com/gameworks/events/GDC2016/msalvi_temporal_supersampling.pdf). We perform variance clipping in the YCoCg space, as it helps to eliminate the artifact of a purple hue appearing on some surfaces [**[Brian Karis, 2014]**](https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf) page 35.
* We use dynamic variance gamma based on pixel velocity, which helps to reduce flickering on static frames.
* During cubic filtering from the history buffer, we use the approach from [**[Jorge Jimenez, 2016]**](https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf) slide 77. This allows a slight performance improvement.

## References

- **[Lei Yang, 2020]**: A Survey of Temporal Antialiasing Techniques - http://behindthepixels.io/assets/files/TemporalAA.pdf
- **[Emilio López, 2022]**: Temporal AA and the quest for the Holy Trail - https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
- **[Brian Karis, 2014]**: High Quality Temporal Supersampling - https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf
- **[Marco Salvi, 2016]**: An Excursion in Temporal Supersampling - https://developer.download.nvidia.com/gameworks/events/GDC2016/msalvi_temporal_supersampling.pdf
- **[ReBLUR, 2020]**: Fast Denoising with Self Stabilizing Recurrent Blurs - https://developer.download.nvidia.com/video/gputechconf/gtc/2020/presentations/s22699-fast-denoising-with-self-stabilizing-recurrent-blurs.pdf
- **[Jorge Jimenez, 2016]**: Filmic SMAA - https://advances.realtimerendering.com/s2016/Filmic%20SMAA%20v7.pptx
