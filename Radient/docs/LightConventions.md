# Light Conventions

Radient lights follow the glTF and OpenUSD convention for oriented lights:

- Directional lights emit along their local negative Z axis.
- Spot lights emit along their local negative Z axis.
- Point lights emit from their world-space position and do not use orientation.

For directional and spot lights, the light emission direction in world space is the transformed local `-Z` axis. With Radient's row-vector matrix convention, this is:

```cpp
const RadientFloat4 LocalZ = WorldMatrix.GetRow(2);
const float3 Direction = normalize(float3{-LocalZ.x, -LocalZ.y, -LocalZ.z});
```

## Light Types

Directional lights use entity orientation only. Their position does not affect direct lighting.

Point lights use entity position only. Their orientation does not affect direct lighting.

Spot lights use both entity position and orientation. Position defines the cone origin, and local `-Z` defines the cone axis.

## Spot Cone Angles

Radient follows the glTF `KHR_lights_punctual` convention for spot cone angles:

- `RadientLightComponent::InnerConeAngle` is stored in radians.
- `RadientLightComponent::OuterConeAngle` is stored in radians.
- Both angles are measured away from the light's local `-Z` axis.

The cone angle is an off-axis half-angle, not the full cone aperture. For example, a value of `PI / 4` means the spot cone extends 45 degrees away from the light's local `-Z` axis.

The inner cone is the region of full intensity. The outer cone is where spot falloff reaches zero. The default values match glTF: inner cone angle is `0`, and outer cone angle is `PI / 4`.

## Renderer Direction

Radient passes light direction to the PBR renderer as the direction the light emits or travels. Shader code that needs the direction from a shaded point toward the light uses the negated direction for directional and spot lighting.
