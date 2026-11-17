export function registerTests({test, assert, api}) {
    test("raw byte spans preserve all byte values and copied prefixes", () => {
        const data = Uint8Array.from({length: 256}, (_, i) => i);
        let checksum = 0;
        for (const byte of data) checksum = (checksum * 31 + byte) >>> 0;
        assert.equal(api.ChecksumBytes(data, 256n, false), checksum);
        const saved = api.EchoByteSpan(data, 256n);
        assert.deepEqual(saved, data);
        assert.equal(saved === data, false);
        assert.deepEqual(api.EchoByteSpan(data, 3n), new Uint8Array([0, 1, 2]));
        data[255] = 0;
        assert.equal(saved[255], 255);
        assert.equal(api.EchoByteSpan(null, 0n), null);
        // JavaScript requires a valid Wasm range for every nonempty output span.
        assert.throws(() => api.EchoByteSpan(null, 7n), RangeError);
        assert.equal(api.ByteSpanHasStorage(new Uint8Array([1]), 1n), true);
        assert.equal(api.EchoBufferDesc({}).pBytes, null);
    });
    test("raw byte snapshots survive native owner updates and destruction", () => {
        const baseline = api.GetLiveBufferCount();
        const input = new Uint8Array([17, 128, 254, 99]);
        const desc = {pBytes: input, ByteCount: 3n};
        const copy = api.EchoBufferDesc(desc);
        const source = api.CreateMarshalBytes(desc).value;
        let saved;
        try {
            input[0] = 0;
            saved = source.GetData();
            assert.deepEqual(saved, new Uint8Array([17, 128, 254]));
            assert.deepEqual(copy.pBytes, saved);
            assert.deepEqual(api.CopyBufferBytes(source), saved);
            source.Resize(1n);
            assert.deepEqual(source.GetData(), new Uint8Array([17]));
        } finally { source.dispose(); }
        assert.equal(saved[2], 254);
        assert.equal(api.GetLiveBufferCount(), baseline);
    });
    test("raw byte outputs honor written counts and initialize unwritten tails", () => {
        const filled = api.FillByteSpan(8n, false);
        assert.equal(filled.status, 3);
        assert.equal(filled.value.Written, 3n);
        assert.deepEqual(filled.value.pBytes, new Uint8Array([17, 128, 254]));
        assert.deepEqual(api.FillByteCapacity(8n, false).value, new Uint8Array([17, 128, 254, 0, 0, 0, 0, 0]));
        const failed = api.FillByteCapacity(8n, true);
        assert.equal(failed.status, -7);
        assert.deepEqual(failed.value, new Uint8Array(8));
        assert.deepEqual(api.FillByteCapacity(0n, false).value, new Uint8Array());
        assert.throws(() => api.FillByteSpan(2n, true), RangeError);
        const calls = api.GetByteSpanCallCount();
        assert.throws(() => api.FillByteCapacity(0xffffffffffffffffn, false), RangeError);
        assert.throws(() => api.ChecksumBytes(new Uint8Array(2), 3n, false), RangeError);
        assert.equal(api.GetByteSpanCallCount(), calls);
    });
    test("failed byte descriptor snapshots release acquired interface references", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 23n}, 0, false).value;
        const packet = {pObject: object, Buffer: {pBytes: new Uint8Array([0, 255]), ByteCount: 2n}};
        let saved;
        try {
            for (let i = 0; i < 3; ++i) {
                assert.throws(() => api.EchoBytePacket(packet, true), TypeError);
                assert.equal(object.GetRefCount(), 1);
            }
            saved = api.EchoBytePacket(packet, false);
            assert.equal(object.GetRefCount(), 2);
            object.dispose();
            packet.Buffer.pBytes[1] = 0;
            assert.equal(saved.pObject.GetSize(), 23n);
            assert.deepEqual(saved.Buffer.pBytes, new Uint8Array([0, 255]));
        } finally {
            object.dispose();
            saved?.pObject.dispose();
        }
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
}
