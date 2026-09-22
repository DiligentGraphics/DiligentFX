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

#include "Assets/RadientAssetValidation.hpp"

#include "Assets/RadientTextureFormat.hpp"
#include "Assets/RadientTextureSource.hpp"
#include "Assets/RadientVertexLayout.hpp"
#include "Core/RadientValidation.hpp"
#include "Math/RadientMath.hpp"
#include "Errors.hpp"
#include "GraphicsAccessories.hpp"
#include "RadientMorphTargets.h"

#include <cstddef>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace Diligent
{

namespace
{

using RadientValidation::IsAddressableArray;
using RadientValidation::IsAddressableSize;
using RadientValidation::IsValidSubrange;

template <typename... ArgsType>
bool LogValidationError(const char* Type, ArgsType&&... Args)
{
    LOG_ERROR_MESSAGE("Invalid ", Type, ": ", std::forward<ArgsType>(Args)...);
    return false;
}

} // namespace

bool ValidateVertexLayout(const RadientVertexLayoutDesc& Layout)
{
    ResolvedVertexLayout Resolved;
    return ResolveVertexLayout(Layout, Resolved);
}

std::string ValidateMeshVertexData(const RadientMeshCreateInfo& MeshCI,
                                   ResolvedVertexLayout&        Resolved)
{
    if (MeshCI.VertexCount == 0)
        return "VertexCount must not be zero.";
    const RadientVertexLayoutDesc& Layout = MeshCI.VertexLayout;
    if (!ResolveVertexLayout(Layout, Resolved))
        return "VertexLayout is invalid.";
    if (Layout.AttributeCount == 0)
        return "VertexLayout requires a three-component POSITION attribute.";
    if (MeshCI.ppVertexBuffers == nullptr)
        return "ppVertexBuffers must not be null.";
    if (!IsAddressableArray(Layout.BufferCount, sizeof(IRadientDataBlob*)))
        return "Vertex buffer array exceeds the addressable range.";
    // Blob data and sizes are checked under retained read access by the source.
    const RadientVertexAttributeDesc* Position = nullptr;
    const RadientVertexAttributeDesc* Joints   = nullptr;
    const RadientVertexAttributeDesc* Weights  = nullptr;
    for (Uint32 AttributeIndex = 0; AttributeIndex < Layout.AttributeCount; ++AttributeIndex)
    {
        const RadientVertexAttributeDesc& Attribute = Layout.pAttributes[AttributeIndex];
        if (MeshCI.ppVertexBuffers[Attribute.BufferIndex] == nullptr)
            return "Referenced vertex buffer blob must not be null.";
        if (std::strcmp(Attribute.Semantic, "POSITION") == 0)
            Position = &Attribute;
        else if (std::strcmp(Attribute.Semantic, "JOINTS_0") == 0)
            Joints = &Attribute;
        else if (std::strcmp(Attribute.Semantic, "WEIGHTS_0") == 0)
            Weights = &Attribute;
    }
    if (Position == nullptr || Position->ComponentCount != 3)
        return "VertexLayout requires a three-component POSITION attribute.";
    if ((Joints != nullptr) != (Weights != nullptr))
        return "JOINTS_0 and WEIGHTS_0 must both be specified or both be absent.";
    if (Joints != nullptr)
    {
        if (Joints->ComponentCount != 4 || Weights->ComponentCount != 4)
            return "JOINTS_0 and WEIGHTS_0 require four components.";
        if (Joints->Normalized ||
            (Joints->ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_UINT8 &&
             Joints->ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_UINT16 &&
             Joints->ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_UINT32 &&
             Joints->ComponentType != RADIENT_VERTEX_COMPONENT_TYPE_FLOAT32))
            return "JOINTS_0 requires non-normalized unsigned integer or FLOAT32 components.";
    }
    return {};
}

bool ValidateMeshCreateInfo(const RadientMeshCreateInfo& MeshCI)
{
    ResolvedVertexLayout Resolved;
    const std::string    Error = ValidateMeshVertexData(MeshCI, Resolved);
    if (!Error.empty())
        return LogValidationError("RadientMeshCreateInfo", Error);

    if (MeshCI.PrimitiveCount == 0)
        return LogValidationError("RadientMeshCreateInfo", "PrimitiveCount must not be zero.");

    if (MeshCI.pPrimitives == nullptr)
        return LogValidationError("RadientMeshCreateInfo", "pPrimitives must not be null.");

    if (MeshCI.MorphTargetCount != 0 && MeshCI.pMorphTargets == nullptr)
    {
        return LogValidationError("RadientMeshCreateInfo",
                                  "pMorphTargets must not be null when MorphTargetCount is nonzero.");
    }

    for (Uint32 TargetIndex = 0; TargetIndex < MeshCI.MorphTargetCount; ++TargetIndex)
    {
        const RadientMorphTargetCreateInfo& TargetCI = MeshCI.pMorphTargets[TargetIndex];
        const RadientMorphTargetDesc&       Target   = TargetCI.Desc;
        if (!RadientMath::IsFinite(Target.DefaultWeight))
        {
            return LogValidationError("RadientMeshCreateInfo",
                                      "pMorphTargets[", TargetIndex, "].Desc.DefaultWeight must be finite.");
        }
        if (Target.AttributeCount != 0 && Target.pAttributes == nullptr)
        {
            return LogValidationError("RadientMeshCreateInfo",
                                      "pMorphTargets[", TargetIndex,
                                      "].Desc.pAttributes must not be null when AttributeCount is nonzero.");
        }
        if (Target.AttributeCount != 0 && TargetCI.pAttributeData == nullptr)
        {
            return LogValidationError("RadientMeshCreateInfo",
                                      "pMorphTargets[", TargetIndex,
                                      "].pAttributeData must not be null when Desc.AttributeCount is nonzero.");
        }

        std::unordered_set<std::string_view> Semantics;
        Semantics.reserve(Target.AttributeCount);
        for (Uint32 AttributeIndex = 0; AttributeIndex < Target.AttributeCount; ++AttributeIndex)
        {
            const RadientMorphTargetAttributeDesc& Attribute = Target.pAttributes[AttributeIndex];
            if (Attribute.Semantic == nullptr || *Attribute.Semantic == '\0')
            {
                return LogValidationError("RadientMeshCreateInfo",
                                          "pMorphTargets[", TargetIndex, "].Desc.pAttributes[", AttributeIndex,
                                          "].Semantic must not be null or empty.");
            }
            if (TargetCI.pAttributeData[AttributeIndex].pDeltas == nullptr)
            {
                return LogValidationError("RadientMeshCreateInfo",
                                          "pMorphTargets[", TargetIndex, "].pAttributeData[", AttributeIndex,
                                          "].pDeltas must not be null.");
            }
            if (Attribute.ComponentCount == 0 || Attribute.ComponentCount > 4)
            {
                return LogValidationError("RadientMeshCreateInfo",
                                          "pMorphTargets[", TargetIndex, "].Desc.pAttributes[", AttributeIndex,
                                          "].ComponentCount must be in [1, 4].");
            }

            const bool IsStandardSemantic =
                std::strcmp(Attribute.Semantic, RadientMorphTargetPositionSemantic) == 0 ||
                std::strcmp(Attribute.Semantic, RadientMorphTargetNormalSemantic) == 0 ||
                std::strcmp(Attribute.Semantic, RadientMorphTargetTangentSemantic) == 0;
            if (IsStandardSemantic && Attribute.ComponentCount != 3)
            {
                return LogValidationError("RadientMeshCreateInfo",
                                          "pMorphTargets[", TargetIndex, "].Desc.pAttributes[", AttributeIndex,
                                          "] standard semantic '", Attribute.Semantic,
                                          "' requires ComponentCount equal to 3.");
            }

            if (!Semantics.emplace(Attribute.Semantic).second)
            {
                return LogValidationError("RadientMeshCreateInfo",
                                          "pMorphTargets[", TargetIndex,
                                          "].Desc contains duplicate attribute semantic '", Attribute.Semantic, "'.");
            }

            if (!IsAddressableArray(MeshCI.VertexCount, Uint64{Attribute.ComponentCount} * sizeof(Float32)))
            {
                return LogValidationError("RadientMeshCreateInfo",
                                          "pMorphTargets[", TargetIndex, "].Desc.pAttributes[", AttributeIndex,
                                          "] data size exceeds the addressable range.");
            }
        }
    }

    if (MeshCI.IndexCount == 0)
        return LogValidationError("RadientMeshCreateInfo", "IndexCount must not be zero.");

    if (MeshCI.pIndexBuffer == nullptr)
        return LogValidationError("RadientMeshCreateInfo", "pIndexBuffer must not be null.");

    if (MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT8 &&
        MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT16 &&
        MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT32)
    {
        return LogValidationError("RadientMeshCreateInfo",
                                  "IndexType must be RADIENT_INDEX_TYPE_UINT8, RADIENT_INDEX_TYPE_UINT16, or RADIENT_INDEX_TYPE_UINT32.");
    }

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < MeshCI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = MeshCI.pPrimitives[PrimitiveIndex];
        if (PrimitiveCI.IndexCount == 0)
        {
            return LogValidationError("RadientMeshCreateInfo",
                                      "pPrimitives[", PrimitiveIndex, "].IndexCount must not be zero.");
        }

        if (PrimitiveCI.FirstIndex >= MeshCI.IndexCount)
        {
            return LogValidationError("RadientMeshCreateInfo",
                                      "pPrimitives[", PrimitiveIndex, "].FirstIndex (", PrimitiveCI.FirstIndex,
                                      ") must be less than IndexCount (", MeshCI.IndexCount, ").");
        }

        if (!IsValidSubrange(PrimitiveCI.FirstIndex, PrimitiveCI.IndexCount, MeshCI.IndexCount))
        {
            return LogValidationError("RadientMeshCreateInfo",
                                      "pPrimitives[", PrimitiveIndex,
                                      "] range [FirstIndex, FirstIndex + IndexCount) exceeds mesh IndexCount (",
                                      MeshCI.IndexCount, ").");
        }
    }

    return true;
}

bool ValidateSceneLoadInfo(const RadientSceneLoadInfo& LoadInfo)
{
    if (LoadInfo.URI == nullptr || *LoadInfo.URI == 0)
        return LogValidationError("RadientSceneLoadInfo", "URI must not be null or empty.");

    if (LoadInfo.Format != RADIENT_SCENE_FORMAT_AUTO &&
        LoadInfo.Format != RADIENT_SCENE_FORMAT_GLTF)
    {
        return LogValidationError("RadientSceneLoadInfo", "Format is invalid.");
    }

    return true;
}

bool ValidateTextureLoadInfo(const RadientTextureLoadInfo& LoadInfo)
{
    const bool HasURI         = LoadInfo.URI != nullptr && *LoadInfo.URI != 0;
    const bool HasEncodedData = LoadInfo.pDataBlob != nullptr;
    const bool HasTextureData = LoadInfo.pTextureData != nullptr;

    if (HasEncodedData && HasTextureData)
    {
        return LogValidationError("RadientTextureLoadInfo",
                                  "pDataBlob and pTextureData must not both be specified.");
    }

    if (!HasEncodedData && !HasTextureData)
    {
        if (!HasURI)
        {
            return LogValidationError("RadientTextureLoadInfo",
                                      "either URI must be non-empty, pDataBlob must not be null, or pTextureData must not be null.");
        }

        return true;
    }

    // Blob storage is validated after read acquisition by RadientTextureSource.
    // Only descriptor metadata is checked here, so concurrent Resize is safe.
    if (HasTextureData)
    {
        const RadientTextureData& TextureData = *LoadInfo.pTextureData;
        if (TextureData.Width == 0 || TextureData.Height == 0)
            return LogValidationError("RadientTextureLoadInfo", "texture data width and height must not be zero.");

        const TEXTURE_FORMAT TextureFormat = RadientToTextureFormat(TextureData.Format);
        if (TextureFormat == TEX_FORMAT_UNKNOWN)
            return LogValidationError("RadientTextureLoadInfo", "texture data format must not be RADIENT_TEXTURE_FORMAT_UNKNOWN.");

        const auto& FmtAttribs = GetTextureFormatAttribs(TextureFormat);
        if (FmtAttribs.ComponentType == COMPONENT_TYPE_COMPRESSED &&
            (TextureData.Width % FmtAttribs.BlockWidth != 0 ||
             TextureData.Height % FmtAttribs.BlockHeight != 0))
        {
            return LogValidationError("RadientTextureLoadInfo",
                                      "compressed texture width and height must be multiples of the block dimensions (",
                                      Uint32{FmtAttribs.BlockWidth}, " x ", Uint32{FmtAttribs.BlockHeight}, ").");
        }

        if (TextureData.pMipLevels == nullptr || TextureData.MipLevelCount == 0 ||
            TextureData.MipLevelCount > ComputeMipLevelsCount(TextureData.Width, TextureData.Height))
        {
            return LogValidationError("RadientTextureLoadInfo", "pMipLevels must not be null and MipLevelCount must describe a nonempty chain no longer than the full mip chain.");
        }

        for (Uint32 Level = 0; Level < TextureData.MipLevelCount; ++Level)
        {
            const RadientTextureMipData& Mip = TextureData.pMipLevels[Level];
            if (Mip.pDataBlob == nullptr)
                return LogValidationError("RadientTextureLoadInfo", "texture data blob must not be null for mip ", Level, ".");

            RadientTextureDataSpan Span;
            if (!GetRadientTextureDataSpan(TextureData, Level, Span))
            {
                return LogValidationError("RadientTextureLoadInfo",
                                          "texture data stride (", Mip.Stride, ") for mip ", Level,
                                          " must be zero or at least the active row size, must align components across uncompressed rows, and texture data size must not overflow.");
            }
            if (!RadientValidation::IsSumRepresentable<Uint64>(Mip.ByteOffset, Span.DataSize) ||
                !IsAddressableSize(Mip.ByteOffset + Span.DataSize))
            {
                return LogValidationError("RadientTextureLoadInfo",
                                          "texture data byte range for mip ", Level,
                                          " overflows or exceeds maximum supported size_t value (",
                                          (std::numeric_limits<size_t>::max)(), ").");
            }
        }
    }

    return true;
}

} // namespace Diligent
