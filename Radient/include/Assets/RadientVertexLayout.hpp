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

#include "RadientVertexLayout.h"

#include <vector>

namespace Diligent
{

/// Explicit byte offsets and strides computed from a validated vertex layout.
/// Stores numeric values only, with no references to client metadata.
struct ResolvedVertexLayout
{
    /// One resolved byte offset per input attribute, in the original array order.
    std::vector<Uint32> AttributeOffsets;

    /// One resolved nonzero byte stride per input buffer, in the original array order.
    std::vector<Uint32> BufferStrides;
};

/// Validates the layout and resolves automatic offsets and strides independently
/// for each buffer, using attribute array order and inserting no alignment padding.
/// Explicit values are preserved. Rejects invalid encodings, semantics, indices,
/// record ranges, reserved-value collisions, and auto strides for unused buffers.
/// Returns false without logging and leaves Resolved unchanged on invalid input.
/// On success, replaces Resolved with explicit values; an empty layout produces
/// empty arrays. Does not modify or retain any part of the input descriptor.
bool ResolveVertexLayout(const RadientVertexLayoutDesc& Layout, ResolvedVertexLayout& Resolved);

/// Returns true if the selected buffers have identical resolved strides and matching
/// semantics, offsets, encodings, component counts, and normalization. Attribute
/// array order and the numeric buffer indices may differ if the resolved byte
/// interpretation matches. Automatic values are resolved before comparison.
/// Invalid layouts or buffer indices return false.
///
/// This only establishes byte interpretation compatibility. A copy operation
/// must also validate byte spans and vertex ranges, account for padding, and
/// ensure that no default attributes need to be synthesized.
bool AreVertexBuffersCompatible(const RadientVertexLayoutDesc& Lhs,
                                Uint32                         LhsBufferIndex,
                                const RadientVertexLayoutDesc& Rhs,
                                Uint32                         RhsBufferIndex);

/// Returns true if both layouts are valid and every corresponding buffer is
/// compatible after resolving automatic offsets and strides. Buffer counts and
/// indices must match; attribute array order may differ if the resolved byte
/// interpretation matches. Two empty layouts are compatible.
bool AreVertexLayoutsCompatible(const RadientVertexLayoutDesc& Lhs,
                                const RadientVertexLayoutDesc& Rhs);

} // namespace Diligent
