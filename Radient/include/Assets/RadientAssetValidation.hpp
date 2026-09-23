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

#include "RadientAssets.h"
#include "RadientMeshImportServices.h"

#include <string>

namespace Diligent
{

struct RadientVertexLayoutDesc;
struct ResolvedVertexLayout;

/// Checks memory-layout structure, encodings, unique nonempty semantics,
/// strides, buffer indices, and attribute ranges within vertex records.
/// Resolves automatic offsets and strides before checking record ranges, without
/// modifying the input. Rejects automatic strides for unreferenced buffers and
/// resolved sizes that reach or exceed the reserved 0xFFFFFFFF sentinel.
/// An empty layout is valid only when both counts are zero; its array
/// pointers are ignored. Does not require POSITION, impose renderer/glTF
/// semantic or alignment restrictions, or check source byte spans.
/// Unaligned and aliased attributes are valid descriptions. Returns false
/// without logging for invalid layouts, including compatibility queries.
bool ValidateVertexLayout(const RadientVertexLayoutDesc& Layout);

/// Validates mesh vertex metadata and resolves its layout. Checks required
/// semantics, skinning pairs, and non-null referenced blobs without acquiring
/// access or inspecting blob storage. The vertex source checks readable ranges
/// after acquiring read access. Returns an empty string on success or an error
/// message on failure. Does not log or retain input; Resolved is only usable on
/// success.
std::string ValidateMeshVertexData(const RadientMeshCreateInfo& MeshCI,
                                   ResolvedVertexLayout&        Resolved);
std::string ValidateMeshVertexData(const RadientMeshVertexDataCreateInfo& CI,
                                   ResolvedVertexLayout&                  Resolved);

bool ValidateMeshCreateInfo(const RadientMeshCreateInfo& MeshCI);
bool ValidateSceneLoadInfo(const RadientSceneLoadInfo& LoadInfo);
bool ValidateTextureLoadInfo(const RadientTextureLoadInfo& LoadInfo);

} // namespace Diligent
