export function registerTests({test, assert, api}) {
    test("native defaults are evaluated per call and explicit values are unchanged", () => {
        const before = api.NextDefaultValue();
        assert.equal(api.WithDefaults(10n), 10n + BigInt(before + 1));
        assert.equal(api.WithDefaults(10n, undefined, undefined), 10n + BigInt(before + 2));
        assert.equal(api.WithDefaults(1000n, false), 1000n - BigInt(before + 3));
        assert.equal(api.WithDefaults(10n, true, 7), 17n);
        assert.equal(api.NextDefaultValue(), before + 4);
        assert.throws(() => api.WithDefaults(), TypeError);
        assert.throws(() => api.WithDefaults(10n, undefined, 7), TypeError);
        assert.throws(() => api.WithDefaults(10n, true, 7, 8), TypeError);
    });
    test("explicit arguments remain available for defaulted inputs required by snapshot lifetimes and counts", () => {
        assert.equal(api.DefaultBorrowedDescriptor({Size: 37n}).Size, 37n);
        assert.throws(() => api.DefaultBorrowedDescriptor(), TypeError);
        assert.deepEqual(Array.from(api.DefaultArrayCount(3)), [1, 2, 3]);
        assert.throws(() => api.DefaultArrayCount(), TypeError);
    });
    test("default descriptors, null pointers, strings and output arguments use native defaults", () => {
        assert.equal(api.DefaultPointer(), 0n);
        assert.equal(api.DefaultPointer({Size: 19n}), 19n);
        assert.equal(api.DefaultDescriptor(), 0xfedcba9876543210n);
        assert.equal(api.DefaultDescriptor({Size: 23n}), 23n);
        assert.equal(api.DefaultOutput(), 0xfedcba9876543210n);
        assert.equal(api.DefaultOutput(false), 7n);
        assert.equal(api.DefaultStringLength(), 6);
        assert.equal(api.DefaultStringLength("test"), 4);
        const object = api.CreateMarshalArraySnapshots().value;
        try {
            object.SetValueCount();
            assert.equal(object.GetAllValues().length, 3);
            object.SetValueCount(0n);
            assert.equal(object.GetAllValues().length, 0);
        } finally {object.dispose();}
    });
}
