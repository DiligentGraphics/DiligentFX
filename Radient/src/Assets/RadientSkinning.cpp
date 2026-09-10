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

#include "Assets/RadientAssetManagerImpl.hpp"
#include "Assets/RadientAssetURI.hpp"
#include "Animation/RadientSkeletonPoseImpl.hpp"
#include "Math/RadientMath.hpp"

#include "RadientSkinning.h"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

bool IsFinite(const RadientTransform& Transform) noexcept
{
    return RadientMath::IsFinite(Transform.Position) &&
        RadientMath::IsFinite(Transform.Rotation) &&
        RadientMath::IsFinite(Transform.Scale);
}

bool IsFinite(const RadientMatrix4x4& Matrix) noexcept
{
    for (Float32 Value : Matrix.Data)
    {
        if (!RadientMath::IsFinite(Value))
            return false;
    }
    return true;
}

RADIENT_STATUS ValidateSkeletonDesc(const RadientSkeletonDesc& Desc,
                                    std::vector<Uint32>&       EvaluationOrder)
{
    if (Desc.JointCount == 0)
    {
        LOG_ERROR_MESSAGE("A Radient skeleton must contain at least one joint");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.pJoints == nullptr)
    {
        LOG_ERROR_MESSAGE("Radient skeleton joint data must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
    {
        const RadientSkeletonJointDesc& Joint = Desc.pJoints[JointIndex];
        if (Joint.ParentJointIndex != InvalidRadientJointIndex &&
            Joint.ParentJointIndex >= Desc.JointCount)
        {
            LOG_ERROR_MESSAGE("Skeleton joint ", JointIndex, " references invalid parent joint ", Joint.ParentJointIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (!IsFinite(Joint.LocalRestTransform))
        {
            LOG_ERROR_MESSAGE("Skeleton joint ", JointIndex, " has a non-finite local rest transform");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    std::vector<Uint32> FirstChild(Desc.JointCount, InvalidRadientJointIndex);
    std::vector<Uint32> NextSibling(Desc.JointCount, InvalidRadientJointIndex);
    EvaluationOrder.clear();
    EvaluationOrder.reserve(Desc.JointCount);
    for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
    {
        const Uint32 ParentIndex = Desc.pJoints[JointIndex].ParentJointIndex;
        if (ParentIndex == InvalidRadientJointIndex)
        {
            EvaluationOrder.push_back(JointIndex);
        }
        else
        {
            NextSibling[JointIndex] = FirstChild[ParentIndex];
            FirstChild[ParentIndex] = JointIndex;
        }
    }

    for (size_t OrderIndex = 0; OrderIndex < EvaluationOrder.size(); ++OrderIndex)
    {
        const Uint32 ParentIndex = EvaluationOrder[OrderIndex];
        for (Uint32 ChildIndex = FirstChild[ParentIndex];
             ChildIndex != InvalidRadientJointIndex;
             ChildIndex = NextSibling[ChildIndex])
        {
            EvaluationOrder.push_back(ChildIndex);
        }
    }

    if (EvaluationOrder.size() != Desc.JointCount)
    {
        LOG_ERROR_MESSAGE("Skeleton hierarchy contains a cycle");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ValidateSkinDesc(const RadientSkinDesc& Desc)
{
    if (Desc.pSkeleton == nullptr)
    {
        LOG_ERROR_MESSAGE("A Radient skin must reference a skeleton");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.JointCount == 0)
    {
        LOG_ERROR_MESSAGE("A Radient skin must contain at least one joint");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.pJoints == nullptr)
    {
        LOG_ERROR_MESSAGE("Radient skin joint data must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const Uint32 SkeletonJointCount = Desc.pSkeleton->GetDesc().JointCount;
    for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
    {
        const RadientSkinJointBindingDesc& Joint = Desc.pJoints[JointIndex];
        if (Joint.SkeletonJointIndex >= SkeletonJointCount)
        {
            LOG_ERROR_MESSAGE("Skin joint ", JointIndex, " references invalid skeleton joint ", Joint.SkeletonJointIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (!IsFinite(Joint.InverseBindMatrix))
        {
            LOG_ERROR_MESSAGE("Skin joint ", JointIndex, " has a non-finite inverse-bind matrix");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
}

class RadientSkeletonAssetImpl;

using PackedMemory = std::unique_ptr<void, STDDeleterRawMem<void>>;

class PackedSkeletonData final
{
public:
    PackedSkeletonData(const RadientSkeletonDesc& Desc,
                       const std::vector<Uint32>& EvaluationOrder)
    {
        const Char* const Name = Desc.Name != nullptr ? Desc.Name : "";
        const std::string URI  = MakeRadientAssetURI("skeleton");

        FixedLinearAllocator Allocator{GetRawAllocator()};
        Allocator.AddSpaceForString(URI.c_str());
        Allocator.AddSpaceForString(Name);
        Allocator.AddSpace<RadientSkeletonJointDesc>(Desc.JointCount);
        Allocator.AddSpace<Uint32>(Desc.JointCount);
        for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
        {
            if (Desc.pJoints[JointIndex].Name != nullptr)
            {
                Allocator.AddSpaceForString(Desc.pJoints[JointIndex].Name);
            }
            else
            {
                const std::string JointName = std::string{"Joint "} + std::to_string(JointIndex);
                Allocator.AddSpaceForString(JointName.c_str());
            }
        }

        Allocator.Reserve();
        const size_t MemorySize = Allocator.GetReservedSize();
        m_Memory                = PackedMemory{Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

        FixedLinearAllocator Writer{m_Memory.get(), MemorySize};
        m_Reference.URI = Writer.CopyString(URI);
        m_Desc.Name     = Writer.CopyString(Name);

        RadientSkeletonJointDesc* const pJoints =
            Writer.ConstructArray<RadientSkeletonJointDesc>(Desc.JointCount);
        VERIFY_EXPR(EvaluationOrder.size() == Desc.JointCount);
        m_pEvaluationOrder = Writer.CopyArray(EvaluationOrder.data(), Desc.JointCount);
        for (Uint32 JointIndex = 0; JointIndex < Desc.JointCount; ++JointIndex)
        {
            pJoints[JointIndex] = Desc.pJoints[JointIndex];
            if (Desc.pJoints[JointIndex].Name != nullptr)
            {
                pJoints[JointIndex].Name = Writer.CopyString(Desc.pJoints[JointIndex].Name);
            }
            else
            {
                const std::string JointName = std::string{"Joint "} + std::to_string(JointIndex);
                pJoints[JointIndex].Name    = Writer.CopyString(JointName);
            }
            pJoints[JointIndex].LocalRestTransform =
                RadientMath::NormalizeTransform(Desc.pJoints[JointIndex].LocalRestTransform);
        }

        m_Reference.Version = 1;
        m_Desc.pJoints      = pJoints;
        m_Desc.JointCount   = Desc.JointCount;
        VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
    }

    const RadientAssetReference& GetReference() const noexcept
    {
        return m_Reference;
    }

    const RadientSkeletonDesc& GetDesc() const noexcept
    {
        return m_Desc;
    }

    const Uint32* GetEvaluationOrder() const noexcept
    {
        return m_pEvaluationOrder;
    }

private:
    PackedMemory          m_Memory;
    RadientAssetReference m_Reference;
    RadientSkeletonDesc   m_Desc;
    const Uint32*         m_pEvaluationOrder = nullptr;
};

class RadientSkeletonAssetImpl final : public ObjectBase<IRadientSkeletonAsset>
{
public:
    using TBase = ObjectBase<IRadientSkeletonAsset>;

    RadientSkeletonAssetImpl(IReferenceCounters*        pRefCounters,
                             const RadientSkeletonDesc& Desc,
                             const std::vector<Uint32>& EvaluationOrder) :
        TBase{pRefCounters},
        m_Data{Desc, EvaluationOrder}
    {}

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientSkeletonAsset || IID == IID_RadientAsset)
        {
            *ppInterface = static_cast<IRadientSkeletonAsset*>(this);
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Data.GetReference();
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_SKELETON;
    }

    virtual const RadientSkeletonDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Data.GetDesc();
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreatePose(IRadientSkeletonPose** ppPose) override final;

    const Uint32* GetEvaluationOrder() const noexcept
    {
        return m_Data.GetEvaluationOrder();
    }

private:
    PackedSkeletonData m_Data;
};

class PackedSkinData final
{
public:
    explicit PackedSkinData(const RadientSkinDesc& Desc) :
        m_pSkeleton{Desc.pSkeleton}
    {
        const Char* const Name = Desc.Name != nullptr ? Desc.Name : "";
        const std::string URI  = MakeRadientAssetURI("skin");

        FixedLinearAllocator Allocator{GetRawAllocator()};
        Allocator.AddSpaceForString(URI.c_str());
        Allocator.AddSpaceForString(Name);
        Allocator.AddSpace<RadientSkinJointBindingDesc>(Desc.JointCount);

        Allocator.Reserve();
        const size_t MemorySize = Allocator.GetReservedSize();
        m_Memory                = PackedMemory{Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

        FixedLinearAllocator Writer{m_Memory.get(), MemorySize};
        m_Reference.URI     = Writer.CopyString(URI);
        m_Reference.Version = 1;
        m_Desc.Name         = Writer.CopyString(Name);
        m_Desc.pSkeleton    = m_pSkeleton;
        m_Desc.pJoints      = Writer.CopyArray(Desc.pJoints, Desc.JointCount);
        m_Desc.JointCount   = Desc.JointCount;
        VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
    }

    const RadientAssetReference& GetReference() const noexcept
    {
        return m_Reference;
    }

    const RadientSkinDesc& GetDesc() const noexcept
    {
        return m_Desc;
    }

private:
    RefCntAutoPtr<IRadientSkeletonAsset> m_pSkeleton;
    PackedMemory                         m_Memory;
    RadientAssetReference                m_Reference;
    RadientSkinDesc                      m_Desc;
};

class RadientSkinAssetImpl final : public ObjectBase<IRadientSkinAsset>
{
public:
    using TBase = ObjectBase<IRadientSkinAsset>;

    RadientSkinAssetImpl(IReferenceCounters*    pRefCounters,
                         const RadientSkinDesc& Desc) :
        TBase{pRefCounters},
        m_Data{Desc}
    {}

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientSkinAsset || IID == IID_RadientAsset)
        {
            *ppInterface = static_cast<IRadientSkinAsset*>(this);
            (*ppInterface)->AddRef();
        }
        else
        {
            TBase::QueryInterface(IID, ppInterface);
        }
    }
    using IObject::QueryInterface;

    virtual const RadientAssetReference& DILIGENT_CALL_TYPE GetReference() const override final
    {
        return m_Data.GetReference();
    }

    virtual RADIENT_ASSET_TYPE DILIGENT_CALL_TYPE GetType() const override final
    {
        return RADIENT_ASSET_TYPE_SKIN;
    }

    virtual const RadientSkinDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Data.GetDesc();
    }

private:
    PackedSkinData m_Data;
};

RADIENT_STATUS RadientSkeletonAssetImpl::CreatePose(IRadientSkeletonPose** ppPose)
{
    if (ppPose == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppPose == nullptr, "Output skeleton pose pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppPose = nullptr;

    try
    {
        RefCntAutoPtr<RadientSkeletonPoseImpl> pPose{
            MakeNewRCObj<RadientSkeletonPoseImpl>()(this, GetEvaluationOrder())};
        *ppPose = pPose.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton pose: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace

RADIENT_STATUS RadientAssetManagerImpl::CreateSkeleton(const RadientSkeletonDesc& SkeletonDesc,
                                                       IRadientSkeletonAsset**    ppSkeleton)
{
    if (ppSkeleton == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppSkeleton == nullptr, "Output skeleton pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppSkeleton = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        std::vector<Uint32>  EvaluationOrder;
        const RADIENT_STATUS ValidationStatus = ValidateSkeletonDesc(SkeletonDesc, EvaluationOrder);
        if (RADIENT_FAILED(ValidationStatus))
            return ValidationStatus;

        RefCntAutoPtr<RadientSkeletonAssetImpl> pSkeleton{
            MakeNewRCObj<RadientSkeletonAssetImpl>()(SkeletonDesc, EvaluationOrder)};
        *ppSkeleton = pSkeleton.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientAssetManagerImpl::CreateSkin(const RadientSkinDesc& SkinDesc,
                                                   IRadientSkinAsset**    ppSkin)
{
    if (ppSkin == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppSkin == nullptr, "Output skin pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppSkin = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        const RADIENT_STATUS ValidationStatus = ValidateSkinDesc(SkinDesc);
        if (RADIENT_FAILED(ValidationStatus))
            return ValidationStatus;

        RefCntAutoPtr<RadientSkinAssetImpl> pSkin{MakeNewRCObj<RadientSkinAssetImpl>()(SkinDesc)};
        *ppSkin = pSkin.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skin: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
