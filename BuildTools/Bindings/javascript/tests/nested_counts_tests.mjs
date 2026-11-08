export function registerTests({test, assert, api}) {
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
