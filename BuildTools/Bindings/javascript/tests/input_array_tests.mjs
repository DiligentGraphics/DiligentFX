export function registerTests({test, assert, api, wasm}) {
    const allocations = new Set();
    const descriptors = new Set();
    const arrays = new Set();
    let arrayCreations = 0;
    let itemCreations = 0;
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
    track("_binding_Diligent_MarshalArrayDesc_Create", "_binding_Diligent_MarshalArrayDesc_Destroy", descriptors);
    track("_binding_Diligent_MarshalArrayItem_Create", "_binding_Diligent_MarshalArrayItem_Destroy", descriptors, () => ++itemCreations);
    for (const name of ["MarshalArrayItem", "MarshalArrayDesc"]) {
        track(`_binding_Diligent_${name}_ArrayCreate`, `_binding_Diligent_${name}_ArrayDestroy`, arrays, () => ++arrayCreations);
    }

    function check(name, callback) {
        test(name, t => {
            t.after(() => {
                assert.equal(allocations.size, 0, "Temporary bytes leaked");
                assert.equal(descriptors.size, 0, "Temporary descriptors leaked");
                assert.equal(arrays.size, 0, "Temporary struct arrays leaked");
                assert.equal(itemCreations, 0, "Array elements must not be allocated separately");
            });
            callback();
        });
    }

    const cases = [
        ["Int8", Int8Array, [-128, 17, 127]], ["Uint8", Uint8Array, [0, 128, 255]],
        ["Int16", Int16Array, [-32768, 17, 32767]], ["Uint16", Uint16Array, [0, 32768, 65535]],
        ["Int32", Int32Array, [-2147483648, 17, 2147483647]], ["Uint32", Uint32Array, [0, 0x80000000, 0xffffffff]],
        ["Int64", BigInt64Array, [-0x7000000000000001n, 17n, 0x7123456789abcdefn]],
        ["Uint64", BigUint64Array, [0n, 0x8000000000000000n, 0xfedcba9876543210n]],
        ["Float32", Float32Array, [-1.5, 0.25, 123.5]], ["Float64", Float64Array, [-1.25, 0.1, 123456.789]],
        ["Bool", Uint8Array, [1, 0, 1]], ["Mode", Uint8Array, [2, 241, 2]],
    ];
    for (const [name, Type, expected] of cases) {
        check(`${name} input arrays preserve values, element counts, and view offsets`, () => {
            const storage = new Type(expected.length + 2);
            storage.set(expected, 1);
            const values = storage.subarray(1, -1);
            const count = n => name === "Uint64" ? BigInt(n) : n;
            const invoke = (value, n) => api[`CheckArray${name}`](value, count(n));
            assert.equal(invoke(values, 3), true);
            assert.equal(invoke(values, 2), true);
            assert.equal(invoke(new Type(), 0), true);
            assert.throws(() => invoke(values, 4), RangeError);
            assert.throws(() => invoke(null, 0), TypeError);
            assert.throws(() => invoke([...expected], 3), TypeError);
            assert.throws(() => invoke(values.buffer, 3), TypeError);
            assert.deepEqual(Array.from(values), expected);
        });
    }

    check("signed byte spans retain Uint8Array compatibility and boolean arrays require valid bits", () => {
        assert.equal(api.CheckArrayInt8(new Uint8Array([128, 17, 127]), 3), true);
        assert.throws(() => api.CheckArrayBool(new Uint8Array([1, 2, 0]), 3), RangeError);
        assert.equal(api.CheckArrayBool(new Uint8Array([1, 0, 2]), 2), true);
    });

    check("typed array validation ignores overridden getters and rejects detached or mismatched views", () => {
        class HostileArray extends Float32Array {
            get buffer() { throw new Error("buffer getter called"); }
            get byteOffset() { throw new Error("offset getter called"); }
            get length() { throw new Error("length getter called"); }
            subarray() { throw new Error("subarray called"); }
        }
        assert.equal(api.CheckArrayFloat32(new HostileArray([-1.5, 0.25, 123.5]), 3), true);
        assert.throws(() => api.CheckArrayFloat32(new Float64Array(3), 3), TypeError);
        const detached = new Float32Array(3);
        structuredClone(detached.buffer, {transfer: [detached.buffer]});
        assert.throws(() => api.CheckArrayFloat32(detached, 0), TypeError);
        assert.throws(() => api.CheckArrayUint64(new BigUint64Array(1), 1), TypeError);
        assert.throws(() => api.CheckArrayUint64(new BigUint64Array(1), 0x100000000n), RangeError);
    });

    const text = "Aé水😀";
    function items() {
        return [{}, {Id: 42, Name: text, Transform: {Scale: {y: 3}, Position: {y: -2}},
            pValues: new Float32Array([99, 1.25, -3.5, 88]).subarray(1, 3)}];
    }
    check("struct arrays use one contiguous allocation and preserve defaults in every element", () => {
        const input = items();
        const before = arrayCreations;
        assert.equal(api.ReadArrayItem(input, 2, 1, 0), 42);
        assert.equal(arrayCreations - before, 1);
        for (const [index, field, expected] of [[0, 0, -7], [0, 1, 1], [0, 2, 1], [0, 3, 0], [0, 4, 2],
            [1, 1, 1], [1, 2, 3], [1, 3, -2], [1, 4, 2], [1, 5, 1.25], [1, 6, -3.5]]) {
            assert.equal(api.ReadArrayItem(input, 2, index, field), expected);
        }
        assert.equal(api.GetArrayItemName(input, 2, 0), "item default");
        const name = api.GetArrayItemName(input, 2, 1);
        input[1].Name = "changed";
        assert.equal(name, text);
        assert.deepEqual(input[0], {});
        assert.deepEqual(input[1].Transform, {Scale: {y: 3}, Position: {y: -2}});
    });

    check("array descriptor fields use native count defaults independently of JavaScript property order", () => {
        const input = items();
        assert.equal(api.ReadArrayDesc({pItems: input}, 1, 0), 42);
        assert.equal(api.ReadArrayDesc({pItems: input, ItemCount: 1}, 1, 0), -1000);
        assert.equal(api.ReadArrayDesc({ItemCount: 1, pItems: input}, 0, 0), -7);
        assert.equal(api.ReadArrayDesc({pItems: input, ItemCount: undefined}, 1, 0), 42);
        const samples = new Uint16Array([99, 10, 20, 30, 88]).subarray(1, 4);
        assert.equal(api.SumArraySamples({pSamples: samples}), 140n);
        assert.equal(api.SumArraySamples({pSamples: samples, SampleCount: 2n}), 50n);
        assert.equal(api.SumArraySamples({SampleCount: 2n, pSamples: samples}), 50n);
        assert.throws(() => api.ReadArrayDesc({pItems: [{}]}, 0, 0), RangeError);
    });

    check("empty, omitted, and nullable arrays preserve the native pointer/count contract", () => {
        assert.equal(api.ReadArrayItem([], 0, 0, 0), -1000);
        assert.equal(api.GetArrayItemName([], 0, 0), null);
        assert.equal(api.ReadArrayDesc({}, 0, 0), -1000);
        assert.equal(api.ReadArrayDesc({pItems: null}, 0, 0), -1000);
        assert.equal(api.ReadArrayDesc({pItems: [], ItemCount: 0}, 0, 0), -1000);
        assert.equal(api.SumArraySamples({pSamples: null}), 0xffffffffffffffffn);
        assert.equal(api.SumArraySamples({pSamples: new Uint16Array(), SampleCount: 0n}), 0xffffffffffffffffn);
    });

    check("nested struct arrays and shared JavaScript elements keep independent native defaults", () => {
        const shared = {Name: text, Transform: {Scale: {x: 5}}};
        assert.equal(api.ReadArrayItem([shared, shared], 2, 1, 1), 5);
        const groups = [{pItems: [shared, {Id: 19}]}, {ItemCount: 1, pItems: [{Id: 73}]}];
        assert.equal(api.ReadArrayGroup(groups, 2, 0, 1), 19);
        assert.equal(api.ReadArrayGroup(groups, 2, 1, 0), 73);
        assert.deepEqual(shared, {Name: text, Transform: {Scale: {x: 5}}});
    });

    check("invalid elements, late arguments, and throwing getters release the complete input tree", () => {
        const before = api.GetArrayCallCount();
        for (const value of [null, undefined, 3, [], {Unknown: true}, {Id: "invalid"}]) {
            assert.throws(() => api.ReadArrayItem([{Name: text}, value], 2, 0, 0), TypeError);
        }
        assert.throws(() => api.ReadArrayItem(new Array(2), 2, 0, 0), TypeError);
        assert.throws(() => api.ReadArrayItem(items(), 2, "invalid", 0), TypeError);
        assert.throws(() => api.ReadArrayItem([{pValues: new Float32Array(1)}], 1, 0, 0), RangeError);
        const error = new Error("array element getter failed");
        assert.throws(() => api.ReadArrayItem([{Name: text}, {get Name() { throw error; }}], 2, 0, 0), error);
        assert.equal(api.GetArrayCallCount(), before);
    });

    check("oversized struct counts fail before native allocation without allocating huge test buffers", () => {
        const max = wasm._binding_Diligent_MarshalArrayItem_ArrayMaxCount() >>> 0;
        // Only fake length metadata is provided; neither JS nor native allocates
        // the oversized storage. The allocator trap makes a missing guard safe.
        const input = new Proxy([], {get(_target, key) {
            if (key === "length") return max + 1;
            throw new Error("Oversized array must not be traversed");
        }});
        const create = wasm._binding_Diligent_MarshalArrayItem_ArrayCreate;
        let attempted = false;
        wasm._binding_Diligent_MarshalArrayItem_ArrayCreate = () => { attempted = true; throw new Error("Unexpected allocation"); };
        try {
            assert.throws(() => api.ReadArrayItem(input, max + 1, 0, 0), RangeError);
            assert.equal(attempted, false);
        } finally {
            wasm._binding_Diligent_MarshalArrayItem_ArrayCreate = create;
        }
    });

    check("allocation failure does not enter the native API and cleans preceding inputs", () => {
        const before = api.GetArrayCallCount();
        for (const name of ["_malloc", "_binding_Diligent_MarshalArrayItem_ArrayCreate"]) {
            const allocate = wasm[name];
            wasm[name] = () => 0;
            try {
                assert.throws(() => api.ReadArrayItem([{Name: text}], 1, 0, 0), /Could not allocate/);
            } finally {
                wasm[name] = allocate;
            }
        }
        assert.equal(api.GetArrayCallCount(), before);
    });

    check("struct array elements and their strings survive Wasm growth during marshalling", () => {
        let allocation = 0;
        try {
            const input = [{Name: text}, {get Name() {
                const oldSize = wasm.HEAPU8.byteLength;
                allocation = wasm._malloc(oldSize + 65536);
                assert.ok(allocation);
                assert.ok(wasm.HEAPU8.byteLength > oldSize);
                return "second";
            }}];
            assert.equal(api.GetArrayItemName(input, 2, 0), text);
        } finally {
            if (allocation) wasm._free(allocation);
        }
    });
}
