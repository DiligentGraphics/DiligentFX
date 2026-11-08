export function registerTests({test, assert, api, constants}) {
    const pending = api.MARSHAL_STATUS.MARSHAL_STATUS_PENDING;

    test("free functions borrow mutable and const interface references", t => {
        const baseline = api.GetLiveObjectCount();
        const source = api.CreateMarshalObject({Size: 0xfedcba9876543210n}, 0, true).value;
        const mutable = source.QueryInterface(api.IMarshalMutableObject);
        t.after(() => { mutable.dispose(); source.dispose(); });
        assert.equal(source.GetNativeDesc, undefined);
        assert.equal(api.ReadObjectReference(source), 0xfedcba9876543210n);
        // A derived interface wrapper also satisfies a const base reference.
        assert.equal(api.ReadObjectReference(mutable), source.GetSize());
        assert.equal(api.WriteObjectReference(mutable, {Size: 0x123456789abcdef0n}), pending);
        assert.equal(source.GetSize(), 0x123456789abcdef0n);
        assert.equal(source.GetRefCount(), 2);
        mutable.dispose(); source.dispose();
        assert.equal(api.GetLiveObjectCount(), baseline);
    });

    test("interface methods accept const and mutable references", t => {
        const source = api.CreateMarshalObject({Size: 71n}, 0, false).value;
        const a = api.CreateMarshalObject({Size: 2n}, 0, true).value;
        const b = api.CreateMarshalObject({Size: 3n}, 0, true).value;
        const mutableA = a.QueryInterface(api.IMarshalMutableObject);
        const mutableB = b.QueryInterface(api.IMarshalMutableObject);
        t.after(() => { mutableA.dispose(); mutableB.dispose(); a.dispose(); b.dispose(); source.dispose(); });
        assert.equal(mutableA.CopySizeFrom(source), pending);
        assert.equal(a.GetSize(), 71n);
        assert.equal(mutableA.CopySizeTo(mutableB), pending);
        assert.equal(b.GetSize(), 71n);
        assert.equal(source.GetRefCount(), 1);
        assert.equal(a.GetRefCount(), 2);
        assert.equal(b.GetRefCount(), 2);
    });

    test("interface references reject null, incompatible, and disposed arguments", t => {
        const source = api.CreateMarshalObject({Size: 91n}, 0, true).value;
        const mutable = source.QueryInterface(api.IMarshalMutableObject);
        t.after(() => { mutable.dispose(); source.dispose(); });
        for (const value of [null, undefined, {}, 0]) {
            assert.throws(() => api.ReadObjectReference(value), TypeError);
            assert.throws(() => api.WriteObjectReference(value, {}), TypeError);
            assert.throws(() => mutable.CopySizeFrom(value), TypeError);
            assert.throws(() => mutable.CopySizeTo(value), TypeError);
        }
        // The caller must query the mutable interface before passing that type.
        assert.throws(() => api.WriteObjectReference(source, {}), TypeError);
        assert.throws(() => mutable.CopySizeTo(source), TypeError);
        assert.equal(source.GetSize(), 91n);
        assert.equal(source.GetRefCount(), 2);
        source.dispose(); mutable.dispose();
        assert.throws(() => api.ReadObjectReference(source), /disposed/);
        assert.throws(() => api.WriteObjectReference(mutable, {}), /disposed/);
    });

    test("interface reference arguments belong to the same Wasm module", async t => {
        const other = await constants.default();
        const source = other.CreateMarshalObject({}, 0, true).value;
        const mutable = source.QueryInterface(other.IMarshalMutableObject);
        t.after(() => { mutable.dispose(); source.dispose(); });
        assert.throws(() => api.ReadObjectReference(source), TypeError);
        assert.throws(() => api.WriteObjectReference(mutable, {}), TypeError);
        assert.equal(source.GetRefCount(), 2);
    });

    test("interface references remain alive during later argument marshalling", t => {
        const baseline = api.GetLiveObjectCount();
        const destroyed = api.GetDestructionCount();
        const source = api.CreateMarshalObject({Size: 15n}, 0, true).value;
        const mutable = source.QueryInterface(api.IMarshalMutableObject);
        t.after(() => { mutable.dispose(); source.dispose(); });
        source.dispose();
        assert.equal(api.WriteObjectReference(mutable, {
            get Size() { mutable.dispose(); return 83n; }
        }), pending);
        assert.equal(api.GetLiveObjectCount(), baseline);
        assert.equal(api.GetDestructionCount(), destroyed + 1);
    });

    test("failed argument marshalling releases temporary interface references", t => {
        const source = api.CreateMarshalObject({Size: 37n}, 0, true).value;
        const mutable = source.QueryInterface(api.IMarshalMutableObject);
        t.after(() => { mutable.dispose(); source.dispose(); });
        assert.throws(() => api.WriteObjectReference(mutable, {Size: "invalid"}), TypeError);
        assert.equal(source.GetRefCount(), 2);
        assert.equal(source.GetSize(), 37n);
    });
}
