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
/// Defines Radient asset data resolution interfaces.

#include "RadientTypes.h"

#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientAssetData     IRadientAssetData;
typedef struct IRadientAssetResolver IRadientAssetResolver;

/// Asset byte-resolution request.
struct RadientAssetResolveInfo
{
    /// Asset URI to resolve. This may be a file path, package path, memory URI,
    /// network URI, or any resolver-defined identifier.
    const Char* URI DEFAULT_INITIALIZER(nullptr);

    /// Optional URI of the asset that references URI. Relative URIs are resolved
    /// against this URI by the active resolver.
    const Char* BaseURI DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientAssetResolveInfo RadientAssetResolveInfo;

// {D2D9530F-ACF3-4CBB-81E0-66A7AC2A12BB}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAssetData =
    {0xd2d9530f, 0xacf3, 0x4cbb, {0x81, 0xe0, 0x66, 0xa7, 0xac, 0x2a, 0x12, 0xbb}};

// {2600DD0C-32BF-423A-A513-9863DEFEA40C}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAssetResolver =
    {0x2600dd0c, 0x32bf, 0x423a, {0xa5, 0x13, 0x98, 0x63, 0xde, 0xfe, 0xa4, 0xc}};


#define DILIGENT_INTERFACE_NAME IRadientAssetData
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAssetDataInclusiveMethods \
    IObjectInclusiveMethods;              \
    IRadientAssetDataMethods RadientAssetData

// clang-format off

/// Resolved asset bytes and their canonical identity.
DILIGENT_BEGIN_INTERFACE(IRadientAssetData, IObject)
{
    /// Returns a read-only pointer to the resolved asset data.
    VIRTUAL const void* METHOD(GetData)(THIS) CONST PURE;

    /// Returns the size of the resolved asset data, in bytes.
    VIRTUAL size_t METHOD(GetSize)(THIS) CONST PURE;

    /// Returns the canonical resolved URI used for cache keys and diagnostics.
    /// The returned string is never null. If the resolver has no better identity,
    /// it returns the URI from the corresponding ResolveAsset() request.
    VIRTUAL const Char* METHOD(GetResolvedURI)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAssetData_GetData(This)         CALL_IFACE_METHOD(RadientAssetData, GetData, This)
#    define IRadientAssetData_GetSize(This)         CALL_IFACE_METHOD(RadientAssetData, GetSize, This)
#    define IRadientAssetData_GetResolvedURI(This)  CALL_IFACE_METHOD(RadientAssetData, GetResolvedURI, This)

#endif

// clang-format on


#define DILIGENT_INTERFACE_NAME IRadientAssetResolver
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAssetResolverInclusiveMethods \
    IObjectInclusiveMethods;                  \
    IRadientAssetResolverMethods RadientAssetResolver

// clang-format off

/// Resolves asset URIs to byte data.
DILIGENT_BEGIN_INTERFACE(IRadientAssetResolver, IObject)
{
    /// Checks whether an asset can be resolved without loading its contents.
    /// Returns RADIENT_STATUS_OK when the asset exists, RADIENT_STATUS_NOT_FOUND
    /// when it does not exist, or another failure status when the check fails.
    /// This operation is synchronous and never returns RADIENT_STATUS_PENDING.
    VIRTUAL RADIENT_STATUS METHOD(CheckAsset)(THIS_
                                              const RadientAssetResolveInfo REF ResolveInfo) PURE;

    /// Resolves a URI to an asset data object. ppData must not be null. On
    /// success, ppData is initialized and its resolved URI is never null.
    VIRTUAL RADIENT_STATUS METHOD(ResolveAsset)(THIS_
                                                const RadientAssetResolveInfo REF ResolveInfo,
                                                IRadientAssetData**               ppData) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAssetResolver_CheckAsset(This, ...)    CALL_IFACE_METHOD(RadientAssetResolver, CheckAsset, This, __VA_ARGS__)
#    define IRadientAssetResolver_ResolveAsset(This, ...)  CALL_IFACE_METHOD(RadientAssetResolver, ResolveAsset, This, __VA_ARGS__)

#endif

// clang-format on

DILIGENT_END_NAMESPACE // namespace Diligent
