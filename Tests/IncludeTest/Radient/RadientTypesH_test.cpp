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

#include "Radient/interface/RadientTypes.h"

#include <cstddef>
#include <type_traits>

namespace
{

using namespace Diligent;

static_assert(std::is_same<RadientHandle, Uint64>::value, "RadientHandle must be Uint64");
static_assert(std::is_same<RadientEntityID, Uint64>::value, "RadientEntityID must be Uint64");
static_assert(std::is_same<RadientComponentTypeID, Uint64>::value, "RadientComponentTypeID must be Uint64");
static_assert(std::is_same<RadientRevision, Uint64>::value, "RadientRevision must be Uint64");
static_assert(std::is_same<RadientFrameID, Uint64>::value, "RadientFrameID must be Uint64");

static_assert(sizeof(RADIENT_STATUS) == sizeof(Int32), "RADIENT_STATUS must be Int32-sized");
static_assert(RADIENT_STATUS_OK == 0, "Unexpected RADIENT_STATUS_OK value");
static_assert(RADIENT_STATUS_NO_CHANGE == 1, "Unexpected RADIENT_STATUS_NO_CHANGE value");
static_assert(RADIENT_STATUS_NOT_FOUND == -1, "Unexpected RADIENT_STATUS_NOT_FOUND value");
static_assert(RADIENT_STATUS_INVALID_ARGUMENT == -2, "Unexpected RADIENT_STATUS_INVALID_ARGUMENT value");
static_assert(RADIENT_STATUS_INVALID_OPERATION == -3, "Unexpected RADIENT_STATUS_INVALID_OPERATION value");
static_assert(RADIENT_SUCCEEDED(RADIENT_STATUS_OK), "RADIENT_STATUS_OK must be successful");
static_assert(RADIENT_SUCCEEDED(RADIENT_STATUS_NO_CHANGE), "RADIENT_STATUS_NO_CHANGE must be successful");
static_assert(RADIENT_FAILED(RADIENT_STATUS_NOT_FOUND), "RADIENT_STATUS_NOT_FOUND must be a failure");
static_assert(RADIENT_FAILED(RADIENT_STATUS_INVALID_ARGUMENT), "RADIENT_STATUS_INVALID_ARGUMENT must be a failure");
static_assert(RADIENT_FAILED(RADIENT_STATUS_INVALID_OPERATION), "RADIENT_STATUS_INVALID_OPERATION must be a failure");

static_assert(InvalidRadientHandle == 0, "Unexpected InvalidRadientHandle value");
static_assert(InvalidRadientEntityID == 0, "Unexpected InvalidRadientEntityID value");
static_assert(InvalidRadientComponentTypeID == 0, "Unexpected InvalidRadientComponentTypeID value");
static_assert(InvalidRadientFrameID == 0, "Unexpected InvalidRadientFrameID value");

static_assert(RADIENT_COMPONENT_TYPE_TRANSFORM == 1, "Unexpected RADIENT_COMPONENT_TYPE_TRANSFORM value");
static_assert(RADIENT_COMPONENT_TYPE_CAMERA == 2, "Unexpected RADIENT_COMPONENT_TYPE_CAMERA value");
static_assert(RADIENT_COMPONENT_TYPE_MESH == 3, "Unexpected RADIENT_COMPONENT_TYPE_MESH value");
static_assert(RADIENT_COMPONENT_TYPE_MESH_RENDERER == 4, "Unexpected RADIENT_COMPONENT_TYPE_MESH_RENDERER value");
static_assert(RADIENT_COMPONENT_TYPE_LIGHT == 5, "Unexpected RADIENT_COMPONENT_TYPE_LIGHT value");

static_assert(std::is_standard_layout<RadientAssetReference>::value, "RadientAssetReference must be a standard-layout type");
static_assert(std::is_trivially_copyable<RadientAssetReference>::value, "RadientAssetReference must be trivially copyable");

constexpr RadientAssetReference DefaultAssetReference{};
static_assert(DefaultAssetReference.URI == nullptr, "Unexpected RadientAssetReference URI default value");
static_assert(DefaultAssetReference.Version == 0, "Unexpected RadientAssetReference Version default value");

} // namespace
