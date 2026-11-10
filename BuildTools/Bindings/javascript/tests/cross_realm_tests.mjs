// Exercise the same inputs through Node's vm and a browser's same-origin iframe.
async function createRealm(t) {
    if (typeof document === "undefined") {
        const {runInNewContext} = await import("node:vm");
        return runInNewContext("globalThis");
    }
    const frame = document.createElement("iframe");
    frame.hidden = true;
    t.after(() => frame.remove());
    document.body.append(frame);
    return frame.contentWindow;
}

export function registerTests({test, assert, api, constants}) {
    test("foreign typed array inputs preserve element types, values, and view offsets", async t => {
        const realm = await createRealm(t);
        for (const [name, Type, expected, count] of [
            ["Uint8", realm.Uint8Array, [0, 128, 255], 3],
            ["Int8", realm.Int8Array, [-128, 17, 127], 3],
            ["Float32", realm.Float32Array, [-1.5, 0.25, 123.5], 3],
            ["Uint64", realm.BigUint64Array, [0n, 0x8000000000000000n, 0xfedcba9876543210n], 3n],
        ]) {
            const storage = new Type(5);
            storage.set(expected, 1);
            const values = storage.subarray(1, 4);
            assert.equal(values instanceof globalThis[Type.name], false);
            assert.equal(api[`CheckArray${name}`](values, count), true);
            assert.deepEqual(Array.from(values), expected);
        }
        // Signed-byte spans also accept Uint8Array, including from another realm.
        assert.equal(api.CheckArrayInt8(new realm.Uint8Array([128, 17, 127]), 3), true);
    });

    test("byte scopes copy foreign subarrays and write into the original foreign destination", async t => {
        const realm = await createRealm(t);
        const {MARSHAL_STATUS_OK: OK, MARSHAL_STATUS_PENDING: WRITE_OK} = constants.MARSHAL_STATUS;
        const source = new realm.Uint8Array([99, 1, 2, 3, 4, 88]);
        const created = api.CreateMarshalBytes({ByteCount: 4n, pBytes: source.subarray(1, 5)});
        assert.equal(created.status, OK);
        const object = created.value;
        t.after(() => object.dispose());

        const mapped = object.MapWrite();
        assert.equal(mapped.status, WRITE_OK);
        const writer = mapped.value;
        t.after(() => writer.dispose());
        writer.copyFrom(new realm.Uint8Array([99, 17, 128, 88]).subarray(1, 3), 1);
        assert.deepEqual(Array.from(writer.toUint8Array()), [1, 17, 128, 4]);
        writer.dispose();

        const mappedRead = object.MapRead();
        assert.equal(mappedRead.status, OK);
        const reader = mappedRead.value;
        t.after(() => reader.dispose());
        const destination = new realm.Uint8Array([99, 0, 0, 88]);
        reader.copyTo(destination.subarray(1, 3), 1);
        assert.deepEqual(Array.from(destination), [99, 17, 128, 88]);
        assert.throws(() => reader.copyTo(new realm.Uint8Array(5)), RangeError);
    });

    test("virtual file initialization accepts foreign byte views", async t => {
        const realm = await createRealm(t);
        const storage = new realm.Uint8Array([99, 1, 2, 255, 88]);
        const loaded = await constants.default({files: {"/foreign.bin": storage.subarray(1, 4)}});
        storage.fill(0);
        assert.equal(loaded.SumFileBytes("/foreign.bin"), 258);
    });

    test("foreign array validation rejects wrong brands, spoofed tags, and detached storage", async t => {
        const realm = await createRealm(t);
        const wrongType = new realm.Uint16Array([1, 2, 3]);
        Object.defineProperty(wrongType, Symbol.toStringTag, {value: "Uint8Array"});
        for (const value of [wrongType, new realm.Uint8ClampedArray(3),
            new realm.DataView(new realm.ArrayBuffer(3)), {[Symbol.toStringTag]: "Uint8Array"}]) {
            assert.throws(() => api.ChecksumBytes(value, 3n, false), TypeError);
            assert.throws(() => api.CheckArrayInt8(value, 3), TypeError);
        }
        // The internal brand must not consult caller-supplied tag accessors.
        const bytes = new realm.Uint8Array([128, 17, 127]);
        Object.defineProperty(bytes, Symbol.toStringTag, {get() { throw new Error("custom tag"); }});
        assert.equal(api.CheckArrayInt8(bytes, 3), true);
        structuredClone(bytes.buffer, {transfer: [bytes.buffer]});
        assert.throws(() => api.ChecksumBytes(bytes, 0n, false), TypeError);
    });
}
