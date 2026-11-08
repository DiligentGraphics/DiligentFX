export function registerTests({test, assert, api}) {
    test("inherited descriptor defaults and fields use native layout", () => {
        const value = api.EchoInherited({});
        assert.equal(value.Tag, 0xfedcba9876543210n);
        assert.equal(value.Scale, 2.5);
        assert.equal(value.Enabled, true);
        assert.equal(value.Count, 0);
        assert.equal(value.pValues, null);
        const input = {Tag: 11n, Scale: 7, Enabled: false, pValues: new Uint32Array([2, 3]), Count: 2};
        assert.equal(api.SumInherited(input), 16n);
        const copy = api.EchoInherited(input);
        assert.equal(copy.Enabled, false);
        assert.equal(copy.Scale, 7);
        assert.deepEqual(Array.from(copy.pValues), [2, 3]);
        input.pValues[0] = 100;
        assert.equal(copy.pValues[0], 2);
    });
}
