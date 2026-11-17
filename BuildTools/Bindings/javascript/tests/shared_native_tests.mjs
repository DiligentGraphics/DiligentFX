// Both Java and JavaScript exercise these declarations from tests/native.
export function registerTests({test, assert, api, constants}) {
    test("shared scalar and string fixtures preserve values and reject invalid inputs before native calls", () => {
        const calls = api.GetCallCount();
        assert.throws(() => api.EchoUint8(256), RangeError);
        assert.equal(api.GetCallCount(), calls);
        assert.equal(api.EchoUint8(255), 255);
        assert.equal(api.GetCallCount(), calls + 1);
        assert.equal(api.EchoTestMode(constants.TEST_MODE.TEST_MODE_FIRST), 0);
        assert.equal(api.EchoTestStatus(constants.TEST_STATUS.TEST_STATUS_FAILED), -1);
        const text = "Aé😀";
        assert.equal(api.CheckUTF8(text), true);
        assert.equal(api.TextByteCount(text), 7);
        assert.equal(api.EchoText(text), text);
        assert.equal(api.EchoText(""), "");
        assert.equal(api.EchoOptionalText(null), null);
        assert.throws(() => api.EchoText(null), TypeError);
        assert.throws(() => api.GetInvalidText(), TypeError);
    });
    test("shared nested descriptors preserve native defaults and independent snapshots", () => {
        assert.equal(api.CheckNestedDefaultDesc({}), true);
        const source = {Name: "material 😀", Label: "label", Position: {X: -9.25, Y: 13.5}};
        const copy = api.EchoOptionalDesc(source);
        assert.equal(copy.Name, source.Name);
        assert.equal(copy.Label, source.Label);
        assert.equal(copy.Position.X, -9.25);
        assert.equal(copy.Position.Y, 13.5);
        source.Position.X = 0;
        assert.equal(copy.Position.X, -9.25);
        assert.equal(api.EchoOptionalDesc(null), null);
        const name = "nested name ".repeat(64) + "😀";
        assert.equal(api.ReadRequiredName({Inner: {Name: name}}), name);
        assert.throws(() => api.ReadRequiredName({Inner: {Name: null}}), TypeError);
    });
    test("shared counted arrays verify sums, typed output copies, and capacity handling", () => {
        const values = new Int32Array([-2147483648, 17, 2147483647]);
        assert.equal(api.SumValues(values, 3), 16n);
        assert.equal(api.SumBytes(new Uint8Array([0, 128, 255]), 3), 383n);
        const copy = api.EchoValues(values, 3);
        assert.deepEqual(Array.from(copy), Array.from(values));
        values[0] = 0;
        assert.equal(copy[0], -2147483648);
        const wide = new BigUint64Array([0n, 0xffffffffffffffffn, 0xfedcba9876543210n]);
        assert.deepEqual(Array.from(api.EchoWideValues(wide, 3)), Array.from(wide));
        const result = api.FillValues(2);
        assert.equal(result.status, constants.TEST_STATUS.TEST_STATUS_PENDING);
        assert.deepEqual(Array.from(result.value.pValues), [65535, 65534]);
        assert.equal(result.value.Written, 2);
        assert.throws(() => api.SumValues(new Int32Array(1), 2), RangeError);
    });
    test("shared fixed-array descriptors preserve scalar arrays, nested records, and string ownership", () => {
        const matrix = new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,42.25,0,1]);
        const source = {
            Matrix: matrix,
            Rows: [new Float32Array([1,2,3,4]), new Float32Array([5,6,7,-7.5])],
            Cube: [[new Int32Array([1,2]), new Int32Array([3,4])],
                   [new Int32Array([5,27]), new Int32Array([7,8])]],
            Children: [{Id: 11, Label: "first"}, {Id: 22, Label: "second", Samples: new Float64Array([-11.5,9.75])}],
            Grid: [[{Label: "grid zero"}], [{Label: "grid one"}]],
        };
        assert.equal(api.CheckFixedDescriptorInputs(source), true);
        const copy = api.EchoFixedDescriptorArrays(source);
        assert.equal(api.CheckFixedDescriptorInputs(copy), true);
        source.Matrix[13] = 0;
        source.Children[0].Label = "changed";
        assert.equal(api.CheckFixedDescriptorInputs(copy), true);
        const calls = api.GetFixedDescriptorCallCount();
        assert.throws(() => api.CheckFixedDescriptorInputs({...source, Matrix: new Float32Array(15)}), RangeError);
        assert.equal(api.GetFixedDescriptorCallCount(), calls);
    });
    test("shared scalar constants match the independently compiled native snapshot", () => {
        const snapshot = api.GetConstantSnapshot();
        for (const [field, value] of Object.entries(snapshot)) {
            assert.ok(Object.is(constants[`TestConstant${field}`], value), field);
        }
        assert.equal(snapshot.InvalidID, 0xffffffffffffffffn);
        assert.equal(snapshot.WideID, 0xfedcba9876543210n);
    });
}
