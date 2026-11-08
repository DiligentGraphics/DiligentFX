export function registerTests({test, assert, api}) {
    test("interface fields and arrays retain wrappers in snapshots", () => {
        const baseline = api.GetLiveObjectCount();
        const a = api.CreateMarshalObject({Size: 7n}, 0, false).value;
        const b = api.CreateMarshalObject({Size: 11n}, 0, true).value;
        try {
            assert.equal(api.SumInterfaces({pObject: a, ppObjects: [a, null, b], Count: 3}), 25n);
            assert.equal(api.SumInterfaceArray([a, b], 1), 7n);
            assert.equal(a.GetRefCount(), 1);
            const result = api.EchoInterfaces({pObject: a, ppObjects: [null, b], Count: 2});
            a.dispose(); b.dispose();
            assert.equal(result.pObject.GetSize(), 7n);
            assert.equal(result.ppObjects[1].GetSize(), 11n);
            assert.equal(result.ppObjects[0], null);
            result.pObject.dispose(); result.ppObjects[1].dispose();
            assert.equal(api.GetLiveObjectCount(), baseline);
            assert.throws(() => api.SumInterfaceArray([a], 1), /disposed/);
            assert.throws(() => api.SumInterfaceArray([{}], 1), TypeError);
        } finally { a.dispose(); b.dispose(); }
    });
    test("descriptor accessors cannot destroy already marshalled interface inputs", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 17n}, 0, false).value;
        assert.equal(api.SumInterfaces({pObject: object, get Count() {object.dispose(); return 0;}}), 17n);
        assert.equal(api.GetLiveObjectCount(), baseline);
        const retained = api.CreateMarshalObject({}, 0, false).value;
        try {
            assert.throws(() => api.SumInterfaces({pObject: retained, Count: 1, ppObjects: [{}]}), TypeError);
            assert.equal(retained.GetRefCount(), 1);
        } finally { retained.dispose(); }
    });
}
