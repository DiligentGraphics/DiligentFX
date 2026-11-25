import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {createHash} from "node:crypto";
import loadRadient, * as packageExports from "@diligentgraphics/radient";

assert.deepEqual(Object.keys(packageExports), ["default"]);

// Exercise package exports and the copied runtime without a browser or bundler.
const assetBaseURL = new URL("./public/radient/", import.meta.url);
const info = JSON.parse(await readFile(new URL("./node_modules/@diligentgraphics/radient/build-info.json", import.meta.url)));
for (const [name, hash] of Object.entries(info.runtimeSHA256)) {
    const bytes = await readFile(new URL(name, assetBaseURL));
    assert.equal(createHash("sha256").update(bytes).digest("hex"), hash, name);
}
let wasm;
const api = await loadRadient({assetBaseURL, onRuntimeInitialized() { wasm = this; }});
try {
    const result = api.CreateRadientDataBlob({Size: 3n, pData: new Uint8Array([1, 2, 3])});
    assert.equal(result.status, 0);
    const blob = result.value;
    try {
        assert.equal(blob.GetSize(), 3n);
        const access = blob.BeginRead();
        assert.equal(access.status, 0);
        try { assert.deepEqual(access.value.toUint8Array(), new Uint8Array([1, 2, 3])); }
        finally { access.value.dispose(); }
    } finally { blob.dispose(); }
    console.log("PASS: installed Node package initializes, preserves runtime hashes, and reads blob data");
} finally { wasm.PThread.terminateAllThreads(); }
