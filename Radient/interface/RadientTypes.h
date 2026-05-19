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
 */

#pragma once

/// \file
/// Defines common Radient API types.

#include "RadientMath.h"

#include "../../../DiligentCore/Primitives/interface/BasicTypes.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// clang-format off

/// Built-in Radient component classes.
DILIGENT_TYPED_ENUM(RADIENT_COMPONENT_TYPE, Uint8)
{
    RADIENT_COMPONENT_TYPE_UNKNOWN = 0,
    RADIENT_COMPONENT_TYPE_TRANSFORM,
    RADIENT_COMPONENT_TYPE_CAMERA,
    RADIENT_COMPONENT_TYPE_MESH,
    RADIENT_COMPONENT_TYPE_MESH_RENDERER,
    RADIENT_COMPONENT_TYPE_LIGHT,
    RADIENT_COMPONENT_TYPE_CUSTOM
};

// clang-format on


/// Stable handle used by Radient objects and remote protocol objects.
typedef Uint64 RadientHandle;

/// Stable entity identifier.
typedef Uint64 RadientEntityID;

/// Stable component type identifier for custom ECS components.
typedef Uint64 RadientComponentTypeID;

/// Monotonic scene or object revision.
typedef Uint64 RadientRevision;

/// Frame identifier produced by a renderer.
typedef Uint64 RadientFrameID;

static DILIGENT_CONSTEXPR RadientHandle          InvalidRadientHandle          = 0;
static DILIGENT_CONSTEXPR RadientEntityID        InvalidRadientEntityID        = 0;
static DILIGENT_CONSTEXPR RadientComponentTypeID InvalidRadientComponentTypeID = 0;
static DILIGENT_CONSTEXPR RadientFrameID         InvalidRadientFrameID         = 0;

/// Asset reference used by scenes, components, and render features.
struct RadientAssetReference
{
    /// Asset URI.
    const Char* URI DEFAULT_INITIALIZER(nullptr);

    /// Optional asset version supplied by the source.
    Uint64 Version DEFAULT_INITIALIZER(0);
};
typedef struct RadientAssetReference RadientAssetReference;

DILIGENT_END_NAMESPACE // namespace Diligent
