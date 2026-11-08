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
    const {registerTests} = await import(config.suite);
    let wasm;
    const api = await bindings.default({onRuntimeInitialized() { wasm = this; }});
    const sharedMemory = typeof SharedArrayBuffer !== "undefined" && wasm.HEAPU8.buffer instanceof SharedArrayBuffer;
    if (config.sharedMemory) {
        assert.equal(crossOriginIsolated, true, "Threaded binding tests require cross-origin isolation");
        assert.equal(sharedMemory, true, "Expected the production shared-memory Wasm build");
    }
    const tests = await runTests(test => registerTests({test, assert, api, constants: bindings, wasm}));
    await report({userAgent: navigator.userAgent, crossOriginIsolated, sharedMemory, tests});
} catch (error) {
    await fatal(error);
}
