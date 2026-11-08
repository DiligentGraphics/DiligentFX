// These cases exercise generated bindings and independent native fixture reads
// in both Node and the browser. No native layout offsets are used by the tests.
export function registerTests({test, assert, api, wasm}) {
    const allocations = new Set();
    const descriptors = new Set();
    let rootCreations = 0;
    let childCreations = 0;
    function track(createName, destroyName, pending, created = () => {}) {
        const create = wasm[createName];
        const destroy = wasm[destroyName];
        wasm[createName] = (...args) => {
            const address = create(...args) >>> 0;
            if (address) {
                assert.equal(pending.has(address), false);
                pending.add(address);
                created();
            }
            return address;
        };
        wasm[destroyName] = address => {
            assert.equal(pending.delete(address >>> 0), true, `Unpaired ${destroyName}`);
            destroy(address);
        };
    }
    track("_malloc", "_free", allocations);
    track("_binding_Diligent_MarshalNestedDesc_Create", "_binding_Diligent_MarshalNestedDesc_Destroy",
        descriptors, () => ++rootCreations);
    for (const name of ["Vector", "Transform", "Data"]) {
        track(`_binding_Diligent_MarshalNested${name}_Create`, `_binding_Diligent_MarshalNested${name}_Destroy`,
            descriptors, () => ++childCreations);
    }

    function check(name, callback) {
        test(name, t => {
            t.after(() => {
                assert.equal(allocations.size, 0, "Temporary bytes leaked");
                assert.equal(descriptors.size, 0, "Temporary descriptors leaked");
                assert.equal(childCreations, 0, "Nested fields must use the parent's initialized storage");
            });
            callback();
        });
    }
    function values(desc) {
        return Array.from({length: 12}, (_, index) => api.ReadNestedValue(desc, index));
    }
    const defaults = [0, 0, 0, 1, 1, 1, 4, 5, 6, 2, 3, 4];
    const text = "Aé水😀";

    check("nested omissions and empty objects preserve all enclosing native defaults", () => {
        for (const desc of [{}, {Transform: undefined, Alternate: undefined, Data: undefined},
            {Transform: {}, Alternate: {}, Data: {}},
            {Transform: {Position: {}, Scale: {}}, Alternate: {Position: {}, Scale: {}}},
            {Alternate: {Position: {y: undefined}, Scale: undefined}}]) {
            assert.deepEqual(values(desc), defaults);
            assert.equal(api.GetNestedTag(desc), 17);
            assert.equal(api.GetNestedName(desc), "parent default");
            assert.equal(api.GetNestedChecksum(desc), 0xffffffff);
        }
    });

    check("nested overrides preserve omitted sibling values and do not modify JavaScript input", () => {
        const desc = {Tag: 99, Transform: {Position: {x: -7.5}, Scale: {y: 8}},
            Alternate: {Position: {z: 11}, Scale: {x: 12}}};
        assert.deepEqual(values(desc), [-7.5, 0, 0, 1, 8, 1, 4, 5, 11, 12, 3, 4]);
        assert.equal(api.GetNestedTag(desc), 99);
        assert.deepEqual(desc, {Tag: 99, Transform: {Position: {x: -7.5}, Scale: {y: 8}},
            Alternate: {Position: {z: 11}, Scale: {x: 12}}});
        assert.deepEqual(values({}), defaults);
    });

    check("reusing a child object respects each containing field's native defaults", () => {
        const shared = {Position: {y: 9}, Scale: {x: -2}};
        assert.deepEqual(values({Transform: shared, Alternate: shared}), [0, 9, 0, -2, 1, 1, 4, 9, 6, -2, 3, 4]);
        assert.deepEqual(shared, {Position: {y: 9}, Scale: {x: -2}});
    });

    check("nested strings and byte spans use their own native defaults and length fields", () => {
        const bytes = new Uint8Array([99, 17, 128, 254, 88]).subarray(1, 4);
        const desc = {Data: {pBytes: bytes, Name: text}};
        assert.equal(api.GetNestedName(desc), text);
        assert.equal(api.GetNestedChecksum(desc), 1035);
        assert.equal(api.GetNestedChecksum({Data: {pBytes: bytes, ByteCount: 2}}), 273);
        assert.equal(api.GetNestedChecksum({Data: {ByteCount: 2, pBytes: bytes}}), 273);
        assert.equal(api.GetNestedChecksum({Data: {pBytes: new Uint8Array(), ByteCount: 0}}), 0);
        assert.equal(api.GetNestedChecksum({Data: {pBytes: null}}), 0xffffffff);
        assert.equal(api.GetNestedName({Data: {Name: null}}), null);
        assert.equal(api.GetNestedName({Data: {Name: ""}}), "");
        // ByteCount is three in this parent, although MarshalNestedData defaults to two.
        assert.throws(() => api.GetNestedChecksum({Data: {pBytes: bytes.subarray(0, 2)}}), RangeError);
    });

    check("invalid nested descriptors and fields are rejected before the native call", () => {
        const before = api.GetNestedCallCount();
        for (const value of [null, 0, false, "invalid", [], () => {}]) {
            assert.throws(() => api.GetNestedTag({Transform: value}), TypeError);
            assert.throws(() => api.GetNestedTag({Transform: {Position: value}}), TypeError);
        }
        assert.throws(() => api.GetNestedTag({Alternate: {Scale: {Unknown: 1}}}), /Unknown MarshalNestedVector field/);
        assert.throws(() => api.GetNestedTag({Transform: {Position: {x: "1"}}}), TypeError);
        assert.throws(() => api.GetNestedTag({Data: {Name: 42}}), TypeError);
        assert.throws(() => api.GetNestedTag({Data: {Name: text, ByteCount: -1}}), RangeError);
        assert.equal(api.GetNestedCallCount(), before);
    });

    check("nested temporary storage is released after late validation or getter failure", () => {
        const before = api.GetNestedCallCount();
        const data = {Name: text, pBytes: new Uint8Array([17, 128, 254])};
        assert.throws(() => api.GetNestedTag({Data: data, Transform: {Scale: {z: "invalid"}}}), TypeError);
        assert.throws(() => api.ReadNestedValue({Data: data}, -1), RangeError);
        const error = new Error("nested getter failed");
        assert.throws(() => api.GetNestedTag({Data: data, Transform: {get Position() { throw error; }}}), error);
        assert.equal(api.GetNestedCallCount(), before);
    });

    check("nested value structs allocate only the root descriptor", () => {
        const before = rootCreations;
        assert.equal(api.GetNestedTag({Transform: {Position: {x: 3}}, Alternate: {Scale: {z: 9}}}), 17);
        assert.equal(rootCreations - before, 1);
        assert.equal(childCreations, 0);
    });

    check("nested input storage remains valid if a property getter grows Wasm memory", () => {
        let allocation = 0;
        try {
            const desc = {
                Data: {Name: text, pBytes: new Uint8Array([17, 128, 254])},
                get Transform() {
                    const oldSize = wasm.HEAPU8.byteLength;
                    allocation = wasm._malloc(oldSize + 65536);
                    assert.ok(allocation);
                    assert.ok(wasm.HEAPU8.byteLength > oldSize, "The test must actually grow memory");
                    return {Position: {x: 12}};
                },
            };
            assert.equal(api.GetNestedName(desc), text);
        } finally {
            if (allocation) wasm._free(allocation);
        }
    });
}
