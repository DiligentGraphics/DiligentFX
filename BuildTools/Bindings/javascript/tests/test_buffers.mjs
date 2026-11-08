import assert from "node:assert/strict";
import {test} from "node:test";
import {pathToFileURL} from "node:url";

const {default: loadBindings, MARSHAL_STATUS} = await import(pathToFileURL(process.argv[2]));
const api = await loadBindings();
const {MARSHAL_STATUS_OK: OK, MARSHAL_STATUS_FAILED: FAILED, MARSHAL_STATUS_PENDING: WRITE_OK} = MARSHAL_STATUS;

function buffer(t, descriptor = {ByteCount: 4n, pBytes: new Uint8Array([1, 2, 3, 4])}) {
    const result = api.CreateMarshalBytes(descriptor);
    assert.equal(result.status, OK);
    assert.ok(result.value);
    t.after(() => result.value.dispose());
    return result.value;
}

function read(t, object) {
    const result = object.MapRead();
    assert.equal(result.status, OK);
    assert.ok(result.value);
    t.after(() => result.value.dispose());
    return result.value;
}

function write(t, object) {
    const result = object.MapWrite();
    assert.equal(result.status, WRITE_OK);
    assert.ok(result.value);
    t.after(() => result.value.dispose());
    return result.value;
}

test("input byte spans respect subarray offsets, lengths, and native parameter order", () => {
    const storage = new Uint8Array([99, 17, 128, 254, 88]);
    const bytes = storage.subarray(1, 4);
    assert.equal(api.CheckBytes(3, bytes), true);
    const expected = (17 * 31 + 128) * 31 + 254;
    assert.equal(api.ChecksumBytes(bytes, 3n, false), expected);
    assert.equal(api.ChecksumBytes(bytes, 2n, false), 17 * 31 + 128);
    assert.equal(api.ChecksumBytes(bytes, 3n, true), (~expected) >>> 0);
    assert.deepEqual(storage, new Uint8Array([99, 17, 128, 254, 88]));
});

test("input byte spans reject invalid types and lengths before native access", () => {
    const bytes = new Uint8Array([1, 2]);
    for (const invalid of [null, undefined, [1, 2], new Uint16Array(2), bytes.buffer]) {
        assert.throws(() => api.ChecksumBytes(invalid, 2n, false), TypeError);
    }
    for (const size of [-1n, 3n, 0x100000000n]) {
        assert.throws(() => api.ChecksumBytes(bytes, size, false), RangeError);
    }
    assert.throws(() => api.ChecksumBytes(bytes, 2, false), TypeError);
    // The input allocation has already happened when this later argument fails.
    assert.throws(() => api.ChecksumBytes(bytes, 2n, "false"), TypeError);
    assert.equal(api.ChecksumBytes(bytes, 2n, false), 33);
    const detached = new Uint8Array(4);
    structuredClone(detached.buffer, {transfer: [detached.buffer]});
    assert.throws(() => api.ChecksumBytes(detached, 0n, false), TypeError);
});

test("descriptor byte input uses native size defaults and copies only the selected span", t => {
    const data = new Uint8Array([99, 17, 128, 254, 88]);
    // Deliberately put the pointer before its size in the JavaScript object.
    const object = buffer(t, {pBytes: data.subarray(1, 4), ByteCount: 3n});
    data.fill(0);
    assert.deepEqual(read(t, object).toUint8Array(), new Uint8Array([17, 128, 254]));
    assert.equal(buffer(t, {pBytes: new Uint8Array([1])}).Size(), 0n);
    assert.throws(() => api.CreateMarshalBytes({ByteCount: 3n, pBytes: new Uint8Array(2)}), RangeError);
});

test("null descriptor buffers preserve native zero initialization", t => {
    for (const descriptor of [{ByteCount: 3n}, {ByteCount: 3n, pBytes: null}]) {
        const object = buffer(t, descriptor);
        assert.deepEqual(read(t, object).toUint8Array(), new Uint8Array(3));
    }
    assert.deepEqual(api.CreateMarshalBytes({ByteCount: 1048577n}), {status: FAILED, value: null});
});

test("empty byte buffers and null successful outputs still own access scopes", t => {
    assert.equal(api.ChecksumBytes(new Uint8Array(), 0n, false), 0);
    const object = buffer(t, {});
    const reader = read(t, object);
    assert.equal(reader.byteLength, 0);
    assert.deepEqual(reader.toUint8Array(), new Uint8Array());
    reader.copyTo(new Uint8Array());
    assert.throws(() => reader.readByte(0), RangeError);
    assert.equal(object.ReaderCount(), 1);
    reader.dispose();
    const writer = write(t, object);
    writer.copyFrom(new Uint8Array());
    assert.equal(object.IsWriting(), true);
    writer.dispose();
    assert.equal(object.IsWriting(), false);
});

test("read scopes expose independent copies and support offset destination views", t => {
    const object = buffer(t);
    const reader = read(t, object);
    assert.equal(reader.byteLength, 4);
    assert.equal(reader.readByte(2), 3);
    const destination = new Uint8Array([99, 0, 0, 88]);
    reader.copyTo(destination.subarray(1, 3), 1);
    assert.deepEqual(destination, new Uint8Array([99, 2, 3, 88]));
    const copy = reader.toUint8Array();
    copy.fill(255);
    assert.equal(reader.readByte(0), 1);
    assert.equal(reader.copyFrom, undefined);
    assert.equal(reader.writeByte, undefined);
    assert.ok(Object.isFrozen(reader));
    // The release API is intentionally only available through the scope.
    assert.equal(object.UnmapRead, undefined);
    assert.equal(object.UnmapWrite, undefined);
});

test("write scopes copy subarrays into native storage and preserve surrounding bytes", t => {
    const object = buffer(t);
    const writer = write(t, object);
    writer.copyFrom(new Uint8Array([99, 17, 128, 88]).subarray(1, 3), 1);
    writer.writeByte(3, 254);
    assert.deepEqual(writer.toUint8Array(), new Uint8Array([1, 17, 128, 254]));
    writer.dispose();
    assert.deepEqual(read(t, object).toUint8Array(), new Uint8Array([1, 17, 128, 254]));
});

test("caller array overrides cannot expose native views or bypass bounds checks", t => {
    const writer = write(t, buffer(t));
    const destination = new Uint8Array(4);
    destination.set = () => assert.fail("Native memory must not reach a caller-provided method");
    Object.defineProperty(destination, "byteLength", {get() { throw new Error("custom length"); }});
    writer.copyTo(destination);
    assert.equal(destination[2], 3);
    const source = new Uint8Array([17, 128, 254]);
    Object.defineProperty(source, "byteLength", {value: 0});
    assert.throws(() => writer.copyFrom(source, 2), RangeError);
    assert.throws(() => api.ChecksumBytes(source, 4n, false), RangeError);
    writer.copyFrom(source, 1);
    assert.deepEqual(writer.toUint8Array(), new Uint8Array([1, 17, 128, 254]));
});

test("all byte operations reject out-of-range access without changing storage", t => {
    const writer = write(t, buffer(t));
    for (const offset of [-1, 4, 5, 0.5, NaN, Infinity, 1n]) {
        assert.throws(() => writer.readByte(offset), RangeError);
        assert.throws(() => writer.writeByte(offset, 42), RangeError);
        assert.throws(() => writer.copyFrom(new Uint8Array([42]), offset), RangeError);
        assert.throws(() => writer.copyTo(new Uint8Array(1), offset), RangeError);
    }
    for (const value of [-1, 256, 1.5, NaN, "1"]) {
        assert.throws(() => writer.writeByte(0, value), RangeError);
    }
    assert.throws(() => writer.copyFrom(new Uint8Array(5)), RangeError);
    assert.throws(() => writer.copyTo(new Uint8Array(5)), RangeError);
    assert.throws(() => writer.copyFrom([1]), TypeError);
    assert.throws(() => writer.copyTo([1]), TypeError);
    writer.copyFrom(new Uint8Array(), 4);
    writer.copyTo(new Uint8Array(), 4);
    assert.deepEqual(writer.toUint8Array(), new Uint8Array([1, 2, 3, 4]));
});

test("multiple readers, writer exclusion, and resize statuses follow native state", t => {
    const object = buffer(t);
    const first = read(t, object);
    const second = read(t, object);
    assert.equal(object.ReaderCount(), 2);
    assert.equal(object.RefCount(), 3);
    assert.deepEqual(object.MapWrite(), {status: FAILED, value: null});
    assert.equal(object.RefCount(), 3);
    assert.equal(object.Resize(8n), FAILED);
    first.dispose();
    second.dispose();
    const writer = write(t, object);
    assert.deepEqual(object.MapRead(), {status: FAILED, value: null});
    assert.deepEqual(object.MapWrite(), {status: FAILED, value: null});
    assert.equal(object.RefCount(), 2);
    assert.equal(object.Resize(8n), FAILED);
    writer.dispose();
    assert.equal(object.RefCount(), 1);
    assert.equal(object.Resize(8n), OK);
    assert.deepEqual(read(t, object).toUint8Array(), new Uint8Array([1, 2, 3, 4, 0, 0, 0, 0]));
});

test("each access scope retains its native owner independently of wrapper lifetime", t => {
    const live = api.GetLiveBufferCount();
    const object = buffer(t);
    const reader = read(t, object);
    object.dispose();
    assert.equal(api.GetLiveBufferCount(), live + 1);
    assert.equal(reader.readByte(0), 1);
    reader.dispose();
    assert.equal(api.GetLiveBufferCount(), live);
});

test("scope disposal is idempotent and prevents every subsequent byte operation", t => {
    const object = buffer(t);
    const writer = write(t, object);
    const releases = api.GetBufferReleaseCount();
    assert.equal(writer.dispose(), OK);
    assert.equal(writer.dispose(), OK);
    assert.equal(api.GetBufferReleaseCount(), releases + 1);
    assert.equal(writer.disposed, true);
    for (const operation of [() => writer.byteLength, () => writer.readByte(0),
        () => writer.toUint8Array(), () => writer.copyTo(new Uint8Array()),
        () => writer.writeByte(0, 1), () => writer.copyFrom(new Uint8Array())]) {
        assert.throws(operation, /disposed/);
    }
});

test("finally releases access when JavaScript processing throws", t => {
    const object = buffer(t);
    const error = new Error("processing failed");
    assert.throws(() => {
        const writer = write(t, object);
        try { writer.writeByte(0, 17); throw error; }
        finally { writer.dispose(); }
    }, error);
    assert.equal(object.IsWriting(), false);
    assert.equal(object.RefCount(), 1);
    assert.equal(read(t, object).readByte(0), 17);
});

test("native release errors still consume the scope and release its strong reference", t => {
    const object = buffer(t);
    object.SetReleaseStatus(FAILED);
    const reader = read(t, object);
    assert.equal(reader.dispose(), FAILED);
    assert.equal(reader.dispose(), FAILED);
    assert.equal(object.ReaderCount(), 0);
    assert.equal(object.RefCount(), 1);
});

test("invalid native spans unwind successful acquisitions and retained references", t => {
    const object = buffer(t);
    for (const length of [0x100000000n, 0xffffffffn]) {
        object.SetReportedSize(length);
        const releases = api.GetBufferReleaseCount();
        assert.throws(() => object.MapRead(), RangeError);
        assert.equal(object.ReaderCount(), 0);
        assert.equal(object.RefCount(), 1);
        assert.equal(api.GetBufferReleaseCount(), releases + 1);
    }
    const empty = buffer(t, {});
    empty.SetReportedSize(1n);
    assert.throws(() => empty.MapRead(), RangeError);
    assert.equal(empty.ReaderCount(), 0);
});

test("active byte scopes remain usable after real Wasm memory growth", t => {
    const object = buffer(t);
    const writer = write(t, object);
    const oldSize = api.GetTestHeapSize();
    assert.equal(api.GrowTestHeap(), true);
    assert.ok(api.GetTestHeapSize() > oldSize);
    writer.copyFrom(new Uint8Array([17, 128]), 1);
    assert.deepEqual(writer.toUint8Array(), new Uint8Array([1, 17, 128, 4]));
    writer.dispose();
    const reader = read(t, object);
    assert.equal(api.GrowTestHeap(), true);
    const copy = new Uint8Array(4);
    reader.copyTo(copy);
    assert.deepEqual(copy, new Uint8Array([1, 17, 128, 4]));
});

test("scoped methods validate their receiver and argument count", t => {
    const object = buffer(t);
    assert.throws(() => object.MapRead(1), TypeError);
    assert.throws(() => api.IMarshalBytes.prototype.MapRead.call({}), TypeError);
    object.dispose();
    assert.throws(() => object.MapWrite(), /disposed/);
});

test("all byte scopes and native buffers have been released", () => {
    assert.equal(api.GetLiveBufferCount(), 0);
});
