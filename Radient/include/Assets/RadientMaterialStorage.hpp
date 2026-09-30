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

#include "Assets/RadientMaterialChangeTracker.hpp"
#include "RadientMaterials.h"

#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <atomic>
#include <memory>
#include <mutex>

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
    Uint64                          GetValueVersion(Uint32 Index) const noexcept;

    bool HasSameValue(Uint32 Index, const void* pData) const noexcept;
    void CopyValue(Uint32 Index, const void* pData, Uint64 ShaderDataVersion) noexcept;
    bool UpdateValueRange(Uint32 Index, Uint32 Offset, const void* pData, Uint32 Size, Uint64 ShaderDataVersion) noexcept;

    IRadientTextureAsset* GetTexture(Uint32 Index, Uint32 ArrayIndex) const noexcept;
    void                  SetTexture(Uint32 Index, Uint32 ArrayIndex, IRadientTextureAsset* pTexture, Uint64 ShaderDataVersion) noexcept;

private:
    struct MaterialParameterValue;

    MaterialParameterValue&       GetValue(Uint32 Index) noexcept;
    const MaterialParameterValue& GetValue(Uint32 Index) const noexcept;

    std::unique_ptr<void, STDDeleterRawMem<void>> m_Memory;
    MaterialParameterValue*                       m_pValues    = nullptr;
    Uint32                                        m_ValueCount = 0;
};

class MaterialParameterChanges;

// A nontexture byte range validated when the animation binding is created.
// The caller owns the source bytes and keeps them valid while applying updates.
struct MaterialParameterUpdate
{
    Uint32      ParameterIndex;
    Uint32      Offset;
    Uint32      Size;
    const void* pData;
};

class MaterialStorage final
{
public:
    MaterialStorage(IRadientMaterialDefinitionAsset* pDefinition,
                    RadientHandle                    DefinitionHandle,
                    const MaterialAssetIdentity&     Identity);

    ~MaterialStorage();

    // clang-format off
    MaterialStorage           (const MaterialStorage&) = delete;
    MaterialStorage& operator=(const MaterialStorage&) = delete;
    MaterialStorage           (MaterialStorage&&)      = delete;
    MaterialStorage& operator=(MaterialStorage&&)      = delete;
    // clang-format on

    IRadientMaterialDefinitionAsset* GetDefinition() const noexcept;
    Uint64                           GetVersion() const noexcept;
    const MaterialAssetIdentity&     GetIdentity() const noexcept;

    // Render-thread/client-serialized access. Worker packing reads versions
    // through ReadAccess so they describe the same state as the packed values.
    const MaterialChangeVersions& GetChangeVersions() const noexcept;

    RADIENT_STATUS GetParameter(RadientMaterialParameterHandle Handle,
                                void*                          pData,
                                Uint32                         DataSize) const;

    RADIENT_STATUS GetTexture(RadientMaterialParameterHandle Handle,
                              Uint32                         ArrayIndex,
                              IRadientTextureAsset**         ppTexture) const;

    RADIENT_STATUS           GetLoadStatus() const noexcept;
    RADIENT_STATUS           GetGPUResourceStatus() const noexcept;
    RadientMaterialAssetView GetMaterialView(IRadientMaterialAsset* pMaterial);

    /// Holds read access to material values and their versions until destruction.
    /// The storage must outlive the access object. References obtained from it
    /// are protected only while the object owns access; moved-from objects may
    /// only be destroyed. Surface properties read by shader packing are protected
    /// by the same scope because writers and animation update them under the same lock.
    class ReadAccess final
    {
    public:
        // clang-format off
        ReadAccess           (const ReadAccess&) = delete;
        ReadAccess& operator=(const ReadAccess&) = delete;
        ReadAccess           (ReadAccess&&) noexcept = default;
        ReadAccess& operator=(ReadAccess&&) = delete;
        // clang-format on

        const PackedMaterialData& GetPackedData() const noexcept;
        Uint64                    GetShaderDataVersion() const noexcept;
        Uint64                    GetSurfaceShaderDataVersion() const noexcept;

    private:
        friend class MaterialStorage;
        explicit ReadAccess(const MaterialStorage& Storage);

        const MaterialStorage&       m_Storage;
        std::unique_lock<std::mutex> m_Lock;
    };

    /// Acquires a consistent view without copying the material. Keep the returned
    /// object alive throughout packing, including reads of surface properties.
    ReadAccess AcquireReadAccess() const;

    /// Applies prevalidated animation ranges and an optional alpha-cutoff assignment
    /// atomically with respect to worker reads, publishing at most one change.
    /// A null pNewAlphaCutoff leaves alpha cutoff unchanged; otherwise pAlphaCutoff
    /// points to the owning material's live surface state.
    RADIENT_STATUS ApplyAnimationUpdates(const MaterialParameterUpdate* pUpdates,
                                         Uint32                         UpdateCount,
                                         Float32*                       pAlphaCutoff,
                                         const Float32*                 pNewAlphaCutoff) noexcept;

    /// Validates and applies the complete commit, including specialized state
    /// and version publication, as one operation coordinated with worker reads.
    template <typename ChangeSetType, typename SpecializedStateType>
    RADIENT_STATUS ApplyChanges(const ChangeSetType& Changes, SpecializedStateType& SpecializedState) noexcept
    {
        std::lock_guard<std::mutex> Lock{m_DataMutex};
        // Reject unsupported edits before applying any of the assignments.
        if (IsInitializationFinished() && Changes.Parameters.HasEffectiveTextureChanges(m_Data))
        {
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        MATERIAL_CHANGE_FLAGS       Flags        = Changes.Parameters.ApplyTo(m_Data, m_ChangeVersions.ShaderDataVersion + 1);
        const MATERIAL_CHANGE_FLAGS SurfaceFlags = Changes.Specialized.ApplyTo(SpecializedState);
        Flags |= SurfaceFlags;
        if (Flags == MATERIAL_CHANGE_FLAG_NONE)
            return RADIENT_STATUS_NO_CHANGE;

        PublishChange(Flags, SurfaceFlags);
        return RADIENT_STATUS_OK;
    }

private:
    friend class MaterialParameterChanges;

    bool IsValidHandle(RadientMaterialParameterHandle Handle) const noexcept;
    bool IsInitializationFinished() const noexcept { return m_InitializationFinished.load(std::memory_order_acquire); }
    void FinishInitialization() const noexcept;
    void PublishChange(MATERIAL_CHANGE_FLAGS Flags,
                       MATERIAL_CHANGE_FLAGS SurfaceFlags = MATERIAL_CHANGE_FLAG_NONE) noexcept;

    struct TextureState;

    RefCntAutoPtr<IRadientMaterialDefinitionAsset> m_pDefinition;
    const RadientHandle                            m_DefinitionHandle;
    const MaterialAssetIdentity                    m_Identity;
    PackedMaterialData                             m_Data;
    MaterialChangeVersions                         m_ChangeVersions;
    Uint64                                         m_SurfaceShaderDataVersion = 1;
    mutable std::mutex                             m_DataMutex;
    mutable std::atomic<bool>                      m_InitializationFinished{false};
    std::unique_ptr<TextureState>                  m_pTextureState;
};

MaterialStorage*       TryGetMaterialStorage(IRadientAsset* pAsset) noexcept;
const MaterialStorage* TryGetMaterialStorage(const IRadientAsset* pAsset) noexcept;

} // namespace RadientMaterialDetail

} // namespace Diligent
