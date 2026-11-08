export function registerTests({test, assert, api, wasm}) {
    test("caller-filled arrays use capacity and written count", () => {
        const before = wasm.HEAPU8.byteLength;
        const {status, value} = api.FillIds(5, false, true);
        assert.equal(status, 3);
        assert.equal(value.Written, 2);
        assert.deepEqual(Array.from(value.pIds), [0xfedcba9876543210n, 0xfedcba9876543211n]);
        assert.ok(wasm.HEAPU8.byteLength > before);
        assert.equal(api.FillIds(0, false, false).value.pIds.length, 0);
        assert.throws(() => api.FillIds(2, true, false), RangeError);
        assert.equal(api.FillIds(1, false, false).value.Written, 1);
    });
    test("struct output arrays preserve native defaults and multiple outputs keep names", () => {
        const result = api.FillRecords(2);
        assert.equal(result[1].Value, 101n);
        assert.equal(result[0].Label, "default");
        assert.deepEqual(Array.from(result[0].Data), [1, 0, 0, 1]);
        assert.deepEqual(api.FillRecords(0), []);
        assert.deepEqual(api.FillMultiple(), {Count: 9, ppLabel: "multiple"});
    });
}
