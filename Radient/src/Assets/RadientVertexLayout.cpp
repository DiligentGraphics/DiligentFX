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

#include "Assets/RadientVertexLayout.hpp"

#include "Core/RadientValidation.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Diligent
{

Uint32 GetRadientVertexComponentSize(RADIENT_VERTEX_COMPONENT_TYPE ComponentType)
{
    switch (ComponentType)
    {
        case RADIENT_VERTEX_COMPONENT_TYPE_INT8:
        case RADIENT_VERTEX_COMPONENT_TYPE_UINT8:
            return 1;

        case RADIENT_VERTEX_COMPONENT_TYPE_INT16:
        case RADIENT_VERTEX_COMPONENT_TYPE_UINT16:
        case RADIENT_VERTEX_COMPONENT_TYPE_FLOAT16:
            return 2;

        case RADIENT_VERTEX_COMPONENT_TYPE_INT32:
        case RADIENT_VERTEX_COMPONENT_TYPE_UINT32:
        case RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32:
            return 4;

        default:
            return 0;
    }
}

Uint32 GetRadientVertexAttributeSize(const RadientVertexAttributeDesc& Attribute)
{
    if (Attribute.ComponentCount == 0 || Attribute.ComponentCount > 4)
        return 0;

    if (Attribute.Normalized &&
        Attribute.ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_INT8 &&
        Attribute.ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_UINT8 &&
        Attribute.ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_INT16 &&
        Attribute.ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_UINT16)
    {
        return 0;
    }

    return GetRadientVertexComponentSize(Attribute.ComponentType) * Attribute.ComponentCount;
}

bool ResolveVertexLayout(const RadientVertexLayoutDesc& Layout, ResolvedVertexLayout& Resolved)
{
    if (Layout.AttributeCount == 0 && Layout.BufferCount == 0)
    {
        Resolved = {};
        return true;
    }

    if (Layout.AttributeCount == 0 || Layout.BufferCount == 0 ||
        Layout.pAttributes == nullptr || Layout.pBuffers == nullptr ||
        !RadientValidation::IsAddressableArray(Layout.AttributeCount, sizeof(*Layout.pAttributes)) ||
        !RadientValidation::IsAddressableArray(Layout.BufferCount, sizeof(*Layout.pBuffers)))
    {
        return false;
    }

    ResolvedVertexLayout Result;
    Result.AttributeOffsets.resize(Layout.AttributeCount);
    // During the attribute pass, each buffer stride tracks its largest end offset.
    Result.BufferStrides.resize(Layout.BufferCount);

    for (Uint32 AttributeIndex = 0; AttributeIndex < Layout.AttributeCount; ++AttributeIndex)
    {
        const RadientVertexAttributeDesc& Attribute   = Layout.pAttributes[AttributeIndex];
        const Uint32                      ElementSize = GetRadientVertexAttributeSize(Attribute);
        if (Attribute.Semantic == nullptr || Attribute.Semantic[0] == '\0' ||
            Attribute.BufferIndex >= Layout.BufferCount || ElementSize == 0)
        {
            return false;
        }

        for (Uint32 PreviousIndex = 0; PreviousIndex < AttributeIndex; ++PreviousIndex)
        {
            if (std::strcmp(Attribute.Semantic, Layout.pAttributes[PreviousIndex].Semantic) == 0)
                return false;
        }

        Uint32&      TightStride = Result.BufferStrides[Attribute.BufferIndex];
        const Uint32 ByteOffset  = Attribute.ByteOffset == RADIENT_VERTEX_AUTO_OFFSET ? TightStride : Attribute.ByteOffset;
        // Reserve the sentinel and widen the sum so large offsets cannot wrap.
        const Uint64 EndOffset = Uint64{ByteOffset} + ElementSize;
        if (EndOffset >= RADIENT_VERTEX_AUTO_STRIDE)
            return false;

        Result.AttributeOffsets[AttributeIndex] = ByteOffset;
        TightStride                             = std::max(TightStride, static_cast<Uint32>(EndOffset));
    }

    for (Uint32 BufferIndex = 0; BufferIndex < Layout.BufferCount; ++BufferIndex)
    {
        const Uint32 TightStride = Result.BufferStrides[BufferIndex];
        const Uint32 ByteStride  = Layout.pBuffers[BufferIndex].ByteStride;
        const Uint32 Stride      = ByteStride == RADIENT_VERTEX_AUTO_STRIDE ? TightStride : ByteStride;
        if (Stride == 0 || Stride < TightStride)
            return false;

        Result.BufferStrides[BufferIndex] = Stride;
    }

    Resolved = std::move(Result);
    return true;
}

namespace
{

// The compatibility functions resolve and validate both layouts before this call.
bool AreBuffersCompatible(const RadientVertexLayoutDesc& Lhs,
                          const ResolvedVertexLayout&    ResolvedLhs,
                          Uint32                         LhsBufferIndex,
                          const RadientVertexLayoutDesc& Rhs,
                          const ResolvedVertexLayout&    ResolvedRhs,
                          Uint32                         RhsBufferIndex)
{
    if (ResolvedLhs.BufferStrides[LhsBufferIndex] != ResolvedRhs.BufferStrides[RhsBufferIndex])
        return false;

    Uint32 LhsAttributeCount = 0;
    for (Uint32 LhsIndex = 0; LhsIndex < Lhs.AttributeCount; ++LhsIndex)
    {
        const RadientVertexAttributeDesc& LhsAttribute = Lhs.pAttributes[LhsIndex];
        if (LhsAttribute.BufferIndex != LhsBufferIndex)
            continue;

        ++LhsAttributeCount;
        bool Matched = false;
        for (Uint32 RhsIndex = 0; RhsIndex < Rhs.AttributeCount; ++RhsIndex)
        {
            const RadientVertexAttributeDesc& RhsAttribute = Rhs.pAttributes[RhsIndex];
            if (RhsAttribute.BufferIndex != RhsBufferIndex ||
                std::strcmp(LhsAttribute.Semantic, RhsAttribute.Semantic) != 0)
                continue;

            if (ResolvedLhs.AttributeOffsets[LhsIndex] != ResolvedRhs.AttributeOffsets[RhsIndex] ||
                LhsAttribute.ComponentType != RhsAttribute.ComponentType ||
                LhsAttribute.ComponentCount != RhsAttribute.ComponentCount ||
                LhsAttribute.Normalized != RhsAttribute.Normalized)
            {
                return false;
            }
            Matched = true;
            break;
        }
        if (!Matched)
            return false;
    }

    Uint32 RhsAttributeCount = 0;
    for (Uint32 RhsIndex = 0; RhsIndex < Rhs.AttributeCount; ++RhsIndex)
    {
        if (Rhs.pAttributes[RhsIndex].BufferIndex == RhsBufferIndex)
            ++RhsAttributeCount;
    }
    return LhsAttributeCount == RhsAttributeCount;
}

} // namespace

bool AreVertexBuffersCompatible(const RadientVertexLayoutDesc& Lhs,
                                Uint32                         LhsBufferIndex,
                                const RadientVertexLayoutDesc& Rhs,
                                Uint32                         RhsBufferIndex)
{
    ResolvedVertexLayout ResolvedLhs;
    ResolvedVertexLayout ResolvedRhs;
    return LhsBufferIndex < Lhs.BufferCount && RhsBufferIndex < Rhs.BufferCount &&
        ResolveVertexLayout(Lhs, ResolvedLhs) && ResolveVertexLayout(Rhs, ResolvedRhs) &&
        AreBuffersCompatible(Lhs, ResolvedLhs, LhsBufferIndex, Rhs, ResolvedRhs, RhsBufferIndex);
}

bool AreVertexLayoutsCompatible(const RadientVertexLayoutDesc& Lhs,
                                const RadientVertexLayoutDesc& Rhs)
{
    ResolvedVertexLayout ResolvedLhs;
    ResolvedVertexLayout ResolvedRhs;
    if (Lhs.BufferCount != Rhs.BufferCount || Lhs.AttributeCount != Rhs.AttributeCount ||
        !ResolveVertexLayout(Lhs, ResolvedLhs) || !ResolveVertexLayout(Rhs, ResolvedRhs))
        return false;

    for (Uint32 BufferIndex = 0; BufferIndex < Lhs.BufferCount; ++BufferIndex)
    {
        if (!AreBuffersCompatible(Lhs, ResolvedLhs, BufferIndex, Rhs, ResolvedRhs, BufferIndex))
            return false;
    }
    return true;
}

} // namespace Diligent

extern "C"
{
    Diligent::Uint32 Diligent_GetRadientVertexComponentSize(Diligent::RADIENT_VERTEX_COMPONENT_TYPE ComponentType)
    {
        return Diligent::GetRadientVertexComponentSize(ComponentType);
    }

    Diligent::Uint32 Diligent_GetRadientVertexAttributeSize(const Diligent::RadientVertexAttributeDesc* pAttribute)
    {
        return pAttribute != nullptr ? Diligent::GetRadientVertexAttributeSize(*pAttribute) : 0;
    }
}
