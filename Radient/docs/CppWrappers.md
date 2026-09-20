# C++ descriptor wrappers

Include `RadientTypesX.hpp` for C++ wrappers that own descriptor metadata and
retain data blobs. The wrappers can be passed to functions accepting the
corresponding C descriptors. `Get()` returns the same descriptor as a const
reference and does not allocate.

## Vertex layouts

`RadientVertexLayoutDescX` owns attribute descriptors, buffer descriptors, and
semantic strings. `AddBuffer` appends a logical source buffer, and `AddAttribute`
appends an attribute. Buffer indices follow insertion order.

```cpp
RadientVertexLayoutDescX Layout;
Layout.AddBuffer()
      .AddAttribute("POSITION", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3)
      .AddAttribute("NORMAL", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3)
      .AddAttribute("TEXCOORD_0", RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2);

RadientMeshCreateInfo MeshCI;
MeshCI.VertexLayout = Layout;
// Supply the mesh's vertex blobs, counts, indices, and primitives before CreateMesh.
```

The default attribute buffer index is zero. Attribute offsets and buffer strides
default to the existing automatic values. Optional arguments to `AddAttribute`
specify buffer index, byte offset, and normalization, in that order. You can also
pass a `RadientVertexAttributeDesc` directly. `SetAttribute` and `SetBuffer` replace
existing entries. These settings describe source bytes; the renderer still
selects its stored layout.

## Texture loading

`RadientTextureDataX` owns its mip descriptor array and retains every referenced
blob. Mips are appended in order from level zero. `AddMip` and `SetMip` accept a
blob, optional byte offset, and optional row stride. A zero stride retains its
existing meaning of tightly packed rows. Multiple levels can share a blob.

```cpp
RadientTextureDataX Data{Width, Height, RADIENT_TEXTURE_FORMAT_RGBA8_UNORM};
Data.AddMip(pMip0Blob)
    .AddMip(pMip1Blob);

RadientTextureLoadInfoX LoadInfo;
LoadInfo.SetTextureData(std::move(Data))
        .SetSRGB(True);

pAssetManager->LoadTexture(LoadInfo, &pTexture);
```

`GenerateMips` keeps its default of `True`; use `SetGenerateMips(False)` to load
only the supplied levels. Format support and loading behavior are unchanged.

`RadientTextureLoadInfoX` owns URI and base-URI strings, nested texture metadata,
and source blob references. `SetTextureData` selects supplied mip data and clears
the encoded source; `SetDataBlob` selects encoded bytes and clears supplied mips.
`ClearSourceData` clears both while preserving URIs and options. `SetURI` preserves
supplied data because the URI can also identify a memory-backed texture. For
URI-based loading, set `URI` on a wrapper with no supplied data.

## Ownership

Constructing a wrapper from a C descriptor copies its arrays and strings and
retains its blobs. Copying a wrapper creates independent metadata and shares
blob references. Moving transfers ownership and leaves the source at its default
state. Self-move preserves the object. No operation copies pixel bytes or
acquires blob read access, so mutable blobs can be populated before loading.
Reference-mode blobs retain their existing external-memory lifetime rules.

Descriptor views are valid until the owning wrapper is modified, moved, or
destroyed. Keep the wrapper alive through the consuming API call; Radient copies
metadata and retains blobs during that call. The wrapper is not required to
remain alive during asynchronous processing. Synchronize access if one thread
modifies a wrapper while another accesses it.
