/*
 *  Copyright 2026 Diligent Graphics LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

/// \file
/// Defines renderer-independent descriptions of vertex memory layouts.

#include "RadientTypes.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

/// Computes an attribute's byte offset automatically; see RadientVertexAttributeDesc::ByteOffset.
/// This macro is also usable in C static initializers.
#define DILIGENT_RADIENT_VERTEX_AUTO_OFFSET 0xFFFFFFFFU

/// Computes a buffer's byte stride automatically; see RadientVertexBufferLayoutDesc::ByteStride.
/// This macro is also usable in C static initializers.
#define DILIGENT_RADIENT_VERTEX_AUTO_STRIDE 0xFFFFFFFFU

/// Automatic attribute offset, equivalent to DILIGENT_RADIENT_VERTEX_AUTO_OFFSET.
static DILIGENT_CONSTEXPR Uint32 RADIENT_VERTEX_AUTO_OFFSET = DILIGENT_RADIENT_VERTEX_AUTO_OFFSET;

/// Automatic buffer stride, equivalent to DILIGENT_RADIENT_VERTEX_AUTO_STRIDE.
static DILIGENT_CONSTEXPR Uint32 RADIENT_VERTEX_AUTO_STRIDE = DILIGENT_RADIENT_VERTEX_AUTO_STRIDE;


/// Scalar encoding of one vertex attribute component.
///
/// Multibyte components use native byte order. Integer
/// normalization is controlled separately by RadientVertexAttributeDesc::Normalized;
/// it changes the interpretation of a value without changing its stored size.
DILIGENT_TYPED_ENUM(RADIENT_VERTEX_COMPONENT_TYPE, Uint8)
{
    /// Unspecified encoding. This is the default value and is invalid for an
    /// attribute in a nonempty layout. GetRadientVertexComponentSize returns zero.
    RADIENT_VERTEX_COMPONENT_TYPE_UNKNOWN = 0,

    /// Signed 8-bit two's-complement integer stored in one byte, in [-128, 127].
    /// May be normalized to [-1, 1]; see RadientVertexAttributeDesc::Normalized.
    RADIENT_VERTEX_COMPONENT_TYPE_INT8,

    /// Unsigned 8-bit integer stored in one byte, in [0, 255].
    /// May be normalized to [0, 1]; see RadientVertexAttributeDesc::Normalized.
    RADIENT_VERTEX_COMPONENT_TYPE_UINT8,

    /// Signed 16-bit two's-complement integer stored in two bytes, in [-32768, 32767].
    /// May be normalized to [-1, 1]; see RadientVertexAttributeDesc::Normalized.
    RADIENT_VERTEX_COMPONENT_TYPE_INT16,

    /// Unsigned 16-bit integer stored in two bytes, in [0, 65535].
    /// May be normalized to [0, 1]; see RadientVertexAttributeDesc::Normalized.
    RADIENT_VERTEX_COMPONENT_TYPE_UINT16,

    /// Signed 32-bit two's-complement integer stored in four bytes,
    /// in [-2147483648, 2147483647]. RadientVertexAttributeDesc::Normalized must be False.
    RADIENT_VERTEX_COMPONENT_TYPE_INT32,

    /// Unsigned 32-bit integer stored in four bytes, in [0, 4294967295].
    /// RadientVertexAttributeDesc::Normalized must be False.
    RADIENT_VERTEX_COMPONENT_TYPE_UINT32,

    /// IEEE 754 binary16 (half-precision) floating-point value stored in two bytes:
    /// one sign bit, five exponent bits, and ten fraction bits.
    /// RadientVertexAttributeDesc::Normalized must be False.
    RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16,

    /// IEEE 754 binary32 (single-precision) floating-point value stored in four bytes:
    /// one sign bit, eight exponent bits, and 23 fraction bits.
    /// RadientVertexAttributeDesc::Normalized must be False.
    RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32
};

// clang-format on


/// Description of one scalar or vector attribute repeated in every vertex record.
///
/// The attribute selects a logical buffer from RadientVertexLayoutDesc::pBuffers
/// and occupies ComponentCount consecutive components starting at ByteOffset in
/// each record. ComponentType and Normalized define how to interpret those bytes.
/// The entire attribute fits within the selected buffer's resolved ByteStride.
/// Automatic offsets and strides are resolved from the attribute sizes and order.
///
/// A default-initialized descriptor is incomplete: Semantic, ComponentType, and
/// ComponentCount must be specified before it can be used in a nonempty layout.
struct RadientVertexAttributeDesc
{
    /// Null-terminated, nonempty, case-sensitive name identifying the attribute.
    /// Must be non-null and unique across all attributes in the layout, including
    /// attributes in different buffers. Examples include POSITION, NORMAL,
    /// TANGENT, TEXCOORD_0, COLOR_1, JOINTS_0, WEIGHTS_0, and application-defined
    /// names. A name does not assign a shader input location or implicitly select
    /// a component encoding or count.
    ///
    /// The default is nullptr, which is invalid for an attribute in a layout.
    /// Radient copies the string when storing the layout. The caller can modify
    /// or release the original string after the API call returns.
    const Char* Semantic DEFAULT_INITIALIZER(nullptr);

    /// Zero-based index of the logical buffer containing this attribute, in
    /// RadientVertexLayoutDesc::pBuffers. Must be less than the layout's BufferCount.
    /// Attributes with the same index share the buffer's vertex records and stride.
    /// This is a layout-local index, not a GPU binding slot or a resource handle.
    /// The default is zero, selecting the first buffer.
    Uint32 BufferIndex DEFAULT_INITIALIZER(0);

    /// Byte offset of the first component relative to the start of each vertex
    /// record in the selected logical buffer. For vertex i, after resolving automatic
    /// values, the attribute starts at bufferStart + i * ByteStride + ByteOffset.
    /// The buffer's base address or allocation/view offset is supplied separately
    /// and is not included here.
    ///
    /// RADIENT_VERTEX_AUTO_OFFSET (the C++ default) places this attribute at the
    /// largest end offset of preceding attributes in the same buffer, in pAttributes
    /// array order. If there are no preceding attributes in that buffer, the offset
    /// is zero. No alignment padding is inserted. Explicit offsets contribute to the
    /// end offset used by subsequent automatic attributes, even when explicit
    /// attributes overlap or appear out of byte order.
    ///
    /// After resolution, ByteOffset + GetRadientVertexAttributeSize(attribute)
    /// must not exceed that buffer's ByteStride. Zero explicitly selects the first
    /// byte of the record; 0xFFFFFFFF is reserved for automatic placement.
    /// C callers set RADIENT_VERTEX_AUTO_OFFSET explicitly to request packing.
    Uint32 ByteOffset DEFAULT_INITIALIZER(RADIENT_VERTEX_AUTO_OFFSET);

    /// Storage encoding shared by all components of this attribute. Must be a
    /// defined RADIENT_VERTEX_COMPONENT_TYPE other than UNKNOWN. Components are
    /// consecutive, with no padding between them, and use native byte order.
    /// Normalized controls integer interpretation separately from this encoding.
    /// The default is UNKNOWN, which must be replaced with a valid encoding.
    RADIENT_VERTEX_COMPONENT_TYPE ComponentType DEFAULT_INITIALIZER(RADIENT_VERTEX_COMPONENT_TYPE_UNKNOWN);

    /// Number of scalar components in one attribute value, from 1 through 4.
    /// One component describes a scalar; two through four describe a vector.
    /// Its byte size is ComponentCount * GetRadientVertexComponentSize(ComponentType).
    /// The default is zero, which is invalid for an attribute in a layout.
    Uint32 ComponentCount DEFAULT_INITIALIZER(0);

    /// Whether integer components represent normalized values. When True, an
    /// unsigned b-bit value v represents v / (2^b - 1) in [0, 1]; a signed b-bit
    /// value represents max(v / (2^(b-1) - 1), -1) in [-1, 1]. These expressions
    /// use real-number division. Both the minimum signed value and the next value
    /// map to -1; the maximum signed or unsigned value maps to 1.
    ///
    /// True is permitted only for INT8, UINT8, INT16, and UINT16. INT32, UINT32,
    /// FLOAT16, and FLOAT32 must use False. The default is False: integer values
    /// retain their numeric values, and floating-point values use their encoding
    /// directly. This flag does not change the component size or the stored bytes.
    Bool Normalized DEFAULT_INITIALIZER(False);
};
typedef struct RadientVertexAttributeDesc RadientVertexAttributeDesc;


/// Description of a logical byte stream containing one fixed-size record per vertex.
///
/// A record may contain one attribute or several interleaved attributes, with
/// optional padding. Attributes select this stream by their BufferIndex. This
/// descriptor contains layout metadata only; it neither provides a data pointer
/// nor requires a separate physical allocation or GPU buffer for the stream.
struct RadientVertexBufferLayoutDesc
{
    /// Distance in bytes between the starts of consecutive vertex records, including
    /// padding within and between records. RADIENT_VERTEX_AUTO_STRIDE (the C++
    /// default) computes the smallest stride containing all attributes in this
    /// buffer: the maximum resolved ByteOffset plus element size. No trailing
    /// alignment padding is added, and gaps from explicit offsets are preserved.
    ///
    /// An explicit stride is preserved and must contain every attribute referencing
    /// this buffer. Explicit and resolved strides are in [1, 0xFFFFFFFE]; zero is
    /// invalid, and 0xFFFFFFFF is reserved for automatic stride. A buffer referenced
    /// by no attributes requires an explicit nonzero stride because there is no
    /// record size to infer. The layout imposes no alignment requirement.
    /// C callers set RADIENT_VERTEX_AUTO_STRIDE explicitly to request packing.
    Uint32 ByteStride DEFAULT_INITIALIZER(RADIENT_VERTEX_AUTO_STRIDE);
};
typedef struct RadientVertexBufferLayoutDesc RadientVertexBufferLayoutDesc;


/// Description of dense, per-vertex byte streams.
///
/// Attributes may be interleaved or stored in separate logical buffers. The same
/// vertex index selects the corresponding record in every buffer. Logical buffers
/// may be views into the same allocation. Semantics must be unique across the
/// layout. Attribute array order determines automatic offsets within each buffer;
/// it does not determine shader input order. With explicit offsets, attribute
/// array order does not affect the memory layout. Different semantics may alias
/// the same bytes, for example two UV sets.
///
/// The layout describes memory organization without requiring any particular
/// semantic, such as POSITION, or enforcing semantic-specific component formats.
/// Source offsets and strides need not satisfy graphics API alignment rules or
/// glTF's stride limits. A consumer may impose requirements for its particular use.
///
/// When a Radient object stores a layout, it copies the descriptor, both
/// arrays, and all semantic strings before the API call returns. Asynchronous
/// work uses this internal copy. The caller can modify or release the original
/// descriptor, arrays, and strings as soon as the call returns.
///
/// The layout contains no vertex data pointers, vertex count, GPU resources, or
/// renderer packing request. Automatic offsets and strides describe tightly
/// packed input data; the renderer chooses its stored representation,
/// which may use a different layout. An empty layout has both counts zero, and
/// its array pointers are ignored. A nonempty layout requires both attributes
/// and buffers. A zero-initialized descriptor represents an empty layout.
struct RadientVertexLayoutDesc
{
    /// Pointer to an array of AttributeCount attribute descriptors. Must be
    /// non-null when AttributeCount is nonzero; ignored for an empty layout.
    /// Every element must satisfy RadientVertexAttributeDesc's requirements,
    /// including a unique semantic and a valid index into pBuffers. Array order
    /// determines placement for attributes using RADIENT_VERTEX_AUTO_OFFSET.
    /// Attributes with explicit offsets keep their specified placement regardless
    /// of order. The default is nullptr.
    ///
    /// Radient copies this array and its semantic strings when storing the layout.
    /// The caller can modify or release them after the API call returns.
    const RadientVertexAttributeDesc* pAttributes DEFAULT_INITIALIZER(nullptr);

    /// Number of descriptors in pAttributes, not the number of vertices or scalar
    /// components. Must be positive for a nonempty layout. The default is zero;
    /// AttributeCount and BufferCount must either both be zero or both be positive.
    Uint32 AttributeCount DEFAULT_INITIALIZER(0);

    /// Pointer to an array of BufferCount logical buffer layout descriptors. Must
    /// be non-null when BufferCount is nonzero; ignored for an empty layout.
    /// An attribute's BufferIndex selects an element of this array. Every element
    /// specifies an explicit nonzero ByteStride or RADIENT_VERTEX_AUTO_STRIDE.
    /// Buffers referenced by no attribute require an explicit nonzero stride.
    /// The array describes logical streams, not physical allocations or resource
    /// bindings. The default is nullptr.
    ///
    /// Radient copies this array when storing the layout. The caller can modify
    /// or release the original array after the API call returns.
    const RadientVertexBufferLayoutDesc* pBuffers DEFAULT_INITIALIZER(nullptr);

    /// Number of descriptors in pBuffers; valid attribute buffer indices are
    /// [0, BufferCount). Must be positive for a nonempty layout. Unreferenced
    /// buffers count toward this value. The default is zero; BufferCount and
    /// AttributeCount must either both be zero or both be positive.
    Uint32 BufferCount DEFAULT_INITIALIZER(0);
};
typedef struct RadientVertexLayoutDesc RadientVertexLayoutDesc;


/// Returns the storage size of one scalar component in bytes.
///
/// \param [in] ComponentType - Scalar encoding to inspect.
/// \return 1 for INT8/UINT8, 2 for INT16/UINT16/FLOAT16, or 4 for
///         INT32/UINT32/FLOAT32. Returns zero for UNKNOWN or an unrecognized value.
Uint32 DILIGENT_GLOBAL_FUNCTION(GetRadientVertexComponentSize)(RADIENT_VERTEX_COMPONENT_TYPE ComponentType);

#include "../../../DiligentCore/Primitives/interface/DefineRefMacro.h"

/// Returns the storage size of one attribute value in bytes, excluding record padding.
///
/// \param [in] Attribute - Attribute descriptor whose ComponentType, ComponentCount,
///                         and Normalized fields define the encoding to inspect.
/// \return ComponentCount times the component size, or zero for an invalid
///         component type, component count, or normalization combination.
///
/// Does not validate Semantic, BufferIndex, or ByteOffset, inspect vertex data,
/// or verify that the attribute fits in a buffer record. This function does not
/// retain the descriptor or its semantic string. C callers pass a pointer rather
/// than a reference; a null attribute pointer returns zero.
Uint32 DILIGENT_GLOBAL_FUNCTION(GetRadientVertexAttributeSize)(const RadientVertexAttributeDesc REF Attribute);

#include "../../../DiligentCore/Primitives/interface/UndefRefMacro.h"

DILIGENT_END_NAMESPACE // namespace Diligent
