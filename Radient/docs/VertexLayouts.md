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

Layout metadata and vertex data have separate lifetimes. Vertex-data ownership
will be documented with the creation and update APIs introduced in later stages.

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
