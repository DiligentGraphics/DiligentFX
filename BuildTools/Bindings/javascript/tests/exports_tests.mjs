export function registerTests({test, assert, api, constants}) {
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
        assert.deepEqual(api.MarshalNames.Parts, [3, 5]);
        assert.throws(() => {api.MarshalNames.Parts[0] = 9;}, TypeError);
        assert.throws(() => {api.MarshalNames.Name = "changed";}, TypeError);
        assert.equal(constants.MarshalNames, api.MarshalNames);
    });
}
