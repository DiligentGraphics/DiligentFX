# Data blobs

`IRadientDataBlob` exposes shared read access to a fixed-size span of CPU bytes.
`IRadientMutableDataBlob` derives from it and adds exclusive write access. Both
factories use `RadientDataBlobCreateInfo` and work without an engine or graphics
device.

| Storage | Factory | Result |
| --- | --- | --- |
| Owning read-only | `CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &Blob)` | `IRadientDataBlob` |
| Non-owning read-only | `CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &Blob)` | `IRadientDataBlob` |
| Owning mutable | `CreateRadientMutableDataBlob(CI, &Blob)` | `IRadientMutableDataBlob` |

Mutability follows the interface returned by the factory. Both read-only modes
return objects that do not expose `IRadientMutableDataBlob` through
`QueryInterface`. Mutable blobs expose both interfaces on the same object, and
all interface views share the same reader/writer state. Passing a mutable blob
through `IRadientDataBlob` provides read access without making its storage immutable.

## Initialization and ownership

The shared descriptor contains `Size`, `pData`, `OnLastReaderReleased`, `pUserData`,
and `OnDestroy`. Creation copies the descriptor. The storage mode is a separate
argument to the read-only factory; mutable creation always owns its bytes.

COPY and mutable creation allocate `Size` bytes and copy `pData` before returning.
The input bytes can then be modified or released. A null pointer selects zero
initialization. REFERENCE creation points directly to `pData` without copying or
freeing those bytes. For a non-empty reference blob, the pointer is non-null and
its storage remains alive and unchanged for the blob's entire lifetime.

Zero-sized blobs are valid in every mode, ignore `pData`, and return a null
pointer even when access succeeds. Initialization does not acquire access or
invoke either callback. Invalid arguments or allocation failure clear a non-null
output and invoke neither callback; the caller handles storage and context
cleanup on failure.

```cpp
const float Position[] = {1.f, 2.f, 3.f};
RadientDataBlobCreateInfo CI;
CI.Size = sizeof(Position);
CI.pData = Position;
RefCntAutoPtr<IRadientDataBlob> Blob;
if (CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_COPY, &Blob) != RADIENT_STATUS_OK)
    return;
```

## Access

`BeginRead` permits multiple simultaneous readers. `BeginWrite`, available on
`IRadientMutableDataBlob`, permits one writer when no reader or writer is active.
Conflicting attempts return `RADIENT_STATUS_INVALID_OPERATION` without waiting
for active scopes to finish. A successful acquisition is paired with exactly one
matching `EndRead` or `EndWrite`. Null output parameters return
`RADIENT_STATUS_INVALID_ARGUMENT`; other acquisition failures clear the output
pointer and leave access state unchanged.

Pointers are usable only within their access scope. Clients hold a strong blob
reference until that scope ends; acquiring access does not retain the object.
Scopes can be handed between threads with caller-provided synchronization. Writes
completed before `EndWrite` are visible to subsequent successful read and write
acquisitions. Data pointers and sizes remain stable throughout the blob's lifetime.

```cpp
RadientDataBlobCreateInfo CI;
CI.Size = sizeof(float) * 3;
RefCntAutoPtr<IRadientMutableDataBlob> Blob;
if (CreateRadientMutableDataBlob(CI, &Blob) != RADIENT_STATUS_OK)
    return;

void* WriteData = nullptr;
if (Blob->BeginWrite(&WriteData) == RADIENT_STATUS_OK)
{
    const float Position[] = {1.f, 2.f, 3.f};
    std::memcpy(WriteData, Position, sizeof(Position));
    Blob->EndWrite();
}

RefCntAutoPtr<IRadientDataBlob> Readable = Blob;
const void* ReadData = nullptr;
if (Readable->BeginRead(&ReadData) == RADIENT_STATUS_OK)
{
    // Consume ReadData while retaining Readable.
    Readable->EndRead();
}
```

## Last-reader notification

`OnLastReaderReleased` runs for each reader-count transition from one to zero,
including for empty blobs. Registration is copied at creation and remains fixed.
Construction, writes, failed access attempts, and destruction do not trigger it.
The callback receives an `IRadientDataBlob` and the shared `pUserData` context.
For a mutable blob, it can query `IID_RadientMutableDataBlob` to attempt recycling.

The callback runs synchronously on the thread performing the final `EndRead`,
outside the internal access lock. The blob stays alive throughout the callback,
even if the callback releases the caller's reference. Retaining it afterward
requires acquiring a separate strong reference. A notification does not reserve
access: another reader or writer may already be active. Recycling starts with a
successful `BeginWrite` on the mutable interface.

New read cycles can produce nested callbacks or concurrent callbacks on different
threads; callback state accounts for both. A callback exception is caught and
makes `EndRead` return `RADIENT_STATUS_FAILED`, with read access already released.
Finishing a read cycle leaves the blob available for later reads.

## Destruction and referenced storage

`OnDestroy` runs once when the final strong reference is released, after all
last-reader callbacks have finished. It applies to every storage mode and receives
`pUserData`. It can release the context and the owner of referenced bytes. It does
not receive a blob pointer because the blob is being destroyed and cannot be
accessed or retained. It runs synchronously on the destroying thread and must not
throw; C++ exceptions are caught and logged.

A reference blob can keep external storage alive by storing an owner reference
in the callback context. The same pattern works for a parsed document that owns
encoded images, avoiding a copy of those image bytes:

```cpp
auto Bytes = std::make_shared<const std::vector<Uint8>>(std::initializer_list<Uint8>{1, 2, 3});
using Owner = decltype(Bytes);
auto Context = std::make_unique<Owner>(Bytes);

RadientDataBlobCreateInfo CI;
CI.Size = Bytes->size();
CI.pData = Bytes->data();
CI.pUserData = Context.get();
CI.OnDestroy = [](void* pUserData) { delete static_cast<Owner*>(pUserData); };

RefCntAutoPtr<IRadientDataBlob> Blob;
const RADIENT_STATUS Status =
    CreateRadientDataBlob(CI, RADIENT_DATA_BLOB_STORAGE_MODE_REFERENCE, &Blob);
if (Status == RADIENT_STATUS_OK)
    Context.release(); // OnDestroy now handles this context's cleanup.
// On failure, Context still owns and releases the retained owner reference.
```

Both callbacks share `pUserData`; its contents remain valid through callback
execution. Releasing referenced storage belongs in `OnDestroy`, since a last-reader
notification ends only one read cycle. Without a destruction callback, the caller
manages the referenced storage's lifetime externally.

## C interface

C callers use `Diligent_CreateRadientDataBlob` or
`Diligent_CreateRadientMutableDataBlob` with a descriptor pointer. The read-only
factory also takes the storage mode. A null descriptor returns
`RADIENT_STATUS_INVALID_ARGUMENT` and clears a non-null output. Use
`IRadientDataBlob_GetSize`, `IRadientDataBlob_BeginRead`, and
`IRadientDataBlob_EndRead` for the base interface. The corresponding
`IRadientMutableDataBlob_*` macros expose both inherited reads and write access.
