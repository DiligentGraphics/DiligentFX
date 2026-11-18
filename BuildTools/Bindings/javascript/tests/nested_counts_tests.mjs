export function registerTests({test, assert, api}) {
    test("computed byte sizes preserve prefixes, nulls, and empty storage", () => {
        const bytes = new Uint8Array([0, 127, 128, 255, 17, 31, 99]);
        const input = {pBytes: bytes};
        const copy = api.EchoComputedBuffer(input, false, false);
        assert.deepEqual(copy.pBytes, bytes.slice(0, 6));
        let checksum = 0;
        for (let i = 0; i < 6; ++i) checksum = (checksum * 31 + bytes[i]) >>> 0;
        assert.equal(api.GetComputedBufferChecksum(input), checksum);
        assert.equal(api.EchoComputedBuffer({ElementCount: 0}, false, false).pBytes, null);
        assert.equal(api.EchoComputedBuffer({pBytes: new Uint8Array(), ElementCount: 0}, false, false).pBytes, null);
        assert.throws(() => api.GetComputedBufferChecksum({pBytes: bytes, ElementCount: 4}), /count exceeds/);
        const source = api.CreateMarshalObject({Size: 23n}, 0, false).value;
        try {
            assert.throws(() => api.EchoComputedBuffer({pObject: source, pBytes: bytes}, true, false), RangeError);
            assert.throws(() => api.EchoComputedBuffer({pObject: source, pBytes: bytes}, false, true), TypeError);
            assert.equal(source.GetRefCount(), 1);
        } finally { source.dispose(); }
        assert.equal(api.GetLiveObjectCount(), 0);
    });
    test("computed typed array snapshots are independent", () => {
        const input = {
            Counts: {ItemCount: 0, SampleCount: 0n},
            pValues: new Uint32Array(), pComputed: new Uint32Array([5, 99]), pPair: new Uint32Array([7, 9]),
        };
        const copy = api.EchoNestedCount(input);
        assert.deepEqual(Array.from(copy.pComputed), [5]);
        input.pComputed[0] = 0;
        assert.equal(copy.pComputed[0], 5);
    });
    test("nested field counts and fixed pointer extents are read after descriptor initialization", () => {
        assert.equal(api.SumNestedCount({pValues: new Uint32Array([2, 3, 7]), pPair: new Uint32Array([11, 13]), Counts: {ItemCount: 3}}), 36);
        assert.equal(api.SumNestedCount({pValues: new Uint32Array([2, 3])}), 5); // Native nested default.
        assert.equal(api.SumNestedCount({pValues: null, Counts: {ItemCount: 0}}), 0);
        assert.equal(api.SumNestedCount({pValues: new Uint32Array([2, 3]), pComputed: new Uint32Array([5, 7, 11])}), 28);
        assert.throws(() => api.SumNestedCount({pValues: new Uint32Array([2, 3]), pComputed: new Uint32Array(2)}), /count exceeds/);
        assert.throws(() => api.SumNestedCount({pValues: new Uint32Array(1), Counts: {ItemCount: 2}}), /count exceeds/);
        assert.throws(() => api.SumNestedCount({pValues: new Uint32Array(2), pPair: new Uint32Array(1)}), /count exceeds/);
    });
}
