export function registerTests({test, assert, api, wasm}) {
    const allocations = new Set();
    let descriptorCreations = 0;
    let childCreations = 0;
    let byteCreations = 0;
    function track(createName, destroyName, created) {
        const create = wasm[createName];
        const destroy = wasm[destroyName];
        wasm[createName] = (...args) => {
            const address = create(...args) >>> 0;
            if (address) {
                assert.equal(allocations.has(address), false);
                allocations.add(address);
                created();
            }
            return address;
        };
        wasm[destroyName] = address => {
            assert.equal(allocations.delete(address >>> 0), true, `Unpaired ${destroyName}`);
            destroy(address);
        };
    }
    track("_malloc", "_free", () => ++byteCreations);
    for (const name of ["MarshalFixedArrays", "MarshalFixedMatrix"]) {
        track(`_binding_Diligent_${name}_Create`, `_binding_Diligent_${name}_Destroy`, () => ++descriptorCreations);
    }
    track("_binding_Diligent_MarshalFixedChild_Create", "_binding_Diligent_MarshalFixedChild_Destroy", () => ++childCreations);
    function check(name, callback) {
        test(name, t => {
            api.ResetFixedArrays();
            t.after(() => {
                api.ResetFixedArrays();
                assert.equal(allocations.size, 0, "Temporary native storage leaked");
                assert.equal(childCreations, 0, "Fixed-array elements must reuse parent storage");
            });
            callback();
        });
    }

    const cases = [
        ["Int8", Int8Array, [-128, 17, 127], [-100, -2, 111]],
        ["Uint8", Uint8Array, [0, 128, 255], [42, 200, 250]],
        ["Int16", Int16Array, [-32768, 17, 32767], [-30000, -20, 30000]],
        ["Uint16", Uint16Array, [0, 32768, 65535], [12, 40000, 60000]],
        ["Int32", Int32Array, [-2147483648, 17, 2147483647], [-2000000000, -20, 2000000000]],
        ["Uint32", Uint32Array, [0, 0x80000000, 0xffffffff], [13, 0x81234567, 0xfedcba98]],
        ["Int64", BigInt64Array, [-0x7000000000000001n, 17n, 0x7123456789abcdefn], [-0x6123456789abcdefn, -30n, 0x6123456789abcdefn]],
        ["Uint64", BigUint64Array, [0n, 0x8000000000000000n, 0xfedcba9876543210n], [25n, 0x8123456789abcdefn, 0xeeeeeeeeeeeeeeeen]],
        ["Float32", Float32Array, [-1.5, 0.25, 123.5], [-2.5, 1.75, 456.25]],
        ["Float64", Float64Array, [-1.25, 0.1, 123456.789], [-2.25, 0.2, 987654.321]],
        ["Bool", Uint8Array, [1, 0, 1], [0, 1, 0]],
        ["MARSHAL_MODE", Uint8Array, [2, 241, 2], [241, 2, 241]],
        ["Char", Uint8Array, [65, 0, 255], [66, 128, 0]],
    ];
    for (const [index, [name, Type, defaults, values]] of cases.entries()) {
        check(`${name} fixed arrays preserve native defaults, input values, and independent output copies`, () => {
            const field = `Values${name}`;
            const original = api.GetFixedArrays(false)[field];
            assert.equal(original.constructor, Type);
            assert.deepEqual(Array.from(original), defaults);
            const storage = new Type(5);
            storage.set(values, 1);
            const view = storage.subarray(1, 4);
            const bytesBefore = byteCreations;
            const descriptorsBefore = descriptorCreations;
            assert.equal(api.CheckFixedInputs({[field]: view}, index), true);
            assert.equal(descriptorCreations - descriptorsBefore, 1);
            assert.equal(byteCreations, bytesBefore, "Embedded arrays need no intermediate native allocation");
            const result = api.EchoFixedArrays({[field]: view}, false)[field];
            assert.equal(result.constructor, Type);
            assert.deepEqual(Array.from(result), values);
            assert.ok(result.buffer !== view.buffer && result.buffer !== wasm.HEAPU8.buffer);
            assert.deepEqual(Array.from(view), values);
            result[0] = typeof result[0] === "bigint" ? 0n : 0;
            assert.deepEqual(Array.from(api.GetFixedArrays(false)[field]), defaults);
            assert.throws(() => api.CheckFixedInputs({[field]: new Type(2)}, index), RangeError);
            assert.throws(() => api.CheckFixedInputs({[field]: new Type(4)}, index), RangeError);
            assert.throws(() => api.CheckFixedInputs({[field]: null}, index), TypeError);
            assert.throws(() => api.CheckFixedInputs({[field]: [...values]}, index), TypeError);
        });
    }

    const identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
    const text = "Aé水😀";
    check("flat matrix fields preserve identity defaults and row-major native indexing", () => {
        assert.deepEqual(Array.from(api.EchoFixedMatrix({}).Data), identity);
        assert.deepEqual(Array.from(api.EchoFixedMatrix({Data: undefined}).Data), identity);
        const data = Float32Array.from({length: 16}, (_, i) => i + 0.5);
        for (let row = 0; row < 4; ++row) {
            for (let column = 0; column < 4; ++column) {
                assert.equal(api.ReadFixedMatrix({Data: data}, row, column), row * 4 + column + 0.5);
            }
        }
        assert.deepEqual(Array.from(api.EchoFixedMatrix({Data: data}).Data), Array.from(data));
        assert.throws(() => api.EchoFixedMatrix({Data: new Float64Array(16)}), TypeError);
    });

    check("multidimensional numeric arrays preserve dimensions, typedefs, and native row strides", () => {
        const rows = [new Float32Array([11, 12, 13, 14]), new Float32Array([21, 22, 23, 24])];
        for (let row = 0; row < 2; ++row) {
            for (let column = 0; column < 4; ++column) {
                assert.equal(api.ReadFixedRow({Rows: rows}, row, column), rows[row][column]);
            }
        }
        const cube = [[new Float32Array([11, 12]), new Float32Array([13, 14])],
            [new Float32Array([21, 22]), new Float32Array([23, 24])]];
        for (let plane = 0; plane < 2; ++plane) {
            for (let row = 0; row < 2; ++row) {
                for (let column = 0; column < 2; ++column) {
                    assert.equal(api.ReadFixedCube({Cube: cube}, plane, row, column), cube[plane][row][column]);
                }
            }
        }
        const result = api.EchoFixedArrays({Rows: rows, Cube: cube}, false);
        assert.deepEqual(result.Rows.map(row => Array.from(row)), [[11, 12, 13, 14], [21, 22, 23, 24]]);
        assert.deepEqual(result.Cube.map(plane => plane.map(row => Array.from(row))), [[[11, 12], [13, 14]], [[21, 22], [23, 24]]]);
        assert.equal(result.Rows[0].constructor, Float32Array);
        assert.equal(result.Cube[1][1].constructor, Float32Array);
        assert.throws(() => api.EchoFixedArrays({Rows: [rows[0]]}, false), RangeError);
        assert.throws(() => api.EchoFixedArrays({Rows: [rows[0], new Float32Array(3)]}, false), RangeError);
        assert.throws(() => api.EchoFixedArrays({Rows: new Float32Array(8)}, false), TypeError);
        assert.throws(() => api.EchoFixedArrays({Cube: [cube[0], [cube[1][0]]]}, false), RangeError);
    });

    check("struct array elements preserve parent initializers and only override supplied fields", () => {
        const input = {Children: [{}, {Id: 73, Samples: new Float32Array([8, 9])}]};
        assert.equal(api.ReadFixedChild(input, 0), 11);
        assert.equal(api.ReadFixedChild(input, 1), 73);
        assert.equal(api.ReadFixedSample(input, 0, 1), 4);
        assert.equal(api.ReadFixedSample(input, 1, 0), 8);
        const result = api.EchoFixedArrays(input, false);
        assert.equal(result.Children[0].Label, "first");
        assert.equal(result.Children[1].Label, "second");
        assert.deepEqual(Array.from(result.Children[0].Samples), [3, 4]);
        assert.deepEqual(input.Children[0], {});
        const unchanged = api.EchoFixedArrays({Children: undefined}, false);
        assert.equal(unchanged.Children[0].Id, 11);
        assert.equal(unchanged.Children[1].Id, 22);
    });

    check("nested struct arrays marshal strings, dynamic spans, and embedded fixed arrays", () => {
        const input = {Children: [{Label: text, pExtra: new Int16Array([-2, 30000]), ExtraCount: 2}, {}],
            Grid: [[{Id: 41}, {}], [{}, {Id: 99, Label: text}]], Matrices: [{}, {}]};
        const result = api.EchoFixedArrays(input, false);
        assert.equal(allocations.size, 0);
        assert.equal(result.Children[0].Label, text);
        assert.deepEqual(Array.from(result.Children[0].pExtra), [-2, 30000]);
        assert.equal(result.Grid[0][0].Id, 41);
        assert.equal(result.Grid[1][1].Id, 99);
        assert.equal(result.Grid[1][1].Label, text);
        assert.deepEqual(Array.from(result.Grid[0][1].Samples), [1, 2]);
        assert.deepEqual(Array.from(result.Matrices[0].Data), identity);
        assert.deepEqual(Array.from(result.Matrices[1].Data), identity);
        assert.ok(result.Matrices[0].Data.buffer !== result.Matrices[1].Data.buffer);
        input.Children[0].pExtra[0] = 99;
        api.EchoFixedArrays({Children: [{Label: "reuse"}, {}]}, false);
        assert.equal(result.Children[0].Label, text);
        assert.equal(result.Children[0].pExtra[0], -2);
    });

    check("fixed-array snapshots remain independent after native mutation and memory growth", () => {
        const first = api.GetFixedArrays(false);
        api.UpdateFixedArrays();
        const size = wasm.HEAPU8.byteLength;
        const second = api.GetFixedArrays(true);
        assert.ok(wasm.HEAPU8.byteLength > size);
        assert.equal(second.ValuesFloat32[0], 10);
        assert.equal(second.Rows[1][2], 20);
        assert.equal(second.Children[0].Id, 30);
        assert.equal(second.Children[0].Samples[1], 40);
        assert.equal(second.Grid[1][0].Id, 50);
        assert.equal(second.Matrices[1].Data[15], 60);
        assert.equal(first.ValuesFloat32[0], -1.5);
        assert.equal(first.Rows[1][2], 7);
        assert.equal(first.Children[0].Id, 11);
        assert.equal(first.Children[0].Label, "first");
        assert.equal(first.Matrices[1].Data[15], 1);
        second.Rows[0][0] = 99;
        assert.equal(api.GetFixedArrays(false).Rows[0][0], 1);
    });

    check("const fixed arrays and const-only element structs support output snapshots", () => {
        const result = api.GetFixedReadOnlyArrays();
        assert.equal(result.Values.constructor, Uint16Array);
        assert.deepEqual(Array.from(result.Values), [10, 20, 30]);
        assert.deepEqual(result.Items, [{Value: 5}, {Value: 9}]);
        result.Items[0].Value = 100;
        result.Values[1] = 99;
        assert.deepEqual(api.GetFixedReadOnlyArrays().Items, [{Value: 5}, {Value: 9}]);
        assert.deepEqual(Array.from(api.GetFixedReadOnlyArrays().Values), [10, 20, 30]);
    });

    check("invalid dimensions, elements, and later arguments clean all temporary storage", () => {
        const before = api.GetFixedCallCount();
        for (const children of [[], [{}], [{}, {}, {}]]) {
            assert.throws(() => api.ReadFixedChild({Children: children}, 0), RangeError);
        }
        for (const child of [null, undefined, 7, [], {Unknown: true}]) {
            assert.throws(() => api.ReadFixedChild({Children: [{Label: text}, child]}, 0), TypeError);
        }
        assert.throws(() => api.ReadFixedChild({Children: new Array(2)}, 0), TypeError);
        assert.throws(() => api.ReadFixedRow({Children: [{Label: text}, {}]}, "invalid", 0), TypeError);
        assert.throws(() => api.ReadFixedChild({Children: [{Samples: new Float32Array(1)}, {}]}, 0), RangeError);
        assert.throws(() => api.CheckFixedInputs({ValuesBool: new Uint8Array([1, 2, 0])}, 10), RangeError);
        const error = new Error("fixed element getter failed");
        assert.throws(() => api.ReadFixedChild({Children: [{Label: text, pExtra: new Int16Array([1]), ExtraCount: 1},
            {get Label() { throw error; }}]}, 0), error);
        assert.equal(api.GetFixedCallCount(), before);
    });

    check("typed fixed arrays reject detached storage and ignore overridden view accessors", () => {
        class HostileArray extends Float32Array {
            get length() { throw new Error("length getter called"); }
            get buffer() { throw new Error("buffer getter called"); }
            subarray() { throw new Error("subarray called"); }
        }
        assert.equal(api.ReadFixedMatrix({Data: new HostileArray(identity)}, 2, 2), 1);
        const detached = new Float32Array(16);
        structuredClone(detached.buffer, {transfer: [detached.buffer]});
        assert.throws(() => api.EchoFixedMatrix({Data: detached}), TypeError);
        assert.equal(api.CheckFixedInputs({ValuesInt8: new Uint8Array([156, 254, 111])}, 0), true);
    });

    check("input getters can grow memory while writing embedded array elements", () => {
        let allocation = 0;
        try {
            const input = {Children: [{Label: text}, {get Samples() {
                const oldSize = wasm.HEAPU8.byteLength;
                allocation = wasm._malloc(oldSize + 65536);
                assert.ok(allocation);
                assert.ok(wasm.HEAPU8.byteLength > oldSize);
                return new Float32Array([8, 9]);
            }}]};
            const result = api.EchoFixedArrays(input, false);
            assert.equal(result.Children[0].Label, text);
            assert.deepEqual(Array.from(result.Children[1].Samples), [8, 9]);
        } finally {
            if (allocation) wasm._free(allocation);
        }
    });

    check("output conversion failures release strings and dynamic arrays nested in fixed elements", () => {
        assert.throws(() => api.EchoFixedArrays({Children: [{Label: text, pExtra: new Int16Array([1]), ExtraCount: 1}, {}]}, true), TypeError);
        assert.equal(allocations.size, 0);
        assert.equal(api.EchoFixedArrays({}, false).Children[0].Label, "first");
    });

    check("fixed-array getters use no native allocation", () => {
        const before = descriptorCreations + childCreations + byteCreations;
        api.GetFixedArrays(false);
        api.GetFixedReadOnlyArrays();
        assert.equal(descriptorCreations + childCreations + byteCreations, before);
    });
}
