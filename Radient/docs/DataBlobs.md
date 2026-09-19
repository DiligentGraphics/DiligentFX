# Data blobs

`IRadientDataBlob` exposes shared read access to a span of CPU bytes.
`IRadientMutableDataBlob` derives from it and adds exclusive write access and
resizing outside access scopes. Both factories use `RadientDataBlobCreateInfo`
and work without an engine or graphics device.

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
acquisitions. Data pointers and sizes remain stable throughout an acquired scope.
For mutable blobs, call `GetSize` after acquiring access to obtain the size that
matches the data pointer; a size queried beforehand may be changed by another
thread's `Resize`. Blobs created by the read-only factory keep their original size.

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

## Resizing mutable blobs

`IRadientMutableDataBlob::Resize(NewSize)` changes the number of bytes when no
reader or writer is active. It preserves the existing prefix, zero-initializes
added bytes, and discards bytes beyond the new size when shrinking. Resizing to
zero creates an empty blob whose successful access calls return null pointers.
Resizing to the current size succeeds without changing the contents when idle.

Any active scope makes `Resize` return `RADIENT_STATUS_INVALID_OPERATION`, even
when the requested size is unchanged. Unsupported sizes return
`RADIENT_STATUS_INVALID_ARGUMENT`, and allocation failure returns
`RADIENT_STATUS_FAILED`. Failure leaves the size, contents, and access state
unchanged. Resizing invokes neither callback and does not acquire an access scope.
The new size is visible through both the mutable and read-only interfaces.

```cpp
if (Blob->Resize(sizeof(float) * 6) != RADIENT_STATUS_OK)
    return;

void* Data = nullptr;
if (Blob->BeginWrite(&Data) == RADIENT_STATUS_OK)
{
    const Uint64 Size = Blob->GetSize();
    // Populate Data using Size while retaining Blob and holding write access.
    Blob->EndWrite();
}
```

Each call is thread-safe, but resizing and acquiring access are separate
operations. If multiple threads resize a blob, the caller synchronizes them when
it needs a particular size for its next access scope.

## Last-reader notification

`OnLastReaderReleased` runs for each reader-count transition from one to zero,
including for empty blobs. Registration is copied at creation and remains fixed.
Construction, writes, resizing, failed access attempts, and destruction do not
trigger it.
The callback receives an `IRadientDataBlob` and the shared `pUserData` context.
For a mutable blob, it can query `IID_RadientMutableDataBlob` to attempt recycling.

The callback runs synchronously on the thread performing the final `EndRead`
and may call blob methods. The blob stays alive throughout the callback, even
if the callback releases the caller's reference. Retaining it afterward
requires acquiring a separate strong reference. A notification does not reserve
access: another reader or writer may already be active. A mutable blob can be
resized from the callback when no access scope is active. Recycling its contents
starts with a successful `BeginWrite` on the mutable interface.

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

A reference blob can keep external storage alive without copying its bytes by
storing an owner reference in the callback context:

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
`IRadientMutableDataBlob_*` macros expose inherited reads, write access, and `Resize`.

## Encoded texture input

Set `RadientTextureLoadInfo::pDataBlob` to a non-empty blob containing the entire
encoded image. `LoadTexture()` retains the blob and starts reading during the
call without copying its bytes. End any write scope before submitting a mutable
blob; an active writer causes `RADIENT_STATUS_INVALID_OPERATION` and no texture
handle. The caller can release its blob reference after the call returns.

```cpp
RadientTextureLoadInfo LoadInfo;
LoadInfo.pDataBlob = Blob;
LoadInfo.IsSRGB = True;
RefCntAutoPtr<IRadientTextureAsset> Texture;
const RADIENT_STATUS Status = AssetManager->LoadTexture(LoadInfo, &Texture);
```

`LoadTexture()` retains read access for as long as it needs the blob's bytes.
Writes and resizing are unavailable during that time.

`OnLastReaderReleased` can run during `LoadTexture()` or later on a worker thread.
It marks the end of a read cycle, independently of GPU upload completion. A
mutable blob can then be reused after acquiring write access; other readers
still prevent writes and resizing. For REFERENCE blobs, external storage remains
alive and unchanged for the blob's entire lifetime; release its owner through
`OnDestroy`, as described above.

`pDataBlob` and the mip-data descriptor `pTextureData` are mutually exclusive.

## Texture mip data

`RadientTextureData` describes a 2D texture with one or more supplied mip levels.
Set `Width`, `Height`, and `Format`, then provide an array of `RadientTextureMipData`
through `pMipLevels` and `MipLevelCount`. Levels are consecutive, starting at mip 0.
Each descriptor supplies a blob, a `ByteOffset` within it, and a row `Stride`.
A zero stride means tightly packed rows. Multiple levels may share one blob or
reference separate blobs.

Each blob must cover its mip's byte offset plus
`(row count - 1) * effective stride + active row size`. For compressed data,
row count is `ceil(logical mip height / block height)` and active row size is
`ceil(logical mip width / block width) * bytes per block`. Block dimensions and
bytes per block are defined by the format. Storage contains complete blocks,
with at least one block in each dimension, even for a logical 1x1 mip.
Padding after the final row is optional; padding, byte offsets, and unused bytes
do not affect texture caching. An invalid range returns
`RADIENT_STATUS_INVALID_ARGUMENT`. For uncompressed data, each mip's first pixel
and each subsequent row must align to the format's component size (1, 2, or 4 bytes).
BC data has no pointer or stride alignment requirement.

```cpp
RadientTextureMipData Mip;
Mip.pDataBlob = PixelBlob; // At least 16 bytes containing four RGBA pixels.

RadientTextureData Pixels;
Pixels.Width = 2;
Pixels.Height = 2;
Pixels.Format = RADIENT_TEXTURE_FORMAT_RGBA8_UNORM;
Pixels.pMipLevels = &Mip;
Pixels.MipLevelCount = 1;
Pixels.GenerateMips = True;

RadientTextureLoadInfo LoadInfo;
LoadInfo.pTextureData = &Pixels;
RefCntAutoPtr<IRadientTextureAsset> Texture;
const RADIENT_STATUS Status = AssetManager->LoadTexture(LoadInfo, &Texture);
```

`LoadTexture()` copies the descriptor and mip array, retains their blobs, and reads
the supplied data without copying it. `GenerateMips` defaults to `True`: for
uncompressed formats, it generates only the missing tail from the last supplied
level. With `False`, it loads exactly the supplied levels. Compressed formats always
load the supplied levels without generation. An incomplete compressed chain with
`GenerateMips = True` logs a warning and continues loading; a complete chain or
`GenerateMips = False` produces no warning.

The caller can discard the descriptors and release its blob references after the
call returns. End write scopes before loading; writes and resizing remain
unavailable while Radient is reading the blobs. Read-only COPY, REFERENCE, and
mutable blobs follow the same ownership and notification rules as encoded
texture input.
