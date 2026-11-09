export function registerTests({test, assert, api, constants}) {
    test("standalone invalid IDs preserve their native 64-bit representation", () => {
        for (const name of ["InvalidRadientHandle", "InvalidRadientComponentTypeID", "InvalidRadientFrameID"]) {
            assert.equal(constants[name], 0n, name);
            assert.equal(api[name], constants[name], name);
        }
    });

    test("the active morph-target limit is a numeric named export and module member", () => {
        assert.equal(constants.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT, 64);
        assert.equal(api.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT, constants.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT);
    });
}
