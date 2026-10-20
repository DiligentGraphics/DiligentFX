# Camera Conventions

Radient cameras follow the glTF and OpenUSD camera convention:

- The camera looks along its local negative Z axis.
- Local positive Y is up.
- The entity world transform stores the camera transform in scene space.

This means an identity camera transform looks down scene-space `-Z`. To look at an object placed at the origin from in front of it, place the camera on positive Z:

```cpp
RadientEntityDesc CameraDesc{};
CameraDesc.Transform.Position = {0.f, 0.f, 5.f};
```

## View And Projection

Diligent shaders, projection helpers, and post-processing effects use camera space where positive Z is forward. The renderer converts the Radient camera world transform to this convention before computing the view matrix:

```cpp
CameraWorld = float4x4::Scale(1.f, 1.f, -1.f) * SceneCameraWorld;
mView       = CameraWorld.Inverse();
mViewInv    = CameraWorld;
```

The scene entity retains its original world transform. The renderer's `mView` and `mViewInv` use the converted transform, and `mProj` is a standard Diligent projection for positive-Z camera space. The projection also accounts for the device's depth range, reverse depth, and temporal jitter when enabled.

Applications should supply camera entity transforms in the local `-Z` convention. The renderer performs the conversion, so applications should not add another Z flip to the entity transform or the renderer's camera matrices.

## Clip Range And Lens Values

`RadientCameraComponent::ClippingRange` stores near and far clipping distances in scene units.

For perspective cameras, `VerticalAperture` and `FocalLength` are measured in millimeters and determine the vertical field of view:

```cpp
VerticalFieldOfView = 2.f * atan(VerticalAperture / (2.f * FocalLength));
```

The projection's aspect ratio comes from the viewport width divided by its height. `HorizontalAperture` stores the horizontal sensor size in millimeters for lens effects; it does not determine the perspective projection's aspect ratio or field of view.

For orthographic cameras, `HorizontalAperture` and `VerticalAperture` specify the view width and height in scene units. Both dimensions are used directly; the viewport aspect ratio and `FocalLength` do not affect the orthographic projection.

`HorizontalApertureOffset` and `VerticalApertureOffset` are stored in the camera component but are currently not applied by the renderer.
