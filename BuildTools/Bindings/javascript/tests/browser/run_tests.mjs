import {assert} from "./assert.mjs";
import {runTests} from "./test_runner.mjs";

const config = JSON.parse(document.getElementById("config").textContent);
let reported = false;
async function report(result) {
    if (reported) return;
    reported = true;
    document.getElementById("result").textContent = JSON.stringify(result, null, 2);
    await fetch("/result", {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(result)});
}
function fatal(error) { return report({fatal: String(error?.stack ?? error)}); }
window.addEventListener("error", event => { void fatal(event.error ?? event.message); });
window.addEventListener("unhandledrejection", event => { void fatal(event.reason); });

try {
    const bindings = await import(config.module);
    const {registerTests, moduleOptions = {}, assetFiles = []} = await import(config.suite);
    if (assetFiles.length && !config.assetRoot) throw new Error("Shared test assets were not supplied");
    const assets = Object.fromEntries(await Promise.all(assetFiles.map(async name => {
        const response = await fetch(new URL(name, new URL(config.assetRoot, location.href)));
        if (!response.ok) throw new Error(`Cannot load test asset ${name}: ${response.status}`);
        return [name, new Uint8Array(await response.arrayBuffer())];
    })));
    const options = assetFiles.length ? {...moduleOptions, files: {...moduleOptions.files,
        ...Object.fromEntries(assetFiles.map(name => ["/assets/" + name, assets[name]]))}} : moduleOptions;
    let wasm;
    const api = await bindings.default({...options, onRuntimeInitialized() { wasm = this; }});
    const sharedMemory = typeof SharedArrayBuffer !== "undefined" && wasm.HEAPU8.buffer instanceof SharedArrayBuffer;
    if (config.sharedMemory) {
        assert.equal(crossOriginIsolated, true, "Threaded binding tests require cross-origin isolation");
        assert.equal(sharedMemory, true, "Expected the production shared-memory Wasm build");
    }
    const moduleURL = new URL(config.module, location.href);
    const tests = await runTests(test => registerTests({test, assert, api, constants: bindings, wasm, moduleURL, testData: config.testData, assets}));
    await report({userAgent: navigator.userAgent, crossOriginIsolated, sharedMemory, tests});
} catch (error) {
    await fatal(error);
}
