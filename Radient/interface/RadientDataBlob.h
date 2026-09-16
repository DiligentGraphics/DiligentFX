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
/// Defines reference-counted CPU data storage with shared read and exclusive write access.

#include "RadientTypes.h"
#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientDataBlob IRadientDataBlob;

/// Notification that a blob's reader count has changed from one to zero.
///
/// \param [in] pBlob     - Blob whose final reader released access. The blob stays
///                        alive throughout the callback. Retaining it after the
///                        callback requires acquiring a separate strong reference.
/// \param [in] pUserData - The value supplied in RadientDataBlobCreateInfo::pUserData.
///
/// Called synchronously by EndRead on the thread releasing the final reader,
/// outside the internal access lock. The callback can call blob methods and
/// acquire its own read or write access. This notification does not reserve
/// write access: another thread may already have acquired access when it runs.
/// New read cycles can cause callbacks to overlap across threads or nest on the
/// same thread, so callback code and its user data account for this concurrency.
/// If a C++ callback throws, EndRead catches the exception and returns
/// RADIENT_STATUS_FAILED; the read access has already been released.
typedef void (*RadientDataBlobReadReleaseCallbackType)(IRadientDataBlob* pBlob, void* pUserData);

/// Creation parameters for a fixed-size CPU data blob.
/// The size, callback, and user-data pointer are copied during creation.
/// Optional initial bytes are copied before CreateRadientDataBlob returns.
struct RadientDataBlobCreateInfo
{
    /// Storage size in bytes. The allocation keeps this size for the blob's
    /// lifetime. Initial contents come from pInitialData when provided, otherwise
    /// the allocation is zero-filled. Zero creates a valid empty blob, whose
    /// successful BeginRead and BeginWrite calls return null data pointers.
    /// Sizes beyond the implementation's addressable allocation range are
    /// rejected with RADIENT_STATUS_INVALID_ARGUMENT. The default is zero.
    Uint64 Size DEFAULT_INITIALIZER(0);

    /// Optional pointer to at least Size readable bytes used to initialize the
    /// allocation. CreateRadientDataBlob copies these bytes before returning;
    /// the pointer is not retained, and the caller can then modify or release
    /// the source memory. Initialization does not acquire an access scope or
    /// invoke OnLastReaderReleased. Ignored when Size is zero. The default is
    /// nullptr, which initializes the allocation to zero.
    const void* pInitialData DEFAULT_INITIALIZER(nullptr);

    /// Optional callback invoked once for each reader-count transition from one
    /// to zero. Creation, EndWrite, failed access attempts, and destruction do
    /// not invoke it. Registration remains fixed for the blob's lifetime.
    /// The default is nullptr, which disables notifications.
    RadientDataBlobReadReleaseCallbackType OnLastReaderReleased DEFAULT_INITIALIZER(nullptr);

    /// Opaque callback context, passed unchanged to OnLastReaderReleased.
    /// The blob stores the pointer without copying or owning the pointed-to data.
    /// When a callback is registered, this context remains valid throughout the
    /// blob's lifetime, including callback execution. May be null; defaults to
    /// nullptr. Ignored when OnLastReaderReleased is null.
    void* pUserData DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientDataBlobCreateInfo RadientDataBlobCreateInfo;

// {8A49B839-9F71-45AF-B8C0-058E75274F7B}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientDataBlob =
    {0x8a49b839, 0x9f71, 0x45af, {0xb8, 0xc0, 0x05, 0x8e, 0x75, 0x27, 0x4f, 0x7b}};

#define DILIGENT_INTERFACE_NAME IRadientDataBlob
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientDataBlobInclusiveMethods \
    IObjectInclusiveMethods;             \
    IRadientDataBlobMethods RadientDataBlob

// clang-format off

/// Reference-counted CPU bytes with concurrent readers or one exclusive writer.
///
/// All methods are thread-safe. Access acquisition returns an error on conflict
/// without waiting for active readers or a writer to finish. Successful Begin
/// calls are paired with exactly one corresponding End call. Access scopes are
/// not tied to a thread; a caller can hand a scope to another thread using its
/// own synchronization. Ending another caller's scope is invalid usage.
///
/// Data pointers are usable only within their corresponding access scope. A
/// caller retains a strong object reference until it finishes accessing the
/// bytes and ends the scope; BeginRead and BeginWrite do not retain the blob.
/// Read access does not permit modifying the bytes. Writes completed before
/// EndWrite are visible to subsequent successful BeginRead and BeginWrite calls.
/// Access methods synchronize scopes; they do not make stale pointer use safe.
DILIGENT_BEGIN_INTERFACE(IRadientDataBlob, IObject)
{
    /// Returns the immutable allocation size in bytes, including during active
    /// read or write access. Does not acquire access or invoke the callback.
    VIRTUAL Uint64 METHOD(GetSize)(THIS) CONST PURE;

    /// Acquires one shared read scope and returns the first byte through ppData.
    /// Additional readers are allowed. Returns RADIENT_STATUS_INVALID_OPERATION
    /// while a writer is active or if the reader counter cannot be incremented.
    /// A null ppData returns RADIENT_STATUS_INVALID_ARGUMENT. On any failure,
    /// a non-null ppData is set to nullptr and access state is unchanged.
    /// Returns RADIENT_STATUS_OK on success, including for an empty blob.
    VIRTUAL RADIENT_STATUS METHOD(BeginRead)(THIS_
                                             const void** ppData) PURE;

    /// Releases one read scope after the caller has finished reading its bytes.
    /// Returns RADIENT_STATUS_INVALID_OPERATION without changing state when
    /// there are no readers. Otherwise returns RADIENT_STATUS_OK, invoking
    /// OnLastReaderReleased if this releases the final reader. The callback
    /// completes before EndRead returns. A callback exception produces
    /// RADIENT_STATUS_FAILED with the read scope already released.
    VIRTUAL RADIENT_STATUS METHOD(EndRead)(THIS) PURE;

    /// Acquires exclusive write access and returns the first byte through ppData.
    /// Returns RADIENT_STATUS_INVALID_OPERATION while any reader or writer is
    /// active, including a second BeginWrite from the current writer. A null
    /// ppData returns RADIENT_STATUS_INVALID_ARGUMENT. On any failure, a non-null
    /// ppData is set to nullptr and access state is unchanged. Returns
    /// RADIENT_STATUS_OK on success, including for an empty blob.
    VIRTUAL RADIENT_STATUS METHOD(BeginWrite)(THIS_
                                              void** ppData) PURE;

    /// Releases exclusive access after the caller has finished writing its bytes.
    /// Returns RADIENT_STATUS_OK when a writer was active, making those writes
    /// visible to subsequent access scopes. Otherwise returns
    /// RADIENT_STATUS_INVALID_OPERATION without changing state. Does not invoke
    /// OnLastReaderReleased.
    VIRTUAL RADIENT_STATUS METHOD(EndWrite)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE
#    define IRadientDataBlob_GetSize(This)          CALL_IFACE_METHOD(RadientDataBlob, GetSize,    This)
#    define IRadientDataBlob_BeginRead(This, ...)   CALL_IFACE_METHOD(RadientDataBlob, BeginRead,  This, __VA_ARGS__)
#    define IRadientDataBlob_EndRead(This)          CALL_IFACE_METHOD(RadientDataBlob, EndRead,    This)
#    define IRadientDataBlob_BeginWrite(This, ...)  CALL_IFACE_METHOD(RadientDataBlob, BeginWrite, This, __VA_ARGS__)
#    define IRadientDataBlob_EndWrite(This)         CALL_IFACE_METHOD(RadientDataBlob, EndWrite,   This)
#endif

// clang-format on

#include "../../../DiligentCore/Primitives/interface/DefineGlobalFuncHelperMacros.h"

/// Creates a CPU data blob without requiring a Radient engine or graphics device.
///
/// \param [in]  CreateInfo - Size, optional initial data, and last-reader notification.
/// \param [out] ppBlob     - Receives an owning reference on success. Must be
///                          non-null and point to a null interface pointer.
///
/// Returns RADIENT_STATUS_OK on success, RADIENT_STATUS_INVALID_ARGUMENT for a
/// null output pointer or an unsupported allocation size, or RADIENT_STATUS_FAILED
/// if allocation fails. On failure, a non-null ppBlob is set to nullptr.
/// C callers use Diligent_CreateRadientDataBlob and pass a CreateInfo pointer;
/// a null CreateInfo pointer returns RADIENT_STATUS_INVALID_ARGUMENT.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientDataBlob)(const RadientDataBlobCreateInfo REF CreateInfo,
                                                               IRadientDataBlob**                  ppBlob);

#include "../../../DiligentCore/Primitives/interface/UndefGlobalFuncHelperMacros.h"

DILIGENT_END_NAMESPACE // namespace Diligent
