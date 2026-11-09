export function registerTests({test, assert, api, constants}) {
    test("inherited constants preserve base fields and match their compiled native values", () => {
        assert.deepEqual(constants.MarshalInherited, {A: 0xfedcba9876543210n, B: 13, C: 1.5});
        assert.deepEqual(constants.MarshalInheritedDefault, {A: 5n, B: 7, C: 0.5});
        assert.equal(api.MarshalInherited, constants.MarshalInherited);
        assert.equal(api.MarshalInheritedDefault, constants.MarshalInheritedDefault);
        assert.equal(api.CheckInheritedExport(constants.MarshalInherited, false), true);
        assert.equal(api.CheckInheritedExport(constants.MarshalInheritedDefault, true), true);
    });
    test("standalone constants are named exports and module members with exact values", () => {
        assert.equal(constants.MarshalInvalidID, 0xffffffffffffffffn);
        assert.equal(api.MarshalInvalidID, api.GetInvalidEntityID());
        assert.equal(api.EchoEntityID(api.MarshalInvalidID), 0xffffffffffffffffn);
        assert.equal(constants.MarshalNegativeID, -0x7123456789abcdefn);
        assert.equal(api.MarshalAutoOffset, 0xffffffff);
        assert.equal(api.MarshalDefaultScale, 1.25);
        assert.equal(api.MarshalDefaultEnabled, true);
        assert.equal(api.MarshalAnimationName, "Translation");
        assert.equal(api.MarshalAnimationNamePointer, api.MarshalAnimationName);
    });
    test("aggregate constants are deeply immutable and preserve nested 64-bit values", () => {
        assert.deepEqual(api.MarshalLookup, [0n, 0xfedcba9876543210n]);
        assert.equal(api.MarshalNames.Invalid, api.MarshalInvalidID);
        assert.equal(api.MarshalNames.Name, "Translation");
        assert.ok(api.MarshalNames.Parts instanceof Uint32Array);
        assert.deepEqual(Array.from(api.MarshalNames.Parts), [3, 5]);
        api.MarshalNames.Parts[0] = 9;
        assert.equal(api.SumExportParts(api.MarshalNames), 8);
        assert.throws(() => {api.MarshalNames.Name = "changed";}, TypeError);
        assert.equal(constants.MarshalNames, api.MarshalNames);
    });
}
