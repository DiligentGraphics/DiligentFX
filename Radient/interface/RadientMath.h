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

/// \file
/// Defines Radient math and geometry types.

#include "../../../DiligentCore/Primitives/interface/BasicTypes.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

/// Two-component floating-point vector.
struct RadientFloat2
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
};
typedef struct RadientFloat2 RadientFloat2;


/// Three-component floating-point vector.
struct RadientFloat3
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
    Float32 z DEFAULT_INITIALIZER(0.f);
};
typedef struct RadientFloat3 RadientFloat3;


/// Four-component floating-point vector.
struct RadientFloat4
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
    Float32 z DEFAULT_INITIALIZER(0.f);
    Float32 w DEFAULT_INITIALIZER(0.f);
};
typedef struct RadientFloat4 RadientFloat4;


/// Quaternion in x, y, z, w order.
struct RadientQuaternion
{
    Float32 x DEFAULT_INITIALIZER(0.f);
    Float32 y DEFAULT_INITIALIZER(0.f);
    Float32 z DEFAULT_INITIALIZER(0.f);
    Float32 w DEFAULT_INITIALIZER(1.f);
};
typedef struct RadientQuaternion RadientQuaternion;


/// Affine transform used by the scene graph.
struct RadientTransform
{
    RadientFloat3 Position     DEFAULT_INITIALIZER({});
    RadientQuaternion Rotation DEFAULT_INITIALIZER({});
    RadientFloat3 Scale        DEFAULT_INITIALIZER((RadientFloat3{1.f, 1.f, 1.f}));
};
typedef struct RadientTransform RadientTransform;


/// Column-major 4x4 matrix.
struct RadientMatrix4x4
{
    Float32 Data[16] DEFAULT_INITIALIZER(
        {1.f, 0.f, 0.f, 0.f,
         0.f, 1.f, 0.f, 0.f,
         0.f, 0.f, 1.f, 0.f,
         0.f, 0.f, 0.f, 1.f});
};
typedef struct RadientMatrix4x4 RadientMatrix4x4;


/// Axis-aligned bounds.
struct RadientBounds
{
    RadientFloat3 Min DEFAULT_INITIALIZER({});
    RadientFloat3 Max DEFAULT_INITIALIZER({});
};
typedef struct RadientBounds RadientBounds;


/// Two-dimensional extent.
struct RadientExtent2D
{
    Uint32 Width  DEFAULT_INITIALIZER(0);
    Uint32 Height DEFAULT_INITIALIZER(0);
};
typedef struct RadientExtent2D RadientExtent2D;

DILIGENT_END_NAMESPACE // namespace Diligent
