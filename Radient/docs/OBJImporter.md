# OBJ import

Radient includes an OBJ scene importer registered as `"obj"`. `LoadScene()` selects
it automatically for `.obj` URIs, or it can be selected with
`RadientSceneLoadInfo::ImporterId = "obj"`.

```cpp
RadientSceneLoadInfo LoadInfo;
LoadInfo.URI = "models/model.obj";
RefCntAutoPtr<IRadientSceneAsset> pScene;
RADIENT_STATUS Status = pAssetManager->LoadScene(LoadInfo, &pScene);
```

Scene loading uses the normal asynchronous asset pipeline. Referenced MTL files
are resolved relative to the OBJ file's resolved URI. Image paths are resolved
relative to the MTL file that declares them, through the configured asset resolver.

## Geometry

- Positions, texture coordinates, normals, and RGB/RGBA vertex colors are supported.
- Faces may use separate position, UV, and normal indices, including negative
  indices. Triangles and simple planar polygons are supported; concave polygons
  are triangulated with Diligent's polygon triangulator.
- Missing normals are generated. Smoothing groups share normals across UV seams;
  `s off` uses a separate face normal. Authored normals are preserved.
- Object and group changes start mesh groups. Material changes produce primitive
  ranges within a mesh. The result contains one scene with identity-transform
  root nodes for its meshes.
- Position units, coordinate axes, winding, and texture coordinates are preserved.
  Homogeneous positions are divided by W. There is no automatic axis conversion
  or V flip.

Lines, points, free-form curves, and surfaces are not rendered. Self-intersecting
or non-planar polygons are outside the polygon triangulator's supported input.
Malformed face indices and invalid numeric data fail loading with a diagnostic.

## Materials

MTL materials are mapped to Radient standard materials:

| MTL property | Radient property |
| --- | --- |
| `Kd`, `d` / `Tr` | Diffuse color and opacity (`d` takes precedence) |
| `Ks` | Specular color |
| `Ke` | Emissive color |
| `Ns` | Glossiness, approximated as `1 - sqrt(2 / (Ns + 2))` |
| `map_Kd` | Diffuse texture, loaded as sRGB |
| `map_Ks` | Specular-glossiness texture, loaded as sRGB |
| `map_Ke` | Emissive texture, loaded as sRGB |
| `norm` | Normal texture, loaded as linear data |

`illum 0` uses an unlit material with `Kd` and `map_Kd` as its base color. Other
illumination models use specular-glossiness shading; advanced reflection and
refraction models are approximated. Opacity below one enables transparent rendering. Diffuse/specular colors and
opacity are clamped to [0, 1]; emissive values are clamped to nonnegative values.

Texture `-s`, `-o`, and `-clamp` options control UV scale, offset, and addressing.
For `norm` maps, `-bm` sets the normal scale.
The specular-glossiness texture's alpha channel also multiplies glossiness; use
opaque specular maps when this modulation is unwanted. Phong shininess and PBR
glossiness are different shading models, so their appearance is approximate.

Height-map `bump`/`map_Bump`, separate opacity maps, and procedural material maps
are not converted. Missing material libraries or unknown material names use the
default material and produce a warning.

Failed texture sources use the standard material texture fallback when available.
Returned texture handles retain their load failure status.
