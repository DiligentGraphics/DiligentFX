# Texture assets

`IRadientTextureAsset::GetDesc()` returns an immutable `RadientTextureAssetDesc`
with the texture's width, height, pixel format, and mip-level count.

```cpp
const RadientTextureAssetDesc& Desc = Texture->GetDesc();
if (Desc.Width != 0)
{
    const Uint32 Width = Desc.Width;
    const Uint32 Height = Desc.Height;
    const RADIENT_TEXTURE_FORMAT Format = Desc.Format;
    const Uint32 MipLevels = Desc.MipLevels;
}
```

Before decoding completes, or if decoding fails, the descriptor has zero dimensions
and mip levels and `RADIENT_TEXTURE_FORMAT_UNKNOWN`. Query `GetDesc()` again after
loading progresses; a previously returned empty descriptor remains empty.

Once available, the description stays unchanged and its reference remains valid
while the texture asset is retained. It is available without a GPU and before GPU
uploads finish. Upload failure or cancellation does not discard decoded metadata.
`GetDesc()` may be called from any thread while the caller retains the asset.

Dimensions describe mip level zero of each array layer or cube face; height is
one for 1D textures. `Format` describes the loaded pixels after decoding and
requested format interpretation, including sRGB and block compression; it is
independent of how a material samples the texture. `MipLevels` includes loaded
and generated mip levels per layer or face. These properties describe the logical
asset; they do not report renderer allocation dimensions or GPU mip residency.

Encoded texture sources retain support for cube maps and texture arrays.
If a loaded format has no corresponding `RADIENT_TEXTURE_FORMAT` value, `Format`
is `RADIENT_TEXTURE_FORMAT_UNKNOWN`; the dimensions and mip count remain available.

`RadientTextureData` accepts all uncompressed formats in `RADIENT_TEXTURE_FORMAT`,
including SNORM, and generates mip levels. Compressed BC formats can be reflected
from encoded texture sources such as DDS or KTX; direct compressed input through
`RadientTextureData` is not supported.

C callers use `IRadientTextureAsset_GetDesc(Texture)` to obtain a pointer to the
same immutable description.
