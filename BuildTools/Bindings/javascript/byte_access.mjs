// Checked byte operations never expose a view into native memory. Each operation
// obtains the current heap, so memory growth cannot leave a cached view detached.

const ByteArray = Uint8Array;
const arrayPrototype = Object.getPrototypeOf(ByteArray.prototype);
const getBuffer = Object.getOwnPropertyDescriptor(arrayPrototype, "buffer").get;
const getOffset = Object.getOwnPropertyDescriptor(arrayPrototype, "byteOffset").get;
const getLength = Object.getOwnPropertyDescriptor(arrayPrototype, "byteLength").get;
const setBytes = ByteArray.prototype.set;

export function typedArray(value, Type) {
    if (!(value instanceof Type)) throw new TypeError(`Expected ${Type.name}`);
    // Normalize caller views without invoking overridden getters, methods, or
    // species constructors. Constructing the view also rejects detached buffers.
    return new Type(getBuffer.call(value), getOffset.call(value), getLength.call(value) / Type.BYTES_PER_ELEMENT);
}

export function byteArray(value) {
    return typedArray(value, ByteArray);
}

function range(offset, count, length) {
    if (!Number.isSafeInteger(offset) || offset < 0 || offset > length || count > length - offset) {
        throw new RangeError("Byte range is outside the access scope");
    }
}

export function byteLength(value) {
    if (typeof value === "bigint") {
        if (value < 0n || value > 0xffffffffn) throw new RangeError("Byte length exceeds Wasm32 memory");
        return Number(value);
    }
    if (!Number.isSafeInteger(value) || value < 0 || value > 0xffffffff) {
        throw new RangeError("Byte length exceeds Wasm32 memory");
    }
    return value;
}

export function createByteAccess(wasm, address, length, writable, release) {
    let disposed = false;
    let releaseStatus;

    function heap(offset, count) {
        if (disposed) throw new Error("Byte access has been disposed");
        range(offset, count, length);
        return wasm.HEAPU8;
    }

    const access = {
        get disposed() { return disposed; },
        get byteLength() { heap(0, 0); return length; },
        readByte(index) { return heap(index, 1)[address + index]; },
        copyTo(destination, sourceOffset = 0) {
            const target = byteArray(destination);
            const source = heap(sourceOffset, target.byteLength);
            // Never pass a native view to a caller-provided set() implementation.
            setBytes.call(target, new ByteArray(getBuffer.call(source), address + sourceOffset, target.byteLength));
        },
        toUint8Array() {
            const source = heap(0, length);
            const copy = new ByteArray(length);
            setBytes.call(copy, new ByteArray(getBuffer.call(source), address, length));
            return copy;
        },
        dispose() {
            if (!disposed) {
                // Release consumes the scope even if the native API reports an
                // error. Mark it closed first, including during reentrant calls.
                disposed = true;
                releaseStatus = release();
            }
            return releaseStatus;
        },
    };
    if (writable) {
        access.writeByte = (index, value) => {
            if (!Number.isInteger(value) || value < 0 || value > 255) throw new RangeError("Expected a byte value");
            heap(index, 1)[address + index] = value;
        };
        access.copyFrom = (source, destinationOffset = 0) => {
            const bytes = byteArray(source);
            setBytes.call(heap(destinationOffset, bytes.byteLength), bytes, address + destinationOffset);
        };
    }
    return Object.freeze(access);
}
