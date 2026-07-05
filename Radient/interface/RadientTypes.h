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
/// Defines common Radient API types.

#include "RadientMath.h"

#include "../../../DiligentCore/Primitives/interface/BasicTypes.h"

#if DILIGENT_CPP_INTERFACE
#    include <cstring>
#endif

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

/// Radient execution backend type.
DILIGENT_TYPED_ENUM(RADIENT_BACKEND_TYPE, Uint8)
{
    /// Radient runs in the current process and uses native Diligent interfaces directly.
    RADIENT_BACKEND_TYPE_LOCAL = 0,

    /// Radient commands are sent to another process or server.
    RADIENT_BACKEND_TYPE_REMOTE
};


/// Generic status returned by Radient operations.
DILIGENT_TYPED_ENUM(RADIENT_STATUS, Int32)
{
    /// Operation completed successfully.
    RADIENT_STATUS_OK = 0,

    /// The operation was valid, but did not change any state.
    RADIENT_STATUS_NO_CHANGE = 1,

    /// The operation completed successfully, but returned cached data may be out of date.
    RADIENT_STATUS_OUT_OF_DATE = 2,

    /// The operation has been accepted, but the requested data is not available yet.
    RADIENT_STATUS_PENDING = 3,

    /// Source data was processed successfully, but GPU resources were not created.
    RADIENT_STATUS_NO_GPU_DATA = 4,

    /// The requested entity, component, or resource was not found.
    RADIENT_STATUS_NOT_FOUND = -1,

    /// One or more arguments are invalid.
    RADIENT_STATUS_INVALID_ARGUMENT = -2,

    /// The operation is not valid for the current state.
    RADIENT_STATUS_INVALID_OPERATION = -3
};

// clang-format on

#define RADIENT_SUCCEEDED(Status) ((Status) >= 0)
#define RADIENT_FAILED(Status)    ((Status) < 0)

/// Stable handle used by Radient objects and remote protocol objects.
typedef Uint64 RadientHandle;

/// Stable entity identifier.
typedef Uint64 RadientEntityID;

/// Stable component type identifier for ECS components.
typedef Uint64 RadientComponentTypeID;

/// Monotonic scene or object revision.
typedef Uint64 RadientRevision;

/// Frame identifier produced by a renderer.
typedef Uint64 RadientFrameID;

static DILIGENT_CONSTEXPR RadientHandle          InvalidRadientHandle          = 0;
static DILIGENT_CONSTEXPR RadientEntityID        InvalidRadientEntityID        = 0;
static DILIGENT_CONSTEXPR RadientComponentTypeID InvalidRadientComponentTypeID = 0;
static DILIGENT_CONSTEXPR RadientFrameID         InvalidRadientFrameID         = 0;

/// Reserved component type identifiers for built-in components.
static DILIGENT_CONSTEXPR RadientComponentTypeID RADIENT_COMPONENT_TYPE_TRANSFORM         = 1;
static DILIGENT_CONSTEXPR RadientComponentTypeID RADIENT_COMPONENT_TYPE_CAMERA            = 2;
static DILIGENT_CONSTEXPR RadientComponentTypeID RADIENT_COMPONENT_TYPE_MESH              = 3;
static DILIGENT_CONSTEXPR RadientComponentTypeID RADIENT_COMPONENT_TYPE_MESH_RENDERER     = 4;
static DILIGENT_CONSTEXPR RadientComponentTypeID RADIENT_COMPONENT_TYPE_LIGHT             = 5;
static DILIGENT_CONSTEXPR RadientComponentTypeID RADIENT_COMPONENT_TYPE_MATERIAL_BINDINGS = 6;

/// Asset reference used by scenes, components, and render features.
struct RadientAssetReference
{
    /// Asset URI.
    const Char* URI DEFAULT_INITIALIZER(nullptr);

    /// Optional asset version supplied by the source.
    Uint64 Version DEFAULT_INITIALIZER(0);

#if DILIGENT_CPP_INTERFACE
    bool operator==(const RadientAssetReference& Rhs) const
    {
        return Version == Rhs.Version &&
            (URI == Rhs.URI ||
             (URI != nullptr && Rhs.URI != nullptr && std::strcmp(URI, Rhs.URI) == 0));
    }

    bool operator!=(const RadientAssetReference& Rhs) const
    {
        return !(*this == Rhs);
    }
#endif
};
typedef struct RadientAssetReference RadientAssetReference;

DILIGENT_END_NAMESPACE // namespace Diligent
