export function registerTests({test, assert, api, wasm}) {
    test("scalar reference outputs preserve status and 64-bit values", () => {
        assert.deepEqual(api.ReadReferenceOutput(3), {status: 3, value: 0xfedcba9876543210n});
        assert.deepEqual(api.ReadReferenceOutput(-7), {status: -7, value: 0n});
    });
    test("struct outputs construct native defaults and copy fields after memory growth", () => {
        const before = wasm.HEAPU8.byteLength;
        const {status, value} = api.ReadRecordOutput(true, true, false);
        assert.equal(status, 3);
        assert.ok(wasm.HEAPU8.byteLength > before);
        assert.equal(value.Value, 0xfedcba9876543210n);
        assert.deepEqual(Array.from(value.Data), [1, 2.5, 0, 1]);
        assert.equal(value.Label, "native output");
        const failed = api.ReadRecordOutput(false, false, false);
        assert.equal(failed.status, -7);
        assert.equal(failed.value.Value, 17n);
        assert.equal(failed.value.Label, "default");
        assert.equal(api.MakeRecordOutput().Value, 42n);
        assert.deepEqual(Array.from(api.MakeRecordOutput().Data), [1, 0, -3, 1]);
    });
    test("struct output storage is destroyed even when snapshot conversion fails", () => {
        const key = "_binding_Diligent_MarshalOutputRecord_Destroy";
        const destroy = wasm[key];
        let count = 0;
        wasm[key] = pointer => { ++count; destroy(pointer); };
        try {
            assert.throws(() => api.ReadRecordOutput(true, false, true), TypeError);
            assert.equal(count, 1);
            assert.equal(api.ReadRecordOutput(true, false, false).value.Label, "native output");
            assert.equal(count, 2);
        } finally { wasm[key] = destroy; }
    });
}
