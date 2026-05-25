# Camera Conventions

Radient cameras follow the glTF and OpenUSD camera convention:

- The camera looks along its local negative Z axis.
- Local positive Y is up.
- The entity world transform stores the camera transform in scene space.
- The view matrix is the inverse of the camera world transform.

This means an identity camera transform looks down scene-space `-Z`. To look at an object placed at the origin from in front of it, place the camera on positive Z:

```cpp
RadientEntityDesc CameraDesc{};
CameraDesc.Transform.Position = {0.f, 0.f, 5.f};
```

## Projection

Diligent projection helpers use left-handed camera space, where positive Z is forward. Radient keeps the scene camera convention as local `-Z` and applies the camera-space adapter inside the projection matrix.

As a result:

- `mView` is the true inverse of the camera world transform.
- `mViewInv` is the camera world transform.
- `mProj` includes the conversion from Radient camera space to the camera space expected by Diligent projection helpers.

Renderer and post-processing code should treat the camera world transform as a Radient scene transform. It should not add another Z flip outside the Radient camera projection path.

## Clip Range And Lens Values

`RadientCameraComponent::ClippingRange` stores near and far clipping distances in scene units. Perspective projection is derived from:

- `FocalLength`
- `HorizontalAperture`
- `VerticalAperture`

Orthographic projection uses `HorizontalAperture` and `VerticalAperture` as the view size.
