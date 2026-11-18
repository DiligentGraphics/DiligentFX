import {readFixtures} from "./fixtures.mjs";
// The production blob cases are shared by the Node and browser runners.
export function registerTests({test, assert, api, constants, wasm, testData}) {
    const data = readFixtures(testData);
    const {
        RADIENT_STATUS_OK: OK,
        RADIENT_STATUS_INVALID_ARGUMENT: INVALID_ARGUMENT,
        RADIENT_STATUS_INVALID_OPERATION: INVALID_OPERATION,
    } = constants.RADIENT_STATUS;

    function blob(t, mutable, info = {}) {
        const result = mutable ? api.CreateRadientMutableDataBlob(info) : api.CreateRadientDataBlob(info);
        assert.equal(result.status, OK);
        assert.ok(result.value);
        t.after(() => result.value.dispose());
        return result.value;
    }

    function read(t, object) {
        const result = object.BeginRead();
        assert.equal(result.status, OK);
        assert.ok(result.value);
        t.after(() => result.value.dispose());
        return result.value;
    }

    function write(t, object) {
        const result = object.BeginWrite();
        assert.equal(result.status, OK);
        assert.ok(result.value);
        t.after(() => result.value.dispose());
        return result.value;
    }

    test("owning factories copy initial data from the selected array view", t => {
        for (const mutable of [false, true]) {
            const source = data.bytes("blob.source");
            const object = blob(t, mutable, {pData: source.subarray(1, 4), Size: 3n});
            source.fill(0);
            assert.equal(object.GetSize(), 3n);
            assert.deepEqual(read(t, object).toUint8Array(), data.bytes("blob.initial"));
        }
    });

    test("omitted and null initial data use Radient's zero initialization", t => {
        for (const mutable of [false, true]) {
            for (const info of [{Size: 4n}, {Size: 4n, pData: null}]) {
                assert.deepEqual(read(t, blob(t, mutable, info)).toUint8Array(), new Uint8Array(4));
            }
        }
    });

    test("default empty blobs have successful read and write scopes", t => {
        const empty = blob(t, false);
        assert.equal(empty.GetSize(), 0n);
        const reader = read(t, empty);
        assert.equal(reader.byteLength, 0);
        assert.deepEqual(reader.toUint8Array(), new Uint8Array());
        const mutable = blob(t, true);
        const emptyReaderBeforeWrite = read(t, mutable);
        assert.deepEqual(emptyReaderBeforeWrite.toUint8Array(), new Uint8Array());
        emptyReaderBeforeWrite.dispose();
        const writer = write(t, mutable);
        assert.equal(writer.byteLength, 0);
        writer.copyFrom(new Uint8Array());
        assert.deepEqual(mutable.BeginRead(), {status: INVALID_OPERATION, value: null});
        writer.dispose();
        const emptyReader = read(t, mutable);
        assert.equal(emptyReader.byteLength, 0);
        emptyReader.dispose();
        assert.equal(mutable.Resize(4n), OK);
        assert.deepEqual(read(t, mutable).toUint8Array(), new Uint8Array(4));
    });

    test("native-only callbacks, context, and reference storage are not exposed", () => {
        for (const factory of [api.CreateRadientDataBlob, api.CreateRadientMutableDataBlob]) {
            for (const field of ["OnDestroy", "OnLastReaderReleased", "pUserData"]) {
                for (const value of [null, undefined, () => assert.fail("Callback must never be invoked")]) {
                    assert.throws(() => factory({[field]: value}), new RegExp(`Unknown .* field: ${field}`));
                }
            }
            assert.throws(() => factory({}, 1), /Expected 1 arguments/);
        }
        assert.equal(api.RADIENT_DATA_BLOB_STORAGE_MODE, undefined);
    });

    test("invalid byte ranges fail before entering the native factory", () => {
        for (const factory of [api.CreateRadientDataBlob, api.CreateRadientMutableDataBlob]) {
            assert.throws(() => factory({Size: 4n, pData: new Uint8Array(3)}), RangeError);
            assert.throws(() => factory({Size: -1n}), RangeError);
            assert.throws(() => factory({Size: 4}), TypeError);
            assert.throws(() => factory({Size: 1n, pData: [17]}), TypeError);
        }
    });

    test("native allocation-size rejection returns status and a null output", () => {
        // Exceeds vector::max_size on native and Wasm, without allocating a large buffer.
        const size = (1n << 64n) - 1n;
        for (const factory of [api.CreateRadientDataBlob, api.CreateRadientMutableDataBlob]) {
            assert.deepEqual(factory({Size: size}), {status: INVALID_ARGUMENT, value: null});
        }
    });

    test("read-only native blobs do not provide a mutable interface", t => {
        const object = blob(t, false, {Size: 2n});
        assert.equal(object.QueryInterface(api.IRadientMutableDataBlob), null);
        const reader = read(t, object);
        assert.equal(reader.copyFrom, undefined);
        assert.equal(reader.writeByte, undefined);
        assert.equal(object.EndRead, undefined);
        const retained = object.QueryInterface(api.IRadientDataBlob);
        assert.ok(retained);
        t.after(() => retained.dispose());
        reader.dispose();
        object.dispose();
        assert.equal(retained.GetSize(), 2n);
        assert.throws(() => object.GetSize(), /disposed/);
    });

    test("queried interface views share native access state", t => {
        const object = blob(t, true, {Size: 4n});
        assert.ok(object instanceof api.IRadientDataBlob);
        const base = object.QueryInterface(api.IRadientDataBlob);
        assert.ok(base);
        t.after(() => base.dispose());
        const queried = base.QueryInterface(api.IRadientMutableDataBlob);
        assert.ok(queried);
        t.after(() => queried.dispose());
        const first = read(t, base);
        const second = read(t, queried);
        assert.deepEqual(object.BeginWrite(), {status: INVALID_OPERATION, value: null});
        assert.equal(object.Resize(4n), INVALID_OPERATION);
        first.dispose();
        assert.deepEqual(object.BeginWrite(), {status: INVALID_OPERATION, value: null});
        second.dispose();
        const writer = write(t, object);
        assert.deepEqual(base.BeginRead(), {status: INVALID_OPERATION, value: null});
        assert.deepEqual(queried.BeginWrite(), {status: INVALID_OPERATION, value: null});
        writer.copyFrom(data.bytes("blob.written"));
        writer.dispose();
        assert.deepEqual(read(t, base).toUint8Array(), data.bytes("blob.written"));
    });

    test("resize preserves data, zeros added bytes, and discards truncated data", t => {
        const object = blob(t, true, {Size: 3n, pData: data.bytes("blob.initial")});
        let reader = read(t, object);
        assert.equal(object.Resize(6n), INVALID_OPERATION);
        assert.deepEqual(object.BeginWrite(), {status: INVALID_OPERATION, value: null});
        reader.dispose();
        assert.equal(object.Resize(6n), OK);
        reader = read(t, object);
        assert.deepEqual(reader.toUint8Array(), data.bytes("blob.grown"));
        reader.dispose();
        assert.equal(object.Resize(1n), OK);
        assert.equal(object.Resize(3n), OK);
        reader = read(t, object);
        assert.deepEqual(reader.toUint8Array(), data.bytes("blob.truncated"));
        reader.dispose();
        assert.equal(object.Resize((1n << 64n) - 1n), INVALID_ARGUMENT);
        assert.equal(object.GetSize(), 3n);
        reader = read(t, object);
        assert.deepEqual(reader.toUint8Array(), data.bytes("blob.truncated"));
        reader.dispose();
        assert.equal(object.Resize(0n), OK);
        assert.equal(read(t, object).byteLength, 0);
    });

    test("scopes retain real blobs after all interface wrappers are disposed", t => {
        const object = blob(t, true, {Size: 2n});
        const writer = write(t, object);
        object.dispose();
        writer.copyFrom(new Uint8Array([17, 128]));
        assert.deepEqual(writer.toUint8Array(), new Uint8Array([17, 128]));
        assert.equal(writer.dispose(), OK);
        assert.equal(writer.dispose(), OK);
        assert.throws(() => writer.readByte(0), /disposed/);
        assert.throws(() => object.GetSize(), /disposed/);

        const immutable = blob(t, false, {Size: 1n, pData: new Uint8Array([254])});
        const reader = read(t, immutable);
        immutable.dispose();
        assert.equal(reader.readByte(0), 254);
        const extra = blob(t, true, {Size: 3n, pData: data.bytes("blob.initial")});
        const extraWriter = write(t, extra);
        extra.dispose();
        extraWriter.copyFrom(data.bytes("blob.retained"));
        assert.equal(extraWriter.readByte(0), 255);
        assert.deepEqual(extraWriter.toUint8Array(), data.bytes("blob.retained"));
    });

    test("finally releases native write access when client processing fails", t => {
        const object = blob(t, true, {Size: 1n});
        const error = new Error("processing failed");
        assert.throws(() => {
            const writer = write(t, object);
            try { writer.writeByte(0, 17); throw error; }
            finally { writer.dispose(); }
        }, error);
        assert.equal(read(t, object).readByte(0), 17);
    });

    test("active byte scopes remain valid after Wasm memory grows", t => {
        const object = blob(t, true, {Size: 3n, pData: data.bytes("blob.initial")});
        const writer = write(t, object);
        const oldSize = wasm.HEAPU8.byteLength;
        // A request larger than the entire current heap forces growth without
        // relying on allocator placement. No native pointer escapes to the client.
        const allocation = wasm._malloc(oldSize + 65536);
        assert.ok(allocation);
        t.after(() => wasm._free(allocation));
        assert.ok(wasm.HEAPU8.byteLength > oldSize, "The test must actually grow Wasm memory");
        assert.deepEqual(writer.toUint8Array(), data.bytes("blob.initial"));
        writer.copyFrom(new Uint8Array([42, 99]), 1);
        writer.writeByte(0, 7);
        const copy = new Uint8Array(3);
        writer.copyTo(copy);
        assert.deepEqual(copy, new Uint8Array([7, 42, 99]));
        writer.dispose();
        assert.deepEqual(read(t, object).toUint8Array(), copy);
    });
}
