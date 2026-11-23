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

typedef struct IRadientAssetLocation IRadientAssetLocation;
typedef struct IRadientAssetData     IRadientAssetData;
typedef struct IRadientAssetResolver IRadientAssetResolver;

/// Asset byte-resolution request.
struct RadientAssetResolveInfo
{
    /// Asset URI to resolve. This may be a file path, package path, memory URI,
    /// network URI, or any resolver-defined identifier. File paths are literal;
    /// importers decode format-specific URI escaping before submitting file paths.
    const Char* URI DEFAULT_INITIALIZER(nullptr);

    /// Optional URI of the asset that references URI. Relative URIs are resolved
    /// against this URI by the active resolver.
    const Char* BaseURI DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientAssetResolveInfo RadientAssetResolveInfo;

/// Configuration of the built-in filesystem resolver.
struct RadientFilesystemAssetResolverCreateInfo
{
    /// Optional base directory for relative requests without BaseURI.
    /// Null or empty preserves the default filesystem working-directory behavior.
    /// A supplied directory is copied and made absolute at creation time.
    /// Absolute requests and requests with BaseURI are unaffected. This is not
    /// an access restriction; paths may refer outside this directory.
    /// On the Web, paths refer to the virtual filesystem.
    const Char* BaseDirectory DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientFilesystemAssetResolverCreateInfo RadientFilesystemAssetResolverCreateInfo;

/// Configuration of the built-in HTTP/HTTPS resolver.
/// With curl's threaded DNS resolver, cleanup may wait for an OS DNS lookup
/// to finish after either timeout expires. These values do not impose a strict
/// deadline on CheckAsset, OpenAsset, or asset-manager shutdown.
struct RadientNetworkAssetResolverCreateInfo
{
    /// Connection timeout in milliseconds, including DNS lookup and TLS handshake.
    /// On Apple platforms, this is NSURLSession's inactivity timeout: it also
    /// applies while waiting for response data and restarts when data arrives.
    /// Must be nonzero and no greater than INT32_MAX.
    Uint32 ConnectTimeoutMs DEFAULT_INITIALIZER(10000);

    /// Timeout for the network transfer, including redirects, in milliseconds.
    /// Must be nonzero and no greater than INT32_MAX.
    Uint32 RequestTimeoutMs DEFAULT_INITIALIZER(60000);

    /// Maximum number of HTTP redirects to follow, no greater than INT32_MAX.
    /// Zero disables redirects.
    Uint32 MaxRedirects DEFAULT_INITIALIZER(10);
};
typedef struct RadientNetworkAssetResolverCreateInfo RadientNetworkAssetResolverCreateInfo;

// {61A897A7-0EF4-4F5F-B769-406DA98B3B5A}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAssetLocation =
    {0x61a897a7, 0xef4, 0x4f5f, {0xb7, 0x69, 0x40, 0x6d, 0xa9, 0x8b, 0x3b, 0x5a}};

// {D2D9530F-ACF3-4CBB-81E0-66A7AC2A12BB}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAssetData =
    {0xd2d9530f, 0xacf3, 0x4cbb, {0x81, 0xe0, 0x66, 0xa7, 0xac, 0x2a, 0x12, 0xbb}};

// {2600DD0C-32BF-423A-A513-9863DEFEA40C}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientAssetResolver =
    {0x2600dd0c, 0x32bf, 0x423a, {0xa5, 0x13, 0x98, 0x63, 0xde, 0xfe, 0xa4, 0xc}};


#define DILIGENT_INTERFACE_NAME IRadientAssetLocation
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientAssetLocationInclusiveMethods \
    IObjectInclusiveMethods;                  \
    IRadientAssetLocationMethods RadientAssetLocation

// clang-format off

/// Lightweight resolved asset identity. Resolving a location does not load asset contents.
DILIGENT_BEGIN_INTERFACE(IRadientAssetLocation, IObject)
{
    /// Returns the canonical resolver-defined location used for cache keys and diagnostics.
    /// The returned string is never null.
    VIRTUAL const Char* METHOD(GetLocation)(THIS) CONST PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAssetLocation_GetLocation(This) CALL_IFACE_METHOD(RadientAssetLocation, GetLocation, This)

#endif

// clang-format on


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

    /// Returns the final resolved URI used for dependency resolution and diagnostics.
    /// The returned string is never null. It may differ from the location passed to
    /// OpenAsset() after a redirect. Use it as BaseURI for dependencies; the original
    /// location remains unchanged and identifies the request for caching.
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

/// Resolves asset identities and opens their byte data.
/// All methods may be called concurrently from multiple threads, including
/// simultaneous calls on the same resolver instance. Implementations must be
/// thread-safe.
DILIGENT_BEGIN_INTERFACE(IRadientAssetResolver, IObject)
{
    /// Checks whether an asset location can be opened without loading its contents.
    /// Returns RADIENT_STATUS_OK when the asset exists, RADIENT_STATUS_NOT_FOUND
    /// when it does not exist, or another failure status when the check fails.
    /// This operation is synchronous and never returns RADIENT_STATUS_PENDING.
    VIRTUAL RADIENT_STATUS METHOD(CheckAsset)(THIS_
                                              IRadientAssetLocation* pLocation) PURE;

    /// Resolves an asset request to its canonical location without loading asset
    /// contents. ppLocation must not be null. A successful resolution does not
    /// necessarily guarantee that the asset can subsequently be opened.
    VIRTUAL RADIENT_STATUS METHOD(ResolveAssetLocation)(THIS_
                                                        const RadientAssetResolveInfo REF ResolveInfo,
                                                        IRadientAssetLocation**           ppLocation) PURE;

    /// Opens an asset location returned by this resolver. ppData must not be null.
    /// On success, ppData reports the final resolved URI, including any redirects.
    VIRTUAL RADIENT_STATUS METHOD(OpenAsset)(THIS_
                                             IRadientAssetLocation* pLocation,
                                             IRadientAssetData**    ppData) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

#    define IRadientAssetResolver_CheckAsset(This, ...)            CALL_IFACE_METHOD(RadientAssetResolver, CheckAsset, This, __VA_ARGS__)
#    define IRadientAssetResolver_ResolveAssetLocation(This, ...)  CALL_IFACE_METHOD(RadientAssetResolver, ResolveAssetLocation, This, __VA_ARGS__)
#    define IRadientAssetResolver_OpenAsset(This, ...)             CALL_IFACE_METHOD(RadientAssetResolver, OpenAsset, This, __VA_ARGS__)

#endif

// clang-format on

#include "../../../DiligentCore/Primitives/interface/DefineGlobalFuncHelperMacros.h"

/// Creates a filesystem resolver. The engine retains a supplied resolver.
/// Literal paths are not decoded, including those supplied with BaseURI.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientFilesystemAssetResolver)(
    const RadientFilesystemAssetResolverCreateInfo REF CreateInfo,
    IRadientAssetResolver**                            ppResolver);

/// Creates a network resolver for HTTP/HTTPS URLs and relative URL references.
/// ResolveAssetLocation performs URL resolution without network I/O and removes
/// fragments. CheckAsset uses HTTP HEAD; servers that reject HEAD return FAILED.
/// OpenAsset downloads the complete resource and follows up to MaxRedirects redirects.
/// HTTP 404/410 return NOT_FOUND; transport errors, timeouts, other unsuccessful HTTP
/// responses, and incomplete transfers return FAILED. Partial data is never published.
/// TLS certificate and hostname verification use the configured native trust store.
/// Calls are synchronous and may run concurrently, using independent transfers.
/// The resolver interface has no cancellation token: stopping an asset manager does
/// not interrupt an active transfer. Timeouts limit the network transfer, but curl's
/// threaded DNS cleanup may still wait for an outstanding OS lookup before returning.
/// CheckAsset, OpenAsset, and shutdown waits can therefore exceed both timeouts.
/// Gzip and deflate responses are decoded before publication. Unsupported encodings
/// and reported decoding errors return FAILED without publishing partial data.
/// On Apple platforms, NSURLSession determines whether a compressed response is valid;
/// the curl transport's additional end-of-stream and checksum validation is not applied.
/// Apple App Transport Security and application network permissions remain in effect.
/// The browser transport is not implemented yet; Web builds currently return UNSUPPORTED.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientNetworkAssetResolver)(
    const RadientNetworkAssetResolverCreateInfo REF CreateInfo,
    IRadientAssetResolver**                         ppResolver);

/// Resolves ResolveInfo and checks whether the resulting asset location can be opened.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CheckAsset)(IRadientAssetResolver*            pResolver,
                                                    const RadientAssetResolveInfo REF ResolveInfo);

/// Resolves ResolveInfo and opens the resulting asset location.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(OpenAsset)(IRadientAssetResolver*            pResolver,
                                                   const RadientAssetResolveInfo REF ResolveInfo,
                                                   IRadientAssetData**               ppData);

#include "../../../DiligentCore/Primitives/interface/UndefGlobalFuncHelperMacros.h"

DILIGENT_END_NAMESPACE // namespace Diligent
