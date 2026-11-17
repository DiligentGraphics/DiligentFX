export function registerTests({test, assert, api, wasm}) {
    test("optional and required struct pointers preserve native defaults", () => {
        assert.equal(api.ReadStructPointer(null), 0n);
        assert.equal(api.ReadRequiredPointer({}), 0xfedcba9876543210n);
        assert.equal(api.ReadStructPointer({Size: 19n}), 19n);
        assert.throws(() => api.ReadRequiredPointer(null), TypeError);
        assert.equal(api.EchoStructPointer(null), null);
        const copy = api.EchoStructPointer({Size: 23n});
        assert.equal(copy.Size, 23n);
        assert.equal(copy.Scale, 1.25);
    });
    test("pointer fields snapshot pointed-to structs before releasing temporary storage", () => {
        assert.equal(api.EchoPointerDesc({}).pData, null);
        assert.equal(api.EchoPointerDesc({pData: null}).pData, null);
        const result = api.EchoPointerDesc({pData: {Size: 29n}});
        api.EchoPointerDesc({pData: {Size: 31n}});
        assert.equal(result.pData.Size, 29n);
        const key = "_binding_Diligent_MarshalDesc_Destroy";
        const destroy = wasm[key]; let count = 0;
        wasm[key] = pointer => {++count; destroy(pointer);};
        try {
            assert.throws(() => api.EchoPointerDesc({pData: {Invalid: 1}}), TypeError);
            assert.equal(count, 1);
        } finally {wasm[key] = destroy;}
    });
    test("descriptor pointer fields preserve non-null defaults and validate required pointers", () => {
        const defaults = api.EchoDefaultPointerDesc({});
        assert.equal(defaults.pData.Size, 31n);
        assert.equal(defaults.pData.Count, -2);
        assert.equal(defaults.pData.Scale, 2);
        defaults.pData.Size = 99n;
        assert.equal(api.EchoDefaultPointerDesc(defaults).pData.Size, 99n);
        assert.equal(api.EchoDefaultPointerDesc({}).pData.Size, 31n);
        assert.equal(api.EchoDefaultPointerDesc({pData: null}).pData, null);
        assert.throws(() => api.EchoRequiredPointerDesc({pData: null}), TypeError);
        assert.throws(() => api.GetRequiredPointerSnapshot({}), /null MarshalDesc reference/);
        assert.equal(api.EchoRequiredPointerDesc({pData: {Size: 37n}}).pData.Size, 37n);
        assert.equal(api.GetRequiredPointerSnapshot({pData: {Size: 39n}}).pData.Size, 39n);
    });
    function nested(object) {
        return {
            pNested: {pData: {Size: 41n}},
            pInterfaces: {pItems: [{pObject: object, ppObjects: [object, null], Count: 2}], Count: 1},
            pArrays: {Name: "Aé水😀", pValues: new Float64Array([1.25, -7.5]), ValueCount: 2n,
                      pItems: [{Id: 53, Label: "nested item", pSamples: new Float32Array([3.5, -2.25]), SampleCount: 2}],
                      ItemCount: 1},
        };
    }
    function dispose(desc) {
        for (const item of desc?.pInterfaces?.pItems ?? []) {
            item.pObject?.dispose();
            for (const object of item.ppObjects ?? []) object?.dispose();
        }
    }
    test("nested pointer snapshots retain data and interfaces through conversion", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 19n}, 0, false).value;
        const input = nested(object);
        let snapshot, array;
        try {
            assert.equal(api.ReadNestedPointerValue(input, 0), 41);
            assert.equal(api.ReadNestedPointerValue(input, 1), 38);
            assert.equal(api.ReadNestedPointerValue(input, 2), 53);
            assert.equal(api.ReadNestedPointerValue(input, 3), 1.25);
            assert.equal(api.ReadNestedPointerValue(input, 4), -1);
            snapshot = api.EchoNestedPointerDesc(input, false);
            assert.equal(snapshot.pNested === input.pNested, false);
            assert.equal(snapshot.pArrays.pItems[0] === input.pArrays.pItems[0], false);
            assert.equal(object.GetRefCount(), 3);
            array = api.EchoNestedPointerArray([input, {}], 2);
            assert.equal(array.length, 2);
            assert.equal(array[1].pNested, null);
            assert.equal(array[1].pArrays, null);
            assert.equal(array[1].pImage, null);
            assert.equal(object.GetRefCount(), 5);
            input.pNested.pData.Size = 99n;
            input.pArrays.pValues[0] = 99;
            input.pArrays.pItems[0].pSamples[0] = 99;
            object.dispose();
            assert.equal(snapshot.pNested.pData.Size, 41n);
            assert.equal(snapshot.pArrays.Name, "Aé水😀");
            assert.deepEqual(Array.from(snapshot.pArrays.pValues), [1.25, -7.5]);
            assert.deepEqual(Array.from(snapshot.pArrays.pItems[0].pSamples), [3.5, -2.25]);
            assert.equal(snapshot.pInterfaces.pItems[0].pObject.GetSize(), 19n);
            assert.equal(array[0].pInterfaces.pItems[0].ppObjects[0].GetSize(), 19n);
            assert.equal(array[0].pInterfaces.pItems[0].ppObjects[1], null);
        } finally {
            object.dispose();
            dispose(snapshot);
            for (const desc of array ?? []) dispose(desc);
        }
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
    test("failed pointer snapshots roll back nested interface references", () => {
        const baseline = api.GetLiveObjectCount();
        const object = api.CreateMarshalObject({Size: 19n}, 0, false).value;
        const input = nested(object);
        try {
            input.pArrays.ItemCount = 2;
            assert.throws(() => api.ReadNestedPointerValue(input, 0), RangeError);
            assert.equal(object.GetRefCount(), 1);
            input.pArrays.ItemCount = 1;
            for (let i = 0; i < 3; ++i) {
                assert.throws(() => api.EchoNestedPointerDesc(input, true), TypeError);
                assert.equal(object.GetRefCount(), 1);
                assert.equal(api.GetLiveObjectCount(), baseline + 1);
            }
        } finally { object.dispose(); }
        assert.equal(api.GetLiveObjectCount(), baseline);
    });
}
