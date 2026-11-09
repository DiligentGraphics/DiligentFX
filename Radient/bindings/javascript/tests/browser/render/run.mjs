import {selectCases, captures, cameraTransform} from "./manifest.mjs";

const config = JSON.parse(document.getElementById("config").textContent);
const options = config.test;
const results = [];
const caseResults = [];
const start = performance.now();
let browser, renderer, manager, view, target, environment, wasm;
let frame = 0;
let reported = false;
const gpuErrors = [];
const delay = () => new Promise(resolve => setTimeout(resolve, 0));
const seconds = since => (performance.now() - since) / 1000;
async function post(path, value) {
    const response = await fetch(path, {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(value)});
    if (!response.ok) throw new Error(`Reporting ${path}: HTTP ${response.status}`);
}
const progress = message => post("/progress", {message});
function check(status, operation = "Radient operation") {
    if (status < 0) throw new Error(`${operation}: status ${status}`);
    return status;
}
function value(result, operation) { check(result.status, operation); return result.value; }
function write(path, bytes) {
    wasm.FS.mkdirTree(path.slice(0, path.lastIndexOf("/")));
    wasm.FS.writeFile(path, bytes);
}
async function bytes(url, optional = false) {
    const response = await fetch(url);
    if (optional && response.status === 404) return null;
    if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
    return new Uint8Array(await response.arrayBuffer());
}
async function artifact(name, path) {
    const response = await fetch("/artifact", {method: "POST", headers: {"X-Artifact-Name": name}, body: wasm.FS.readFile(path)});
    if (!response.ok) throw new Error(`Saving ${name}: HTTP ${response.status}`);
}

// Discover and fetch external glTF buffers/images in JavaScript. The native
// importer then reads the files normally through the module's virtual FS.
async function loadModel(model) {
    const url = new URL("/models/" + model, location.origin);
    const data = await bytes(url);
    const paths = [];
    const install = (url, data) => { const path = decodeURIComponent(url.pathname); write(path, data); paths.push(path); };
    install(url, data);
    try {
        let json = data;
        if (data.length >= 20 && new DataView(data.buffer).getUint32(0, true) === 0x46546c67) {
            json = data.subarray(20, 20 + new DataView(data.buffer).getUint32(12, true));
        }
        const document = JSON.parse(new TextDecoder().decode(json).replace(/\0+$/, ""));
        const uris = new Set([...(document.buffers ?? []), ...(document.images ?? [])]
            .map(item => item.uri).filter(uri => uri && !uri.startsWith("data:")));
        // Finish every fetch before removing files if one resource fails.
        const resources = await Promise.allSettled([...uris].map(async uri => {
            const resource = new URL(uri, url);
            if (resource.origin !== location.origin || !resource.pathname.startsWith("/models/")) {
                throw new Error(`External resource escapes the model directory: ${uri}`);
            }
            install(resource, await bytes(resource));
        }));
        const failed = resources.find(result => result.status === "rejected");
        if (failed) throw failed.reason;
    } catch (error) {
        paths.forEach(path => wasm.FS.unlink(path));
        throw error;
    }
    return {path: decodeURIComponent(url.pathname), paths};
}

function visualization(name) {
    if (!name) return 0;
    const normalize = text => text.replaceAll("_", "").toLowerCase();
    const entry = Object.entries(browser.api.RADIENT_DEBUG_VISUALIZATION)
        .find(([key]) => normalize(key.replace("RADIENT_DEBUG_VISUALIZATION_", "")) === normalize(name));
    if (!entry) throw new Error(`Unknown visualization ${name}`);
    return entry[1];
}

async function render(importer, writer, asset, capture, deadline) {
    const api = browser.api;
    for (;;) {
        if (performance.now() > deadline) throw new Error("Timed out waiting for import/render/capture readiness");
        const imported = check(importer.ProcessPendingImports(), "ProcessPendingImports");
        check(writer.CommitChanges(), "CommitChanges");
        const loaded = check(api.GetRadientAssetLoadStatus(asset), "Scene load");
        const envLoaded = check(api.GetRadientAssetLoadStatus(environment), "Environment load");
        const begun = check(renderer.BeginFrame({Time: frame++ / 60, DeltaTime: 1 / 60}), "BeginFrame");
        let status;
        let capturing = false;
        try {
            status = check(renderer.Render({pView: view}), "Render");
        } finally {
            check(renderer.EndFrame(), "EndFrame");
        }
        const ready = imported === 0 && loaded === 0 && envLoaded === 0 && begun === 0 && status === 0;
        if (ready && capture) {
            check(api.BeginBrowserImageCapture(browser.engine), "Begin capture");
            capturing = true;
        }
        browser.present();
        // Presentation and readback callbacks must run on the browser event loop.
        await browser.device.queue.onSubmittedWorkDone();
        await delay();
        if (gpuErrors.length) throw new Error(gpuErrors.join("\n"));
        if (capturing) {
            while (check(api.SaveBrowserImageCapture(capture), "Save capture") !== 0) {
                if (performance.now() > deadline) throw new Error("Timed out waiting for GPU readback");
                await delay();
            }
        }
        if (ready) return;
    }
}

function evaluate(registry, animation, time) {
    const state = registry.GetState();
    let found = false;
    try {
        for (const entry of state.pEntries) {
            if (entry.pClip.GetDesc().Name !== animation.name) continue;
            if (time > entry.pClip.GetDesc().Duration || !entry.ppBindings.length) throw new Error("Invalid animation capture");
            found = true;
            for (const binding of entry.ppBindings) check(binding.Evaluate({Time: time, UpdateDerivedState: true}), "Evaluate");
        }
    } finally {
        for (const entry of state.pEntries) {
            entry.ppBindings.forEach(binding => binding.dispose());
            entry.pClip.dispose();
        }
    }
    if (!found) throw new Error(`Animation not found: ${animation.name}`);
}

async function runCase(test) {
    const started = performance.now();
    const firstResult = results.length;
    const owned = [];
    const keep = object => { if (object) owned.push(object); return object; };
    let files;
    const caseCaptures = captures(test);
    await progress(`RUN ${test.name} (${caseCaptures.length} captures)`);
    try {
        files = await loadModel(test.model);
        const scene = keep(value(browser.engine.CreateScene({Desc: {Name: test.name}})));
        const writer = keep(value(browser.engine.CreateSceneWriter(scene)));
        const importer = keep(value(browser.engine.CreateSceneImporter(writer)));
        const registry = test.animation ? keep(value(browser.engine.CreateAnimationRegistry(scene))) : null;
        const camera = value(writer.CreateEntity({Name: "Render test camera", Transform: cameraTransform(test.camera)}));
        const aperture = 15.2908;
        check(writer.SetCamera(camera, {VerticalAperture: aperture,
            FocalLength: aperture / (2 * Math.tan((test.camera.fov ?? 45) * Math.PI / 360)),
            ClippingRange: {x: test.camera.clippingRange?.[0] ?? 0.1, y: test.camera.clippingRange?.[1] ?? 1000}}));
        if (test.directionalLight !== false) {
            const light = value(writer.CreateEntity({Name: "Render test directional light"}));
            check(writer.SetLight(light, {Type: browser.api.RADIENT_LIGHT_TYPE.RADIENT_LIGHT_TYPE_DIRECTIONAL, Intensity: 2}));
        }
        check(view.SetScene(scene));
        check(view.SetCamera(camera));
        check(view.SetIBLEnabled(test.enableIBL !== false));
        check(view.SetToneMapping({Mode: browser.api.RADIENT_TONE_MAPPING_MODE.RADIENT_TONE_MAPPING_MODE_UNCHARTED2,
            ...(test.toneMapping?.averageLogLum !== undefined ? {AverageLogLum: test.toneMapping.averageLogLum} : {})}));
        const imported = value(importer.ImportScene({URI: files.path}, {Name: test.name, pAnimationRegistry: registry}), "ImportScene");
        const asset = keep(imported.ppScene);
        const deadline = () => performance.now() + options.caseTimeout * 1000;
        // Prime loading and renderer resources before evaluating an animation.
        check(view.SetDebugVisualization(0));
        await render(importer, writer, asset, null, deadline());
        let animationIndex = -1;
        for (const capture of caseCaptures) {
            const captureStarted = performance.now();
            if (test.animation && animationIndex !== capture.index) {
                evaluate(registry, test.animation, capture.time);
                animationIndex = capture.index;
            }
            check(view.SetDebugVisualization(visualization(capture.visualization)));
            const actual = `/output/${capture.name}.png`;
            const reference = `/references/${capture.name}.png`;
            const difference = `/output/${capture.name}_diff.png`;
            const png = await bytes(`/references/${test.name}/${capture.name}.png`, true);
            if (png) write(reference, png);
            await render(importer, writer, asset, actual, deadline());
            const comparison = value(browser.api.CompareBrowserImages(actual, reference, difference,
                test.comparison?.maxChannelError ?? options.maxChannelError,
                test.comparison?.maxBadPixelRatio ?? options.maxBadPixelRatio), "Compare images");
            const errors = [];
            if (!comparison.HasReference) errors.push("Missing reference image; rendered candidate saved");
            else if (!comparison.SameDimensions) errors.push("Reference dimensions do not match capture dimensions");
            else if (!comparison.Passed) errors.push(`${comparison.BadPixels} bad pixels; maximum channel difference ${comparison.MaxDifference}`);
            await artifact(`${test.name}/${capture.name}.png`, actual);
            if (wasm.FS.analyzePath(difference).exists) {
                await artifact(`${test.name}/${capture.name}_diff.png`, difference);
                wasm.FS.unlink(difference);
            }
            wasm.FS.unlink(actual);
            if (png) wasm.FS.unlink(reference);
            const result = {name: capture.name, errors, seconds: seconds(captureStarted), comparison};
            results.push(result);
            await progress(`${errors.length ? "FAIL" : "PASS"} ${capture.name}: ${result.seconds.toFixed(2)}s${errors.length ? " — " + errors.join("; ") : ""}`);
        }
    } catch (error) {
        results.push({name: test.name, errors: [String(error.stack ?? error)], seconds: seconds(started)});
        await progress(`FAIL ${test.name}: ${error.message}`);
    } finally {
        browser.api.ReleaseBrowserImageCapture();
        check(view.SetCamera(browser.api.InvalidRadientEntityID));
        check(view.SetScene(null));
        for (const object of owned.reverse()) object.dispose();
        for (const path of files?.paths ?? []) wasm.FS.unlink(path);
        const elapsed = seconds(started);
        caseResults.push({name: test.name, seconds: elapsed,
            captures: results.length - firstResult, failed: results.slice(firstResult).some(result => result.errors.length)});
        await progress(`DONE ${test.name}: ${elapsed.toFixed(2)}s`);
    }
}

async function report(fatal) {
    if (reported) return;
    reported = true;
    const report = {userAgent: navigator.userAgent, crossOriginIsolated, sharedMemory: true,
        seconds: seconds(start), width: options.width, height: options.height, cases: caseResults,
        tests: results, ...(fatal ? {fatal: String(fatal.stack ?? fatal)} : {})};
    await post("/render-report", report);
    await post("/result", report);
}
window.addEventListener("error", event => { void report(event.error ?? event.message); });
window.addEventListener("unhandledrejection", event => { void report(event.reason); });

try {
    const selected = selectCases(options.manifest, options.filter);
    await progress(`Rendering ${selected.length} cases / ${selected.flatMap(captures).length} images at ${options.width}x${options.height} in one browser session`);
    const {default: createBrowser} = await import("/generated/RadientBrowser.mjs");
    const canvas = document.createElement("canvas");
    canvas.width = options.width;
    canvas.height = options.height;
    document.body.append(canvas);
    const environmentPath = "/assets/Environments/colosseum_1k.hdr";
    browser = await createBrowser({canvas, enableCapture: true, moduleOptions: {
        files: {[environmentPath]: await bytes(environmentPath)}, onRuntimeInitialized() { wasm = this; },
    }});
    browser.device.addEventListener("uncapturederror", event => gpuErrors.push(event.error.message));
    browser.device.lost.then(info => { if (info.reason !== "destroyed") void report(new Error(`WebGPU device lost: ${info.message}`)); });
    wasm.FS.mkdirTree("/output");
    wasm.FS.mkdirTree("/references");
    manager = value(browser.engine.GetAssetManager());
    environment = value(manager.LoadTexture({URI: environmentPath}));
    renderer = value(browser.engine.CreateRenderer({EnableAsyncPipelineCompilation: true, PostFXTransitionDuration: 0}));
    target = browser.createRenderTarget(renderer);
    view = value(renderer.CreateView({pRenderTarget: target, ClearColor: {x: 0.05, y: 0.05, z: 0.05, w: 1},
        TemporalAntiAliasing: {Enabled: false}, Environment: {pEnvironmentMap: environment, Yaw: Math.PI / 2},
        Skybox: {Source: browser.api.RADIENT_SKYBOX_SOURCE.RADIENT_SKYBOX_SOURCE_PREFILTERED_ENVIRONMENT, MipLevel: 1}}));
    for (const test of selected) await runCase(test);
    await progress(`Suite completed: ${selected.length} cases, ${results.length} results, ${seconds(start).toFixed(2)}s`);
    view.dispose(); target.dispose(); renderer.dispose(); environment.dispose(); manager.dispose(); browser.dispose();
    await report();
} catch (error) {
    browser?.api.ReleaseBrowserImageCapture();
    view?.dispose(); target?.dispose(); renderer?.dispose(); environment?.dispose(); manager?.dispose(); browser?.dispose();
    await report(error);
}
