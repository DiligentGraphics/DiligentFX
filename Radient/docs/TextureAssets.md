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

`RadientTextureData` accepts all defined formats except `RADIENT_TEXTURE_FORMAT_UNKNOWN`.
Supply consecutive levels starting at mip 0 through `pMipLevels` and `MipLevelCount`.
Each `RadientTextureMipData` specifies a blob, byte offset, and row stride. Different
levels can share a blob or use separate blobs; zero stride selects tightly packed
pixel rows or compressed block rows for that level. The supplied bytes are consumed
without copying or repacking.

Mip dimensions describe the logical image in pixels. Compressed storage rounds
those dimensions up to complete blocks using the format's block width and height.
Even a logical 1x1 mip requires one full compression block.

`GenerateMips` defaults to `True`. For uncompressed input, missing levels through
1x1 are generated from the last supplied level. Supplied levels remain unchanged,
and a complete chain needs no generation. Set it to `False` to load only the supplied
levels. Compressed formats always load the supplied chain without generation.
An incomplete compressed chain with `GenerateMips = True` logs a warning and still
loads successfully; a complete chain or `GenerateMips = False` produces no warning.
BC mip chains can be supplied directly or loaded from an encoded texture such as
DDS or KTX.

BC mip 0 width and height must be multiples of four for all sources. Invalid raw
dimensions return `RADIENT_STATUS_INVALID_ARGUMENT`; encoded textures with these
dimensions fail loading with `RADIENT_STATUS_UNSUPPORTED`. Smaller dimensions in
subsequent mip levels remain valid. Texture reflection reports the number of loaded
levels, including any generated levels.

C callers use `IRadientTextureAsset_GetDesc(Texture)` to obtain a pointer to the
same immutable description.
