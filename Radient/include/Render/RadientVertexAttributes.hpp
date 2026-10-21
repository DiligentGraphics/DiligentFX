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

#include "GLTFLoader.hpp"

#include <array>

namespace Diligent
{

// Shared by mesh packing and the renderer's input layout. Each optional
// attribute has its own stream so absent attributes use no vertex storage.
// Joint indices and weights are always used together and share one stream.
// Array order defines shader attribute indices in VertexAttributesToInputLayout;
// keep it consistent with PBR_Renderer::VERTEX_ATTRIB_ID, independently of slots.
// clang-format off
static constexpr std::array<GLTF::VertexAttributeDesc, 8> RadientDefaultVertexAttributes =
    {
        GLTF::VertexAttributeDesc{GLTF::PositionAttributeName,    0, VT_FLOAT32, 3},
        GLTF::VertexAttributeDesc{GLTF::NormalAttributeName,      1, VT_FLOAT32, 3},
        GLTF::VertexAttributeDesc{GLTF::Texcoord0AttributeName,   2, VT_FLOAT32, 2},
        GLTF::VertexAttributeDesc{GLTF::Texcoord1AttributeName,   3, VT_FLOAT32, 2},
        GLTF::VertexAttributeDesc{GLTF::JointsAttributeName,      5, VT_FLOAT32, 4},
        GLTF::VertexAttributeDesc{GLTF::WeightsAttributeName,     5, VT_FLOAT32, 4},
        GLTF::VertexAttributeDesc{GLTF::VertexColorAttributeName, 4, VT_FLOAT32, 4, &GLTF::DefaultVertexColor},
        GLTF::VertexAttributeDesc{GLTF::TangentAttributeName,     6, VT_FLOAT32, 3}
    };
// clang-format on

} // namespace Diligent
