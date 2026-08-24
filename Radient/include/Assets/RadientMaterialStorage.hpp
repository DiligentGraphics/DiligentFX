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

#include "RadientMaterials.h"

#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <memory>

namespace Diligent
{

struct RadientMaterialAssetView;

namespace RadientMaterialDetail
{

// Value records, raw parameter data, and retained texture arrays share one allocation.
class PackedMaterialData final
{
public:
    using TexturePtr = RefCntAutoPtr<IRadientTextureAsset>;

    explicit PackedMaterialData(const RadientMaterialDefinitionDesc& Desc);

    // clang-format off
    PackedMaterialData           (const PackedMaterialData&) = delete;
    PackedMaterialData& operator=(const PackedMaterialData&) = delete;
    PackedMaterialData           (PackedMaterialData&&)      = delete;
    PackedMaterialData& operator=(PackedMaterialData&&)      = delete;
    // clang-format on

    ~PackedMaterialData();

    Uint32 GetValueCount() const noexcept;

    RADIENT_MATERIAL_PARAMETER_TYPE GetValueType(Uint32 Index) const noexcept;
    Uint32                          GetValueSize(Uint32 Index) const noexcept;
    const void*                     GetValueData(Uint32 Index) const noexcept;

    bool HasSameValue(Uint32 Index, const void* pData) const noexcept;
    void CopyValue(Uint32 Index, const void* pData) noexcept;

    IRadientTextureAsset* GetTexture(Uint32 Index, Uint32 ArrayIndex) const noexcept;
    void                  SetTexture(Uint32 Index, Uint32 ArrayIndex, IRadientTextureAsset* pTexture) noexcept;

private:
    struct MaterialParameterValue;

    MaterialParameterValue&       GetValue(Uint32 Index) noexcept;
    const MaterialParameterValue& GetValue(Uint32 Index) const noexcept;

    std::unique_ptr<void, STDDeleterRawMem<void>> m_Memory;
    MaterialParameterValue*                       m_pValues    = nullptr;
    Uint32                                        m_ValueCount = 0;
};

class MaterialParameterChanges;

class MaterialStorage final
{
public:
    MaterialStorage(IRadientMaterialDefinitionAsset* pDefinition,
                    RadientHandle                    DefinitionHandle);

    ~MaterialStorage();

    // clang-format off
    MaterialStorage           (const MaterialStorage&) = delete;
    MaterialStorage& operator=(const MaterialStorage&) = delete;
    MaterialStorage           (MaterialStorage&&)      = delete;
    MaterialStorage& operator=(MaterialStorage&&)      = delete;
    // clang-format on

    IRadientMaterialDefinitionAsset* GetDefinition() const noexcept;
    Uint64                           GetVersion() const noexcept;

    RADIENT_STATUS GetParameter(RadientMaterialParameterHandle Handle,
                                void*                          pData,
                                Uint32                         DataSize) const;

    RADIENT_STATUS GetTexture(RadientMaterialParameterHandle Handle,
                              Uint32                         ArrayIndex,
                              IRadientTextureAsset**         ppTexture) const;

    RADIENT_STATUS           GetLoadStatus() const noexcept;
    RADIENT_STATUS           GetGPUResourceStatus() const noexcept;
    RadientMaterialAssetView GetMaterialView(IRadientMaterialAsset* pMaterial);

    PackedMaterialData&       GetPackedData() noexcept;
    const PackedMaterialData& GetPackedData() const noexcept;

    void IncrementVersion() noexcept;

private:
    friend class MaterialParameterChanges;

    bool IsValidHandle(RadientMaterialParameterHandle Handle) const noexcept;

    struct TextureState;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> m_pDefinition;
    const RadientHandle                            m_DefinitionHandle;
    PackedMaterialData                             m_Data;
    Uint64                                         m_Version = 1;
    std::unique_ptr<TextureState>                  m_pTextureState;
};

MaterialStorage*       TryGetMaterialStorage(IRadientAsset* pAsset) noexcept;
const MaterialStorage* TryGetMaterialStorage(const IRadientAsset* pAsset) noexcept;

} // namespace RadientMaterialDetail

} // namespace Diligent
