export function registerTests({test, assert, api, constants, wasm}) {
    const {MARSHAL_STATUS_OK: OK, MARSHAL_STATUS_PENDING: PENDING} = constants.MARSHAL_STATUS;

    function object(t, mutable = false) {
        const result = api.CreateMarshalObject({Size: 17n}, OK, mutable);
        assert.equal(result.status, OK);
        const value = result.value;
        // Keep cleanup independent of the prototype changes made by each test.
        const dispose = value.dispose.bind(value);
        t.after(dispose);
        return value;
    }

    function bytes(t) {
        const result = api.CreateMarshalBytes({ByteCount: 4n});
        assert.equal(result.status, OK);
        const value = result.value;
        const dispose = value.dispose.bind(value);
        t.after(dispose);
        return value;
    }

    function rejectsBeforeDispatch(name, callback) {
        const exported = wasm[name];
        assert.equal(typeof exported, "function");
        // A regression must fail the assertion without executing an incompatible
        // C++ virtual call, which could trap or corrupt this module's state.
        wasm[name] = () => { throw new Error("Incompatible interface reached native dispatch"); };
        try { assert.throws(callback, TypeError); }
        finally { wasm[name] = exported; }
    }

    test("changing a wrapper prototype cannot change its native interface", t => {
        const value = object(t);
        Object.setPrototypeOf(value, api.IMarshalBytes.prototype);
        assert.equal(value instanceof api.IMarshalBytes, true);
        rejectsBeforeDispatch("_binding_Diligent_IMarshalBytes_Size", () => value.Size());

        // The genuine interface remains usable through a borrowed method or an
        // argument even when its JavaScript prototype no longer identifies it.
        assert.equal(api.IMarshalObject.prototype.GetSize.call(value), 17n);
        assert.equal(api.GetObjectSize(value), 17n);
        assert.equal(api.IMarshalObject.prototype.GetRefCount.call(value), 1);
        value.dispose();
        assert.throws(() => api.IMarshalObject.prototype.GetSize.call(value), /disposed/);
    });

    test("altered prototypes cannot bypass interface argument, field, or array validation", t => {
        const valid = object(t);
        const invalid = bytes(t);
        Object.setPrototypeOf(invalid, api.IMarshalObject.prototype);
        assert.equal(invalid instanceof api.IMarshalObject, true);
        rejectsBeforeDispatch("_binding_Diligent_IMarshalObject_GetSize", () => invalid.GetSize());
        rejectsBeforeDispatch("_binding_Diligent_GetObjectSize", () => api.GetObjectSize(invalid));
        rejectsBeforeDispatch("_binding_Diligent_SumInterfaces", () => api.SumInterfaces({pObject: invalid}));
        rejectsBeforeDispatch("_binding_Diligent_SumInterfaceArray", () => api.SumInterfaceArray([valid, invalid], 2));
        assert.equal(valid.GetRefCount(), 1);
        assert.equal(api.IMarshalBytes.prototype.Size.call(invalid), 4n);
    });

    test("derived access requires QueryInterface and preserves genuine inherited methods", t => {
        const base = object(t, true);
        Object.setPrototypeOf(base, api.IMarshalMutableObject.prototype);
        rejectsBeforeDispatch("_binding_Diligent_IMarshalMutableObject_SetSize", () => base.SetSize(29n));

        const derived = base.QueryInterface(api.IMarshalMutableObject);
        assert.ok(derived);
        t.after(() => derived.dispose());
        assert.equal(derived.SetSize(29n), PENDING);
        assert.equal(derived.GetSize(), 29n);
        assert.equal(api.GetObjectSize(derived), 29n);
        assert.equal(base.GetRefCount(), 2);
        const queriedBase = derived.QueryInterface(api.IMarshalObject);
        assert.ok(queriedBase);
        t.after(() => queriedBase.dispose());
        assert.equal(queriedBase.GetSize(), 29n);
        rejectsBeforeDispatch("_binding_Diligent_IMarshalMutableObject_SetSize",
            () => api.IMarshalMutableObject.prototype.SetSize.call(queriedBase, 31n));

        const immutable = object(t);
        Object.setPrototypeOf(immutable, api.IMarshalMutableObject.prototype);
        assert.equal(immutable.QueryInterface(api.IMarshalMutableObject), null);
        assert.equal(immutable.GetRefCount(), 1);
    });

    test("native adoption does not invoke an altered interface constructor chain", t => {
        const type = api.IMarshalObject;
        const originalBase = Object.getPrototypeOf(type);
        t.after(() => Object.setPrototypeOf(type, originalBase));
        Object.setPrototypeOf(type, function() {
            throw new Error("Native adoption must not expose private state to a constructor");
        });
        const value = object(t);
        assert.equal(value.GetSize(), 17n);
        assert.equal(api.GetObjectSize(value), 17n);
        assert.throws(() => api.GetObjectSize(Object.create(type.prototype)), TypeError);
    });

    test("interface prototype ancestry and Symbol.hasInstance cannot grant compatibility", t => {
        const value = bytes(t);
        const prototype = api.IMarshalBytes.prototype;
        const originalBase = Object.getPrototypeOf(prototype);
        t.after(() => Object.setPrototypeOf(prototype, originalBase));
        Object.setPrototypeOf(prototype, api.IMarshalObject.prototype);
        assert.equal(value instanceof api.IMarshalObject, true);
        rejectsBeforeDispatch("_binding_Diligent_GetObjectSize", () => api.GetObjectSize(value));
        Object.setPrototypeOf(prototype, originalBase);

        t.after(() => { delete api.IMarshalObject[Symbol.hasInstance]; });
        Object.defineProperty(api.IMarshalObject, Symbol.hasInstance, {
            configurable: true,
            value: () => { throw new Error("Compatibility must not invoke Symbol.hasInstance"); },
        });
        rejectsBeforeDispatch("_binding_Diligent_GetObjectSize", () => api.GetObjectSize(value));
        assert.equal(api.GetObjectSize(object(t)), 17n);
    });
}
