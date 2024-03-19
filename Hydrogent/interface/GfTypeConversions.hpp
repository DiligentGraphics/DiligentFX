/*
 *  Copyright 2023-2024 Diligent Graphics LLC
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

#include "../../../DiligentCore/Common/interface/BasicMath.hpp"
#include "../../../DiligentCore/Common/interface/AdvancedMath.hpp"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec4d.h"
#include "pxr/base/gf/range3f.h"
#include "pxr/base/gf/range3d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/matrix4d.h"

namespace Diligent
{

namespace USD
{

template <typename T>
pxr::GfVec2f ToGfVec2f(const Vector2<T>& v)
{
    return pxr::GfVec2f{
        static_cast<float>(v.x),
        static_cast<float>(v.y),
    };
}

template <typename T>
pxr::GfVec3f ToGfVec3f(const Vector3<T>& v)
{
    return pxr::GfVec3f{
        static_cast<float>(v.x),
        static_cast<float>(v.y),
        static_cast<float>(v.z),
    };
}

template <typename T>
pxr::GfVec4f ToGfVec4f(const Vector4<T>& v)
{
    return pxr::GfVec4f{
        static_cast<float>(v.x),
        static_cast<float>(v.y),
        static_cast<float>(v.z),
        static_cast<float>(v.w),
    };
}

template <typename T>
Vector2<T> ToVector2(const pxr::GfVec2f& v)
{
    return Vector2<T>{
        static_cast<T>(v[0]),
        static_cast<T>(v[1]),
    };
}

template <typename T>
Vector2<T> ToVector2(const pxr::GfVec2d& v)
{
    return Vector2<T>{
        static_cast<T>(v[0]),
        static_cast<T>(v[1]),
    };
}

template <typename T>
Vector3<T> ToVector3(const pxr::GfVec3f& v)
{
    return Vector3<T>{
        static_cast<T>(v[0]),
        static_cast<T>(v[1]),
        static_cast<T>(v[2]),
    };
}

template <typename T>
Vector3<T> ToVector3(const pxr::GfVec3d& v)
{
    return Vector3<T>{
        static_cast<T>(v[0]),
        static_cast<T>(v[1]),
        static_cast<T>(v[2]),
    };
}

template <typename T>
Vector4<T> ToVector4(const pxr::GfVec4f& v)
{
    return Vector4<T>{
        static_cast<T>(v[0]),
        static_cast<T>(v[1]),
        static_cast<T>(v[2]),
        static_cast<T>(v[3]),
    };
}

template <typename T>
Vector4<T> ToVector4(const pxr::GfVec4d& v)
{
    return Vector4<T>{
        static_cast<T>(v[0]),
        static_cast<T>(v[1]),
        static_cast<T>(v[2]),
        static_cast<T>(v[3]),
    };
}

template <typename T>
pxr::GfMatrix4f ToGfMatrix4f(const Matrix4x4<T>& m)
{
    // clang-format off
	return pxr::GfMatrix4f{
		static_cast<float>(m._11), static_cast<float>(m._12), static_cast<float>(m._13), static_cast<float>(m._14),
		static_cast<float>(m._21), static_cast<float>(m._22), static_cast<float>(m._23), static_cast<float>(m._24),
		static_cast<float>(m._31), static_cast<float>(m._32), static_cast<float>(m._33), static_cast<float>(m._34),
		static_cast<float>(m._41), static_cast<float>(m._42), static_cast<float>(m._43), static_cast<float>(m._44),
	};
    // clang-format on
}

template <typename T>
pxr::GfMatrix4d ToGfMatrix4d(const Matrix4x4<T>& m)
{
    // clang-format off
	return pxr::GfMatrix4d{
		static_cast<double>(m._11), static_cast<double>(m._12), static_cast<double>(m._13), static_cast<double>(m._14),
		static_cast<double>(m._21), static_cast<double>(m._22), static_cast<double>(m._23), static_cast<double>(m._24),
		static_cast<double>(m._31), static_cast<double>(m._32), static_cast<double>(m._33), static_cast<double>(m._34),
		static_cast<double>(m._41), static_cast<double>(m._42), static_cast<double>(m._43), static_cast<double>(m._44),
	};
    // clang-format on
}

template <typename T>
Matrix4x4<T> ToMatrix4x4(const pxr::GfMatrix4f& m)
{
    // clang-format off
	return Matrix4x4<T>{
		static_cast<T>(m[0][0]), static_cast<T>(m[0][1]), static_cast<T>(m[0][2]), static_cast<T>(m[0][3]),
		static_cast<T>(m[1][0]), static_cast<T>(m[1][1]), static_cast<T>(m[1][2]), static_cast<T>(m[1][3]),
		static_cast<T>(m[2][0]), static_cast<T>(m[2][1]), static_cast<T>(m[2][2]), static_cast<T>(m[2][3]),
		static_cast<T>(m[3][0]), static_cast<T>(m[3][1]), static_cast<T>(m[3][2]), static_cast<T>(m[3][3]),
	};
    // clang-format on
}

template <typename T>
Matrix4x4<T> ToMatrix4x4(const pxr::GfMatrix4d& m)
{
    // clang-format off
	return Matrix4x4<T>{
		static_cast<T>(m[0][0]), static_cast<T>(m[0][1]), static_cast<T>(m[0][2]), static_cast<T>(m[0][3]),
		static_cast<T>(m[1][0]), static_cast<T>(m[1][1]), static_cast<T>(m[1][2]), static_cast<T>(m[1][3]),
		static_cast<T>(m[2][0]), static_cast<T>(m[2][1]), static_cast<T>(m[2][2]), static_cast<T>(m[2][3]),
		static_cast<T>(m[3][0]), static_cast<T>(m[3][1]), static_cast<T>(m[3][2]), static_cast<T>(m[3][3]),
	};
    // clang-format on
}

inline float2 ToFloat2(const pxr::GfVec2f& v)
{
    return ToVector2<float>(v);
}

inline float2 ToFloat2(const pxr::GfVec2d& v)
{
    return ToVector2<float>(v);
}

inline float3 ToFloat3(const pxr::GfVec3f& v)
{
    return ToVector3<float>(v);
}

inline float3 ToFloat3(const pxr::GfVec3d& v)
{
    return ToVector3<float>(v);
}

inline float4 ToFloat4(const pxr::GfVec4f& v)
{
    return ToVector4<float>(v);
}

inline float4 ToFloat4(const pxr::GfVec4d& v)
{
    return ToVector4<float>(v);
}

inline float4x4 ToFloat4x4(const pxr::GfMatrix4d& m)
{
    return ToMatrix4x4<float>(m);
}

inline float4x4 ToFloat4x4(const pxr::GfMatrix4f& m)
{
    return ToMatrix4x4<float>(m);
}

inline BoundBox ToBoundBox(const pxr::GfRange3d& r)
{
    return BoundBox{
        ToFloat3(r.GetMin()),
        ToFloat3(r.GetMax()),
    };
}

inline BoundBox ToBoundBox(const pxr::GfRange3f& r)
{
    return BoundBox{
        ToFloat3(r.GetMin()),
        ToFloat3(r.GetMax()),
    };
}

} // namespace USD

} // namespace Diligent
