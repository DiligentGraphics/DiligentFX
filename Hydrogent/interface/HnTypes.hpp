/*
 *  Copyright 2023-2025 Diligent Graphics LLC
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

#include "../../../Common/interface/BasicMath.hpp"

#include "pxr/usd/usd/stage.h"

namespace Diligent
{

namespace USD
{

/// Geometry rendering mode
enum HN_GEOMETRY_MODE : Uint8
{
    /// Render solid geometry
    HN_GEOMETRY_MODE_SOLID,

    /// Render wireframe
    HN_GEOMETRY_MODE_MESH_EDGES,

    /// Render points
    HN_GEOMETRY_MODE_POINTS,

    HN_GEOMETRY_MODE_COUNT
};

/// Material texture binding mode.
enum HN_MATERIAL_TEXTURES_BINDING_MODE : Uint8
{
    /// Legacy mode: each material uses its own SRB.
    HN_MATERIAL_TEXTURES_BINDING_MODE_LEGACY,

    /// Texture atlas mode: material textures are stored in texture atlases,
    /// which are generally referenced by a single SRB (with the exception
    /// of materials that use textures not fitting into the atlas).
    HN_MATERIAL_TEXTURES_BINDING_MODE_ATLAS,

    /// Dynamic (aka bindless) mode: each texture is a separate resource,
    /// and all textures are referenced by a single SRB.
    ///
    /// \note   This mode requires bindless support from the device.
    HN_MATERIAL_TEXTURES_BINDING_MODE_DYNAMIC
};

/// Renderer view mode.
enum HN_VIEW_MODE : Uint8
{
    /// Render shaded geometry.
    HN_VIEW_MODE_SHADED,

    /// Display texture coordinate set 0.
    HN_VIEW_MODE_TEXCOORD0,

    /// Display texture coordinate set 1.
    HN_VIEW_MODE_TEXCOORD1,

    /// Display base color texture.
    HN_VIEW_MODE_BASE_COLOR,

    /// Display transparency.
    HN_VIEW_MODE_TRANSPARENCY,

    /// Display occlusion.
    HN_VIEW_MODE_OCCLUSION,

    /// Display emissive texture.
    HN_VIEW_MODE_EMISSIVE,

    /// Display metallic.
    HN_VIEW_MODE_METALLIC,

    /// Display roughness.
    HN_VIEW_MODE_ROUGHNESS,

    /// Display diffuse color.
    HN_VIEW_MODE_DIFFUSE_COLOR,

    /// Display specular color.
    HN_VIEW_MODE_SPECULAR_COLOR,

    /// Display normal reflectance.
    HN_VIEW_MODE_REFLECTANCE90,

    /// Display mesh normals.
    HN_VIEW_MODE_MESH_NORMAL,

    /// Display shading normals.
    HN_VIEW_MODE_SHADING_NORMAL,

    /// Display motion vectors.
    HN_VIEW_MODE_MOTION_VECTORS,

    /// Display (Normal, View) product.
    HN_VIEW_MODE_NDOTV,

    /// Display punctual lighting.
    HN_VIEW_MODE_PUNCTUAL_LIGHTING,

    /// Display diffuse IBL.
    HN_VIEW_MODE_DIFFUSE_IBL,

    /// Display specular IBL.
    HN_VIEW_MODE_SPECULAR_IBL,

    /// Render the scene with white base color.
    HN_VIEW_MODE_WHITE_BASE_COLOR,

    /// Display clear coat.
    HN_VIEW_MODE_CLEARCOAT,

    /// Display clear coat factor.
    HN_VIEW_MODE_CLEARCOAT_FACTOR,

    /// Display clear coat roughness.
    HN_VIEW_MODE_CLEARCOAT_ROUGHNESS,

    /// Display clear coat normal.
    HN_VIEW_MODE_CLEARCOAT_NORMAL,

    /// Display scene depth.
    HN_VIEW_MODE_SCENE_DEPTH,

    /// Display edge map.
    HN_VIEW_MODE_EDGE_MAP,

    /// Display mesh ID.
    HN_VIEW_MODE_MESH_ID,

    /// The total number of view modes.
    HN_VIEW_MODE_COUNT
};

} // namespace USD

} // namespace Diligent
