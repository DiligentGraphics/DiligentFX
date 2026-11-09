// Run against both the generic fixture and the threaded Radient module.
export function registerTests({test, assert, constants, moduleURL}) {
    const binaryURL = new URL(moduleURL.href.replace(/\.mjs$/, "Wasm.wasm"));
    let binaryPromise;
    async function readBinary() {
        if (binaryURL.protocol === "file:") {
            const {readFile} = await import("node:fs/promises");
            const bytes = await readFile(binaryURL);
            return bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
        }
        const response = await fetch(binaryURL);
        assert.ok(response.ok, "The test must read the module's valid Wasm bytes");
        return response.arrayBuffer();
    }
    for (const type of ["ArrayBuffer", "Uint8Array"]) {
        test(`initializes from wasmBinary ${type} without loading the Wasm file`, async t => {
            const binary = await (binaryPromise ??= readBinary());
            const originalFetch = globalThis.fetch;
            let fetchCount = 0;
            globalThis.fetch = (...args) => {
                ++fetchCount;
                return originalFetch(...args);
            };
            t.after(() => { globalThis.fetch = originalFetch; });
            let wasm;
            t.after(() => wasm?.PThread?.terminateAllThreads());
            const api = await constants.default({
                wasmBinary: type === "ArrayBuffer" ? binary : new Uint8Array(binary),
                // Initialization must use the supplied bytes; fetching this file
                // instead would fail in both Node and the browser.
                locateFile(path, prefix) {
                    return path.endsWith(".wasm") ? new URL("missing.wasm", moduleURL).href : prefix + path;
                },
                onRuntimeInitialized() { wasm = this; },
            });
            assert.equal(fetchCount, 0, "Supplied Wasm bytes must avoid a fetch");
            assert.ok(wasm, "Native initialization must complete");
            assert.ok(wasm.HEAPU8 instanceof Uint8Array);
            assert.ok(api, "The generated bindings must be created");
        });
    }
}
