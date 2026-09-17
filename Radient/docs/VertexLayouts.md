# Vertex Layouts

`RadientVertexLayout.h` describes the memory organization of dense vertex
streams. A layout is metadata only: it does not own vertex bytes, allocate GPU
buffers, select shader locations, or request a renderer destination layout.
The renderer remains responsible for choosing its stored vertex representation.

## Describing a layout

Each attribute has a case-sensitive semantic, a logical buffer index, a byte
offset within a vertex record, and a scalar encoding. The encoding consists of
component type, component count, and normalization. Each buffer declares a byte
stride, including padding. Offsets and strides can be explicit or automatic.
Attribute order determines automatic placement within each buffer; with explicit
offsets, attribute order does not affect byte placement.

For example, this C++ layout has interleaved positions, normals, and UVs in one
buffer and normalized byte colors in another:

```cpp
#include "RadientVertexLayout.h"
using namespace Diligent;

const RadientVertexAttributeDesc Attributes[] = {
    {"POSITION",   0,  0, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False},
    {"NORMAL",     0, 12, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False},
    {"TEXCOORD_0", 0, 24, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False},
    {"COLOR_0",    1,  0, RADIENT_VERTEX_COMPONENT_TYPE_UINT8,   4, True},
};
const RadientVertexBufferLayoutDesc Buffers[] = {{32}, {4}};
const RadientVertexLayoutDesc Layout = {Attributes, 4, Buffers, 2};
```

For actual C/C++ vertex structures, use `offsetof` and `sizeof` to account for
compiler padding. An attribute offset is relative to the first vertex record,
not a containing file, allocation, or GPU pool. A logical buffer can begin at a
view into a larger allocation; describing that view's data is separate from the
layout.

## Automatic tight packing

`RADIENT_VERTEX_AUTO_OFFSET` and `RADIENT_VERTEX_AUTO_STRIDE` are the C++ defaults
for `ByteOffset` and `ByteStride`. Both use the reserved value `0xFFFFFFFFU`.
Zero remains an explicit offset, and a zero stride is invalid.

An automatic offset is the largest end offset of preceding attributes in the
same buffer, considering attribute array order. With no preceding attributes,
it is zero. Each buffer is resolved independently. Explicit offsets are
preserved and contribute to the end offset; an explicit attribute at a lower
offset never moves subsequent automatic attributes backward. No alignment
padding is inserted.

An automatic stride is the maximum resolved attribute end offset in that buffer,
with no trailing padding. An explicit stride is preserved and includes any
padding between records. Both explicit and resolved strides range from 1 through
`0xFFFFFFFE` and contain every attribute in the buffer. An unreferenced buffer
requires an explicit nonzero stride because there is no record size to infer.

The following describes the same vertex bytes as the explicit example above:

```cpp
const RadientVertexAttributeDesc Attributes[] = {
    {"POSITION",   0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False},
    {"NORMAL",     0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 3, False},
    {"TEXCOORD_0", 0, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32, 2, False},
    {"COLOR_0",    1, RADIENT_VERTEX_AUTO_OFFSET, RADIENT_VERTEX_COMPONENT_TYPE_UINT8,   4, True},
};
const RadientVertexBufferLayoutDesc Buffers[2] = {};
const RadientVertexLayoutDesc Layout = {Attributes, 4, Buffers, 2};
```

The resolved offsets are 0, 12, 24, and 0; buffer strides are 32 and 4 bytes.
These settings describe how source bytes are already packed. The renderer
continues to choose its stored representation independently.

C callers set the automatic values explicitly. The `DILIGENT_`-prefixed macros
`DILIGENT_RADIENT_VERTEX_AUTO_OFFSET` and `DILIGENT_RADIENT_VERTEX_AUTO_STRIDE`
also work in C static initializers. C zero initialization does not select
automatic offsets or strides.

## Metadata ownership

Radient objects store independent copies of their layouts. When storing a
layout, Radient copies the descriptor, attribute array, buffer-layout array,
and all semantic strings before the API call returns. Asynchronous work uses
the object's internal copy of the metadata.

The caller can modify or free the original descriptor, arrays, and strings as
soon as the call returns. Stack-local arrays and temporary strings are valid
inputs. The object's copy remains valid for its lifetime, independently of the
caller's original metadata.

`CreateMesh` retains the supplied vertex data blobs and acquires read access
before returning. It does not copy vertex bytes merely to retain the input.
The layout arrays, semantic strings, and array of blob pointers can be released
immediately after the call. The caller may also release its own blob references;
Radient retains the blobs while it needs their source data.

A mutable blob cannot be written or resized while Radient holds read access.
Finish any write scope before calling `CreateMesh`. Use the blob's
`OnLastReaderReleased` callback to learn when all read access has ended and its
storage can be reused; other readers can also delay or reacquire access. A
read-only blob created with `RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE` requires
its external storage to remain alive and unchanged until the blob is destroyed.
Its `OnDestroy` callback can release that storage's owner. See
[Data Blobs](DataBlobs.md) for the storage modes and access rules.

## Layout requirements

`GetRadientVertexComponentSize` and `GetRadientVertexAttributeSize` report byte
sizes. Layouts require valid encodings, matching array/count pairs, unique
semantics, valid buffer indices, nonzero strides, and attributes contained
within their vertex records. Validation is internal to Radient; clients do
not need to call a separate public validation function. Automatic values are
resolved before checking attribute ranges; arithmetic overflow and resolved
sizes that reach the reserved sentinel are rejected.
An empty layout is valid; a nonempty layout needs both attributes and buffers.
Different semantics may alias source bytes. Unaligned source layouts and strides
larger than glTF's limit are permitted. Validation does not require `POSITION`
or enforce renderer/glTF semantic-specific format restrictions. Normalization
is available for signed and unsigned 8-bit and 16-bit components only.

Buffer and layout compatibility checks are internal to Radient. They support
copy-versus-repack decisions without adding a client-facing packing contract.
They compare resolved offsets and strides, so automatic and explicit descriptions
of the same bytes are compatible. Resolution leaves the caller's descriptors
unchanged. The public helpers only report component and attribute sizes.

C callers use the same descriptors and the `Diligent_`-prefixed size helper
names, passing an attribute pointer where the C++ helper accepts a reference.
A null attribute pointer returns zero.

## Creating a mesh

`RadientMeshCreateInfo::VertexLayout` describes the supplied CPU data.
`ppVertexBuffers` points to one `IRadientDataBlob*` per layout buffer,
and `VertexCount` is shared by all attributes. Attribute offsets are relative
to the start of the corresponding blob. For each attribute, the blob contains
at least `(VertexCount - 1) * ByteStride + ByteOffset + attribute size` bytes
after automatic values are resolved. Padding after the final attribute value
is optional. A buffer with no attributes can supply a null blob pointer.
Blob sizes and data are checked while read access is held. An active writer
prevents mesh creation and results in `RADIENT_STATUS_INVALID_OPERATION`.

Mesh creation requires a nonzero vertex count and a three-component `POSITION`.
`JOINTS_0` and `WEIGHTS_0` appear together with four components each; joint
indices use non-normalized unsigned integers or `FLOAT32` components. Source
conversion supports integer and `FLOAT32` components, including normalized
8/16-bit values. `FLOAT16` layouts can be described, but the current renderer
stores `FLOAT32` attributes and does not support converting `FLOAT16` input.
Other semantics can have one through four components. The renderer consumes
the semantics it supports; supplying an attribute does not enable a new shader
feature by itself.

For the explicit interleaved layout above, populate mutable blobs directly.
Here `FillVertexBuffer` is caller code that writes `VertexCount` records in the
layout for the specified buffer. Check each returned status in application code:

```cpp
RefCntAutoPtr<IRadientMutableDataBlob> VertexBlobs[2];
IRadientDataBlob* VertexData[2] = {};
for (Uint32 BufferIndex = 0; BufferIndex < 2; ++BufferIndex)
{
    RadientDataBlobCreateInfo BlobCI;
    BlobCI.Size = Uint64{VertexCount} * Buffers[BufferIndex].ByteStride;
    CreateRadientMutableDataBlob(BlobCI, &VertexBlobs[BufferIndex]);

    void* pData = nullptr;
    VertexBlobs[BufferIndex]->BeginWrite(&pData);
    FillVertexBuffer(BufferIndex, pData, VertexCount);
    VertexBlobs[BufferIndex]->EndWrite();
    VertexData[BufferIndex] = VertexBlobs[BufferIndex];
}

RadientMeshCreateInfo MeshCI;
MeshCI.VertexLayout = Layout;
MeshCI.ppVertexBuffers = VertexData;
MeshCI.VertexCount = VertexCount;
// Set indices and primitives as usual, then call CreateMesh.
```

Existing bytes can instead be supplied through read-only blobs. Use `COPY` to
initialize independent owned storage, or `REFERENCE` to retain external storage
without copying, with the lifetime requirements described above.

The renderer chooses the destination layout. Compatible complete buffers can be
copied directly. Other buffers are repacked, with scalar conversion and
normalization as required. Padding contents are unspecified and do not affect
cache reuse. Missing destination attributes use
renderer defaults, or zero when no default is provided; extra source components
are discarded and additional destination components remain zero. The source
layout does not request GPU buffer placement or packing.
