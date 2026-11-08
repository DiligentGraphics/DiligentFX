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
}
