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
#include "Errors.hpp"

#include <cstddef>
#include <limits>
#include <utility>

namespace Diligent
{

namespace
{

template <typename... ArgsType>
bool LogValidationError(const char* Type, ArgsType&&... Args)
{
    LOG_ERROR_MESSAGE("Invalid ", Type, ": ", std::forward<ArgsType>(Args)...);
    return false;
}

} // namespace

bool ValidateMeshCreateInfo(const RadientMeshCreateInfo& MeshCI)
{
    if (MeshCI.VertexCount == 0)
        return LogValidationError("RadientMeshCreateInfo", "VertexCount must not be zero.");

    if (MeshCI.pPositions == nullptr)
        return LogValidationError("RadientMeshCreateInfo", "pPositions must not be null.");

    if (MeshCI.PrimitiveCount == 0)
        return LogValidationError("RadientMeshCreateInfo", "PrimitiveCount must not be zero.");

    if (MeshCI.pPrimitives == nullptr)
        return LogValidationError("RadientMeshCreateInfo", "pPrimitives must not be null.");

    const bool HasBoneIndices = MeshCI.pBoneIndices0 != nullptr;
    const bool HasBoneWeights = MeshCI.pBoneWeights0 != nullptr;
    if (HasBoneIndices != HasBoneWeights)
    {
        return LogValidationError("RadientMeshCreateInfo",
                                  "pBoneIndices0 and pBoneWeights0 must both be specified or both be null.");
    }

    if (MeshCI.IndexCount == 0)
        return LogValidationError("RadientMeshCreateInfo", "IndexCount must not be zero.");

    if (MeshCI.pIndices == nullptr)
        return LogValidationError("RadientMeshCreateInfo", "pIndices must not be null.");

    if (MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT16 &&
        MeshCI.IndexType != RADIENT_INDEX_TYPE_UINT32)
    {
        return LogValidationError("RadientMeshCreateInfo",
                                  "IndexType must be RADIENT_INDEX_TYPE_UINT16 or RADIENT_INDEX_TYPE_UINT32.");
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

        if (PrimitiveCI.IndexCount > MeshCI.IndexCount - PrimitiveCI.FirstIndex)
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
    const bool HasEncodedData = LoadInfo.pData != nullptr;
    const bool HasTextureData = LoadInfo.pTextureData != nullptr;

    if (HasEncodedData && HasTextureData)
    {
        return LogValidationError("RadientTextureLoadInfo",
                                  "pData and pTextureData must not both be specified.");
    }

    if (!HasEncodedData && !HasTextureData)
    {
        if (!HasURI)
        {
            return LogValidationError("RadientTextureLoadInfo",
                                      "either URI must be non-empty, pData must not be null, or pTextureData must not be null.");
        }

        return true;
    }

    if (HasEncodedData)
    {
        if (LoadInfo.DataSize == 0)
            return LogValidationError("RadientTextureLoadInfo", "DataSize must not be zero when pData is specified.");

        if (LoadInfo.DataSize > static_cast<Uint64>((std::numeric_limits<size_t>::max)()))
        {
            return LogValidationError("RadientTextureLoadInfo",
                                      "DataSize (", LoadInfo.DataSize,
                                      ") exceeds maximum supported size_t value (",
                                      (std::numeric_limits<size_t>::max)(), ").");
        }
    }

    if (HasTextureData)
    {
        const RadientTextureData& TextureData = *LoadInfo.pTextureData;
        if (TextureData.Width == 0 || TextureData.Height == 0)
            return LogValidationError("RadientTextureLoadInfo", "texture data width and height must not be zero.");

        const TEXTURE_FORMAT TextureFormat = RadientToTextureFormat(TextureData.Format);
        if (TextureFormat == TEX_FORMAT_UNKNOWN)
            return LogValidationError("RadientTextureLoadInfo", "texture data format must not be RADIENT_TEXTURE_FORMAT_UNKNOWN.");

        if (TextureData.pData == nullptr)
            return LogValidationError("RadientTextureLoadInfo", "texture data pointer must not be null.");

        RadientTextureDataSpan Span;
        if (!GetRadientTextureDataSpan(TextureData, Span))
        {
            return LogValidationError("RadientTextureLoadInfo",
                                      "texture data stride (", TextureData.Stride,
                                      ") must be zero or at least the active row size and texture data size must not overflow.");
        }

        if (Span.DataSize > static_cast<Uint64>((std::numeric_limits<size_t>::max)()))
        {
            return LogValidationError("RadientTextureLoadInfo",
                                      "texture data size (", Span.DataSize,
                                      ") exceeds maximum supported size_t value (",
                                      (std::numeric_limits<size_t>::max)(), ").");
        }
    }

    return true;
}

} // namespace Diligent
