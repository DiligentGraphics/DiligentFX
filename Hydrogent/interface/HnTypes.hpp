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

#include "../../../Common/interface/BasicMath.hpp"

#include "pxr/usd/usd/stage.h"

namespace Diligent
{

namespace USD
{

/// Renering mode
enum HN_RENDER_MODE
{
    /// Render solid geometry
    HN_RENDER_MODE_SOLID,

    /// Render wireframe
    HN_RENDER_MODE_MESH_EDGES,

    /// Render points
    HN_RENDER_MODE_POINTS,

    HN_RENDER_MODE_COUNT
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

} // namespace USD

} // namespace Diligent
