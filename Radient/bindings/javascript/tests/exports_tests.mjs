export function registerTests({test, assert, api, constants}) {
    test("standalone invalid IDs preserve their native 64-bit representation", () => {
        for (const name of ["InvalidRadientHandle", "InvalidRadientComponentTypeID", "InvalidRadientFrameID"]) {
            assert.equal(constants[name], 0n, name);
            assert.equal(api[name], constants[name], name);
        }
    });

    test("automatically discovered interface IDs preserve native fields", () => {
        const id = constants.IID_RadientEngine;
        assert.equal(id.Data1, 0x8013033e);
        assert.equal(id.Data2, 0x71b8);
        assert.equal(id.Data3, 0x4ff6);
        assert.deepEqual(Array.from(id.Data4), [0xb2, 0xa0, 0x51, 0x9e, 0xe1, 0x1b, 0xa4, 0xe0]);
        assert.equal(api.IID_RadientEngine, id);
        assert.ok(Object.isFrozen(id));
        id.Data4[0] = 0;
        assert.equal(id.Data4[0], 0xb2);
    });

    test("the active morph-target limit is a numeric named export and module member", () => {
        assert.equal(constants.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT, 64);
        assert.equal(api.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT, constants.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT);
    });
}
