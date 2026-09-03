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
#include "Math/RadientMath.hpp"

#include "RadientSkinning.h"

#include "DebugUtilities.hpp"
#include "EngineMemory.h"
#include "FixedLinearAllocator.hpp"
#include "ObjectBase.hpp"
#include "RefCntAutoPtr.hpp"
#include "STDAllocator.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Diligent
{

namespace
{

static_assert(std::is_trivially_copyable<RadientTransform>::value,
              "RadientTransform must support byte-wise copying");
static_assert(std::is_trivially_copyable<RadientMatrix4x4>::value,
              "RadientMatrix4x4 must support byte-wise copying");

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

bool IsValidRange(Uint32 First, Uint32 Count, Uint32 Total) noexcept
{
    return First <= Total && Count <= Total - First;
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

size_t GetAnimationCurveValueCount(const RadientAnimationCurveDesc& Curve) noexcept
{
    return Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ?
        static_cast<size_t>(Curve.KeyframeCount) * 3u :
        Curve.KeyframeCount;
}

struct AnimationTimeRange
{
    Float32 Start = std::numeric_limits<Float32>::infinity();
    Float32 End   = -std::numeric_limits<Float32>::infinity();
};

struct JointAnimationStart
{
    Float32 StartTime  = std::numeric_limits<Float32>::infinity();
    Uint32  JointIndex = 0;
};

AnimationTimeRange GetAnimationTrackTimeRange(const RadientSkeletonAnimationTrackDesc& Track) noexcept
{
    AnimationTimeRange Range;
    const auto         IncludeCurve = [&Range](const RadientAnimationCurveDesc& Curve) {
        if (Curve.KeyframeCount == 0)
            return;

        Range.Start = std::min(Range.Start, Curve.pTimes[0]);
        Range.End   = std::max(Range.End, Curve.pTimes[Curve.KeyframeCount - 1]);
    };

    IncludeCurve(Track.Translation);
    IncludeCurve(Track.Rotation);
    IncludeCurve(Track.Scale);
    return Range;
}

template <typename ValueType>
void AddAnimationCurveSpace(FixedLinearAllocator&            Allocator,
                            const RadientAnimationCurveDesc& Curve) noexcept
{
    if (Curve.KeyframeCount == 0)
        return;

    Allocator.AddSpace<Float32>(Curve.KeyframeCount);
    Allocator.AddSpace<ValueType>(GetAnimationCurveValueCount(Curve));
}

template <typename ValueType>
void CopyAnimationCurve(FixedLinearAllocator&            Writer,
                        const RadientAnimationCurveDesc& SrcCurve,
                        RadientAnimationCurveDesc&       DstCurve)
{
    DstCurve.Interpolation = SrcCurve.Interpolation;
    DstCurve.KeyframeCount = SrcCurve.KeyframeCount;
    if (SrcCurve.KeyframeCount == 0)
        return;

    DstCurve.pTimes  = Writer.CopyArray(SrcCurve.pTimes, SrcCurve.KeyframeCount);
    DstCurve.pValues = Writer.CopyArray(
        static_cast<const ValueType*>(SrcCurve.pValues),
        GetAnimationCurveValueCount(SrcCurve));
}

template <typename ValueType>
RADIENT_STATUS ValidateAnimationCurve(const RadientAnimationCurveDesc& Curve,
                                      Float32                          ClipDuration,
                                      Uint32                           TrackIndex,
                                      const char*                      CurveName,
                                      bool                             ValidateUnitValues)
{
    if (Curve.Interpolation >= RADIENT_ANIMATION_INTERPOLATION_COUNT)
    {
        LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName, " curve uses an invalid interpolation mode");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    if (Curve.KeyframeCount == 0)
    {
        if (Curve.pTimes != nullptr || Curve.pValues != nullptr)
        {
            LOG_ERROR_MESSAGE("Absent animation track ", TrackIndex, ' ', CurveName,
                              " curve must not provide keyframe data");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        return RADIENT_STATUS_OK;
    }

    if (Curve.pTimes == nullptr)
    {
        LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName, " curve keyframe times must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Curve.pValues == nullptr)
    {
        LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName, " curve values must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE &&
        Curve.KeyframeCount > std::numeric_limits<Uint32>::max() / 3u)
    {
        LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName, " curve contains too many keyframes");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    for (Uint32 KeyIndex = 0; KeyIndex < Curve.KeyframeCount; ++KeyIndex)
    {
        const Float32 Time = Curve.pTimes[KeyIndex];
        if (!RadientMath::IsFinite(Time) || Time < 0.f || Time > ClipDuration)
        {
            LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName,
                              " curve keyframe ", KeyIndex, " has time outside the clip duration");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (KeyIndex != 0 && Time <= Curve.pTimes[KeyIndex - 1])
        {
            LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName,
                              " curve keyframe times must be strictly increasing");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    const ValueType* const pValues    = static_cast<const ValueType*>(Curve.pValues);
    const size_t           ValueCount = GetAnimationCurveValueCount(Curve);
    for (size_t ValueIndex = 0; ValueIndex < ValueCount; ++ValueIndex)
    {
        if (!RadientMath::IsFinite(pValues[ValueIndex]))
        {
            LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName,
                              " curve contains a non-finite value at index ", ValueIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    if (ValidateUnitValues)
    {
        constexpr Float32 UnitLengthTolerance = 1e-4f;
        for (Uint32 KeyIndex = 0; KeyIndex < Curve.KeyframeCount; ++KeyIndex)
        {
            const size_t ValueIndex = Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ?
                static_cast<size_t>(KeyIndex) * 3u + 1u :
                KeyIndex;
            if (std::abs(RadientMath::LengthSq(pValues[ValueIndex]) - 1.f) > UnitLengthTolerance)
            {
                LOG_ERROR_MESSAGE("Animation track ", TrackIndex, ' ', CurveName,
                                  " curve keyframe ", KeyIndex, " is not a normalized rotation");
                return RADIENT_STATUS_INVALID_ARGUMENT;
            }
        }
    }

    return RADIENT_STATUS_OK;
}

RADIENT_STATUS ValidateSkeletonAnimationDesc(const RadientSkeletonAnimationDesc& Desc)
{
    if (Desc.pSkeleton == nullptr)
    {
        LOG_ERROR_MESSAGE("A Radient skeleton animation must reference a skeleton");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (!RadientMath::IsFinite(Desc.Duration) || Desc.Duration < 0.f)
    {
        LOG_ERROR_MESSAGE("Radient skeleton animation duration must be finite and non-negative");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }
    if (Desc.TrackCount != 0 && Desc.pTracks == nullptr)
    {
        LOG_ERROR_MESSAGE("Radient skeleton animation track data must not be null");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    const Uint32      SkeletonJointCount = Desc.pSkeleton->GetDesc().JointCount;
    std::vector<bool> AnimatedJoints(SkeletonJointCount, false);
    for (Uint32 TrackIndex = 0; TrackIndex < Desc.TrackCount; ++TrackIndex)
    {
        const RadientSkeletonAnimationTrackDesc& Track = Desc.pTracks[TrackIndex];
        if (Track.SkeletonJointIndex >= SkeletonJointCount)
        {
            LOG_ERROR_MESSAGE("Animation track ", TrackIndex, " references invalid skeleton joint ", Track.SkeletonJointIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (AnimatedJoints[Track.SkeletonJointIndex])
        {
            LOG_ERROR_MESSAGE("Animation tracks contain duplicate skeleton joint ", Track.SkeletonJointIndex);
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        AnimatedJoints[Track.SkeletonJointIndex] = true;

        RADIENT_STATUS Status = ValidateAnimationCurve<RadientFloat3>(Track.Translation, Desc.Duration, TrackIndex, "translation", false);
        if (RADIENT_FAILED(Status))
            return Status;
        Status = ValidateAnimationCurve<RadientQuaternion>(Track.Rotation, Desc.Duration, TrackIndex, "rotation", true);
        if (RADIENT_FAILED(Status))
            return Status;
        Status = ValidateAnimationCurve<RadientFloat3>(Track.Scale, Desc.Duration, TrackIndex, "scale", false);
        if (RADIENT_FAILED(Status))
            return Status;

        if (Track.Translation.KeyframeCount == 0 &&
            Track.Rotation.KeyframeCount == 0 &&
            Track.Scale.KeyframeCount == 0)
        {
            LOG_ERROR_MESSAGE("Animation track ", TrackIndex, " does not contain any curves");
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
    }

    return RADIENT_STATUS_OK;
}

class RadientSkeletonAssetImpl;
class RadientSkeletonPoseImpl;
class RadientSkeletonPoseWriterImpl;
class RadientSkeletonAnimationAssetImpl;

struct SkeletonPoseState
{
    std::vector<RadientTransform> LocalTransforms;
    std::vector<RadientMatrix4x4> GlobalMatrices;
    Uint64                        Version               = 1;
    bool                          GlobalTransformsDirty = false;
};

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

class PackedSkeletonAnimationData final
{
public:
    explicit PackedSkeletonAnimationData(const RadientSkeletonAnimationDesc& Desc) :
        m_pSkeleton{Desc.pSkeleton}
    {
        const Char* const Name               = Desc.Name != nullptr ? Desc.Name : "";
        const std::string URI                = MakeRadientAssetURI("skeleton-animation");
        const Uint32      SkeletonJointCount = m_pSkeleton->GetDesc().JointCount;

        FixedLinearAllocator Allocator{GetRawAllocator()};
        Allocator.AddSpaceForString(URI.c_str());
        Allocator.AddSpaceForString(Name);
        Allocator.AddSpace<RadientSkeletonAnimationTrackDesc>(Desc.TrackCount);
        Allocator.AddSpace<AnimationTimeRange>(Desc.TrackCount);
        Allocator.AddSpace<JointAnimationStart>(SkeletonJointCount);
        for (Uint32 TrackIndex = 0; TrackIndex < Desc.TrackCount; ++TrackIndex)
        {
            const RadientSkeletonAnimationTrackDesc& Track = Desc.pTracks[TrackIndex];
            AddAnimationCurveSpace<RadientFloat3>(Allocator, Track.Translation);
            AddAnimationCurveSpace<RadientQuaternion>(Allocator, Track.Rotation);
            AddAnimationCurveSpace<RadientFloat3>(Allocator, Track.Scale);
        }

        Allocator.Reserve();
        const size_t MemorySize = Allocator.GetReservedSize();
        m_Memory                = PackedMemory{Allocator.ReleaseOwnership(), STDDeleterRawMem<void>{GetRawAllocator()}};

        FixedLinearAllocator Writer{m_Memory.get(), MemorySize};
        m_Reference.URI = Writer.CopyString(URI);
        m_Desc.Name     = Writer.CopyString(Name);

        RadientSkeletonAnimationTrackDesc* const pTracks =
            Writer.ConstructArray<RadientSkeletonAnimationTrackDesc>(Desc.TrackCount);
        m_pTrackTimeRanges   = Writer.ConstructArray<AnimationTimeRange>(Desc.TrackCount);
        m_pJointsByStartTime = Writer.ConstructArray<JointAnimationStart>(SkeletonJointCount);
        for (Uint32 JointIndex = 0; JointIndex < SkeletonJointCount; ++JointIndex)
            m_pJointsByStartTime[JointIndex].JointIndex = JointIndex;

        for (Uint32 TrackIndex = 0; TrackIndex < Desc.TrackCount; ++TrackIndex)
        {
            const RadientSkeletonAnimationTrackDesc& SrcTrack = Desc.pTracks[TrackIndex];
            RadientSkeletonAnimationTrackDesc&       DstTrack = pTracks[TrackIndex];
            DstTrack.SkeletonJointIndex                       = SrcTrack.SkeletonJointIndex;
            CopyAnimationCurve<RadientFloat3>(Writer, SrcTrack.Translation, DstTrack.Translation);
            CopyAnimationCurve<RadientQuaternion>(Writer, SrcTrack.Rotation, DstTrack.Rotation);
            CopyAnimationCurve<RadientFloat3>(Writer, SrcTrack.Scale, DstTrack.Scale);

            const AnimationTimeRange TimeRange                          = GetAnimationTrackTimeRange(DstTrack);
            m_pTrackTimeRanges[TrackIndex]                              = TimeRange;
            m_pJointsByStartTime[DstTrack.SkeletonJointIndex].StartTime = TimeRange.Start;
        }

        std::sort(m_pJointsByStartTime, m_pJointsByStartTime + SkeletonJointCount,
                  [](const JointAnimationStart& Lhs, const JointAnimationStart& Rhs) {
                      return Lhs.StartTime < Rhs.StartTime ||
                          (Lhs.StartTime == Rhs.StartTime && Lhs.JointIndex < Rhs.JointIndex);
                  });

        m_Reference.Version = 1;
        m_Desc.pSkeleton    = m_pSkeleton;
        m_Desc.pTracks      = pTracks;
        m_Desc.TrackCount   = Desc.TrackCount;
        m_Desc.Duration     = Desc.Duration;
        VERIFY_EXPR(Writer.GetCurrentSize() <= Writer.GetReservedSize());
    }

    const RadientAssetReference& GetReference() const noexcept
    {
        return m_Reference;
    }

    const RadientSkeletonAnimationDesc& GetDesc() const noexcept
    {
        return m_Desc;
    }

    IRadientSkeletonAsset* GetSkeleton() const noexcept
    {
        return m_pSkeleton;
    }

    const AnimationTimeRange* GetTrackTimeRanges() const noexcept
    {
        return m_pTrackTimeRanges;
    }

    const JointAnimationStart* GetJointsByStartTime() const noexcept
    {
        return m_pJointsByStartTime;
    }

private:
    RefCntAutoPtr<IRadientSkeletonAsset> m_pSkeleton;
    PackedMemory                         m_Memory;
    RadientAssetReference                m_Reference;
    RadientSkeletonAnimationDesc         m_Desc;
    AnimationTimeRange*                  m_pTrackTimeRanges   = nullptr;
    JointAnimationStart*                 m_pJointsByStartTime = nullptr;
};

class RadientSkeletonAnimationAssetImpl final : public ObjectBase<IRadientSkeletonAnimationAsset>
{
public:
    using TBase = ObjectBase<IRadientSkeletonAnimationAsset>;

    RadientSkeletonAnimationAssetImpl(IReferenceCounters*                 pRefCounters,
                                      const RadientSkeletonAnimationDesc& Desc) :
        TBase{pRefCounters},
        m_Data{Desc}
    {
    }

    virtual void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override final
    {
        if (ppInterface == nullptr)
            return;

        if (IID == IID_RadientSkeletonAnimationAsset || IID == IID_RadientAsset)
        {
            *ppInterface = static_cast<IRadientSkeletonAnimationAsset*>(this);
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
        return RADIENT_ASSET_TYPE_SKELETON_ANIMATION;
    }

    virtual const RadientSkeletonAnimationDesc& DILIGENT_CALL_TYPE GetDesc() const override final
    {
        return m_Data.GetDesc();
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Evaluate(Float64               Time,
                                                       IRadientSkeletonPose* pPose,
                                                       Bool                  UpdateGlobalTransforms) const override final;

private:
    PackedSkeletonAnimationData m_Data;
};

class RadientSkeletonPoseImpl final : public ObjectBase<IRadientSkeletonPose>
{
public:
    using TBase = ObjectBase<IRadientSkeletonPose>;

    RadientSkeletonPoseImpl(IReferenceCounters*       pRefCounters,
                            RadientSkeletonAssetImpl* pSkeleton) :
        TBase{pRefCounters},
        m_pSkeleton{pSkeleton}
    {
        VERIFY_EXPR(m_pSkeleton != nullptr);

        const RadientSkeletonDesc& SkeletonDesc = m_pSkeleton->GetDesc();
        m_State.LocalTransforms.reserve(SkeletonDesc.JointCount);
        for (Uint32 JointIndex = 0; JointIndex < SkeletonDesc.JointCount; ++JointIndex)
            m_State.LocalTransforms.push_back(SkeletonDesc.pJoints[JointIndex].LocalRestTransform);
        m_State.GlobalMatrices.resize(m_State.LocalTransforms.size());
        ComputeGlobalMatrices();
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSkeletonPose, TBase)

    virtual IRadientSkeletonAsset* DILIGENT_CALL_TYPE GetSkeleton() const override final
    {
        return m_pSkeleton;
    }

    virtual Uint64 DILIGENT_CALL_TYPE GetVersion() const override final
    {
        return m_State.Version;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointLocalTransforms(Uint32            FirstJoint,
                                                                      Uint32            JointCount,
                                                                      RadientTransform* pTransforms) const override final
    {
        if (!IsValidRange(FirstJoint, JointCount, static_cast<Uint32>(m_State.LocalTransforms.size())) ||
            (JointCount != 0 && pTransforms == nullptr))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (JointCount != 0)
        {
            std::memcpy(pTransforms,
                        m_State.LocalTransforms.data() + FirstJoint,
                        sizeof(RadientTransform) * JointCount);
        }
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE GetJointGlobalMatrices(Uint32            FirstJoint,
                                                                     Uint32            JointCount,
                                                                     RadientMatrix4x4* pMatrices) const override final
    {
        if (!IsValidRange(FirstJoint, JointCount, static_cast<Uint32>(m_State.GlobalMatrices.size())) ||
            (JointCount != 0 && pMatrices == nullptr))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }
        if (JointCount == 0)
            return RADIENT_STATUS_OK;
        if (m_State.GlobalTransformsDirty)
            return RADIENT_STATUS_PENDING;

        std::memcpy(pMatrices,
                    m_State.GlobalMatrices.data() + FirstJoint,
                    sizeof(RadientMatrix4x4) * JointCount);
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ComputeSkinningMatrices(IRadientSkinAsset* pSkin,
                                                                      RadientMatrix4x4*  pMatrices,
                                                                      Bool               TransposeMatrices,
                                                                      Bool               UpdateGlobalMatrices) override final
    {
        if (pSkin == nullptr || pMatrices == nullptr)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        const RadientSkinDesc& SkinDesc = pSkin->GetDesc();
        if (SkinDesc.pSkeleton != m_pSkeleton)
            return RADIENT_STATUS_INVALID_ARGUMENT;

        if (m_State.GlobalTransformsDirty)
        {
            if (!UpdateGlobalMatrices)
                return RADIENT_STATUS_PENDING;

            const RADIENT_STATUS Status = UpdateGlobalTransforms();
            if (Status != RADIENT_STATUS_OK && Status != RADIENT_STATUS_NO_CHANGE)
                return Status;
        }

        for (Uint32 JointIndex = 0; JointIndex < SkinDesc.JointCount; ++JointIndex)
        {
            const RadientSkinJointBindingDesc& Joint = SkinDesc.pJoints[JointIndex];

            const RadientMatrix4x4 Matrix = RadientMath::MultiplyMatrices(
                Joint.InverseBindMatrix,
                m_State.GlobalMatrices[Joint.SkeletonJointIndex]);

            pMatrices[JointIndex] = TransposeMatrices ?
                RadientMath::TransposeMatrix(Matrix) :
                Matrix;
        }
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE UpdateGlobalTransforms() override final
    {
        if (!m_State.GlobalTransformsDirty)
            return RADIENT_STATUS_NO_CHANGE;
        if (m_State.Version == std::numeric_limits<Uint64>::max())
        {
            LOG_ERROR_MESSAGE("Skeleton pose version is exhausted");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        ComputeGlobalMatrices();
        m_State.GlobalTransformsDirty = false;
        ++m_State.Version;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE CreateWriter(IRadientSkeletonPoseWriter** ppWriter) override final;

private:
    friend class RadientSkeletonPoseWriterImpl;
    friend class RadientSkeletonAnimationAssetImpl;

    const std::vector<RadientTransform>& GetLocalTransforms() const noexcept
    {
        return m_State.LocalTransforms;
    }

    RADIENT_STATUS ApplyLocalTransforms(const std::vector<RadientTransform>& LocalTransforms,
                                        Bool                                 UpdateGlobals) noexcept
    {
        VERIFY_EXPR(LocalTransforms.size() == m_State.LocalTransforms.size());
        if (m_State.Version == std::numeric_limits<Uint64>::max())
        {
            LOG_ERROR_MESSAGE("Skeleton pose version is exhausted");
            return RADIENT_STATUS_INVALID_OPERATION;
        }

        std::memcpy(m_State.LocalTransforms.data(),
                    LocalTransforms.data(),
                    sizeof(RadientTransform) * LocalTransforms.size());
        m_State.GlobalTransformsDirty = true;

        return UpdateGlobals ?
            UpdateGlobalTransforms() :
            RADIENT_STATUS_OK;
    }

    void ComputeGlobalMatrices() noexcept
    {
        VERIFY_EXPR(m_State.GlobalMatrices.size() == m_State.LocalTransforms.size());
        const RadientSkeletonDesc& SkeletonDesc     = m_pSkeleton->GetDesc();
        const Uint32* const        pEvaluationOrder = m_pSkeleton->GetEvaluationOrder();
        for (Uint32 OrderIndex = 0; OrderIndex < SkeletonDesc.JointCount; ++OrderIndex)
        {
            const Uint32           JointIndex  = pEvaluationOrder[OrderIndex];
            const RadientMatrix4x4 LocalMatrix = RadientMath::TransformToMatrix(m_State.LocalTransforms[JointIndex]);
            const Uint32           ParentIndex = SkeletonDesc.pJoints[JointIndex].ParentJointIndex;
            m_State.GlobalMatrices[JointIndex] = ParentIndex == InvalidRadientJointIndex ?
                LocalMatrix :
                RadientMath::MultiplyMatrices(LocalMatrix, m_State.GlobalMatrices[ParentIndex]);
        }
    }

private:
    const RefCntAutoPtr<RadientSkeletonAssetImpl> m_pSkeleton;
    SkeletonPoseState                             m_State;
};

class RadientSkeletonPoseWriterImpl final : public ObjectBase<IRadientSkeletonPoseWriter>
{
public:
    using TBase = ObjectBase<IRadientSkeletonPoseWriter>;

    RadientSkeletonPoseWriterImpl(IReferenceCounters*      pRefCounters,
                                  RadientSkeletonPoseImpl* pPose) :
        TBase{pRefCounters},
        m_pPose{pPose},
        m_LocalTransforms{pPose->GetLocalTransforms()}
    {
        VERIFY_EXPR(m_pPose != nullptr);
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE(IID_RadientSkeletonPoseWriter, TBase)

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE SetJointLocalTransforms(Uint32                  FirstJoint,
                                                                      Uint32                  JointCount,
                                                                      const RadientTransform* pTransforms) override final
    {
        if (!IsValidRange(FirstJoint, JointCount, static_cast<Uint32>(m_LocalTransforms.size())) ||
            (JointCount != 0 && pTransforms == nullptr))
        {
            return RADIENT_STATUS_INVALID_ARGUMENT;
        }

        if (JointCount == 0)
            return RADIENT_STATUS_NO_CHANGE;

        std::memcpy(m_LocalTransforms.data() + FirstJoint,
                    pTransforms,
                    sizeof(RadientTransform) * JointCount);
        m_HasPendingChanges = true;
        return RADIENT_STATUS_OK;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE ResetToRestPose() override final
    {
        const RadientSkeletonDesc& SkeletonDesc = m_pPose->GetSkeleton()->GetDesc();

        bool Changed = false;
        for (Uint32 JointIndex = 0; JointIndex < SkeletonDesc.JointCount; ++JointIndex)
        {
            if (m_LocalTransforms[JointIndex] != SkeletonDesc.pJoints[JointIndex].LocalRestTransform)
            {
                m_LocalTransforms[JointIndex] = SkeletonDesc.pJoints[JointIndex].LocalRestTransform;
                Changed                       = true;
            }
        }

        m_HasPendingChanges |= Changed;
        return Changed ? RADIENT_STATUS_OK : RADIENT_STATUS_NO_CHANGE;
    }

    virtual RADIENT_STATUS DILIGENT_CALL_TYPE Commit(Bool UpdateGlobalTransforms) override final
    {
        if (!m_HasPendingChanges)
            return RADIENT_STATUS_NO_CHANGE;

        const RADIENT_STATUS Status = m_pPose->ApplyLocalTransforms(m_LocalTransforms, UpdateGlobalTransforms);
        if (RADIENT_SUCCEEDED(Status))
            m_HasPendingChanges = false;
        return Status;
    }

private:
    const RefCntAutoPtr<RadientSkeletonPoseImpl> m_pPose;
    std::vector<RadientTransform>                m_LocalTransforms;
    bool                                         m_HasPendingChanges = false;
};

RADIENT_STATUS RadientSkeletonAssetImpl::CreatePose(IRadientSkeletonPose** ppPose)
{
    if (ppPose == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppPose == nullptr, "Output skeleton pose pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppPose = nullptr;

    try
    {
        RefCntAutoPtr<RadientSkeletonPoseImpl> pPose{MakeNewRCObj<RadientSkeletonPoseImpl>()(this)};
        *ppPose = pPose.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton pose: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

RADIENT_STATUS RadientSkeletonPoseImpl::CreateWriter(IRadientSkeletonPoseWriter** ppWriter)
{
    if (ppWriter == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppWriter == nullptr, "Output skeleton pose writer pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppWriter = nullptr;

    try
    {
        RefCntAutoPtr<RadientSkeletonPoseWriterImpl> pWriter{MakeNewRCObj<RadientSkeletonPoseWriterImpl>()(this)};
        *ppWriter = pWriter.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton pose writer: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

struct AnimationCurveInterval
{
    Uint32  StartKey = 0;
    Uint32  EndKey   = 0;
    Float32 Factor   = 0.f;
    Float32 Duration = 0.f;
};

AnimationCurveInterval FindAnimationCurveInterval(const RadientAnimationCurveDesc& Curve,
                                                  Float32                          Time) noexcept
{
    VERIFY_EXPR(Curve.KeyframeCount != 0);
    if (Curve.KeyframeCount == 1 || Time <= Curve.pTimes[0])
        return {};

    const Uint32 LastKey = Curve.KeyframeCount - 1;
    if (Time >= Curve.pTimes[LastKey])
        return {LastKey, LastKey, 0.f, 0.f};

    const Float32* const pEndTime = std::upper_bound(Curve.pTimes, Curve.pTimes + Curve.KeyframeCount, Time);
    const Uint32         EndKey   = static_cast<Uint32>(pEndTime - Curve.pTimes);
    const Uint32         StartKey = EndKey - 1;
    const Float32        Duration = Curve.pTimes[EndKey] - Curve.pTimes[StartKey];
    return {StartKey, EndKey, (Time - Curve.pTimes[StartKey]) / Duration, Duration};
}

RadientFloat3 EvaluateAnimationCurve(const RadientAnimationCurveDesc& Curve,
                                     Float32                          Time,
                                     const RadientFloat3&             DefaultValue) noexcept
{
    if (Curve.KeyframeCount == 0)
        return DefaultValue;

    const RadientFloat3* const   pValues         = static_cast<const RadientFloat3*>(Curve.pValues);
    const AnimationCurveInterval Interval        = FindAnimationCurveInterval(Curve, Time);
    const size_t                 StartValueIndex = Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ?
        static_cast<size_t>(Interval.StartKey) * 3u + 1u :
        Interval.StartKey;
    if (Interval.StartKey == Interval.EndKey || Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_STEP)
        return pValues[StartValueIndex];

    if (Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_LINEAR)
        return RadientMath::Lerp(pValues[Interval.StartKey], pValues[Interval.EndKey], Interval.Factor);

    return RadientMath::CubicHermite(
        pValues[static_cast<size_t>(Interval.StartKey) * 3u + 1u],
        pValues[static_cast<size_t>(Interval.StartKey) * 3u + 2u],
        pValues[static_cast<size_t>(Interval.EndKey) * 3u + 1u],
        pValues[static_cast<size_t>(Interval.EndKey) * 3u],
        Interval.Factor,
        Interval.Duration);
}

RadientQuaternion EvaluateAnimationCurve(const RadientAnimationCurveDesc& Curve,
                                         Float32                          Time,
                                         const RadientQuaternion&         DefaultValue) noexcept
{
    if (Curve.KeyframeCount == 0)
        return DefaultValue;

    const RadientQuaternion* const pValues         = static_cast<const RadientQuaternion*>(Curve.pValues);
    const AnimationCurveInterval   Interval        = FindAnimationCurveInterval(Curve, Time);
    const size_t                   StartValueIndex = Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_CUBIC_SPLINE ?
        static_cast<size_t>(Interval.StartKey) * 3u + 1u :
        Interval.StartKey;
    if (Interval.StartKey == Interval.EndKey || Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_STEP)
        return pValues[StartValueIndex];

    if (Curve.Interpolation == RADIENT_ANIMATION_INTERPOLATION_LINEAR)
        return RadientMath::Slerp(pValues[Interval.StartKey], pValues[Interval.EndKey], Interval.Factor);

    return RadientMath::CubicHermite(
        pValues[static_cast<size_t>(Interval.StartKey) * 3u + 1u],
        pValues[static_cast<size_t>(Interval.StartKey) * 3u + 2u],
        pValues[static_cast<size_t>(Interval.EndKey) * 3u + 1u],
        pValues[static_cast<size_t>(Interval.EndKey) * 3u],
        Interval.Factor,
        Interval.Duration);
}

RADIENT_STATUS RadientSkeletonAnimationAssetImpl::Evaluate(Float64               Time,
                                                           IRadientSkeletonPose* pPose,
                                                           Bool                  UpdateGlobalTransforms) const
{
    if (pPose == nullptr || !std::isfinite(Time))
        return RADIENT_STATUS_INVALID_ARGUMENT;

    const RadientSkeletonAnimationDesc& AnimationDesc = m_Data.GetDesc();
    IRadientSkeletonAsset* const        pSkeleton     = m_Data.GetSkeleton();
    if (pPose->GetSkeleton() != pSkeleton)
    {
        LOG_ERROR_MESSAGE("Skeleton animation and target pose use different skeletons");
        return RADIENT_STATUS_INVALID_ARGUMENT;
    }

    RadientSkeletonPoseImpl* const pPoseImpl = ClassPtrCast<RadientSkeletonPoseImpl>(pPose);
    if (pPoseImpl->m_State.Version == std::numeric_limits<Uint64>::max())
    {
        LOG_ERROR_MESSAGE("Skeleton pose version is exhausted");
        return RADIENT_STATUS_INVALID_OPERATION;
    }

    std::vector<RadientTransform>& LocalTransforms = pPoseImpl->m_State.LocalTransforms;
    const RadientSkeletonDesc&     SkeletonDesc    = pSkeleton->GetDesc();
    VERIFY_EXPR(LocalTransforms.size() == SkeletonDesc.JointCount);
    const Float32 ClampedTime = static_cast<Float32>(std::clamp(Time, 0.0, static_cast<Float64>(AnimationDesc.Duration)));

    const JointAnimationStart* const pJointsByStartTime = m_Data.GetJointsByStartTime();
    const auto                       FirstNotStarted    = std::upper_bound(
        pJointsByStartTime, pJointsByStartTime + SkeletonDesc.JointCount, ClampedTime,
        [](Float32 Time, const JointAnimationStart& Joint) {
            return Time < Joint.StartTime;
        });
    for (auto JointIt = FirstNotStarted;
         JointIt != pJointsByStartTime + SkeletonDesc.JointCount;
         ++JointIt)
    {
        LocalTransforms[JointIt->JointIndex] =
            SkeletonDesc.pJoints[JointIt->JointIndex].LocalRestTransform;
    }

    const AnimationTimeRange* const pTrackTimeRanges = m_Data.GetTrackTimeRanges();
    for (Uint32 TrackIndex = 0; TrackIndex < AnimationDesc.TrackCount; ++TrackIndex)
    {
        const AnimationTimeRange& TimeRange = pTrackTimeRanges[TrackIndex];
        if (ClampedTime < TimeRange.Start || ClampedTime > TimeRange.End)
            continue;

        const RadientSkeletonAnimationTrackDesc& Track     = AnimationDesc.pTracks[TrackIndex];
        RadientTransform                         Transform = SkeletonDesc.pJoints[Track.SkeletonJointIndex].LocalRestTransform;
        Transform.Position                                 = EvaluateAnimationCurve(Track.Translation, ClampedTime, Transform.Position);
        Transform.Rotation                                 = EvaluateAnimationCurve(Track.Rotation, ClampedTime, Transform.Rotation);
        Transform.Scale                                    = EvaluateAnimationCurve(Track.Scale, ClampedTime, Transform.Scale);
        LocalTransforms[Track.SkeletonJointIndex]          = Transform;
    }

    pPoseImpl->m_State.GlobalTransformsDirty = true;
    return UpdateGlobalTransforms ?
        pPoseImpl->UpdateGlobalTransforms() :
        RADIENT_STATUS_OK;
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

RADIENT_STATUS RadientAssetManagerImpl::CreateSkeletonAnimation(const RadientSkeletonAnimationDesc& AnimationDesc,
                                                                IRadientSkeletonAnimationAsset**    ppAnimation)
{
    if (ppAnimation == nullptr)
        return RADIENT_STATUS_INVALID_ARGUMENT;
    DEV_CHECK_ERR(*ppAnimation == nullptr, "Output skeleton animation pointer must be null. Overwriting a non-null output pointer may result in memory leaks.");
    *ppAnimation = nullptr;

    if (m_Stopped.load(std::memory_order_acquire))
        return RADIENT_STATUS_INVALID_OPERATION;

    try
    {
        const RADIENT_STATUS ValidationStatus = ValidateSkeletonAnimationDesc(AnimationDesc);
        if (RADIENT_FAILED(ValidationStatus))
            return ValidationStatus;

        RefCntAutoPtr<RadientSkeletonAnimationAssetImpl> pAnimation{
            MakeNewRCObj<RadientSkeletonAnimationAssetImpl>()(AnimationDesc)};
        *ppAnimation = pAnimation.Detach();
        return RADIENT_STATUS_OK;
    }
    catch (const std::exception& Error)
    {
        LOG_ERROR_MESSAGE("Failed to create a Radient skeleton animation: ", Error.what());
        return RADIENT_STATUS_FAILED;
    }
}

} // namespace Diligent
