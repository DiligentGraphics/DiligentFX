import assert from "node:assert/strict";
import {test} from "node:test";
import {assert as browserAssert} from "./browser/assert.mjs";
import {runTests} from "./browser/test_runner.mjs";

test("browser assertions reject mismatched data and missing or wrong exceptions", () => {
    browserAssert.deepEqual({status: 0, bytes: new Uint8Array([17, 128]), size: 2n},
        {status: 0, bytes: new Uint8Array([17, 128]), size: 2n});
    for (const [actual, expected] of [[0, false], [null, {}], [1n, 1], [{x: 1}, {x: 2}],
        [{x: undefined}, {}], [new Uint8Array([1]), new Uint8Array([2])], [new Uint8Array([1]), [1]],
        [new Array(1), []]]) {
        assert.throws(() => browserAssert.deepEqual(actual, expected));
    }
    assert.throws(() => browserAssert.ok(false));
    assert.throws(() => browserAssert.equal(0, false));
    assert.throws(() => browserAssert.throws(() => {}, Error));
    assert.throws(() => browserAssert.throws(() => { throw new Error("wrong"); }, /expected/));
    assert.throws(() => browserAssert.throws(() => { throw new TypeError(); }, RangeError));
    browserAssert.throws(() => { throw new RangeError("range"); }, RangeError);
    browserAssert.throws(() => { throw new Error("range"); }, /range/);
    const error = new Error("identity");
    browserAssert.throws(() => { throw error; }, error);
});

test("browser runner awaits cases, runs all cleanup, and reports failures", async () => {
    const cleaned = [];
    const results = await runTests(register => {
        register("assertion and cleanup failure", t => {
            t.after(() => cleaned.push("first"));
            t.after(() => { cleaned.push("second"); throw new Error("cleanup failed"); });
            throw new Error("assertion failed");
        });
        register("following async case", async t => {
            await Promise.resolve();
            t.after(async () => { await Promise.resolve(); cleaned.push("third"); });
        });
    });
    assert.deepEqual(cleaned, ["second", "first", "third"]);
    assert.equal(results[0].errors.length, 2);
    assert.match(results[0].errors[0], /assertion failed/);
    assert.match(results[0].errors[1], /cleanup failed/);
    assert.deepEqual(results[1], {name: "following async case", errors: []});
    await assert.rejects(runTests(() => {}), /did not register/);
});
