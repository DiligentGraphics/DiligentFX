export function registerTests({test, assert, api}) {
    test("borrowed interface results retain independent references", () => {
        const baseline = api.GetLiveObjectCount();
        const source = api.CreateMarshalObject({Size: 123n}, 0, true).value;
        const result = api.BorrowObject(source, false);
        const second = api.BorrowObjectOutput(source);
        assert.equal(source.GetRefCount(), 3);
        assert.equal(api.BorrowObject(source, true), null);
        source.dispose();
        assert.equal(result.GetSize(), 123n);
        result.dispose();
        assert.equal(second.GetSize(), 123n);
        second.dispose();
        assert.equal(api.GetLiveObjectCount(), baseline);
        assert.throws(() => api.BorrowObject(source, false), /disposed/);
    });
    test("multiple output conversion failure releases converted and unconverted interfaces", () => {
        const baseline = api.GetLiveObjectCount();
        assert.throws(() => api.MakeFailingOutputs(), TypeError);
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
}
