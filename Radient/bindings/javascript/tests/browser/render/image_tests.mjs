import assert from "node:assert/strict";
import {readFile, readdir} from "node:fs/promises";
import {join} from "node:path";
import {pathToFileURL} from "node:url";
import {selectCases, captures, cameraTransform} from "./manifest.mjs";

const [modulePath, directory] = process.argv.slice(2);
const {default: create} = await import(pathToFileURL(modulePath));
const files = Object.fromEntries(await Promise.all((await readdir(directory)).map(async name =>
    ["/images/" + name, new Uint8Array(await readFile(join(directory, name)))])));
let wasm;
const api = await create({files, onRuntimeInitialized() { wasm = this; }});
try {
    function compare(reference, threshold = 0, ratio = 0) {
        const result = api.CompareBrowserImages("/images/actual.png", "/images/" + reference, "/images/diff.png", threshold, ratio);
        assert.equal(result.status, 0);
        return result.value;
    }
    assert.deepEqual(compare("actual.png"), {HasReference: true, SameDimensions: true, Passed: true, DifferentPixels: 0, BadPixels: 0, MaxDifference: 0});
    assert.equal(compare("near.png").Passed, false);
    assert.equal(compare("near.png", 1).Passed, true);
    assert.equal(compare("different.png").MaxDifference, 100);
    assert.equal(compare("different.png", 0, 0.49).Passed, false);
    assert.equal(compare("different.png", 0, 0.5).Passed, true);
    assert.equal(compare("size.png").SameDimensions, false);
    assert.equal(compare("missing.png").HasReference, false);
    assert.ok(wasm.FS.readFile("/images/diff.png").length > 0);
    console.log("PASS: native PNG comparisons, thresholds, dimensions, missing references, and difference output");

    const manifest = {version: 1, tests: [{name: "A", animation: {captures: [{name: "Red", time: 0}, {name: "Blue", time: 1}]}, debugVisualizations: ["BaseColor"]}, {name: "B"}]};
    assert.equal(selectCases(manifest).length, 2);
    assert.equal(selectCases(manifest, "A").length, 1);
    assert.throws(() => selectCases(manifest, "missing"));
    assert.deepEqual(captures(manifest.tests[0]).map(c => c.name), ["A_Red_wgpu", "A_Red_BaseColor_wgpu", "A_Blue_wgpu", "A_Blue_BaseColor_wgpu"]);
    for (const eye of [[0, 0, 1], [0, 0, -1], [1, 0, 0], [-1, 0, 0], [2, 3, -4]]) {
        const {Rotation: q} = cameraTransform({eye});
        const back = [2*(q.x*q.z+q.w*q.y), 2*(q.y*q.z-q.w*q.x), 1-2*(q.x*q.x+q.y*q.y)];
        const length = Math.hypot(...eye);
        back.forEach((v, i) => assert.ok(Math.abs(v - eye[i] / length) < 1e-6));
    }
    console.log("PASS: complete manifest selection, animation/debug captures, camera orientation");
} finally {
    wasm.PThread.terminateAllThreads();
}
