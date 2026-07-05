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

#include "Assets/RadientMeshViewSource.hpp"

#include "XXH128Hasher.hpp"

#include <cstdint>
#include <cstring>

namespace Diligent
{

namespace
{

void UpdateString(XXH128State& Hasher, const Char* Str)
{
    const Uint64 Len = Str != nullptr ? static_cast<Uint64>(std::strlen(Str)) : 0;
    Hasher.Update(Len);
    if (Len != 0)
        Hasher.UpdateRaw(Str, Len);
}

void HashMaterial(XXH128State& Hasher, IRadientMaterialAsset* pMaterial)
{
    const bool HasMaterial = pMaterial != nullptr;
    Hasher.Update(HasMaterial);
    if (!HasMaterial)
        return;

    const RadientAssetReference& MaterialRef = pMaterial->GetReference();
    const bool                   HasURI      = MaterialRef.URI != nullptr && MaterialRef.URI[0] != '\0';
    Hasher.Update(HasURI);
    if (HasURI)
    {
        UpdateString(Hasher, MaterialRef.URI);
        Hasher.Update(MaterialRef.Version);
    }
    else
    {
        Hasher.Update(reinterpret_cast<uintptr_t>(pMaterial));
    }
}

bool ValidateMeshViewCreateInfo(const RadientMeshViewCreateInfo& CI,
                                Uint32                           IndexCount)
{
    if (IndexCount == 0 ||
        CI.PrimitiveCount == 0 ||
        CI.pPrimitives == nullptr)
    {
        return false;
    }

    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < CI.PrimitiveCount; ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& PrimitiveCI = CI.pPrimitives[PrimitiveIndex];
        if (PrimitiveCI.IndexCount == 0 ||
            PrimitiveCI.FirstIndex >= IndexCount ||
            PrimitiveCI.IndexCount > IndexCount - PrimitiveCI.FirstIndex)
        {
            return false;
        }
    }

    return true;
}

} // namespace

RadientMeshViewSource::RadientMeshViewSource(const RadientMeshViewCreateInfo& CI,
                                             Uint32                           IndexCount)
{
    if (!ValidateMeshViewCreateInfo(CI, IndexCount))
    {
        m_Status = RADIENT_STATUS_INVALID_ARGUMENT;
        return;
    }

    m_Primitives.assign(CI.pPrimitives, CI.pPrimitives + CI.PrimitiveCount);

    m_Materials.reserve(CI.PrimitiveCount);
    for (Uint32 PrimitiveIndex = 0; PrimitiveIndex < CI.PrimitiveCount; ++PrimitiveIndex)
        m_Materials.emplace_back(CI.pPrimitives[PrimitiveIndex].pMaterial);
}

std::string RadientMeshViewSource::MakeCacheKey(const char* MeshSourceCacheKey) const
{
    if (RADIENT_FAILED(m_Status) ||
        MeshSourceCacheKey == nullptr ||
        MeshSourceCacheKey[0] == '\0')
    {
        return {};
    }

    XXH128State Hasher;
    Hasher.Update(Uint32{1}); // Mesh view cache key version.
    UpdateString(Hasher, MeshSourceCacheKey);

    Hasher.Update(static_cast<Uint64>(m_Primitives.size()));
    for (size_t PrimitiveIndex = 0; PrimitiveIndex < m_Primitives.size(); ++PrimitiveIndex)
    {
        const RadientMeshPrimitiveCreateInfo& Primitive = m_Primitives[PrimitiveIndex];
        Hasher.Update(Primitive.FirstIndex,
                      Primitive.IndexCount);
        HashMaterial(Hasher, m_Materials[PrimitiveIndex]);
    }

    return std::string{"mesh-view:"} + Hasher.Digest().ToString();
}

} // namespace Diligent
