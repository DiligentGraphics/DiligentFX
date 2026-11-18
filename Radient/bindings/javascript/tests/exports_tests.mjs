import {readFixtures} from "./fixtures.mjs";

export function registerTests({test, assert, api, constants, testData}) {
    const data = readFixtures(testData);
    test("standalone invalid IDs preserve their native 64-bit representation", () => {
        for (const name of ["InvalidRadientHandle", "InvalidRadientComponentTypeID", "InvalidRadientFrameID"]) {
            assert.equal(constants[name], BigInt(data.string("exports.invalidID")), name);
            assert.equal(api[name], constants[name], name);
        }
    });

    test("automatically discovered interface IDs preserve native fields", () => {
        const id = constants.IID_RadientEngine;
        assert.equal(id.Data1, data.number("exports.engineID.Data1"));
        assert.equal(id.Data2, data.number("exports.engineID.Data2"));
        assert.equal(id.Data3, data.number("exports.engineID.Data3"));
        assert.deepEqual(Array.from(id.Data4), data.numbers("exports.engineID.Data4"));
        assert.equal(api.IID_RadientEngine, id);
        assert.ok(Object.isFrozen(id));
        id.Data4[0] = 0;
        assert.equal(id.Data4[0], data.numbers("exports.engineID.Data4")[0]);
    });

    test("the active morph-target limit is a numeric named export and module member", () => {
        assert.equal(constants.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT, data.number("exports.maxActiveMorphTargets"));
        assert.equal(api.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT, constants.RADIENT_MAX_ACTIVE_MORPH_TARGET_COUNT);
    });
}
