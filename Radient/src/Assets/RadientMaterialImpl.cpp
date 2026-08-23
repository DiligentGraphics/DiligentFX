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

#include "RadientMaterials.h"
#include "Assets/RadientMaterialDefinitionImpl.hpp"
#include "Assets/RadientTextureAssetManager.hpp"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "GLTFLoader.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace Diligent
{

using RadientMaterialDetail::GetMaterialParameterDataSize;
using RadientMaterialDetail::IsTextureParameter;

namespace
{

using ShaderTextureAttribs = GLTF::Material::TextureShaderAttribs;

static_assert(static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_WRAP) == static_cast<Uint32>(TEXTURE_ADDRESS_WRAP));
static_assert(static_cast<Uint32>(RADIENT_MATERIAL_TEXTURE_ADDRESS_MODE_CLAMP) == static_cast<Uint32>(TEXTURE_ADDRESS_CLAMP));
static_assert(sizeof(ShaderTextureAttribs) <= std::numeric_limits<Uint32>::max());

static constexpr Uint32 ShaderTexturePackedPropsOffset = static_cast<Uint32>(offsetof(ShaderTextureAttribs, PackedProps));
static constexpr Uint32 ShaderTextureSliceOffset       = static_cast<Uint32>(offsetof(ShaderTextureAttribs, TextureSlice));
static constexpr Uint32 ShaderTextureAtlasUVOffset     = static_cast<Uint32>(offsetof(ShaderTextureAttribs, AtlasUVScaleAndBias));

struct MaterialParameterValue
{
    // Size is the byte size for value parameters and the element count for textures.
    RADIENT_MATERIAL_PARAMETER_TYPE Type  = RADIENT_MATERIAL_PARAMETER_TYPE_UNKNOWN;
    Uint32                          Size  = 0;
    void*                           pData = nullptr;
};

// Value records, raw parameter data, and retained texture arrays share one allocation.
class PackedMaterialInstanceData
{
public:
    using TexturePtr = RefCntAutoPtr<IRadientTextureAsset>;

    explicit PackedMaterialInstanceData(const RadientMaterialDefinitionDesc& Desc,
                                        const PackedMaterialInstanceData*    pSource = nullptr);

    PackedMaterialInstanceData(const PackedMaterialInstanceData&) = delete;
    PackedMaterialInstanceData& operator=(const PackedMaterialInstanceData&) = delete;
    PackedMaterialInstanceData(PackedMaterialInstanceData&&)                 = delete;
    PackedMaterialInstanceData& operator=(PackedMaterialInstanceData&&) = delete;

    ~PackedMaterialInstanceData()
    {
        for (Uint32 ValueIndex = 0; ValueIndex < m_ValueCount; ++ValueIndex)
        {
            const MaterialParameterValue& Value = m_pValues[ValueIndex];
            if (!IsTextureParameter(Value.Type))
                continue;

            TexturePtr* const pTextures = static_cast<TexturePtr*>(Value.pData);
            for (Uint32 TextureIndex = 0; TextureIndex < Value.Size; ++TextureIndex)
                pTextures[TextureIndex].~TexturePtr();
        }
    }

    Uint32 GetValueCount() const noexcept
    {
        return m_ValueCount;
    }

    MaterialParameterValue& GetValue(Uint32 Index) noexcept
    {
        VERIFY_EXPR(Index < m_ValueCount);
        return m_pValues[Index];
    }

    const MaterialParameterValue& GetValue(Uint32 Index) const noexcept
    {
        VERIFY_EXPR(Index < m_ValueCount);
        return m_pValues[Index];
    }

    const void* GetValueData(Uint32 Index) const noexcept
    {
        return m_pValues[Index].pData;
    }

    bool HasSameValue(Uint32 Index, const void* pData) const noexcept
    {
        const MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(!IsTextureParameter(Value.Type));
        return std::memcmp(Value.pData, pData, Value.Size) == 0;
    }

    void CopyValue(Uint32 Index, const void* pData) noexcept
    {
        MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(!IsTextureParameter(Value.Type));
        std::memcpy(Value.pData, pData, Value.Size);
    }

    IRadientTextureAsset* GetTexture(Uint32 Index, Uint32 ArrayIndex) const noexcept
    {
        const MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
        return static_cast<const TexturePtr*>(Value.pData)[ArrayIndex];
    }

    void SetTexture(Uint32 Index, Uint32 ArrayIndex, IRadientTextureAsset* pTexture) noexcept
    {
        MaterialParameterValue& Value = GetValue(Index);
        VERIFY_EXPR(IsTextureParameter(Value.Type) && ArrayIndex < Value.Size);
        static_cast<TexturePtr*>(Value.pData)[ArrayIndex] = pTexture;
    }

private:
    std::unique_ptr<void, STDDeleterRawMem<void>> m_Memory;
    MaterialParameterValue*                       m_pValues    = nullptr;
    Uint32                                        m_ValueCount = 0;
};

PackedMaterialInstanceData::PackedMaterialInstanceData(const RadientMaterialDefinitionDesc& Desc,
                                                       const PackedMaterialInstanceData*    pSource)
{
    VERIFY_EXPR(pSource == nullptr || pSource->GetValueCount() == Desc.ParameterCount);

    FixedLinearAllocator Allocator{GetRawAllocator()};
    Allocator.AddSpace<MaterialParameterValue>(Desc.ParameterCount);
    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Parameter = Desc.pParameters[Index];
        Uint32                              DataSize;
        const bool                          IsValidDataSize = GetMaterialParameterDataSize(Parameter, DataSize);
        VERIFY_EXPR(IsValidDataSize);
        (void)IsValidDataSize;

        if (IsTextureParameter(Parameter.Type))
            Allocator.AddSpace<PackedMaterialInstanceData::TexturePtr>(Parameter.ArraySize);
        else
            Allocator.AddSpace(DataSize, alignof(Uint32));
    }

    Allocator.Reserve();
    const size_t MemorySize = Allocator.GetReservedSize();
    m_Memory                = decltype(m_Memory){Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

    FixedLinearAllocator Writer{m_Memory.get(), MemorySize};
    m_pValues = Writer.ConstructArray<MaterialParameterValue>(Desc.ParameterCount);

    for (Uint32 Index = 0; Index < Desc.ParameterCount; ++Index)
    {
        const RadientMaterialParameterDesc& Parameter = Desc.pParameters[Index];
        MaterialParameterValue&             Value     = m_pValues[Index];
        Value.Type                                    = Parameter.Type;

        Uint32     DataSize;
        const bool IsValidDataSize = GetMaterialParameterDataSize(Parameter, DataSize);
        VERIFY_EXPR(IsValidDataSize);
        (void)IsValidDataSize;

        if (IsTextureParameter(Parameter.Type))
        {
            auto* const pTextures =
                Writer.ConstructArray<PackedMaterialInstanceData::TexturePtr>(Parameter.ArraySize);
            Value.pData = pTextures;
            Value.Size  = Parameter.ArraySize;
            ++m_ValueCount;

            if (pSource != nullptr)
            {
                const MaterialParameterValue& SourceValue = pSource->GetValue(Index);
                VERIFY_EXPR(SourceValue.Type == Value.Type && SourceValue.Size == Value.Size);
                const auto* const pSourceTextures =
                    static_cast<const PackedMaterialInstanceData::TexturePtr*>(SourceValue.pData);
                for (Uint32 TextureIndex = 0; TextureIndex < Value.Size; ++TextureIndex)
                    pTextures[TextureIndex] = pSourceTextures[TextureIndex];
            }
            else if (Parameter.ArraySize == 1)
            {
                pTextures[0] = Parameter.pDefaultTexture;
            }
        }
        else
        {
            Value.pData = Writer.Allocate(DataSize, alignof(Uint32));
            Value.Size  = DataSize;
            ++m_ValueCount;

            if (pSource != nullptr)
            {
                const MaterialParameterValue& SourceValue = pSource->GetValue(Index);
                VERIFY_EXPR(SourceValue.Type == Value.Type && SourceValue.Size == Value.Size);
                std::memcpy(Value.pData, SourceValue.pData, Value.Size);
            }
            else if (Parameter.pDefaultValue != nullptr)
            {
                std::memcpy(Value.pData, Parameter.pDefaultValue, Value.Size);
            }
            else
            {
                std::memset(Value.pData, 0, Value.Size);
            }
        }
    }

    VERIFY_EXPR(m_ValueCount == Desc.ParameterCount);
    if (Writer.GetReservedSize() != 0)
        VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
}

} // namespace

namespace
{

template <typename InterfaceType, typename InstanceType>
class RadientMaterialInstanceWriterBase;

class RadientMaterialInstanceWriterImpl;
class RadientSurfaceMaterialInstanceWriterImpl;

template <typename InterfaceType>
class RadientMaterialInstanceBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    RadientMaterialInstanceBase(IReferenceCounters*               pRefCounters,
                                IRadientMaterialDefinitionAsset*  pDefinition,
                                RadientHandle                     DefinitionHandle,
                                const PackedMaterialInstanceData* pSourceData = nullptr) :
        TBase{pRefCounters},
        m_pDefinition{pDefinition},
        m_DefinitionHandle{DefinitionHandle},
        m_Data{pDefinition->GetDesc(), pSourceData}
    {}

    virtual IRadientMaterialDefinitionAsset* DILIGENT_CALL_TYPE GetDefinition() const override final
    {
        return m_pDefinition;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_Version;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetParameter(RadientMaterialParameterHandle Handle,
                                                           void*                          pData,
                                                           Uint32                         DataSize) const override final
    {
        if (!IsValidHandle(Handle))
            return RADIENT_STATUS_INVALID_ARGUMENT;
        const MaterialParameterValue& Value = m_Data.GetValue(Handle.Index);
        if (IsTextureParameter(Value.Type))
            return RADIENT_STATUS_INVALID_OPERATION;

        if (pData == nullptr || DataSize != Value.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        std::memcpy(pData, Value.pData, Value.Size);
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset**         ppTexture) const override final
    {
        if (ppTexture == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppTexture = nullptr;

        if (!IsValidHandle(Handle))
            return RADIENT_STATUS_INVALID_ARGUMENT;
        const MaterialParameterValue& Value = m_Data.GetValue(Handle.Index);
        if (!IsTextureParameter(Value.Type))
            return RADIENT_STATUS_INVALID_OPERATION;

        if (ArrayIndex >= Value.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const auto* const pTextures =
            static_cast<const PackedMaterialInstanceData::TexturePtr*>(Value.pData);
        *ppTexture = pTextures[ArrayIndex];
        if (*ppTexture != nullptr)
            (*ppTexture)->AddRef();
        return RADIENT_STATUS_OK;
    }

    const PackedMaterialInstanceData& GetPackedData() const noexcept
    {
        return m_Data;
    }

protected:
    template <typename, typename>
    friend class RadientMaterialInstanceWriterBase;

    bool IsValidHandle(RadientMaterialParameterHandle Handle) const noexcept
    {
        return Handle.Definition == m_DefinitionHandle &&
            Handle.Index < m_Data.GetValueCount() &&
            Handle.Reserved == 0;
    }

protected:
    RefCntAutoPtr<IRadientMaterialDefinitionAsset> m_pDefinition;
    const RadientHandle                            m_DefinitionHandle;
    PackedMaterialInstanceData                     m_Data;
    Uint64                                         m_Version = 1;
};

class RadientMaterialInstanceImpl final : public RadientMaterialInstanceBase<IRadientMaterialInstance>
{
public:
    using TBase = RadientMaterialInstanceBase<IRadientMaterialInstance>;

    RadientMaterialInstanceImpl(IReferenceCounters*                pRefCounters,
                                IRadientMaterialDefinitionAsset*   pDefinition,
                                RadientHandle                      DefinitionHandle,
                                const RadientMaterialInstanceImpl* pSource = nullptr) :
        TBase{pRefCounters,
              pDefinition,
              DefinitionHandle,
              pSource != nullptr ? &pSource->GetPackedData() : nullptr}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstance, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final;
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final;
};

class RadientSurfaceMaterialInstanceImpl final : public RadientMaterialInstanceBase<IRadientSurfaceMaterialInstance>
{
public:
    using TBase = RadientMaterialInstanceBase<IRadientSurfaceMaterialInstance>;

    RadientSurfaceMaterialInstanceImpl(IReferenceCounters*                       pRefCounters,
                                       IRadientMaterialDefinitionAsset*          pDefinition,
                                       RadientHandle                             DefinitionHandle,
                                       const RadientSurfaceMaterialInstanceImpl* pSource = nullptr) :
        TBase{pRefCounters,
              pDefinition,
              DefinitionHandle,
              pSource != nullptr ? &pSource->GetPackedData() : nullptr},
        m_SurfaceMode{pSource != nullptr ? pSource->m_SurfaceMode : RADIENT_MATERIAL_SURFACE_MODE_OPAQUE},
        m_AlphaCutoff{pSource != nullptr ? pSource->m_AlphaCutoff : 0.5f},
        m_IsDoubleSided{pSource != nullptr ? pSource->m_IsDoubleSided : False}
    {}

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientSurfaceMaterialInstance, IID_RadientMaterialInstance, TBase)

    virtual RADIENT_MATERIAL_SURFACE_MODE DILIGENT_CALL_TYPE GetSurfaceMode() const override final
    {
        return m_SurfaceMode;
    }

    virtual Float32 DILIGENT_CALL_TYPE GetAlphaCutoff() const override final
    {
        return m_AlphaCutoff;
    }

    virtual Bool DILIGENT_CALL_TYPE IsDoubleSided() const override final
    {
        return m_IsDoubleSided;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const override final;
    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Clone(IRadientMaterialInstance** ppInstance) const override final;

private:
    friend class RadientSurfaceMaterialInstanceWriterImpl;

    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode   = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff   = 0.5f;
    Bool                          m_IsDoubleSided = False;
};

template <typename InterfaceType, typename InstanceType>
class RadientMaterialInstanceWriterBase : public ObjectBase<InterfaceType>
{
public:
    using TBase = ObjectBase<InterfaceType>;

    RadientMaterialInstanceWriterBase(IReferenceCounters* pRefCounters,
                                      InstanceType*       pInstance) :
        TBase{pRefCounters},
        m_pInstance{pInstance}
    {}

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetParameter(RadientMaterialParameterHandle Handle,
                                                           const void*                    pData,
                                                           Uint32                         DataSize) override final
    {
        if (!m_pInstance->IsValidHandle(Handle) ||
            IsTextureParameter(m_pInstance->m_Data.GetValue(Handle.Index).Type))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const MaterialParameterValue& InstanceValue = m_pInstance->m_Data.GetValue(Handle.Index);
        if (pData == nullptr || DataSize != InstanceValue.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        ChangeIterator    ChangeIt     = FindChange(Handle.Index, ValueArrayIndex);
        const bool        HasChange    = ChangeIt != m_Changes.end();
        const void* const pCurrentData = HasChange ?
            m_ValueData.data() + ChangeIt->DataOffset :
            InstanceValue.pData;
        if (std::memcmp(pCurrentData, pData, InstanceValue.Size) == 0)
            return RADIENT_STATUS_NO_CHANGE;

        if (HasChange)
        {
            std::memcpy(m_ValueData.data() + ChangeIt->DataOffset, pData, InstanceValue.Size);
            return RADIENT_STATUS_OK;
        }

        if (m_ValueData.size() > std::numeric_limits<Uint32>::max() - InstanceValue.Size)
        {
            LOG_ERROR_MESSAGE("Material instance writer value storage exceeds the supported size");
            return RADIENT_STATUS_FAILED;
        }

        try
        {
            const Uint32 DataOffset = static_cast<Uint32>(m_ValueData.size());
            m_Changes.reserve(m_Changes.size() + 1);
            m_ValueData.resize(m_ValueData.size() + InstanceValue.Size);
            std::memcpy(m_ValueData.data() + DataOffset, pData, InstanceValue.Size);
            m_Changes.push_back(ParameterChange{Handle.Index, ValueArrayIndex, DataOffset});
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to store a material instance parameter change: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }

        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetTexture(RadientMaterialParameterHandle Handle,
                                                         Uint32                         ArrayIndex,
                                                         IRadientTextureAsset*          pTexture) override final
    {
        if (!m_pInstance->IsValidHandle(Handle) ||
            !IsTextureParameter(m_pInstance->m_Data.GetValue(Handle.Index).Type))
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const MaterialParameterValue& InstanceValue = m_pInstance->m_Data.GetValue(Handle.Index);
        if (ArrayIndex >= InstanceValue.Size)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        ChangeIterator              ChangeIt        = FindChange(Handle.Index, ArrayIndex);
        const bool                  HasChange       = ChangeIt != m_Changes.end();
        IRadientTextureAsset* const pCurrentTexture = HasChange ?
            m_TextureData[ChangeIt->DataOffset].RawPtr() :
            m_pInstance->m_Data.GetTexture(Handle.Index, ArrayIndex);
        if (pCurrentTexture == pTexture)
            return RADIENT_STATUS_NO_CHANGE;

        if (HasChange)
        {
            m_TextureData[ChangeIt->DataOffset] = pTexture;
            return RADIENT_STATUS_OK;
        }

        if (m_TextureData.size() >= std::numeric_limits<Uint32>::max())
        {
            LOG_ERROR_MESSAGE("Material instance writer texture storage exceeds the supported size");
            return RADIENT_STATUS_FAILED;
        }

        try
        {
            const Uint32 DataOffset = static_cast<Uint32>(m_TextureData.size());
            m_Changes.reserve(m_Changes.size() + 1);
            m_TextureData.emplace_back(pTexture);
            m_Changes.push_back(ParameterChange{Handle.Index, ArrayIndex, DataOffset});
        }
        catch (const std::exception& Error)
        {
            LOG_ERROR_MESSAGE("Failed to store a material instance texture change: ", Error.what());
            return RADIENT_STATUS_FAILED;
        }

        return RADIENT_STATUS_OK;
    }

protected:
    bool ApplyParameterChanges()
    {
        bool StateChanged = false;
        for (const ParameterChange& Change : m_Changes)
        {
            const MaterialParameterValue& InstanceValue = m_pInstance->m_Data.GetValue(Change.ParameterIndex);
            if (IsTextureParameter(InstanceValue.Type))
            {
                VERIFY_EXPR(Change.ArrayIndex != ValueArrayIndex);
                IRadientTextureAsset* const pTexture = m_TextureData[Change.DataOffset];
                if (m_pInstance->m_Data.GetTexture(Change.ParameterIndex, Change.ArrayIndex) != pTexture)
                {
                    m_pInstance->m_Data.SetTexture(Change.ParameterIndex, Change.ArrayIndex, pTexture);
                    StateChanged = true;
                }
            }
            else
            {
                VERIFY_EXPR(Change.ArrayIndex == ValueArrayIndex);
                const void* const pChangedData = m_ValueData.data() + Change.DataOffset;
                if (!m_pInstance->m_Data.HasSameValue(Change.ParameterIndex, pChangedData))
                {
                    m_pInstance->m_Data.CopyValue(Change.ParameterIndex, pChangedData);
                    StateChanged = true;
                }
            }
        }
        return StateChanged;
    }

    RADIENT_STATUS FinishCommit(bool StateChanged)
    {
        m_Changes.clear();
        m_ValueData.clear();
        m_TextureData.clear();

        if (!StateChanged)
            return RADIENT_STATUS_NO_CHANGE;

        ++m_pInstance->m_Version;
        return RADIENT_STATUS_OK;
    }

    InstanceType& GetInstance() noexcept
    {
        return *m_pInstance;
    }

private:
    static constexpr Uint32 ValueArrayIndex = ~Uint32{0};

    // Each pending complete value or texture element has one record. Offsets
    // remain valid when the backing value and texture arrays grow.
    struct ParameterChange
    {
        Uint32 ParameterIndex = 0;
        Uint32 ArrayIndex     = ValueArrayIndex;
        Uint32 DataOffset     = 0;
    };

    using ChangeList     = std::vector<ParameterChange>;
    using ChangeIterator = typename ChangeList::iterator;

    ChangeIterator FindChange(Uint32 ParameterIndex, Uint32 ArrayIndex) noexcept
    {
        return std::find_if(
            m_Changes.begin(), m_Changes.end(),
            [ParameterIndex, ArrayIndex](const ParameterChange& Change) {
                return Change.ParameterIndex == ParameterIndex && Change.ArrayIndex == ArrayIndex;
            });
    }

private:
    RefCntAutoPtr<InstanceType>                         m_pInstance;
    ChangeList                                          m_Changes;
    std::vector<Uint8>                                  m_ValueData;
    std::vector<PackedMaterialInstanceData::TexturePtr> m_TextureData;
};

class RadientMaterialInstanceWriterImpl final :
    public RadientMaterialInstanceWriterBase<IRadientMaterialInstanceWriter, RadientMaterialInstanceImpl>
{
public:
    using TBase = RadientMaterialInstanceWriterBase<IRadientMaterialInstanceWriter, RadientMaterialInstanceImpl>;

    RadientMaterialInstanceWriterImpl(IReferenceCounters*          pRefCounters,
                                      RadientMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters, pInstance}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientMaterialInstanceWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        return FinishCommit(ApplyParameterChanges());
    }
};

class RadientSurfaceMaterialInstanceWriterImpl final :
    public RadientMaterialInstanceWriterBase<IRadientSurfaceMaterialInstanceWriter, RadientSurfaceMaterialInstanceImpl>
{
public:
    using TBase = RadientMaterialInstanceWriterBase<IRadientSurfaceMaterialInstanceWriter, RadientSurfaceMaterialInstanceImpl>;

    RadientSurfaceMaterialInstanceWriterImpl(IReferenceCounters*                 pRefCounters,
                                             RadientSurfaceMaterialInstanceImpl* pInstance) :
        TBase{pRefCounters, pInstance}
    {}

    IMPLEMENT_QUERY_INTERFACE2_IN_PLACE(IID_RadientSurfaceMaterialInstanceWriter, IID_RadientMaterialInstanceWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetSurfaceMode(RADIENT_MATERIAL_SURFACE_MODE SurfaceMode) override final
    {
        if (SurfaceMode >= RADIENT_MATERIAL_SURFACE_MODE_COUNT)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        RadientSurfaceMaterialInstanceImpl& Instance = GetInstance();
        const RADIENT_MATERIAL_SURFACE_MODE CurrentMode =
            m_SurfaceModeChanged ? m_SurfaceMode : Instance.m_SurfaceMode;
        if (CurrentMode == SurfaceMode)
            return RADIENT_STATUS_NO_CHANGE;

        m_SurfaceMode        = SurfaceMode;
        m_SurfaceModeChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetAlphaCutoff(Float32 AlphaCutoff) override final
    {
        RadientSurfaceMaterialInstanceImpl& Instance = GetInstance();
        const Float32                       CurrentValue =
            m_AlphaCutoffChanged ? m_AlphaCutoff : Instance.m_AlphaCutoff;
        if (CurrentValue == AlphaCutoff)
            return RADIENT_STATUS_NO_CHANGE;

        m_AlphaCutoff        = AlphaCutoff;
        m_AlphaCutoffChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetDoubleSided(Bool DoubleSided) override final
    {
        DoubleSided                                  = DoubleSided != False ? True : False;
        RadientSurfaceMaterialInstanceImpl& Instance = GetInstance();
        const Bool                          CurrentValue =
            m_DoubleSidedChanged ? m_IsDoubleSided : Instance.m_IsDoubleSided;
        if (CurrentValue == DoubleSided)
            return RADIENT_STATUS_NO_CHANGE;

        m_IsDoubleSided      = DoubleSided;
        m_DoubleSidedChanged = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit() override final
    {
        bool                                StateChanged = ApplyParameterChanges();
        RadientSurfaceMaterialInstanceImpl& Instance     = GetInstance();

        if (m_SurfaceModeChanged && Instance.m_SurfaceMode != m_SurfaceMode)
        {
            Instance.m_SurfaceMode = m_SurfaceMode;
            StateChanged           = true;
        }
        if (m_AlphaCutoffChanged && Instance.m_AlphaCutoff != m_AlphaCutoff)
        {
            Instance.m_AlphaCutoff = m_AlphaCutoff;
            StateChanged           = true;
        }
        if (m_DoubleSidedChanged && Instance.m_IsDoubleSided != m_IsDoubleSided)
        {
            Instance.m_IsDoubleSided = m_IsDoubleSided;
            StateChanged             = true;
        }

        m_SurfaceModeChanged = false;
        m_AlphaCutoffChanged = false;
        m_DoubleSidedChanged = false;
        return FinishCommit(StateChanged);
    }

private:
    RADIENT_MATERIAL_SURFACE_MODE m_SurfaceMode        = RADIENT_MATERIAL_SURFACE_MODE_OPAQUE;
    Float32                       m_AlphaCutoff        = 0.5f;
    Bool                          m_IsDoubleSided      = False;
    bool                          m_SurfaceModeChanged = false;
    bool                          m_AlphaCutoffChanged = false;
    bool                          m_DoubleSidedChanged = false;
};

RADIENT_STATUS RadientMaterialInstanceImpl::CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppWriter = nullptr;

    try
    {
        RefCntAutoPtr<RadientMaterialInstanceWriterImpl> pWriter{
            MakeNewRCObj<RadientMaterialInstanceWriterImpl>()(
                const_cast<RadientMaterialInstanceImpl*>(this))};
        *ppWriter = pWriter.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material instance writer: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientMaterialInstanceImpl::Clone(IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    try
    {
        RefCntAutoPtr<RadientMaterialInstanceImpl> pInstance{
            MakeNewRCObj<RadientMaterialInstanceImpl>()(
                m_pDefinition,
                m_DefinitionHandle,
                this)};
        *ppInstance = pInstance.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to clone Radient material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientSurfaceMaterialInstanceImpl::CreateWriter(IRadientMaterialInstanceWriter** ppWriter) const
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppWriter = nullptr;

    try
    {
        RefCntAutoPtr<RadientSurfaceMaterialInstanceWriterImpl> pWriter{
            MakeNewRCObj<RadientSurfaceMaterialInstanceWriterImpl>()(
                const_cast<RadientSurfaceMaterialInstanceImpl*>(this))};
        *ppWriter = pWriter.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient surface material instance writer: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientSurfaceMaterialInstanceImpl::Clone(IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    try
    {
        RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl> pInstance{
            MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(
                m_pDefinition,
                m_DefinitionHandle,
                this)};
        *ppInstance = pInstance.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to clone Radient surface material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

const PackedMaterialInstanceData& GetMaterialInstanceData(const IRadientMaterialInstance& Instance) noexcept
{
    if (Instance.GetDefinition()->GetDesc().Type == RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE)
        return static_cast<const RadientSurfaceMaterialInstanceImpl&>(Instance).GetPackedData();

    return static_cast<const RadientMaterialInstanceImpl&>(Instance).GetPackedData();
}

} // namespace

// Definition operations that create or consume concrete instances live here to
// keep the instance representation private to this translation unit.

void RadientMaterialDefinitionImpl::WriteShaderData(
    const IRadientMaterialInstance& Instance,
    void*                           pData) const noexcept
{
    if (m_Data.PackingPlan.Size == 0)
        return;

    Uint8* const                                    pShaderData  = static_cast<Uint8*>(pData);
    const PackedMaterialInstanceData&               InstanceData = GetMaterialInstanceData(Instance);
    const RadientSurfaceMaterialInstanceImpl* const pSurfaceInstance =
        m_Data.GetDesc().Type == RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE ?
        &static_cast<const RadientSurfaceMaterialInstanceImpl&>(Instance) :
        nullptr;

    std::memset(pShaderData, 0, m_Data.PackingPlan.Size);
    for (Uint32 InitializationIndex = 0;
         InitializationIndex < m_Data.PackingPlan.InitializationCount;
         ++InitializationIndex)
    {
        const RadientMaterialShaderDataInitialization& Initialization =
            m_Data.PackingPlan.pInitializations[InitializationIndex];
        std::memcpy(pShaderData + Initialization.Offset,
                    Initialization.pData,
                    Initialization.Size);
    }

    const RadientSurfaceMaterialShaderParameterPacking* const pSurfacePacking =
        m_Data.PackingPlan.pSurfacePacking;
    if (pSurfacePacking != nullptr && pSurfacePacking->SurfaceModeOffset != ~Uint32{0})
    {
        VERIFY_EXPR(pSurfaceInstance != nullptr);
        const Uint32 SurfaceMode = static_cast<Uint32>(pSurfaceInstance->GetSurfaceMode());
        std::memcpy(pShaderData + pSurfacePacking->SurfaceModeOffset,
                    &SurfaceMode,
                    sizeof(SurfaceMode));
    }
    if (pSurfacePacking != nullptr && pSurfacePacking->AlphaCutoffOffset != ~Uint32{0})
    {
        VERIFY_EXPR(pSurfaceInstance != nullptr);
        const Float32 AlphaCutoff = pSurfaceInstance->GetAlphaCutoff();
        std::memcpy(pShaderData + pSurfacePacking->AlphaCutoffOffset,
                    &AlphaCutoff,
                    sizeof(AlphaCutoff));
    }

    for (Uint32 CommandIndex = 0; CommandIndex < m_Data.PackingPlan.CopyCommandCount; ++CommandIndex)
    {
        const ShaderDataCopyCommand& Command = m_Data.PackingPlan.pCopyCommands[CommandIndex];
        std::memcpy(pShaderData + Command.DestinationOffset,
                    InstanceData.GetValueData(Command.ParameterIndex),
                    Command.Size);
    }

    for (Uint32 CommandIndex = 0; CommandIndex < m_Data.PackingPlan.TextureCommandCount; ++CommandIndex)
    {
        const RadientMaterialShaderTexturePacking& Command =
            m_Data.PackingPlan.pTextureCommands[CommandIndex];

        const Int32 UVSelector =
            *static_cast<const Int32*>(InstanceData.GetValueData(Command.UVSelectorParameterIndex));
        const Uint32 WrapU =
            *static_cast<const Uint32*>(InstanceData.GetValueData(Command.WrapUParameterIndex));
        const Uint32 WrapV =
            *static_cast<const Uint32*>(InstanceData.GetValueData(Command.WrapVParameterIndex));
        ShaderTextureAttribs TextureAttribs{};
        TextureAttribs.SetUVSelector(UVSelector);
        TextureAttribs.SetWrapUMode(static_cast<TEXTURE_ADDRESS_MODE>(WrapU));
        TextureAttribs.SetWrapVMode(static_cast<TEXTURE_ADDRESS_MODE>(WrapV));
        std::memcpy(pShaderData + Command.Offset + ShaderTexturePackedPropsOffset,
                    &TextureAttribs.PackedProps,
                    sizeof(TextureAttribs.PackedProps));

        IRadientTextureAsset* const pTexture = InstanceData.GetTexture(Command.TextureParameterIndex, 0);
        if (pTexture != nullptr)
        {
            RadientTextureSamplingInfo SamplingInfo{};
            const bool                 SamplingInfoAvailable =
                RadientTextureAssetManager::GetTextureSamplingInfo(pTexture, SamplingInfo);
            VERIFY_EXPR(SamplingInfoAvailable);
            if (SamplingInfoAvailable)
            {
                std::memcpy(pShaderData + Command.Offset + ShaderTextureSliceOffset,
                            &SamplingInfo.TextureSlice,
                            sizeof(SamplingInfo.TextureSlice));
                std::memcpy(pShaderData + Command.Offset + ShaderTextureAtlasUVOffset,
                            &SamplingInfo.UVScaleBias,
                            sizeof(SamplingInfo.UVScaleBias));
            }
        }
    }
}

RADIENT_STATUS DILIGENT_CALL_TYPE RadientMaterialDefinitionImpl::CreateInstance(
    IRadientMaterialInstance** ppInstance) const
{
    if (ppInstance == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    *ppInstance = nullptr;

    const RADIENT_STATUS DefinitionStatus = GetStatus();
    if (RADIENT_FAILED(DefinitionStatus))
        return DefinitionStatus;

    try
    {
        switch (m_Data.GetDesc().Type)
        {
            case RADIENT_MATERIAL_DEFINITION_TYPE_SURFACE:
            {
                RefCntAutoPtr<RadientSurfaceMaterialInstanceImpl> pInstance{
                    MakeNewRCObj<RadientSurfaceMaterialInstanceImpl>()(
                        const_cast<RadientMaterialDefinitionImpl*>(this),
                        m_DefinitionHandle)};
                *ppInstance = pInstance.Detach();
                return RADIENT_STATUS_OK;
            }

            case RADIENT_MATERIAL_DEFINITION_TYPE_POST_PROCESS:
            case RADIENT_MATERIAL_DEFINITION_TYPE_COMPUTE:
            {
                RefCntAutoPtr<RadientMaterialInstanceImpl> pInstance{
                    MakeNewRCObj<RadientMaterialInstanceImpl>()(
                        const_cast<RadientMaterialDefinitionImpl*>(this),
                        m_DefinitionHandle)};
                *ppInstance = pInstance.Detach();
                return RADIENT_STATUS_OK;
            }

            default:
                UNEXPECTED("Unexpected material definition type");
                return RADIENT_STATUS_INVALID_OPERATION;
        }
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create Radient material instance: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
