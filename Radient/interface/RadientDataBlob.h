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
/// Defines read-only and mutable reference-counted CPU data storage.

#include "RadientTypes.h"
#include "../../../DiligentCore/Primitives/interface/Object.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

typedef struct IRadientDataBlob        IRadientDataBlob;
typedef struct IRadientMutableDataBlob IRadientMutableDataBlob;

/// Storage selected when creating a read-only blob with CreateRadientDataBlob.
/// Mutable blobs always allocate their own storage and do not take this parameter.
DILIGENT_TYPED_ENUM(RADIENT_DATA_BLOB_STORAGE_MODE, Uint8){
    /// Allocates storage owned by the blob and copies Size bytes from pData during
    /// creation. A null pData initializes the allocation to zero. The input bytes
    /// are not retained and may be modified or released after creation returns.
    RADIENT_DATA_BLOB_STORAGE_MODE_COPY = 0,

    /// References pData directly without copying or freeing the bytes. Non-empty
    /// blobs require a non-null pData. The referenced span remains alive and
    /// unchanged until the blob is destroyed; OnDestroy can release its owner.
    RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE = 1};

/// Notification that a blob's reader count has changed from one to zero.
///
/// \param [in] pBlob     - Blob whose final reader released access. The blob stays
///                        alive throughout the callback. Retaining it afterward
///                        requires acquiring a separate strong reference.
/// \param [in] pUserData - The value supplied in RadientDataBlobCreateInfo::pUserData.
///
/// Called synchronously by EndRead on the thread releasing the final reader,
/// outside the internal access lock. The callback can acquire read access and,
/// for mutable blobs, query IRadientMutableDataBlob to resize or acquire write
/// access.
/// Notification does not reserve access. New read cycles can make callbacks
/// overlap across threads or nest on the same thread. A C++ callback exception
/// makes EndRead return RADIENT_STATUS_FAILED; read access is already released.
typedef void (*RadientDataBlobReadReleaseCallbackType)(IRadientDataBlob* pBlob, void* pUserData);

/// Notification invoked once when the blob's final strong reference is released.
///
/// \param [in] pUserData - The value supplied in RadientDataBlobCreateInfo::pUserData.
///
/// Runs synchronously on the thread destroying the blob, after all last-reader
/// callbacks have finished. The blob is being destroyed and cannot be accessed
/// or retained from this callback. The callback can release the context and the
/// owner of referenced bytes. It must not throw; a C++ exception is caught and
/// logged. Failed creation does not invoke this callback.
typedef void (*RadientDataBlobDestroyCallbackType)(void* pUserData);

/// Shared creation parameters for read-only and mutable CPU blobs.
/// Both factories copy the descriptor during the call. The source bytes are
/// copied for owning storage or referenced for REFERENCE storage as described below.
struct RadientDataBlobCreateInfo
{
    /// Initial number of bytes exposed by the blob. Read-only blobs keep this
    /// size; mutable blobs can change it with IRadientMutableDataBlob::Resize.
    /// Zero creates a valid empty blob; successful access returns a null pointer.
    /// Owning storage is limited to the implementation's addressable allocation
    /// range; REFERENCE storage requires a size representable by size_t.
    /// Unsupported sizes return RADIENT_STATUS_INVALID_ARGUMENT. Defaults to zero.
    Uint64 Size DEFAULT_INITIALIZER(0);

    /// Source bytes. For COPY storage and CreateRadientMutableDataBlob, creation
    /// copies Size bytes from this pointer, or zero-fills storage when it is null.
    /// For REFERENCE storage, the blob uses this pointer directly: it is required
    /// when Size is nonzero, and the bytes remain alive and unchanged throughout
    /// the blob's lifetime. OnDestroy and pUserData can retain and release their
    /// owner. Ignored when Size is zero. Defaults to nullptr.
    const void* pData DEFAULT_INITIALIZER(nullptr);

    /// Optional callback invoked once per reader-count transition from one to
    /// zero, including for empty blobs. Creation, writes, resizing, failed access
    /// attempts, and destruction do not invoke it. Registration remains fixed for
    /// the blob's lifetime. Defaults to nullptr, which disables notifications.
    RadientDataBlobReadReleaseCallbackType OnLastReaderReleased DEFAULT_INITIALIZER(nullptr);

    /// Opaque context passed unchanged to both callbacks. The pointer is stored;
    /// the pointed-to data is not copied or automatically destroyed. When a
    /// callback uses the context, it remains valid through callback execution.
    /// OnDestroy can release it after successful creation. On failure, cleanup
    /// remains with the caller. May be null; defaults to nullptr.
    void* pUserData DEFAULT_INITIALIZER(nullptr);

    /// Optional final-destruction callback for any storage mode. Invoked exactly
    /// once for a successfully created blob, independently of read cycles. It can
    /// release referenced storage or its owner, and clean up pUserData. It is never
    /// invoked on creation failure. It must not access the dying blob or throw.
    /// Defaults to nullptr; in REFERENCE mode, storage lifetime is then managed
    /// externally and still extends through the blob's entire lifetime.
    RadientDataBlobDestroyCallbackType OnDestroy DEFAULT_INITIALIZER(nullptr);
};
typedef struct RadientDataBlobCreateInfo RadientDataBlobCreateInfo;

// {8A49B839-9F71-45AF-B8C0-058E75274F7B}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientDataBlob =
    {0x8a49b839, 0x9f71, 0x45af, {0xb8, 0xc0, 0x05, 0x8e, 0x75, 0x27, 0x4f, 0x7b}};

// {A116D809-A621-4368-B29B-2FDB30553BEC}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RadientMutableDataBlob =
    {0xa116d809, 0xa621, 0x4368, {0xb2, 0x9b, 0x2f, 0xdb, 0x30, 0x55, 0x3b, 0xec}};

#define DILIGENT_INTERFACE_NAME IRadientDataBlob
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientDataBlobInclusiveMethods \
    IObjectInclusiveMethods;             \
    IRadientDataBlobMethods RadientDataBlob

// clang-format off

/// Reference-counted CPU bytes with shared read access.
///
/// All methods are thread-safe. Each successful BeginRead is paired with exactly
/// one EndRead. Scopes are not tied to a thread; handing a scope to another thread
/// requires caller-provided synchronization. Ending another caller's scope is
/// invalid usage. Access acquisition returns an error on conflict without waiting
/// for active scopes to finish.
///
/// A read-only interface does not imply immutable storage: a mutable blob also
/// exposes this interface, and its read scopes prevent concurrent writes and
/// resizing through IRadientMutableDataBlob. Blobs created by CreateRadientDataBlob
/// expose only read access and do not provide IRadientMutableDataBlob through
/// QueryInterface.
///
/// Data pointers are usable only within their corresponding access scope. The
/// caller holds a strong reference until it ends the scope; BeginRead does not
/// retain the blob. Read access does not permit modifying the bytes.
DILIGENT_BEGIN_INTERFACE(IRadientDataBlob, IObject)
{
    /// Returns the current data size in bytes, including during active read or
    /// write access. The size is stable within an acquired access scope. Outside
    /// a scope, a mutable blob can be resized before the caller uses this value;
    /// query the size after acquiring access when using it with a data pointer.
    /// Does not acquire access or invoke either callback.
    VIRTUAL Uint64 METHOD(GetSize)(THIS) CONST PURE;

    /// Acquires one shared read scope and returns the first byte through ppData.
    /// Additional readers are allowed. Returns RADIENT_STATUS_INVALID_OPERATION
    /// while a mutable blob has a writer or if the reader counter cannot increase.
    /// A null ppData returns RADIENT_STATUS_INVALID_ARGUMENT. Any failure clears
    /// a non-null output and leaves access state unchanged. Returns RADIENT_STATUS_OK
    /// on success, including for an empty blob, whose output pointer is null.
    VIRTUAL RADIENT_STATUS METHOD(BeginRead)(THIS_
                                             const void** ppData) PURE;

    /// Releases one read scope after its caller has finished reading the bytes.
    /// Returns RADIENT_STATUS_INVALID_OPERATION without changing state if there
    /// are no readers. Otherwise returns RADIENT_STATUS_OK, invoking
    /// OnLastReaderReleased if set and this releases the final reader. The callback
    /// completes before EndRead returns. A callback exception produces
    /// RADIENT_STATUS_FAILED with access already released. The blob remains
    /// available for further reads after a successful read cycle.
    VIRTUAL RADIENT_STATUS METHOD(EndRead)(THIS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE
#    define IRadientDataBlob_GetSize(This)        CALL_IFACE_METHOD(RadientDataBlob, GetSize,   This)
#    define IRadientDataBlob_BeginRead(This, ...) CALL_IFACE_METHOD(RadientDataBlob, BeginRead, This, __VA_ARGS__)
#    define IRadientDataBlob_EndRead(This)        CALL_IFACE_METHOD(RadientDataBlob, EndRead,   This)
#endif

// clang-format on

#define DILIGENT_INTERFACE_NAME IRadientMutableDataBlob
#include "../../../DiligentCore/Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRadientMutableDataBlobInclusiveMethods \
    IRadientDataBlobInclusiveMethods;           \
    IRadientMutableDataBlobMethods RadientMutableDataBlob

// clang-format off

/// An owning data blob with shared reads, exclusive writes, and resizable storage.
///
/// Created by CreateRadientMutableDataBlob. QueryInterface exposes both
/// IID_RadientDataBlob and IID_RadientMutableDataBlob on the same object. All
/// interface views share the same access state. Methods are thread-safe, and
/// access scopes follow the lifetime and synchronization rules of IRadientDataBlob.
DILIGENT_BEGIN_INTERFACE(IRadientMutableDataBlob, IRadientDataBlob)
{
    /// Acquires exclusive write access and returns the first byte through ppData.
    /// Returns RADIENT_STATUS_INVALID_OPERATION while any reader or writer is
    /// active, including a second BeginWrite from the current writer. A null
    /// ppData returns RADIENT_STATUS_INVALID_ARGUMENT. Any failure clears a
    /// non-null output and leaves access state unchanged. Returns RADIENT_STATUS_OK
    /// on success, including for an empty blob, whose output pointer is null.
    /// The caller retains the blob through EndWrite; BeginWrite does not retain it.
    VIRTUAL RADIENT_STATUS METHOD(BeginWrite)(THIS_
                                              void** ppData) PURE;

    /// Releases exclusive access after the caller has finished writing the bytes.
    /// Returns RADIENT_STATUS_OK when a writer was active. Completed writes are
    /// visible to subsequent successful read or write acquisitions through either
    /// interface. Otherwise returns RADIENT_STATUS_INVALID_OPERATION without
    /// changing state. Does not invoke OnLastReaderReleased or OnDestroy.
    VIRTUAL RADIENT_STATUS METHOD(EndWrite)(THIS) PURE;

    /// Changes the data size when no read or write scope is active.
    ///
    /// \param [in] NewSize - New size in bytes. Zero makes the blob empty; subsequent
    ///                       successful access returns a null data pointer.
    ///
    /// Preserves existing bytes up to the smaller of the old and new sizes, and
    /// zero-initializes added bytes. Shrinking discards bytes beyond NewSize.
    /// The allocation may move, so subsequent access scopes can return a different
    /// data pointer.
    ///
    /// Returns RADIENT_STATUS_INVALID_OPERATION if any reader or writer is active,
    /// even if NewSize equals the current size. Does not wait for active scopes to
    /// finish. Otherwise returns RADIENT_STATUS_INVALID_ARGUMENT for a size beyond
    /// the implementation's addressable allocation range, RADIENT_STATUS_FAILED
    /// if allocation fails, or RADIENT_STATUS_OK on success. Resizing to the current
    /// size succeeds without changing the contents when no scope is active.
    /// Failure leaves the size, contents, and access state unchanged. Resizing
    /// does not acquire an access scope or invoke either callback. The new size
    /// is visible through every interface view of the blob.
    VIRTUAL RADIENT_STATUS METHOD(Resize)(THIS_
                                          Uint64 NewSize) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../DiligentCore/Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE
#    define IRadientMutableDataBlob_GetSize(This)         CALL_IFACE_METHOD(RadientDataBlob, GetSize,           This)
#    define IRadientMutableDataBlob_BeginRead(This, ...)  CALL_IFACE_METHOD(RadientDataBlob, BeginRead,         This, __VA_ARGS__)
#    define IRadientMutableDataBlob_EndRead(This)         CALL_IFACE_METHOD(RadientDataBlob, EndRead,           This)
#    define IRadientMutableDataBlob_BeginWrite(This, ...) CALL_IFACE_METHOD(RadientMutableDataBlob, BeginWrite, This, __VA_ARGS__)
#    define IRadientMutableDataBlob_EndWrite(This)        CALL_IFACE_METHOD(RadientMutableDataBlob, EndWrite,   This)
#    define IRadientMutableDataBlob_Resize(This, ...)      CALL_IFACE_METHOD(RadientMutableDataBlob, Resize,     This, __VA_ARGS__)
#endif

// clang-format on

#include "../../../DiligentCore/Primitives/interface/DefineGlobalFuncHelperMacros.h"

/// Creates a read-only blob without a Radient engine or graphics device.
///
/// \param [in]  CreateInfo  - Size, source bytes, callbacks, and callback context.
/// \param [in]  StorageMode - COPY allocates and initializes owned storage;
///                           REFERENCE uses CreateInfo.pData directly without copying.
/// \param [out] ppBlob      - Receives a strong reference on success. Must be
///                           non-null and point to a null interface pointer.
///
/// Returns RADIENT_STATUS_OK on success, RADIENT_STATUS_INVALID_ARGUMENT for an
/// invalid mode, unsupported size, null output, or null data for a non-empty
/// REFERENCE blob, or RADIENT_STATUS_FAILED if allocation fails. A non-null output
/// is cleared on failure. Neither callback runs on failure, and the caller retains
/// responsibility for referenced storage and context cleanup. On success, OnDestroy
/// is registered for final destruction. The returned object has no mutable interface.
/// C callers use Diligent_CreateRadientDataBlob with a descriptor pointer; a null
/// descriptor is invalid and clears a non-null output.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientDataBlob)(const RadientDataBlobCreateInfo REF CreateInfo,
                                                               RADIENT_DATA_BLOB_STORAGE_MODE      StorageMode,
                                                               IRadientDataBlob**                  ppBlob);

/// Creates an owning mutable blob without a Radient engine or graphics device.
///
/// \param [in]  CreateInfo - Size, optional source bytes, callbacks, and callback context.
/// \param [out] ppBlob     - Receives a strong reference on success. Must be
///                          non-null and point to a null interface pointer.
///
/// Allocates Size bytes and copies pData, or zero-fills when pData is null, before
/// returning. Size zero creates an empty blob and ignores pData. The source pointer
/// is not retained. There is no reference-storage mode for mutable blobs.
/// Returns RADIENT_STATUS_OK on success, RADIENT_STATUS_INVALID_ARGUMENT for a null
/// output or unsupported allocation size, or RADIENT_STATUS_FAILED if allocation
/// fails. Failure clears a non-null output and invokes neither callback. OnDestroy
/// is registered only on successful creation. The caller handles context cleanup
/// on failure. C callers use Diligent_CreateRadientMutableDataBlob with a descriptor
/// pointer; a null descriptor is invalid and clears a non-null output.
RADIENT_STATUS DILIGENT_GLOBAL_FUNCTION(CreateRadientMutableDataBlob)(const RadientDataBlobCreateInfo REF CreateInfo,
                                                                      IRadientMutableDataBlob**           ppBlob);

#include "../../../DiligentCore/Primitives/interface/UndefGlobalFuncHelperMacros.h"

DILIGENT_END_NAMESPACE // namespace Diligent
