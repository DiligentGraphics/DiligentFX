# Data blobs

`IRadientDataBlob` owns a fixed-size allocation of CPU bytes. It is
reference-counted and can be created without an engine or graphics device.
Its public methods expose Radient types and status codes.

Set `RadientDataBlobCreateInfo::pInitialData` to initialize the blob from `Size`
readable bytes. Creation copies those bytes before returning, so the caller can
immediately modify or release the source memory. A null pointer selects zero
initialization; when `Size` is zero, the pointer is ignored. Initialization
does not acquire access or invoke the recycling callback.

```cpp
const float Position[] = {1.f, 2.f, 3.f};
RadientDataBlobCreateInfo CI;
CI.Size = sizeof(Position);
CI.pInitialData = Position;
RefCntAutoPtr<IRadientDataBlob> Blob;
if (CreateRadientDataBlob(CI, &Blob) != RADIENT_STATUS_OK)
    return;
```

## Access

`BeginRead` permits multiple simultaneous readers. `BeginWrite` permits a
single writer when there are no readers or other writers. Conflicting attempts
return `RADIENT_STATUS_INVALID_OPERATION` without waiting for active access to
finish. A successful acquisition is paired with exactly one corresponding
`EndRead` or `EndWrite`. Null output parameters return
`RADIENT_STATUS_INVALID_ARGUMENT`; other acquisition failures clear the output
pointer and leave access state unchanged.

Pointers are used only within their access scope. Clients hold a strong blob
reference until that scope ends; acquiring access does not retain the object.
Access can be handed between threads with caller-provided synchronization.
Writes completed before `EndWrite` are visible to subsequent successful access.
The allocation never moves or resizes. Empty blobs are valid and return null
pointers even on successful acquisition, so callers check the status.

```cpp
RadientDataBlobCreateInfo CI;
CI.Size = sizeof(float) * 3;
RefCntAutoPtr<IRadientDataBlob> Blob;
if (CreateRadientDataBlob(CI, &Blob) != RADIENT_STATUS_OK)
    return;

void* WriteData = nullptr;
if (Blob->BeginWrite(&WriteData) == RADIENT_STATUS_OK)
{
    const float Position[] = {1.f, 2.f, 3.f};
    std::memcpy(WriteData, Position, sizeof(Position));
    Blob->EndWrite();
}

const void* ReadData = nullptr;
if (Blob->BeginRead(&ReadData) == RADIENT_STATUS_OK)
{
    // Consume ReadData while retaining Blob.
    Blob->EndRead();
}
```

## Recycling notification

Set `RadientDataBlobCreateInfo::OnLastReaderReleased` and `pUserData` to register
a callback. Registration is copied at creation and remains fixed. The callback
runs once for each reader-count transition from one to zero, including for
empty blobs. Construction, writes, failed operations, and destruction do not
trigger it. The context is not owned by the blob and stays valid for its lifetime.

The callback runs synchronously on the thread performing the final `EndRead`,
outside the internal access lock. The blob stays alive throughout the callback,
even if the callback releases the caller's reference. A pool keeping the blob
afterward acquires a separate strong reference. Clients can queue notifications
onto their own thread and retain the blob while queued.

Notification does not reserve access. Another reader or writer may already be
active, so recycling still starts with a successful `BeginWrite`. Callbacks can
call blob methods. New read cycles can produce nested callbacks or concurrent
callbacks on different threads; callback state accounts for both. A callback
exception is caught and makes `EndRead` return `RADIENT_STATUS_FAILED`, with
the read access already released.

C callers use `Diligent_CreateRadientDataBlob` with a pointer to the creation
descriptor and the `IRadientDataBlob_*` access macros.
