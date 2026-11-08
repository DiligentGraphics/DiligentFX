export function registerTests({test, assert, api, wasm}) {
    const allocations = new Set();
    const owners = [];
    let creations = 0;
    function track(createName, destroyName) {
        const create = wasm[createName];
        const destroy = wasm[destroyName];
        wasm[createName] = (...args) => {
            const address = create(...args) >>> 0;
            if (address) {
                assert.equal(allocations.has(address), false);
                allocations.add(address);
                ++creations;
            }
            return address;
        };
        wasm[destroyName] = address => {
            assert.equal(allocations.delete(address >>> 0), true, `Unpaired ${destroyName}`);
            destroy(address);
        };
    }
    track("_malloc", "_free");
    track("_binding_Diligent_MarshalArrayRoundTrip_Create", "_binding_Diligent_MarshalArrayRoundTrip_Destroy");
    track("_binding_Diligent_MarshalOutputArrayItem_ArrayCreate", "_binding_Diligent_MarshalOutputArrayItem_ArrayDestroy");

    function check(name, callback) {
        test(name, t => {
            t.after(() => {
                for (const owner of owners.splice(0)) owner.dispose();
                assert.equal(allocations.size, 0, "Temporary native input storage leaked");
                assert.equal(api.GetLiveArraySnapshotCount(), 0, "Array snapshots retained a native owner");
            });
            callback();
        });
    }
    function object() {
        const {status, value} = api.CreateMarshalArraySnapshots();
        assert.equal(status, api.MARSHAL_STATUS.MARSHAL_STATUS_OK);
        assert.ok(value);
        owners.push(value);
        return value;
    }

    // Independently authored native values ensure these are output tests, not
    // input/output round trips whose conversion mistakes could cancel out.
    const cases = [
        ["Int8", Int8Array, [-128, 17, 127]], ["Uint8", Uint8Array, [0, 128, 255]],
        ["Int16", Int16Array, [-32768, 17, 32767]], ["Uint16", Uint16Array, [0, 32768, 65535]],
        ["Int32", Int32Array, [-2147483648, 17, 2147483647]], ["Uint32", Uint32Array, [0, 0x80000000, 0xffffffff]],
        ["Int64", BigInt64Array, [-0x7000000000000001n, 17n, 0x7123456789abcdefn]],
        ["Uint64", BigUint64Array, [0n, 0x8000000000000000n, 0xfedcba9876543210n]],
        ["Float32", Float32Array, [-1.5, 0.25, 123.5]], ["Float64", Float64Array, [-1.25, 0.1, 123456.789]],
        ["Bool", Uint8Array, [1, 0, 1]], ["MARSHAL_MODE", Uint8Array, [2, 241, 2]],
        ["Char", Uint8Array, [65, 0, 255]],
    ];
    for (const [name, Type, expected] of cases) {
        check(`${name} output arrays copy native values into independent typed arrays`, () => {
            const source = object();
            const first = source.GetDesc()[`p${name}`];
            const second = source.GetDesc()[`p${name}`];
            assert.equal(first.constructor, Type);
            assert.deepEqual(Array.from(first), expected);
            assert.equal(first.length, 3);
            assert.equal(first.buffer.byteLength, 3 * Type.BYTES_PER_ELEMENT);
            assert.ok(first.buffer !== wasm.HEAPU8.buffer && first.buffer !== second.buffer);
            first[0] = typeof first[0] === "bigint" ? 5n : 5;
            assert.deepEqual(Array.from(second), expected);
            assert.deepEqual(Array.from(source.GetDesc()[`p${name}`]), expected);
            source.dispose();
            assert.deepEqual(Array.from(second), expected);
        });
    }

    const text = "Aé水😀";
    function transform(y = 0) {
        return {Position: {x: 0, y, z: 0}, Scale: {x: 1, y: 1, z: 1}};
    }
    function firstItem() {
        return {Id: 41, Label: text, Transform: transform(-2.5), pSamples: new Float32Array([0.25, 123.5]), SampleCount: 2};
    }
    function secondItem() {
        return {Id: 0xfedcba98, Label: null, Transform: transform(), pSamples: null, SampleCount: 0};
    }
    // Browser assertions intentionally stay small; compare typed arrays by
    // constructor/value and compare the surrounding plain fields independently.
    function assertItem(actual, expected) {
        assert.deepEqual({...actual, pSamples: null}, {...expected, pSamples: null});
        if (expected.pSamples === null) assert.equal(actual.pSamples, null);
        else {
            assert.equal(actual.pSamples.constructor, Float32Array);
            assert.deepEqual(Array.from(actual.pSamples), Array.from(expected.pSamples));
        }
    }

    check("nested struct arrays copy aliases, strings, value structs, and numeric spans", () => {
        const desc = object().GetDesc();
        assert.equal(desc.Count, 3);
        assert.equal(desc.WideCount, 3n);
        assert.equal(desc.GroupCount, 2);
        assert.equal(desc.pGroups.length, 2);
        assert.equal(desc.pGroups[0].ItemCount, 2);
        assert.equal(desc.pGroups[0].pItems.length, 2);
        assertItem(desc.pGroups[0].pItems[0], firstItem());
        assertItem(desc.pGroups[0].pItems[1], secondItem());
        assert.equal(desc.pGroups[1].ItemCount, 1);
        assert.equal(desc.pGroups[1].pItems.length, 1);
        assertItem(desc.pGroups[1].pItems[0], secondItem());
        assert.ok(desc.pGroups[0].pItems[1] !== desc.pGroups[1].pItems[0]);
        assert.equal(Object.getPrototypeOf(desc.pGroups[0].pItems[0]), Object.prototype);
    });

    check("array snapshots survive native mutation, disposal, and edits to other snapshots", () => {
        const source = object();
        const first = source.GetDesc();
        source.Update();
        const second = source.GetDesc();
        assert.equal(second.pFloat32[1], 64);
        assert.equal(second.pGroups[0].pItems[0].Id, 99);
        assert.equal(second.pGroups[0].pItems[0].Label, "changed label");
        assert.equal(second.pGroups[0].pItems[0].pSamples[0], 64);
        second.pGroups[0].pItems[0].Transform.Scale.y = 700;
        source.dispose();
        assertItem(first.pGroups[0].pItems[0], firstItem());
        assert.deepEqual(Array.from(first.pFloat32), [-1.5, 0.25, 123.5]);
        assert.equal(api.GetLiveArraySnapshotCount(), 0);
    });

    check("direct pointer results use explicit element counts for scalar and struct arrays", () => {
        const source = object();
        assert.deepEqual(Array.from(source.GetValues(2)), [-1.5, 0.25]);
        assert.equal(source.GetValues(0).length, 0);
        const items = source.GetItems(1);
        assert.equal(items.length, 1);
        assertItem(items[0], firstItem());
        assert.deepEqual(source.GetItems(0), []);
        assert.deepEqual(Array.from(api.GetStaticOutputArray(2n)), [-1.25, 0.1]);
        assert.equal(api.GetStaticOutputArray(0n), null);
        assert.throws(() => api.GetStaticOutputArray(1), TypeError);
        assert.throws(() => source.GetValues(4), RangeError);
        assert.throws(() => source.GetItems(3), RangeError);
        assert.throws(() => api.GetStaticOutputArray(0x100000000n), RangeError);
        assert.throws(() => source.GetValues(-1), RangeError);
        source.dispose();
        assertItem(items[0], firstItem());
    });

    check("output arrays copy temporary input trees before their storage is released", () => {
        const input = {Name: text, pItems: [{Label: text, pSamples: new Float32Array([1.25, -3.5]), SampleCount: 2}, {}],
            ItemCount: 2, pValues: new Float64Array([2.5, -4.75]), ValueCount: 2n};
        const result = api.EchoArrayDescriptor(input, false);
        assert.equal(allocations.size, 0);
        assertItem(result.pItems[0], {Id: 7, Label: text, Transform: transform(), pSamples: new Float32Array([1.25, -3.5]), SampleCount: 2});
        assertItem(result.pItems[1], {Id: 7, Label: "item default", Transform: transform(), pSamples: null, SampleCount: 0});
        input.pItems[0].pSamples[0] = 99;
        input.pValues[0] = 99;
        api.EchoArrayDescriptor({pValues: new Float64Array([100]), ValueCount: 1n}, false);
        assert.deepEqual(Array.from(result.pValues), [2.5, -4.75]);
        assert.equal(result.pItems[0].pSamples[0], 1.25);
        assert.equal(result.Name, text);
    });

    check("empty pointer/count fields preserve nullable and non-null empty spans", () => {
        const empty = api.EchoArrayDescriptor({}, false);
        assert.equal(empty.pItems, null);
        assert.equal(empty.pValues, null);
        assert.equal(empty.ItemCount, 0);
        assert.equal(empty.ValueCount, 0n);
        assert.equal(api.GetInvalidOutputArray(9).pValues, null);
        const nonnull = api.GetInvalidOutputArray(10).pValues;
        assert.equal(nonnull.constructor, Float64Array);
        assert.equal(nonnull.length, 0);
    });

    check("invalid scalar and struct output ranges fail without large allocations or reads", () => {
        for (let scenario = 0; scenario <= 8; ++scenario) {
            assert.throws(() => api.GetInvalidOutputArray(scenario), RangeError);
        }
        assert.deepEqual(Array.from(api.GetStaticOutputArray(3n)), [-1.25, 0.1, 123456.789]);
    });

    check("output conversion failures release every temporary input allocation", () => {
        assert.throws(() => api.EchoArrayDescriptor({Name: text, pItems: [{Label: text}], ItemCount: 1,
            pValues: new Float64Array([2.5]), ValueCount: 1n}, true), TypeError);
        assert.equal(allocations.size, 0);
        assert.throws(() => api.EchoArrayDescriptor({pItems: null, ItemCount: 1}, false), RangeError);
        assert.equal(allocations.size, 0);
        assert.equal(api.EchoArrayDescriptor({}, false).Name, "round trip");
    });

    check("output arrays use current Wasm memory after native heap growth", () => {
        const source = object();
        const size = wasm.HEAPU8.byteLength;
        const result = source.GetDescAfterGrowingMemory();
        assert.ok(wasm.HEAPU8.byteLength > size);
        source.dispose();
        assert.deepEqual(Array.from(result.pUint64), [0n, 0x8000000000000000n, 0xfedcba9876543210n]);
        assertItem(result.pGroups[0].pItems[0], firstItem());
    });

    check("borrowed array getters allocate only JavaScript snapshots", () => {
        const source = object();
        const before = creations;
        source.GetDesc();
        source.GetValues(3);
        source.GetItems(2);
        api.GetStaticOutputArray(3n);
        assert.equal(creations, before);
        assert.equal(api.GetLiveArraySnapshotCount(), 1);
    });
}
