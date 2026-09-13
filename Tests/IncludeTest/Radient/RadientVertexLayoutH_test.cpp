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

#include "Radient/interface/RadientVertexLayout.h"

#include <type_traits>

using namespace Diligent;

namespace
{

static_assert(std::is_standard_layout<RadientVertexAttributeDesc>::value, "Vertex attributes must remain C compatible");
static_assert(std::is_trivially_copyable<RadientVertexAttributeDesc>::value, "Vertex attribute descriptors must remain trivially copyable");
static_assert(std::is_standard_layout<RadientVertexBufferLayoutDesc>::value, "Buffer layouts must remain C compatible");
static_assert(std::is_standard_layout<RadientVertexLayoutDesc>::value, "Vertex layouts must remain C compatible");
static_assert(std::is_trivially_copyable<RadientVertexLayoutDesc>::value, "Vertex layout descriptors must remain trivially copyable");

constexpr RadientVertexAttributeDesc Attribute;
static_assert(Attribute.Semantic == nullptr && Attribute.BufferIndex == 0 && Attribute.ByteOffset == RADIENT_VERTEX_AUTO_OFFSET, "Unexpected attribute defaults");
static_assert(Attribute.ComponentType == RADIENT_VERTEX_COMPONENT_TYPE_UNKNOWN && Attribute.ComponentCount == 0 && !Attribute.Normalized, "Unexpected encoding defaults");
constexpr RadientVertexBufferLayoutDesc Buffer;
static_assert(Buffer.ByteStride == RADIENT_VERTEX_AUTO_STRIDE, "Unexpected automatic stride default");
static_assert(RADIENT_VERTEX_AUTO_OFFSET == DILIGENT_RADIENT_VERTEX_AUTO_OFFSET, "C and C++ automatic offsets must match");
static_assert(RADIENT_VERTEX_AUTO_STRIDE == DILIGENT_RADIENT_VERTEX_AUTO_STRIDE, "C and C++ automatic strides must match");
constexpr RadientVertexLayoutDesc DefaultLayout;
static_assert(DefaultLayout.pAttributes == nullptr && DefaultLayout.AttributeCount == 0 && DefaultLayout.pBuffers == nullptr && DefaultLayout.BufferCount == 0, "Unexpected layout defaults");

} // namespace

void RadientVertexLayout_CPP_UseHelpers()
{
    (void)GetRadientVertexComponentSize(RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32);
    (void)GetRadientVertexAttributeSize(RadientVertexAttributeDesc{});
}
