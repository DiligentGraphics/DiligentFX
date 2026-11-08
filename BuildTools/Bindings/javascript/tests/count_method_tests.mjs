export function registerTests({test, assert, api, wasm}) {
    test("array results obtain counts from the native owner and survive memory growth", () => {
        const object = api.CreateMarshalArraySnapshots().value;
        try {
            const before = wasm.HEAPU8.byteLength;
            const values = object.GetAllValues();
            assert.deepEqual(Array.from(values), [-1.5, 0.25, 123.5]);
            assert.ok(wasm.HEAPU8.byteLength > before);
            const items = object.GetAllItems();
            assert.equal(items.length, 2);
            assert.equal(items[0].Id, 41);
            object.Update();
            assert.equal(values[1], 0.25);
            assert.equal(object.GetAllValues()[1], 64);
            object.SetValueCount(0n);
            assert.equal(object.GetAllValues().length, 0);
            object.SetValueCount(0xffffffffffffffffn);
            assert.throws(() => object.GetAllValues(), RangeError);
        } finally {object.dispose();}
    });
}
