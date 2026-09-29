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

#include "Animation/RadientMaterialAnimationDestinationBinding.hpp"
#include "Animation/RadientAnimationValueType.hpp"
#include "Assets/RadientMaterialStorage.hpp"
#include "Core/RadientValidation.hpp"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <cstring>
#include <exception>
#include <memory>
#include <vector>

namespace Diligent
{

namespace
{

using RadientMaterialDetail::MaterialParameterUpdate;
using RadientMaterialDetail::MaterialStorage;

RADIENT_ANIMATION_VALUE_TYPE GetParameterAnimationValueType(RADIENT_MATERIAL_PARAMETER_TYPE Type) noexcept
{
    switch (Type)
    {
        case RADIENT_MATERIAL_PARAMETER_TYPE_BOOL:
            return RADIENT_ANIMATION_VALUE_TYPE_BOOL;
        case RADIENT_MATERIAL_PARAMETER_TYPE_INT:
            return RADIENT_ANIMATION_VALUE_TYPE_INT;
        case RADIENT_MATERIAL_PARAMETER_TYPE_INT2:
            return RADIENT_ANIMATION_VALUE_TYPE_INT2;
        case RADIENT_MATERIAL_PARAMETER_TYPE_INT3:
            return RADIENT_ANIMATION_VALUE_TYPE_INT3;
        case RADIENT_MATERIAL_PARAMETER_TYPE_INT4:
            return RADIENT_ANIMATION_VALUE_TYPE_INT4;
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT:
            return RADIENT_ANIMATION_VALUE_TYPE_UINT;
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT2:
            return RADIENT_ANIMATION_VALUE_TYPE_UINT2;
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT3:
            return RADIENT_ANIMATION_VALUE_TYPE_UINT3;
        case RADIENT_MATERIAL_PARAMETER_TYPE_UINT4:
            return RADIENT_ANIMATION_VALUE_TYPE_UINT4;
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT:
            return RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2:
            return RADIENT_ANIMATION_VALUE_TYPE_FLOAT2;
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3:
            return RADIENT_ANIMATION_VALUE_TYPE_FLOAT3;
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4:
            return RADIENT_ANIMATION_VALUE_TYPE_FLOAT4;
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT2X2:
            return RADIENT_ANIMATION_VALUE_TYPE_FLOAT2X2;
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT3X3:
            return RADIENT_ANIMATION_VALUE_TYPE_FLOAT3X3;
        case RADIENT_MATERIAL_PARAMETER_TYPE_FLOAT4X4:
            return RADIENT_ANIMATION_VALUE_TYPE_FLOAT4X4;
        default:
            return RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN;
    }
}

class MaterialAnimationBinding final : public ObjectBase<IRadientAnimationDestinationBinding>
{
public:
    using TBase = ObjectBase<IRadientAnimationDestinationBinding>;

    MaterialAnimationBinding(IReferenceCounters*    pRefCounters,
                             IRadientMaterialAsset* pMaterial,
                             MaterialStorage&       Storage,
                             Float32*               pAlphaCutoff) :
        TBase{pRefCounters},
        m_pMaterial{pMaterial},
        m_Storage{Storage},
        m_pAlphaCutoff{pAlphaCutoff}
    {}

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientAnimationDestinationBinding, TBase)

    RADIENT_STATUS Initialize(const RadientAnimationPropertyBindingDesc* pProperties,
                              Uint32                                     PropertyCount,
                              RadientAnimationResolvedPropertyDesc*      pResolvedProperties)
    {
        IRadientMaterialDefinitionAsset* const pDefinition = m_pMaterial->GetDefinition();
        if (pDefinition == nullptr)
            return RADIENT_STATUS_INVALID_OPERATION;

        struct BoundOutput
        {
            Uint32 UpdateIndex; // ~0u denotes the surface alpha cutoff.
            Uint32 Size;
            Uint32 Alignment;
        };
        std::vector<MaterialParameterUpdate> Updates;
        std::vector<BoundOutput>             Outputs;
        Updates.reserve(PropertyCount);
        Outputs.reserve(PropertyCount);
        bool HasAlphaCutoff = false;
        for (Uint32 Index = 0; Index < PropertyCount; ++Index)
        {
            const RadientAnimationPropertyBindingDesc& Property = pProperties[Index];
            if (Property.Schema == InvalidRadientAnimationSchemaID ||
                Property.Property == nullptr || Property.Property[0] == '\0' ||
                Property.DestinationElement == InvalidRadientAnimationDestinationElement ||
                Property.Value.Type <= RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN ||
                Property.Value.Type >= RADIENT_ANIMATION_VALUE_TYPE_COUNT ||
                Property.Value.ArraySize == 0)
            {
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }

            RadientMaterialParameterHandle Handle;
            RADIENT_ANIMATION_VALUE_TYPE   ValueType = RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN;
            Uint32                         ArraySize = 1;
            if (Property.Schema == RadientMaterialAnimationSchemaID)
            {
                const RADIENT_STATUS Status = pDefinition->FindParameter(Property.Property, &Handle);
                if (Status == RADIENT_STATUS_NOT_FOUND)
                    continue;
                if (RADIENT_FAILED(Status))
                    return Status;
                const RadientMaterialParameterDesc& Parameter = pDefinition->GetParameterDesc(Handle.Index);
                ValueType                                     = GetParameterAnimationValueType(Parameter.Type);
                if (ValueType == RADIENT_ANIMATION_VALUE_TYPE_UNKNOWN)
                    continue;
                ArraySize = Parameter.ArraySize;
            }
            else if (Property.Schema == RadientSurfaceMaterialAnimationSchemaID &&
                     std::strcmp(Property.Property, RadientSurfaceMaterialAlphaCutoffPropertyName) == 0)
            {
                if (m_pAlphaCutoff == nullptr)
                    continue;
                ValueType = RADIENT_ANIMATION_VALUE_TYPE_FLOAT;
            }
            else
            {
                continue;
            }

            if (Property.DestinationElement != 0)
                return RADIENT_STATUS_NOT_FOUND;
            if (Property.Value.Type != ValueType ||
                !RadientValidation::IsValidSubrange(Property.FirstArrayElement, Property.Value.ArraySize, ArraySize))
            {
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
            const AnimationValueTypeInfo TypeInfo    = GetAnimationValueTypeInfo(ValueType);
            const Uint32                 ElementSize = TypeInfo.NativeSize;
            if (!RadientValidation::IsProductRepresentable<Uint32>(ElementSize, ArraySize) ||
                !RadientValidation::IsAddressableArray(ArraySize, ElementSize))
            {
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }

            const Uint32 Size        = Property.Value.ArraySize * ElementSize;
            Uint32       UpdateIndex = ~0u;
            if (Handle)
            {
                const MaterialParameterUpdate Update{Handle.Index, Property.FirstArrayElement * ElementSize, Size, nullptr};
                for (const MaterialParameterUpdate& Existing : Updates)
                {
                    if (Existing.ParameterIndex == Update.ParameterIndex &&
                        Existing.Offset < Update.Offset + Update.Size && Update.Offset < Existing.Offset + Existing.Size)
                    {
                        return RADIENT_STATUS_INVALID_ARGUMENT;
                    }
                }

                UpdateIndex = static_cast<Uint32>(Updates.size());
                Updates.push_back(Update);
            }
            else
            {
                if (HasAlphaCutoff)
                    return RADIENT_STATUS_INVALID_ARGUMENT;
                HasAlphaCutoff = true;
            }
            Outputs.push_back({UpdateIndex, Size, TypeInfo.NativeAlignment});
            pResolvedProperties[Index].Semantic = RADIENT_ANIMATION_VALUE_SEMANTIC_COMPONENT_WISE;
        }

        if (Outputs.empty())
            return RADIENT_STATUS_UNSUPPORTED;

        FixedLinearAllocator Allocator{GetRawAllocator()};
        Allocator.AddSpace<MaterialParameterUpdate>(Updates.size());
        Allocator.AddSpace<void*>(Outputs.size());
        // Reserve and allocate the same regions in the same order; the allocator
        // handles all padding within the single runtime allocation.
        for (const BoundOutput& Output : Outputs)
            Allocator.AddSpace(Output.Size, Output.Alignment);
        Allocator.Reserve();

        m_pUpdates    = Allocator.CopyArray(Updates.data(), Updates.size());
        m_UpdateCount = static_cast<Uint32>(Updates.size());
        m_ppOutputs   = Allocator.ConstructArray<void*>(Outputs.size());
        for (size_t Index = 0; Index < Outputs.size(); ++Index)
        {
            const BoundOutput& Output = Outputs[Index];
            void* const        pValue = Allocator.Allocate(Output.Size, Output.Alignment);
            std::memset(pValue, 0, Output.Size);
            m_ppOutputs[Index] = pValue;
            if (Output.UpdateIndex != ~0u)
                m_pUpdates[Output.UpdateIndex].pData = pValue;
            else
                m_pNewAlphaCutoff = new (pValue) Float32{0};
        }
        m_RuntimeMemory = decltype(m_RuntimeMemory){Allocator.Release(), STDDeleterRawMem<void>{GetRawAllocator()}};
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE BeginUpdate(void* const** ppOutputs) override final
    {
        if (ppOutputs == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;
        *ppOutputs = nullptr;
        if (m_UpdateActive)
            return RADIENT_STATUS_INVALID_OPERATION;

        m_UpdateActive = true;
        *ppOutputs     = m_ppOutputs;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE EndUpdate(Bool UpdateDerivedState) override final
    {
        static_cast<void>(UpdateDerivedState);
        if (!m_UpdateActive)
            return RADIENT_STATUS_INVALID_OPERATION;
        m_UpdateActive = false;

        // Compare against live values so external edits are respected even when
        // the sampled values repeat. Publish the entire batch under one lock;
        // unanimated array elements are never read back or overwritten.
        const RADIENT_STATUS Status = m_Storage.ApplyAnimationUpdates(
            m_pUpdates, m_UpdateCount, m_pAlphaCutoff, m_pNewAlphaCutoff);
        // Publication is a primary write even when derived updates are deferred.
        return RADIENT_FAILED(Status) ? Status : RADIENT_STATUS_OK;
    }

private:
    // The retained asset owns both the storage and the optional alpha-cutoff field.
    const RefCntAutoPtr<IRadientMaterialAsset> m_pMaterial;
    MaterialStorage&                           m_Storage;
    Float32* const                             m_pAlphaCutoff;
    // The descriptors, output pointers, and aligned sampled values share one block.
    std::unique_ptr<void, STDDeleterRawMem<void>> m_RuntimeMemory;
    MaterialParameterUpdate*                      m_pUpdates        = nullptr;
    void**                                        m_ppOutputs       = nullptr;
    Float32*                                      m_pNewAlphaCutoff = nullptr;
    Uint32                                        m_UpdateCount     = 0;
    bool                                          m_UpdateActive    = false;
};

} // namespace

RADIENT_STATUS CreateRadientMaterialAnimationBinding(
    IRadientMaterialAsset*                     pMaterial,
    MaterialStorage&                           Storage,
    Float32*                                   pAlphaCutoff,
    const RadientAnimationPropertyBindingDesc* pProperties,
    Uint32                                     PropertyCount,
    RadientAnimationResolvedPropertyDesc*      pResolvedProperties,
    IRadientAnimationDestinationBinding**      ppBinding)
{
    if (ppBinding == nullptr || *ppBinding != nullptr || pMaterial == nullptr ||
        PropertyCount == 0 || pProperties == nullptr || pResolvedProperties == nullptr)
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (!RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationPropertyBindingDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(RadientAnimationResolvedPropertyDesc)) ||
        !RadientValidation::IsAddressableArray(PropertyCount, sizeof(void*)))
    {
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    for (Uint32 Index = 0; Index < PropertyCount; ++Index)
        pResolvedProperties[Index] = {};

    try
    {
        RefCntAutoPtr<MaterialAnimationBinding> pBinding{MakeNewRCObj<MaterialAnimationBinding>()(pMaterial, Storage, pAlphaCutoff)};
        const RADIENT_STATUS                    Status = pBinding->Initialize(pProperties, PropertyCount, pResolvedProperties);
        if (Status != RADIENT_STATUS_OK)
            return Status;
        *ppBinding = pBinding.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a material animation binding: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to create a material animation binding due to an unknown error");
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
